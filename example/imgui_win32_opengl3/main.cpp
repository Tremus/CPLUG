#include "cplug.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl3.h>

#define GL_SILENCE_DEPRECATION
#include <GL/GL.h>


#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define my_assert(cond) (cond) ? (void)0 : __debugbreak()

// Apparently denormals aren't a problem on ARM & M1?
// https://en.wikipedia.org/wiki/Subnormal_number
// https://www.kvraudio.com/forum/viewtopic.php?t=575799
#include <immintrin.h>
#define DISABLE_DENORMALS _mm_setcsr(_mm_getcsr() & ~0x8040);
#define ENABLE_DENORMALS _mm_setcsr(_mm_getcsr() | 0x8040);

static_assert((int)CPLUG_NUM_PARAMS == kParameterCount, "Must be equal");

typedef struct ParamInfo
{
    float min;
    float max;
    float defaultValue;
    int   flags;
} ParamInfo;

typedef struct MyPlugin
{
    ParamInfo paramInfo[kParameterCount];
    
    float    sampleRate;
    uint32_t maxBufferSize;

    float paramValuesAudio[kParameterCount];

    int   midiNote; // -1 == not playing, 0-127+ playing
    float velocity; // 0-1

    // GUI zone
    void* gui;
    float paramValuesMain[kParameterCount];

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

void* cplug_createPlugin()
{
    MyPlugin* plugin = (MyPlugin*)malloc(sizeof(MyPlugin));
    memset(plugin, 0, sizeof(*plugin));

    // Init params
    plugin->paramInfo[kGain].flags        = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
    plugin->paramInfo[kGain].max          = 1.0;
    plugin->paramInfo[kGain].min          = 0.0;
    plugin->paramInfo[kGain].defaultValue = 0.0f;

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
    (void)ptr;
    if (idx == 0)
        return 2; // 1 bus, stereo
    return 0;
}

uint32_t cplug_getOutputBusChannelCount(void* ptr, uint32_t idx)
{
    (void)ptr;
    if (idx == 0)
        return 2; // 1 bus, stereo
    return 0;
}

const char* cplug_getInputBusName(void* ptr, uint32_t idx)
{
    (void)ptr;
    if (idx == 0)
        return "Stereo Input";
    return "";
}

const char* cplug_getOutputBusName(void* ptr, uint32_t idx)
{
    (void)ptr;
    if (idx == 0)
        return "Stereo Output";
    return "";
}

/* --------------------------------------------------------------------------------------------------------
 * Parameters */

const char* cplug_getParameterName(void* ptr, uint32_t index)
{
    (void)ptr;
    static const char* param_names[] = {"Parameter Gain"};
    static_assert((sizeof(param_names) / sizeof(param_names[0])) == kParameterCount, "Invalid length");
    return param_names[index];
}

double cplug_getParameterValue(void* ptr, uint32_t index)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    double          val    = plugin->paramValuesAudio[index];
    if (plugin->paramInfo[index].flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        val = round(val);
    return val;
}

double cplug_getDefaultParameterValue(void* ptr, uint32_t index)
{
    MyPlugin* plugin = (MyPlugin*)ptr;
    return plugin->paramInfo[index].defaultValue;
}

void cplug_setParameterValue(void* ptr, uint32_t index, double value)
{
    MyPlugin* plugin = (MyPlugin*)ptr;

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
        plugin->audioToMainQueue[queueWritePos].parameter.idx   = index;
        plugin->audioToMainQueue[queueWritePos].parameter.value = value;

        cplug_atomic_fetch_add_i32(&plugin->audioToMainHead, 1);
        cplug_atomic_fetch_and_i32(&plugin->audioToMainHead, CPLUG_EVENT_QUEUE_MASK);
    }
}

double cplug_denormaliseParameterValue(void* ptr, uint32_t index, double normalised)
{
    const MyPlugin*  plugin = (MyPlugin*)ptr;
    const ParamInfo* info   = &plugin->paramInfo[index];

    double denormalised = normalised * (info->max - info->min) + info->min;

    if (denormalised < info->min)
        denormalised = info->min;
    if (denormalised > info->max)
        denormalised = info->max;
    return denormalised;
}

double cplug_normaliseParameterValue(void* ptr, uint32_t index, double denormalised)
{
    const MyPlugin*  plugin = (MyPlugin*)ptr;
    const ParamInfo* info   = &plugin->paramInfo[index];

    // If this fails, your param range is likely not initialised, causing a division by zero and producing infinity
    double normalised = (denormalised - info->min) / (info->max - info->min);
    my_assert(normalised == normalised);

    if (normalised < 0.0f)
        normalised = 0.0f;
    if (normalised > 1.0f)
        normalised = 1.0f;
    return normalised;
}

double cplug_parameterStringToValue(void* ptr, uint32_t index, const char* str)
{
    double          value;
    const MyPlugin* plugin = (MyPlugin*)ptr;
    const int flags = plugin->paramInfo[index].flags;

    if (flags & CPLUG_FLAG_PARAMETER_IS_INTEGER)
        value = (double)atoi(str);
    else
        value = atof(str);

    return value;
}

void cplug_parameterValueToString(void* ptr, uint32_t index, char* buf, size_t bufsize, double value)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    const int flags = plugin->paramInfo[index].flags;

    if (flags & CPLUG_FLAG_PARAMETER_IS_BOOL)
        value = value >= 0.5 ? 1 : 0;

    if (flags & (CPLUG_FLAG_PARAMETER_IS_INTEGER | CPLUG_FLAG_PARAMETER_IS_BOOL))
        snprintf(buf, bufsize, "%d", (int)value);
    else
        snprintf(buf, bufsize, "%.2f", value);
}

void cplug_getParameterRange(void* ptr, uint32_t index, double* min, double* max)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    *min                   = plugin->paramInfo[index].min;
    *max                   = plugin->paramInfo[index].max;
}

uint32_t cplug_getParameterFlags(void* ptr, uint32_t index)
{
    const MyPlugin* plugin = (MyPlugin*)ptr;
    return (uint32_t)plugin->paramInfo[index].flags;
}

/* --------------------------------------------------------------------------------------------------------
 * Audio/MIDI Processing */

uint32_t cplug_getLatencyInSamples(void* ptr) {
    (void)ptr;
    return 0;
}
uint32_t cplug_getTailInSamples(void* ptr) {
    (void)ptr;
    return 0;
}

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
            plugin->paramValuesAudio[event->parameter.idx] = (float)event->parameter.value;

        ctx->enqueueEvent(ctx, event, 0);

        tail++;
        tail &= CPLUG_EVENT_QUEUE_MASK;
    }
    cplug_atomic_exchange_i32(&plugin->mainToAudioTail, tail);

    // "Sample accurate" process loop
    CplugEvent event;
    int        frame = 0;
    while (ctx->dequeueEvent(ctx, &event, (uint32_t)frame))
    {
        switch (event.type)
        {
        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            cplug_setParameterValue(plugin, event.parameter.idx, event.parameter.value);
            break;
        case CPLUG_EVENT_MIDI:
        {
            break;
        }
        case CPLUG_EVENT_PROCESS_AUDIO:
        {
            // If your plugin does not require sample accurate processing, use this line below to break the loop
            // frame = event.processAudio.endFrame;

            float** output = ctx->getAudioOutput(ctx, 0);
            float** input = ctx->getAudioInput(ctx, 0);
            CPLUG_LOG_ASSERT(output != NULL)
            CPLUG_LOG_ASSERT(output[0] != NULL);
            CPLUG_LOG_ASSERT(output[1] != NULL);

            for (; frame < (int)event.processAudio.endFrame; frame++)
            {
                for (int ch = 0; ch < 2; ch++) {
                    output[ch][frame] = input[ch][frame] * plugin->paramValuesAudio[kGain];
                }
            }

            break;
        }
        default:
            break;
        }
    }
    ENABLE_DENORMALS
}

/* --------------------------------------------------------------------------------------------------------
 * State */

// In these methods we will use a very basic binary preset format: a flat array of param values

void cplug_saveState(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;
    writeProc(stateCtx, plugin->paramValuesAudio, sizeof(plugin->paramValuesAudio));
}

void cplug_loadState(void* userPlugin, const void* stateCtx, cplug_readProc readProc)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;

    float vals[kParameterCount + 1];

    int64_t bytesRead = readProc(stateCtx, vals, sizeof(vals));

    if (bytesRead == sizeof(plugin->paramValuesAudio))
    {
        // Send update to queue so we notify host
        for (uint32_t i = 0; i < kParameterCount; i++)
        {
            plugin->paramValuesAudio[i] = vals[i];
            plugin->paramValuesMain[i]  = vals[i];
            sendParamEventFromMain(plugin, CPLUG_EVENT_PARAM_CHANGE_UPDATE, i, vals[i]);
        }
    }
}

void sendParamEventFromMain(MyPlugin* plugin, uint32_t type, uint32_t paramIdx, double value)
{
    int         mainToAudioHead = cplug_atomic_load_i32(&plugin->mainToAudioHead) & CPLUG_EVENT_QUEUE_MASK;
    CplugEvent* paramEvent      = &plugin->mainToAudioQueue[mainToAudioHead];
    paramEvent->parameter.type  = type;
    paramEvent->parameter.idx   = paramIdx;
    paramEvent->parameter.value = value;

    cplug_atomic_fetch_add_i32(&plugin->mainToAudioHead, 1);
    cplug_atomic_fetch_and_i32(&plugin->mainToAudioHead, CPLUG_EVENT_QUEUE_MASK);

    // request_flush from CLAP host? Doesn't seem to be required
}


/////////////////////////// GUI PART /////////////////////////////////

// Gui windows data
struct WGL_WindowData { HDC hDC; };

static HGLRC            g_hRC;
static WGL_WindowData   g_MainWindow;

#define GUI_DEFAULT_WIDTH 640
#define GUI_DEFAULT_HEIGHT 360
#define GUI_RATIO_X 16
#define GUI_RATIO_Y 9

static bool gui_is_created = false;

typedef struct MyGUI {

    MyPlugin* plugin;
    void*     window; // HWND / NSView
#ifdef _WIN32
    char uniqueClassName[64];
#endif

    uint32_t* img;
    uint32_t  width;
    uint32_t  height;
} MyGUI;


// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
#define MY_TIMER_ID 1

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    fprintf(stderr, "msg: %u wParam: %llu lParam: %lld\n", message, wParam, lParam);

    // NOTE: Might be NULL during initialisation
    MyGUI* gui = (MyGUI*)GetWindowLongPtrA(window, 0);

    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
        return true;
    }

    switch (message) {
        case WM_TIMER: {

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
                    plugin->paramValuesMain[event->parameter.idx] = (float)event->parameter.value;
                    break;
                default:
                    break;
                }
        
                tail++;
                tail &= CPLUG_EVENT_QUEUE_MASK;
            }
            cplug_atomic_exchange_i32(&plugin->audioToMainTail, tail);
        
            bool tick_gui_result = needRedraw;

            if (tick_gui_result)
                RedrawWindow(window, 0, 0, RDW_INVALIDATE);
            // fallthrough to paint
        }
        
        case WM_PAINT: {
         
            // exectute what would be in the main loop on an imgui application
            MyPlugin* plugin = gui->plugin;    
            
            if (!gui_is_created) { break; }
            
            ImGuiContext* context = ImGui::GetCurrentContext();
            
            if (context) {
                ImGui::SetCurrentContext(context);
            }
            
            assert(context);
            
            ImGuiIO& io = ImGui::GetIO();
            
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            {
                static bool use_work_area = true;
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(use_work_area ? viewport->WorkPos : viewport->Pos);
                ImGui::SetNextWindowSize(use_work_area ? viewport->WorkSize : viewport->Size);
        
                ImGui::Begin("CPLUG with ImGui");        
                ImGui::Text("Hi from imgui");
                
                static float gain_slider_value = 0.0f;
                gain_slider_value = (float)cplug_getParameterValue(plugin, kGain);
                if (ImGui::SliderFloat("Gain slider", &gain_slider_value, 0.0f, 1.0f)) {
                    cplug_setParameterValue(plugin, kGain, gain_slider_value);
                }
        
                ImGui::End();
            }
        
            // Rendering
            ImGui::Render();
            glViewport(0, 0, (GLsizei)gui->width, (GLsizei)gui->height);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
            // Present
            SwapBuffers(g_MainWindow.hDC);
        
            break;
        }
        
        default: break;
    }

    return DefWindowProcA(window, message, wParam, lParam);
}


// Helper function (taken from the imgui win32 opengl3 example)
bool CreateDeviceWGL(HWND hWnd, WGL_WindowData* data)
{
    HDC hDc = ::GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    const int pf = ChoosePixelFormat(hDc, &pfd);
    if (pf == 0)
        return false;
    if (SetPixelFormat(hDc, pf, &pfd) == FALSE)
        return false;
    ReleaseDC(hWnd, hDc);

    data->hDC = GetDC(hWnd);
    if (!g_hRC)
        g_hRC = wglCreateContext(data->hDC);
    return true;
}

void* cplug_createGUI(void* userPlugin)
{
    MyPlugin* plugin = (MyPlugin*)userPlugin;
    MyGUI*    gui    = (MyGUI*)malloc(sizeof(MyGUI));
    memset(gui, 0, sizeof(*gui));

    bool debug_result;

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
    wc.lpfnWndProc   = window_procedure;
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
        
    // DWORD err = GetLastError();
    my_assert(gui->window != NULL);            
    
    SetWindowLongPtrA((HWND)gui->window, 0, (LONG_PTR)gui);

    // Initialize OpenGL
    if (!CreateDeviceWGL((HWND)gui->window, &g_MainWindow))
    {

        wglMakeCurrent(nullptr, nullptr);
        ReleaseDC((HWND)gui->window, g_MainWindow.hDC);

        DestroyWindow((HWND)gui->window);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return nullptr;
    }
    
    debug_result = wglMakeCurrent(g_MainWindow.hDC, g_hRC);

    // Show the window
    ShowWindow((HWND)gui->window, SW_SHOWDEFAULT);
    UpdateWindow((HWND)gui->window);

    // To make sure that only one ImGui context can exist at a time
    // init taken from the win32 opengl3 example in the imgui repo
    if (!gui_is_created) { 

        IMGUI_CHECKVERSION();
        ImGuiContext *context = ImGui::CreateContext();
        ImGui::SetCurrentContext(context);
    
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable Keyboard Controls
        io.DisplaySize = ImVec2((float)gui->width, (float)gui->height);

        ImGui::StyleColorsDark();    

        ImGui_ImplWin32_InitForOpenGL((HWND)gui->window);
        ImGui_ImplOpenGL3_Init();
    
        gui_is_created = true; 
    }

    return (void*)gui;
}

void cplug_destroyGUI(void* userGUI)
{
    MyGUI* gui = (MyGUI*)userGUI;
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    wglMakeCurrent(nullptr, nullptr);
    ReleaseDC((HWND)gui->window, g_MainWindow.hDC);
    
    wglDeleteContext(g_hRC);
    g_hRC = nullptr;
    DestroyWindow((HWND)gui->window);
    UnregisterClassA(gui->uniqueClassName, NULL);

    gui_is_created = false;
    gui->plugin->gui = NULL;

    if (gui->img)
        free(gui->img);

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
    (void)userGUI;
    (void)scale;
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
        (int)width,
        (int)height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);
}


void cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    (void)userGUI;
        
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
    (void)userGUI;

    *resizableX          = true;
    *resizableY          = true;
    *preserveAspectRatio = true;
    *aspectRatioX        = GUI_RATIO_X;
    *aspectRatioY        = GUI_RATIO_Y;
    return true;
}
