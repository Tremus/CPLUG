#include <cplug.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define my_assert(cond) (cond) ? (void)0 : __debugbreak()
#else
#define my_assert(cond) (cond) ? (void)0 : __builtin_debugtrap()
#endif

// Apparently denormals aren't a problem on ARM & M1?
// https://en.wikipedia.org/wiki/Subnormal_number
// https://www.kvraudio.com/forum/viewtopic.php?t=575799
#if __arm64__
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#elif defined(_WIN32)
// https://softwareengineering.stackexchange.com/a/337251
#include <immintrin.h>
#define DISABLE_DENORMALS                                                                                              \
    unsigned int oldMXCSR = _mm_getcsr();       /*read the old MXCSR setting  */                                       \
    unsigned int newMXCSR = oldMXCSR |= 0x8040; /* set DAZ and FZ bits        */                                       \
    _mm_setcsr(newMXCSR);                       /* write the new MXCSR setting to the MXCSR */
#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#else
#include <fenv.h>
#define DISABLE_DENORMALS                                                                                              \
    fenv_t _fenv;                                                                                                      \
    fegetenv(&_fenv);                                                                                                  \
    fesetenv(FE_DFL_DISABLE_SSE_DENORMS_ENV);
#define RESTORE_DENORMALS fesetenv(&_fenv);
#endif

#define ARRLEN(a)              (sizeof(a) / sizeof((a)[0]))
#define CPLUG_EVENT_QUEUE_MASK (CPLUG_EVENT_QUEUE_SIZE - 1)

static const uint32_t PARAM_IDS[] = {
    'pf32',
    'pi32',
    'bool',
    'utf8',
};
static_assert(ARRLEN(PARAM_IDS) == CPLUG_NUM_PARAMS, "Size must match");

// returns 'CPLUG_NUM_PARAMS' on failure
uint32_t get_param_index(void* ptr, uint32_t paramId)
{
    uint32_t i = 0;
    for (; i < ARRLEN(PARAM_IDS); i++)
        if (paramId == PARAM_IDS[i])
            break;
    return i;
}

typedef struct ParamInfo
{
    float min;
    float max;
    float defaultValue;
    int   flags;
} ParamInfo;

typedef struct MyPlugin
{
    CplugHostContext* hostContext;

    ParamInfo paramInfo[CPLUG_NUM_PARAMS];

    float    sampleRate;
    uint32_t maxBufferSize;

    float paramValuesAudio[CPLUG_NUM_PARAMS];

    float oscPhase; // 0-1
    int   midiNote; // -1 == not playing, 0-127+ playing
    float velocity; // 0-1

    // GUI zone
    void* gui;
    float paramValuesMain[CPLUG_NUM_PARAMS];

    // Single reader writer queue. Pretty sure atomics aren't required, but here anyway
    cplug_atomic_i32 mainToAudioHead;
    cplug_atomic_i32 mainToAudioTail;
    CplugEvent       mainToAudioQueue[CPLUG_EVENT_QUEUE_SIZE];

    cplug_atomic_i32 audioToMainHead;
    cplug_atomic_i32 audioToMainTail;
    CplugEvent       audioToMainQueue[CPLUG_EVENT_QUEUE_SIZE];
} MyPlugin;

void sendParamEventFromMain(MyPlugin* plugin, uint32_t type, uint32_t paramIdx, double value);

void cplug_libraryLoad(){};
void cplug_libraryUnload(){};

void* cplug_createPlugin(CplugHostContext* ctx)
{
    MyPlugin* plugin    = (MyPlugin*)calloc(1, sizeof(MyPlugin));
    plugin->hostContext = ctx;

    uint32_t idx;
    // Init params
    // 'pf32'
    idx                                 = get_param_index(plugin, 'pf32');
    plugin->paramInfo[idx].flags        = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
    plugin->paramInfo[idx].max          = 100.0f;
    plugin->paramInfo[idx].defaultValue = 50.0f;

    // 'pi32'
    idx                                 = get_param_index(plugin, 'pi32');
    plugin->paramValuesAudio[idx]       = 2.0f;
    plugin->paramInfo[idx].flags        = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE | CPLUG_FLAG_PARAMETER_IS_INTEGER;
    plugin->paramInfo[idx].min          = 2.0f;
    plugin->paramInfo[idx].max          = 5.0f;
    plugin->paramInfo[idx].defaultValue = 2.0f;

    // 'bool'
    idx                           = get_param_index(plugin, 'bool');
    plugin->paramValuesAudio[idx] = 0.0f;
    plugin->paramInfo[idx].flags  = CPLUG_FLAG_PARAMETER_IS_BOOL;
    plugin->paramInfo[idx].max    = 1.0f;

    // 'utf8'
    idx                                 = get_param_index(plugin, 'utf8');
    plugin->paramValuesAudio[idx]       = 0.0f;
    plugin->paramInfo[idx].flags        = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
    plugin->paramInfo[idx].min          = 0.0f;
    plugin->paramInfo[idx].max          = 1.0f;
    plugin->paramInfo[idx].defaultValue = 0.0f;

    plugin->midiNote = -1;

    return plugin;
}
void cplug_destroyPlugin(void* ptr)
{
    // Free any allocated resources in your plugin here
    free(ptr);
}

/* --------------------------------------------------------------------------------------------------------
 * Busses */

uint32_t cplug_getInputBusChannelCount(void* ptr, uint32_t idx)
{
    if (idx == 0)
        return 2; // 1 bus, stereo
    return 0;
}

uint32_t cplug_getOutputBusChannelCount(void* ptr, uint32_t idx)
{
    if (idx == 0)
        return 2; // 1 bus, stereo
    return 0;
}

const char* cplug_getInputBusName(void* ptr, uint32_t idx)
{
    if (idx == 0)
        return "Stereo Input";
    return "";
}

const char* cplug_getOutputBusName(void* ptr, uint32_t idx)
{
    if (idx == 0)
        return "Stereo Output";
    return "";
}

/* --------------------------------------------------------------------------------------------------------
 * Parameters */

uint32_t cplug_getParameterID(void* ptr, uint32_t paramIndex) { return PARAM_IDS[paramIndex]; }

const char* cplug_getParameterName(void* ptr, uint32_t paramId)
{
    static const char* param_names[] = {
        "Parameter Float",
        "Parameter Int",
        "Parameter Bool",
        // https://utf8everywhere.org/
        // UTF8    = 1 byte per character
        // Приве́т  = 2 bytes
        // नमस्ते     = 3 bytes
        // שלום = 3 בייטים
        // 🐨       = 4 bytes
        "UTF8 Приве́т नमस्ते שָׁלוֹם 🐨"};
    static_assert(ARRLEN(param_names) == CPLUG_NUM_PARAMS, "Invalid length");

    uint32_t index = get_param_index(ptr, paramId);
    return param_names[index];
}

double cplug_getParameterValue(void* ptr, uint32_t paramId)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);

    double val = plugin->paramValuesAudio[index];
    if (plugin->paramInfo[index].flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        val = round(val);
    return val;
}

double cplug_getDefaultParameterValue(void* ptr, uint32_t paramId)
{
    MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t  index  = get_param_index(ptr, paramId);
    return plugin->paramInfo[index].defaultValue;
}

void cplug_setParameterValue(void* ptr, uint32_t paramId, double value)
{
    MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t  index  = get_param_index(ptr, paramId);

    ParamInfo* info = &plugin->paramInfo[index];
    if (value < info->min)
        value = info->min;
    if (value > info->max)
        value = info->max;
    plugin->paramValuesAudio[index] = (float)value;

    // Send incoming param update to GUI
    if (plugin->gui)
    {
        int queueWritePos = cplug_atomic_load_i32(&plugin->audioToMainHead) & CPLUG_EVENT_QUEUE_MASK;

        plugin->audioToMainQueue[queueWritePos].parameter.type  = CPLUG_EVENT_PARAM_CHANGE_UPDATE;
        plugin->audioToMainQueue[queueWritePos].parameter.id    = paramId;
        plugin->audioToMainQueue[queueWritePos].parameter.value = value;

        cplug_atomic_fetch_add_i32(&plugin->audioToMainHead, 1);
        cplug_atomic_fetch_and_i32(&plugin->audioToMainHead, CPLUG_EVENT_QUEUE_MASK);
    }
}

double cplug_denormaliseParameterValue(void* ptr, uint32_t paramId, double normalised)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);

    const ParamInfo* info = &plugin->paramInfo[index];

    double denormalised = normalised * (info->max - info->min) + info->min;

    if (denormalised < info->min)
        denormalised = info->min;
    if (denormalised > info->max)
        denormalised = info->max;
    return denormalised;
}

double cplug_normaliseParameterValue(void* ptr, uint32_t paramId, double denormalised)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);

    const ParamInfo* info = &plugin->paramInfo[index];

    // If this fails, your param range is likely not initialised, causing a division by zero and producing infinity
    double normalised = (denormalised - info->min) / (info->max - info->min);
    my_assert(normalised == normalised);

    if (normalised < 0.0f)
        normalised = 0.0f;
    if (normalised > 1.0f)
        normalised = 1.0f;
    return normalised;
}

double cplug_parameterStringToValue(void* ptr, uint32_t paramId, const char* str)
{
    double          value;
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);

    const unsigned flags = plugin->paramInfo[index].flags;

    if (flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        value = (double)atoi(str);
    else
        value = atof(str);

    return value;
}

void cplug_parameterValueToString(void* ptr, uint32_t paramId, char* buf, size_t bufsize, double value)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;

    uint32_t index = get_param_index(ptr, paramId);

    const uint32_t flags = plugin->paramInfo[index].flags;

    if (flags & CPLUG_FLAG_PARAMETER_IS_BOOL)
        value = value >= 0.5 ? 1 : 0;

    if (paramId == 'utf8')
        snprintf(buf, bufsize, "%.2f Приве́т नमस्ते שָׁלוֹם 🐨", value);
    else if (flags & (CPLUG_FLAG_PARAMETER_IS_INTEGER | CPLUG_FLAG_PARAMETER_IS_BOOL))
        snprintf(buf, bufsize, "%d", (int)value);
    else
        snprintf(buf, bufsize, "%.2f", value);
}

void cplug_getParameterRange(void* ptr, uint32_t paramId, double* min, double* max)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);

    *min = plugin->paramInfo[index].min;
    *max = plugin->paramInfo[index].max;
}

uint32_t cplug_getParameterFlags(void* ptr, uint32_t paramId)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    uint32_t        index  = get_param_index(ptr, paramId);
    return plugin->paramInfo[index].flags;
}

/* --------------------------------------------------------------------------------------------------------
 * Audio/MIDI Processing */

uint32_t cplug_getLatencyInSamples(void* ptr) { return 0; }
uint32_t cplug_getTailInSamples(void* ptr) { return 0; }

void cplug_setSampleRateAndBlockSize(void* ptr, double sampleRate, uint32_t maxBlockSize)
{
    MyPlugin* plugin      = (MyPlugin*)ptr;
    plugin->sampleRate    = (float)sampleRate;
    plugin->maxBufferSize = maxBlockSize;
}

void cplug_process(void* ptr, CplugProcessContext* ctx)
{
    DISABLE_DENORMALS

    MyPlugin* plugin = (MyPlugin*)ptr;

    // Audio thread has chance to respond to incoming GUI events before being sent to the host
    int head = cplug_atomic_load_i32(&plugin->mainToAudioHead) & CPLUG_EVENT_QUEUE_MASK;
    int tail = cplug_atomic_load_i32(&plugin->mainToAudioTail);

    while (tail != head)
    {
        CplugEvent* event = &plugin->mainToAudioQueue[tail];

        if (event->type == CPLUG_EVENT_PARAM_CHANGE_UPDATE)
        {
            uint32_t idx                  = get_param_index(ptr, event->parameter.id);
            plugin->paramValuesAudio[idx] = event->parameter.value;
        }

        ctx->enqueueEvent(ctx, event, 0);

        tail++;
        tail &= CPLUG_EVENT_QUEUE_MASK;
    }
    cplug_atomic_exchange_i32(&plugin->mainToAudioTail, tail);

    // "Sample accurate" process loop
    CplugEvent event;
    int        frame = 0;
    while (ctx->dequeueEvent(ctx, &event, frame))
    {
        switch (event.type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
        {
            cplug_setParameterValue(plugin, event.parameter.id, event.parameter.value);
            break;
        }
        case CPLUG_EVENT_MIDI:
        {
            static const uint8_t MIDI_NOTE_OFF         = 0x80;
            static const uint8_t MIDI_NOTE_ON          = 0x90;
            static const uint8_t MIDI_NOTE_PITCH_WHEEL = 0xe0;

            if ((event.midi.status & 0xf0) == MIDI_NOTE_ON)
            {
                plugin->midiNote = event.midi.data1;
                plugin->velocity = (float)event.midi.data2 / 127.0f;
            }
            if ((event.midi.status & 0xf0) == MIDI_NOTE_OFF)
            {
                int note = event.midi.data1;
                if (note == plugin->midiNote)
                    plugin->midiNote = -1;
                plugin->velocity = (float)event.midi.data2 / 127.0f;
            }
            if ((event.midi.status & 0xf0) == MIDI_NOTE_PITCH_WHEEL)
            {
                // int pb = (int)event.midi.data1 | ((int)event.midi.data2 << 7);
            }
            break;
        }
        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            // If your plugin does not require sample accurate processing, use this line below to break the loop
            // frame = event.processAudio.endFrame;

            float** output = ctx->getAudioOutput(ctx, 0);
            CPLUG_LOG_ASSERT(output != NULL)
            CPLUG_LOG_ASSERT(output[0] != NULL);
            CPLUG_LOG_ASSERT(output[1] != NULL);

            if (plugin->midiNote == -1)
            {
                // Silence
                memset(&output[0][frame], 0, sizeof(float) * (event.processAudio.endFrame - frame));
                memset(&output[1][frame], 0, sizeof(float) * (event.processAudio.endFrame - frame));
                frame = event.processAudio.endFrame;
            }
            else
            {
                float phase = plugin->oscPhase;

                float Hz  = 440.0f * exp2f(((float)plugin->midiNote - 69.0f) * 0.0833333f);
                float inc = Hz / plugin->sampleRate;
                float dB  = -60.0f + plugin->velocity * 54; // -6dB max
                float vol = powf(10.0f, dB / 20.0f);

                for (; frame < event.processAudio.endFrame; frame++)
                {
                    static const float pi = 3.141592653589793f;

                    float sample = vol * sinf(2 * pi * phase);

                    for (int ch = 0; ch < 2; ch++)
                        output[ch][frame] = sample;

                    phase += inc;
                    phase -= (int)phase;
                }

                plugin->oscPhase = phase;
            }
            break;
        }
        default:
            break;
        }
    }
    RESTORE_DENORMALS
}

/* --------------------------------------------------------------------------------------------------------
 * State */

// In these methods we will use a very basic binary preset format: a flat array of param values
struct ParamState
{
    uint32_t paramId;
    float    value;
};

void cplug_saveState(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;

    struct ParamState state[CPLUG_NUM_PARAMS];
    for (int i = 0; i < CPLUG_NUM_PARAMS; i++)
    {
        state[i].paramId = PARAM_IDS[i];
        state[i].value   = plugin->paramValuesAudio[i];
    }
    writeProc(stateCtx, state, sizeof(state));
}

void cplug_loadState(void* userPlugin, const void* stateCtx, cplug_readProc readProc)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;

    struct ParamState state[CPLUG_NUM_PARAMS * 2];

    // If your plugin has added/removed parameters, requesting for more data then you actually expect may be a good idea
    int64_t bytesRead = readProc(stateCtx, state, sizeof(state));

    for (int i = 0; i < bytesRead / sizeof(state[0]); i++)
    {
        uint32_t paramIdx = get_param_index(userPlugin, state[i].paramId);
        if (paramIdx < CPLUG_NUM_PARAMS)
        {
            plugin->paramValuesAudio[paramIdx] = state[i].value;
            plugin->paramValuesMain[paramIdx]  = state[i].value;
            sendParamEventFromMain(plugin, CPLUG_EVENT_PARAM_CHANGE_UPDATE, state[i].paramId, state[i].value);
        }
    }
}

void sendParamEventFromMain(MyPlugin* plugin, uint32_t type, uint32_t paramId, double value)
{
    int         mainToAudioHead = cplug_atomic_load_i32(&plugin->mainToAudioHead) & CPLUG_EVENT_QUEUE_MASK;
    CplugEvent* paramEvent      = &plugin->mainToAudioQueue[mainToAudioHead];
    paramEvent->parameter.type  = type;
    paramEvent->parameter.id    = paramId;
    paramEvent->parameter.value = value;

    cplug_atomic_fetch_add_i32(&plugin->mainToAudioHead, 1);
    cplug_atomic_fetch_and_i32(&plugin->mainToAudioHead, CPLUG_EVENT_QUEUE_MASK);
}

#ifdef CPLUG_WANT_GUI

#define GUI_DEFAULT_WIDTH  640
#define GUI_DEFAULT_HEIGHT 360
#define GUI_RATIO_X        16
#define GUI_RATIO_Y        9

typedef struct MyGUI
{
    MyPlugin* plugin;
    void*     window; // HWND / NSView
#ifdef _WIN32
    char uniqueClassName[64];
#endif

    uint32_t* img;
    uint32_t  width;
    uint32_t  height;

    bool     mouseDragging;
    uint32_t dragParamId;
    int      dragStartX;
    int      dragStartY;
    double   dragStartParamNormalised;
    double   dragCurrentParamNormalised;
} MyGUI;

void drawRect(MyGUI* gui, uint32_t left, uint32_t right, uint32_t top, uint32_t bottom, uint32_t border, uint32_t fill)
{
    CPLUG_LOG_ASSERT(gui->img != NULL);
    for (uint32_t i = top; i < bottom; i++)
    {
        for (uint32_t j = left; j < right; j++)
        {
            gui->img[i * gui->width + j] = (i == top || i == bottom - 1 || j == left || j == right - 1) ? border : fill;
        }
    }
}

static void drawGUI(MyGUI* gui)
{
    my_assert(gui->width > 0);
    my_assert(gui->height > 0);
    drawRect(gui, 0, gui->width, 0, gui->height, 0xC0C0C0, 0xC0C0C0);
    drawRect(gui, 10, 40, 10, 40, 0x000000, 0xC0C0C0);

    double v = cplug_getParameterValue(gui->plugin, 'pf32');
    v        = cplug_normaliseParameterValue(gui->plugin, 'pf32', v);

    drawRect(gui, 10, 40, 10 + 30 * (1.0 - v), 40, 0x000000, 0x000000);
}

static void handleMouseDown(MyGUI* gui, int x, int y)
{
    if (x >= 10 && x < 40 && y >= 10 && y < 40)
    {
        gui->mouseDragging = true;
        gui->dragParamId   = 'pf32';
        gui->dragStartX    = x;
        gui->dragStartY    = y;

        double v                        = cplug_getParameterValue(gui->plugin, 'pf32');
        gui->dragStartParamNormalised   = cplug_normaliseParameterValue(gui->plugin, 'pf32', v);
        gui->dragCurrentParamNormalised = gui->dragStartParamNormalised;

        sendParamEventFromMain(gui->plugin, CPLUG_EVENT_PARAM_CHANGE_BEGIN, gui->dragParamId, 0);
    }
}

static void handleMouseUp(MyGUI* gui)
{
    if (gui->mouseDragging)
    {
        gui->mouseDragging = false;
        sendParamEventFromMain(gui->plugin, CPLUG_EVENT_PARAM_CHANGE_END, gui->dragParamId, 0);
    }
}

static void handleMouseMove(MyGUI* gui, int x, int y)
{
    if (gui->mouseDragging)
    {
        double nextValNormalised = gui->dragStartParamNormalised + (gui->dragStartY - y) * 0.01;
        if (nextValNormalised < 0)
            nextValNormalised = 0;
        if (nextValNormalised > 1)
            nextValNormalised = 1;
        gui->dragCurrentParamNormalised = nextValNormalised;

        double nextValDenormalised = cplug_denormaliseParameterValue(gui->plugin, gui->dragParamId, nextValNormalised);
        uint32_t paramIdx          = get_param_index(gui->plugin, gui->dragParamId);
        gui->plugin->paramValuesMain[paramIdx] = nextValDenormalised;
        sendParamEventFromMain(gui->plugin, CPLUG_EVENT_PARAM_CHANGE_UPDATE, gui->dragParamId, nextValDenormalised);
    }
}

bool tickGUI(MyGUI* gui)
{
    bool needRedraw = false;

    MyPlugin* plugin = gui->plugin;
    int       head   = cplug_atomic_load_i32(&plugin->audioToMainHead) & CPLUG_EVENT_QUEUE_MASK;
    int       tail   = cplug_atomic_load_i32(&plugin->audioToMainTail);

    needRedraw = tail != head;

    while (tail != head)
    {
        CplugEvent* event = &plugin->audioToMainQueue[tail];

        switch (event->type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
        {
            uint32_t idx                 = get_param_index(gui->plugin, event->parameter.id);
            plugin->paramValuesMain[idx] = event->parameter.value;
            break;
        }
        default:
            break;
        }

        tail++;
        tail &= CPLUG_EVENT_QUEUE_MASK;
    }
    cplug_atomic_exchange_i32(&plugin->audioToMainTail, tail);

    return needRedraw;
}

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>

#define MY_TIMER_ID 1

LRESULT CALLBACK MyWinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // fprintf(stderr, "msg: %u wParam: %llu lParam: %lld\n", uMsg, wParam, lParam);

    // NOTE: Might be NULL during initialisation
    MyGUI* gui = (MyGUI*)GetWindowLongPtrA(hwnd, 0);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        drawGUI(gui);
        PAINTSTRUCT paint;
        HDC         dc   = BeginPaint(hwnd, &paint);
        BITMAPINFO  info = {{sizeof(BITMAPINFOHEADER), (LONG)gui->width, (LONG)-gui->height, 1, 32, BI_RGB}};
        StretchDIBits(
            dc,
            0,
            0,
            gui->width,
            gui->height,
            0,
            0,
            gui->width,
            gui->height,
            gui->img,
            &info,
            DIB_RGB_COLORS,
            SRCCOPY);
        EndPaint(hwnd, &paint);
        break;
    }
    case WM_MOUSEMOVE:
        handleMouseMove(gui, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (gui->mouseDragging)
            RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
        break;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        handleMouseDown(gui, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;
    case WM_LBUTTONUP:
        ReleaseCapture();
        handleMouseUp(gui);
        break;
    case WM_TIMER:
        if (tickGUI(gui))
            RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
        break;
    default:
        break;
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void* cplug_createGUI(void* userPlugin)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;
    MyGUI*    gui    = (MyGUI*)malloc(sizeof(MyGUI));
    memset(gui, 0, sizeof(*gui));

    gui->plugin = plugin;
    plugin->gui = gui;

    gui->width  = GUI_DEFAULT_WIDTH;
    gui->height = GUI_DEFAULT_HEIGHT;
    gui->img    = (uint32_t*)realloc(gui->img, gui->width * gui->height * sizeof(*gui->img));

    LARGE_INTEGER timenow;
    QueryPerformanceCounter(&timenow);
    sprintf_s(gui->uniqueClassName, sizeof(gui->uniqueClassName), "%s-%llx", CPLUG_PLUGIN_NAME, timenow.QuadPart);

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = MyWinProc;
    wc.lpszClassName = gui->uniqueClassName;
    wc.cbWndExtra    = 32; // leave space for our pointer we set
    ATOM result      = RegisterClassExA(&wc);
    my_assert(result != 0);

    gui->window = CreateWindowExA(
        0L,
        gui->uniqueClassName,
        CPLUG_PLUGIN_NAME,
        WS_CHILD | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        GUI_DEFAULT_WIDTH,
        GUI_DEFAULT_HEIGHT,
        GetDesktopWindow(),
        NULL,
        NULL,
        NULL);
    my_assert(gui->window != NULL);

    SetWindowLongPtrA((HWND)gui->window, 0, (LONG_PTR)gui);

    return gui;
}

void cplug_destroyGUI(void* userGUI)
{
    MyGUI* gui       = (MyGUI*)userGUI;
    gui->plugin->gui = NULL;

    if (gui->img)
        free(gui->img);

    DestroyWindow((HWND)gui->window);
    UnregisterClassA(gui->uniqueClassName, NULL);
    free(gui);
}

void cplug_setParent(void* userGUI, void* newParent)
{
    MyGUI* gui = (MyGUI*)userGUI;

    HWND oldParent = GetParent((HWND)gui->window);
    if (oldParent)
    {
        KillTimer((HWND)gui->window, MY_TIMER_ID);

        SetParent((HWND)gui->window, NULL);
        DefWindowProcA((HWND)gui->window, WM_UPDATEUISTATE, UIS_CLEAR, WS_CHILD);
        DefWindowProcA((HWND)gui->window, WM_UPDATEUISTATE, UIS_SET, WS_POPUP);
    }

    if (newParent)
    {
        SetParent((HWND)gui->window, (HWND)newParent);
        memcpy(gui->plugin->paramValuesMain, gui->plugin->paramValuesAudio, sizeof(gui->plugin->paramValuesMain));
        DefWindowProcA((HWND)gui->window, WM_UPDATEUISTATE, UIS_CLEAR, WS_POPUP);
        DefWindowProcA((HWND)gui->window, WM_UPDATEUISTATE, UIS_SET, WS_CHILD);

        SetTimer((HWND)gui->window, MY_TIMER_ID, 10, NULL);
    }
}

void cplug_setVisible(void* userGUI, bool visible)
{
    MyGUI* gui = (MyGUI*)userGUI;
    ShowWindow((HWND)gui->window, visible ? SW_SHOW : SW_HIDE);
}

void cplug_setScaleFactor(void* userGUI, float scale)
{
    // handle change
}
void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    MyGUI* gui = (MyGUI*)userGUI;
    // GetWindowRect?
    *width  = gui->width;
    *height = gui->height;
}
bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height)
{
    MyGUI* gui  = (MyGUI*)userGUI;
    gui->width  = width;
    gui->height = height;
    gui->img    = (uint32_t*)realloc(gui->img, width * height * sizeof(*gui->img));
    return SetWindowPos(
        (HWND)gui->window,
        HWND_TOP,
        0,
        0,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);
}

#endif // _WIN32

void cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    if (*width < (GUI_RATIO_X * 10))
        *width = (GUI_RATIO_X * 10);
    if (*height < (GUI_RATIO_Y * 10))
        *height = (GUI_RATIO_Y * 10);

    // This preserves the aspect ratio when resizing from a corner, or expanding horizontally/vertically.
    // Shrinking the window from the edge doesn't work, and I'm currently not sure how to disable resizing from the edge
    // Win/macOS aren't very helpful at letting you know which edge/corner the user is pulling from.
    // Some people wanting to preserve aspect ratio will disable resizing the window and add a widget in the corner
    // The user of this library is left to implement their own strategy
    uint32_t numX = *width / GUI_RATIO_X;
    uint32_t numY = *height / GUI_RATIO_Y;
    uint32_t num  = numX > numY ? numX : numY;
    *width        = num * GUI_RATIO_X;
    *height       = num * GUI_RATIO_Y;
}

bool cplug_getResizeHints(
    void*     userGUI,
    bool*     resizableX,
    bool*     resizableY,
    bool*     preserveAspectRatio,
    uint32_t* aspectRatioX,
    uint32_t* aspectRatioY)
{
    *resizableX          = true;
    *resizableY          = true;
    *preserveAspectRatio = true;
    *aspectRatioX        = GUI_RATIO_X;
    *aspectRatioY        = GUI_RATIO_Y;
    return true;
}

#endif // CPLUG_WANT_GUI