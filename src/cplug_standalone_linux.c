/* Released into the public domain by Tré Dudman - 2026
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#include <cplug.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <xcb/xcb.h>

static struct
{
    void*                 plugin;
    void*                 gui;
    volatile sig_atomic_t quit;

    // The top-level window. The GUI is a child window inside it, on its own connection, like in a DAW
    xcb_connection_t* connection;
    xcb_window_t      window;
    xcb_atom_t        wmProtocols;
    xcb_atom_t        wmDeleteWindow;

    // An unused slot is -1. A timer's ID is its index
    int timerFds[CPLUG_MAX_TIMERS];
    struct
    {
        int      fd;
        uint32_t flags;
    } fds[CPLUG_MAX_FDS];
} g_app;

void Cplug_HostContext_SendParamEvent(CplugHostContext* ctx, const CplugEvent* e) {}
void Cplug_HostContext_Rescan(CplugHostContext* ctx, uint32_t flags) {}
bool Cplug_HostContext_GetHostName(CplugHostContext* ctx, char* buf, size_t buflen)
{
    snprintf(buf, buflen, "CPLUG Standalone Linux");
    return true;
}
static void set_fixed_size(uint32_t width, uint32_t height);

// The only way the window resizes. The GUI decides its own size, so it's taken as given
bool Cplug_HostContext_RequestResize(CplugHostContext* ctx, uint32_t width, uint32_t height)
{
    if (g_app.gui == NULL)
        return false;
    // The fixed size moves first, or the WM would hold the window at the old one
    set_fixed_size(width, height);
    const uint32_t size[] = {width, height};
    xcb_configure_window(g_app.connection, g_app.window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    xcb_flush(g_app.connection);
    return cplug_setSize(g_app.gui, width, height);
}

bool Cplug_HostContext_RegisterTimer(CplugHostContext* ctx, uint32_t periodMs, uint32_t* timerId)
{
    CPLUG_LOG_ASSERT_RETURN(periodMs > 0, false);
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
    {
        if (g_app.timerFds[i] != -1)
            continue;

        int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        CPLUG_LOG_ASSERT_RETURN(fd != -1, false);

        struct itimerspec spec;
        spec.it_interval.tv_sec  = periodMs / 1000;
        spec.it_interval.tv_nsec = (long)(periodMs % 1000) * 1000000;
        spec.it_value            = spec.it_interval;
        if (timerfd_settime(fd, 0, &spec, NULL) != 0)
        {
            close(fd);
            return false;
        }

        g_app.timerFds[i] = fd;
        *timerId          = (uint32_t)i;
        return true;
    }
    CPLUG_LOG_ASSERT(false && "Out of timer slots. Increase CPLUG_MAX_TIMERS");
    return false;
}

void Cplug_HostContext_UnregisterTimer(CplugHostContext* ctx, uint32_t timerId)
{
    CPLUG_LOG_ASSERT_RETURN(timerId < CPLUG_ARRLEN(g_app.timerFds) && g_app.timerFds[timerId] != -1, );
    close(g_app.timerFds[timerId]);
    g_app.timerFds[timerId] = -1;
}

bool Cplug_HostContext_RegisterFd(CplugHostContext* ctx, int fd, uint32_t flags)
{
    CPLUG_LOG_ASSERT_RETURN(fd >= 0, false);
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
    {
        if (g_app.fds[i].fd != -1)
            continue;
        g_app.fds[i].fd    = fd;
        g_app.fds[i].flags = flags;
        return true;
    }
    CPLUG_LOG_ASSERT(false && "Out of fd slots. Increase CPLUG_MAX_FDS");
    return false;
}

void Cplug_HostContext_UnregisterFd(CplugHostContext* ctx, int fd)
{
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
    {
        if (g_app.fds[i].fd == fd)
        {
            g_app.fds[i].fd = -1;
            return;
        }
    }
    CPLUG_LOG_ASSERT(false && "fd was never registered");
}

static_assert(sizeof(CplugHostContext) == 72, "You may need to add support for new methods");

/*
     ██╗ █████╗  ██████╗██╗  ██╗
     ██║██╔══██╗██╔════╝██║ ██╔╝
     ██║███████║██║     █████╔╝
██   ██║██╔══██║██║     ██╔═██╗
╚█████╔╝██║  ██║╚██████╗██║  ██╗
 ╚════╝ ╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝
*/

// libjack is loaded at runtime so the app still starts without it, so these are declared here instead of jack.h
typedef struct jack_client_t jack_client_t;
typedef struct jack_port_t   jack_port_t;
typedef uint32_t             jack_nframes_t;
typedef int (*JackProcessCallback)(jack_nframes_t nframes, void* arg);
typedef void (*JackShutdownCallback)(void* arg);
typedef struct jack_midi_event_t
{
    jack_nframes_t time;
    size_t         size;
    uint8_t*       buffer;
} jack_midi_event_t;

enum
{
    JackNoStartServer = 0x01,

    JackPortIsInput    = 0x1,
    JackPortIsOutput   = 0x2,
    JackPortIsPhysical = 0x4,
};
#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"
#define JACK_DEFAULT_MIDI_TYPE  "8 bit raw midi"

// Return type, name, arguments
#define JACK_FUNCTIONS(X)                                                                                              \
    X(jack_client_t*, jack_client_open, (const char* name, int options, int* status, ...))                             \
    X(int, jack_client_close, (jack_client_t*))                                                                        \
    X(int, jack_activate, (jack_client_t*))                                                                            \
    X(int, jack_deactivate, (jack_client_t*))                                                                          \
    X(int, jack_set_process_callback, (jack_client_t*, JackProcessCallback, void* arg))                                \
    X(int, jack_set_buffer_size_callback, (jack_client_t*, JackProcessCallback, void* arg))                            \
    X(int, jack_set_sample_rate_callback, (jack_client_t*, JackProcessCallback, void* arg))                            \
    X(void, jack_on_shutdown, (jack_client_t*, JackShutdownCallback, void* arg))                                       \
    X(jack_nframes_t, jack_get_sample_rate, (jack_client_t*))                                                          \
    X(jack_nframes_t, jack_get_buffer_size, (jack_client_t*))                                                          \
    X(jack_port_t*,                                                                                                    \
      jack_port_register,                                                                                              \
      (jack_client_t*, const char* name, const char* type, unsigned long flags, unsigned long bufferSize))             \
    X(void*, jack_port_get_buffer, (jack_port_t*, jack_nframes_t))                                                     \
    X(uint32_t, jack_midi_get_event_count, (void* portBuffer))                                                         \
    X(int, jack_midi_event_get, (jack_midi_event_t*, void* portBuffer, uint32_t eventIndex))                           \
    X(const char*, jack_port_name, (const jack_port_t*))                                                               \
    X(const char**, jack_get_ports, (jack_client_t*, const char* namePattern, const char* typePattern, unsigned long)) \
    X(int, jack_connect, (jack_client_t*, const char* sourcePort, const char* destinationPort))                        \
    X(void, jack_free, (void*))

static struct
{
    void*          lib;
    jack_client_t* client;
    jack_port_t*   outputs[2];
    jack_port_t*   midiInput;

    // The plugin's sample rate and block size can only change on the main thread while cplug_process isn't running.
    // The notification thread stores the server's new settings and wakes the main thread through this eventfd. The
    // main thread clears 'processing', waits for 'inProcess' to clear, applies them, then sets 'processing' again.
    // Until then the process callback outputs silence
    int              reconfigureFd;
    cplug_atomic_i32 newSampleRate;
    cplug_atomic_i32 newBufferSize;
    cplug_atomic_i32 processing;
    cplug_atomic_i32 inProcess;
    // Written by the main thread only while 'processing' is clear
    uint32_t sampleRate;
    uint32_t bufferSize;

#define X(ret, name, args) ret(*name) args;
    JACK_FUNCTIONS(X)
#undef X
} g_jack;

typedef struct JackProcessContext
{
    CplugProcessContext cplugContext;
    float*              output[2];
    void*               midiBuffer;
    uint32_t            midiEventIdx;
} JackProcessContext;

static bool JackProcessContext_enqueueEvent(CplugProcessContext* ctx, const CplugEvent* e, uint32_t frameIdx)
{
    return true;
}

// JACK sorts events by frame. Audio up to the next event's frame is processed before it is sent
static bool JackProcessContext_dequeueEvent(CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    JackProcessContext* jackCtx = (JackProcessContext*)ctx;
    if (frameIdx >= ctx->numFrames)
        return false;

    uint32_t numEvents = g_jack.jack_midi_get_event_count(jackCtx->midiBuffer);
    for (; jackCtx->midiEventIdx < numEvents; jackCtx->midiEventIdx++)
    {
        jack_midi_event_t midi;
        if (g_jack.jack_midi_event_get(&midi, jackCtx->midiBuffer, jackCtx->midiEventIdx) != 0)
            continue;
        // CplugEvent holds up to 3 bytes. SysEx is skipped. JACK never sends running status
        if (midi.size == 0 || midi.size > 3 || midi.buffer[0] == 0xf0)
            continue;

        // An event behind frameIdx means the plugin skipped ahead, so it's sent now
        if (midi.time > frameIdx)
        {
            event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
            event->processAudio.endFrame = midi.time < ctx->numFrames ? midi.time : ctx->numFrames;
            return true;
        }

        event->midi.type       = CPLUG_EVENT_MIDI;
        event->midi.frame      = frameIdx;
        event->midi.bytesAsInt = 0;
        memcpy(event->midi.bytes, midi.buffer, midi.size);
        jackCtx->midiEventIdx++;
        return true;
    }

    event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
    event->processAudio.endFrame = ctx->numFrames;
    return true;
}

static void* JackProcessContext_getAudioInput(const CplugProcessContext* ctx, uint32_t busIdx) { return NULL; }
static void* JackProcessContext_getAudioOutput(const CplugProcessContext* ctx, uint32_t busIdx)
{
    const JackProcessContext* jackCtx = (const JackProcessContext*)ctx;
    return busIdx == 0 ? (void*)jackCtx->output : NULL;
}

// Real-time thread, owned by JACK
static int jack_process(jack_nframes_t nframes, void* arg)
{
    float* output[CPLUG_ARRLEN(g_jack.outputs)];
    for (size_t i = 0; i < CPLUG_ARRLEN(output); i++)
        output[i] = (float*)g_jack.jack_port_get_buffer(g_jack.outputs[i], nframes);

    // Set before reading 'processing', and the main thread clears 'processing' before reading this, so at least one of
    // the two sees the other's write
    cplug_atomic_exchange_i32(&g_jack.inProcess, 1);
    // A bigger buffer can arrive before the main thread has reconfigured the plugin for it
    if (!cplug_atomic_load_i32(&g_jack.processing) || nframes > g_jack.bufferSize)
    {
        cplug_atomic_exchange_i32(&g_jack.inProcess, 0);
        for (size_t i = 0; i < CPLUG_ARRLEN(output); i++)
            memset(output[i], 0, nframes * sizeof(float));
        return 0;
    }

    JackProcessContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cplugContext.numFrames       = nframes;
    ctx.cplugContext.numInputBusses  = 0;
    ctx.cplugContext.numOutputBusses = 1;
    ctx.cplugContext.audioSampleType = CPLUG_AUDIO_SAMPLE_FLOAT32;
    ctx.cplugContext.enqueueEvent    = JackProcessContext_enqueueEvent;
    ctx.cplugContext.dequeueEvent    = JackProcessContext_dequeueEvent;
    ctx.cplugContext.getAudioInput   = JackProcessContext_getAudioInput;
    ctx.cplugContext.getAudioOutput  = JackProcessContext_getAudioOutput;
    memcpy(ctx.output, output, sizeof(output));
    ctx.midiBuffer = g_jack.jack_port_get_buffer(g_jack.midiInput, nframes);

    cplug_process(g_app.plugin, &ctx.cplugContext);
    cplug_atomic_exchange_i32(&g_jack.inProcess, 0);
    return 0;
}

// Notification thread
static void request_reconfigure()
{
    uint64_t one = 1;
    write(g_jack.reconfigureFd, &one, sizeof(one));
}

static int jack_sample_rate_changed(jack_nframes_t sampleRate, void* arg)
{
    cplug_atomic_exchange_i32(&g_jack.newSampleRate, sampleRate);
    request_reconfigure();
    return 0;
}

static int jack_buffer_size_changed(jack_nframes_t bufferSize, void* arg)
{
    cplug_atomic_exchange_i32(&g_jack.newBufferSize, bufferSize);
    request_reconfigure();
    return 0;
}

// Main thread, when reconfigureFd is readable
static void reconfigure_jack()
{
    uint64_t count;
    read(g_jack.reconfigureFd, &count, sizeof(count));

    uint32_t sampleRate = cplug_atomic_load_i32(&g_jack.newSampleRate);
    uint32_t bufferSize = cplug_atomic_load_i32(&g_jack.newBufferSize);
    // JACK also reports the settings the client started with
    if (sampleRate == g_jack.sampleRate && bufferSize == g_jack.bufferSize)
        return;

    cplug_atomic_exchange_i32(&g_jack.processing, 0);
    // At most one cplug_process call to wait for
    while (cplug_atomic_load_i32(&g_jack.inProcess))
        usleep(100);

    cplug_log("JACK changed to %u Hz, %u frames", sampleRate, bufferSize);
    g_jack.sampleRate = sampleRate;
    g_jack.bufferSize = bufferSize;
    cplug_setSampleRateAndBlockSize(g_app.plugin, sampleRate, bufferSize);
    cplug_atomic_exchange_i32(&g_jack.processing, 1);
}

static void jack_shutdown(void* arg) { fprintf(stderr, "The JACK server shut down. Audio has stopped\n"); }

static void close_jack()
{
    if (g_jack.client)
        g_jack.jack_client_close(g_jack.client);
    if (g_jack.reconfigureFd != -1)
    {
        Cplug_HostContext_UnregisterFd(NULL, g_jack.reconfigureFd);
        close(g_jack.reconfigureFd);
    }
    if (g_jack.lib)
        dlclose(g_jack.lib);
    memset(&g_jack, 0, sizeof(g_jack));
    g_jack.reconfigureFd = -1;
}

// Without JACK the app runs without audio
static bool open_jack()
{
    g_jack.reconfigureFd = -1;
    g_jack.lib           = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
    if (g_jack.lib == NULL)
    {
        fprintf(stderr, "JACK not available. Install pipewire-jack (or jackd2) for audio\n");
        return false;
    }
#define X(ret, name, args)                                                                                             \
    *(void**)&g_jack.name = dlsym(g_jack.lib, #name);                                                                  \
    if (g_jack.name == NULL)                                                                                           \
    {                                                                                                                  \
        fprintf(stderr, "libjack.so.0 is missing " #name "\n");                                                        \
        close_jack();                                                                                                  \
        return false;                                                                                                  \
    }
    JACK_FUNCTIONS(X)
#undef X

    // JackNoStartServer: when no server is running, don't spawn jackd behind the user's back
    int status    = 0;
    g_jack.client = g_jack.jack_client_open(CPLUG_PLUGIN_NAME, JackNoStartServer, &status);
    if (g_jack.client == NULL)
    {
        fprintf(stderr, "Failed connecting to a JACK server (status 0x%x). Is one running?\n", status);
        close_jack();
        return false;
    }

    static const char* const OUTPUT_NAMES[] = {"out_L", "out_R"};
    for (size_t i = 0; i < CPLUG_ARRLEN(g_jack.outputs); i++)
    {
        g_jack.outputs[i] =
            g_jack.jack_port_register(g_jack.client, OUTPUT_NAMES[i], JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (g_jack.outputs[i] == NULL)
        {
            fprintf(stderr, "Failed registering JACK port %s\n", OUTPUT_NAMES[i]);
            close_jack();
            return false;
        }
    }
    g_jack.midiInput = g_jack.jack_port_register(g_jack.client, "midi_in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
    if (g_jack.midiInput == NULL)
    {
        fprintf(stderr, "Failed registering JACK port midi_in\n");
        close_jack();
        return false;
    }

    int reconfigureFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (reconfigureFd == -1 || !Cplug_HostContext_RegisterFd(NULL, reconfigureFd, CPLUG_FD_READ))
    {
        fprintf(stderr, "Failed creating an eventfd for JACK\n");
        if (reconfigureFd != -1)
            close(reconfigureFd);
        close_jack();
        return false;
    }
    g_jack.reconfigureFd = reconfigureFd;

    // Set up before the first process callback, which can come as soon as the client is active
    g_jack.sampleRate = g_jack.jack_get_sample_rate(g_jack.client);
    g_jack.bufferSize = g_jack.jack_get_buffer_size(g_jack.client);
    cplug_atomic_exchange_i32(&g_jack.newSampleRate, g_jack.sampleRate);
    cplug_atomic_exchange_i32(&g_jack.newBufferSize, g_jack.bufferSize);
    cplug_setSampleRateAndBlockSize(g_app.plugin, g_jack.sampleRate, g_jack.bufferSize);
    cplug_atomic_exchange_i32(&g_jack.processing, 1);

    g_jack.jack_set_process_callback(g_jack.client, jack_process, NULL);
    g_jack.jack_set_sample_rate_callback(g_jack.client, jack_sample_rate_changed, NULL);
    g_jack.jack_set_buffer_size_callback(g_jack.client, jack_buffer_size_changed, NULL);
    g_jack.jack_on_shutdown(g_jack.client, jack_shutdown, NULL);
    if (g_jack.jack_activate(g_jack.client) != 0)
    {
        fprintf(stderr, "Failed activating the JACK client\n");
        close_jack();
        return false;
    }

    // Ports can only be connected once active. Physical playback ports are inputs from JACK's point of view
    const char** playback =
        g_jack.jack_get_ports(g_jack.client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput);
    if (playback == NULL || playback[0] == NULL)
    {
        fprintf(stderr, "No JACK playback ports to connect to\n");
    }
    else
    {
        // A mono device gets both channels
        const char* right = playback[1] ? playback[1] : playback[0];
        g_jack.jack_connect(g_jack.client, g_jack.jack_port_name(g_jack.outputs[0]), playback[0]);
        g_jack.jack_connect(g_jack.client, g_jack.jack_port_name(g_jack.outputs[1]), right);
    }
    if (playback)
        g_jack.jack_free(playback);

    // Physical MIDI capture ports are outputs. Software keyboards aren't physical, so they're connected by hand
    const char** capture =
        g_jack.jack_get_ports(g_jack.client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical | JackPortIsOutput);
    if (capture == NULL || capture[0] == NULL)
        fprintf(stderr, "No JACK MIDI capture ports to connect to\n");
    else
        g_jack.jack_connect(g_jack.client, capture[0], g_jack.jack_port_name(g_jack.midiInput));
    if (capture)
        g_jack.jack_free(capture);
    return true;
}

static void handle_signal(int sig) { g_app.quit = 1; }

static xcb_atom_t intern_atom(const char* name)
{
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(g_app.connection, 0, strlen(name), name);
    xcb_intern_atom_reply_t* reply  = xcb_intern_atom_reply(g_app.connection, cookie, NULL);
    xcb_atom_t               atom   = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return atom;
}

static void set_property(xcb_atom_t property, xcb_atom_t type, uint8_t format, uint32_t len, const void* data)
{
    xcb_change_property(g_app.connection, XCB_PROP_MODE_REPLACE, g_app.window, property, type, format, len, data);
}

// WM_SIZE_HINTS from ICCCM 4.1.2.3. The WM never resizes the window. The GUI resizes it through requestResize
static void set_fixed_size(uint32_t width, uint32_t height)
{
    enum
    {
        P_MIN_SIZE = 1 << 4,
        P_MAX_SIZE = 1 << 5,
    };
    uint32_t sizeHints[18] = {0};
    sizeHints[0]           = P_MIN_SIZE | P_MAX_SIZE;
    sizeHints[5]           = width;
    sizeHints[6]           = height;
    sizeHints[7]           = width;
    sizeHints[8]           = height;
    set_property(XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, CPLUG_ARRLEN(sizeHints), sizeHints);
}

static void create_window(uint32_t width, uint32_t height)
{
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(g_app.connection)).data;

    // If a WM resizes the window anyway, the area outside the GUI is cleared instead of left as garbage
    const uint32_t valueMask   = XCB_CW_BACK_PIXEL;
    const uint32_t valueList[] = {screen->black_pixel};

    g_app.window = xcb_generate_id(g_app.connection);
    xcb_create_window(
        g_app.connection,
        XCB_COPY_FROM_PARENT,
        g_app.window,
        screen->root,
        0,
        0,
        width,
        height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        valueMask,
        valueList);

    static const char name[] = CPLUG_PLUGIN_NAME;
    set_property(XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(name) - 1, name);
    set_property(intern_atom("_NET_WM_NAME"), intern_atom("UTF8_STRING"), 8, sizeof(name) - 1, name);
    // Instance and class names, each null terminated
    static const char wmClass[] = CPLUG_PLUGIN_NAME "\0" CPLUG_PLUGIN_NAME;
    set_property(XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);

    // _NET_WM_PID is only meaningful together with WM_CLIENT_MACHINE
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);
    set_property(XCB_ATOM_WM_CLIENT_MACHINE, XCB_ATOM_STRING, 8, strlen(hostname), hostname);
    uint32_t pid = (uint32_t)getpid();
    set_property(intern_atom("_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1, &pid);

    // Close button
    g_app.wmProtocols    = intern_atom("WM_PROTOCOLS");
    g_app.wmDeleteWindow = intern_atom("WM_DELETE_WINDOW");
    set_property(g_app.wmProtocols, XCB_ATOM_ATOM, 32, 1, &g_app.wmDeleteWindow);

    // Some WMs read these instead of the fixed size. Everything but resizing and maximising
    enum
    {
        MWM_HINTS_FUNCTIONS   = 1 << 0,
        MWM_HINTS_DECORATIONS = 1 << 1,
        MWM_FUNC_MOVE         = 1 << 2,
        MWM_FUNC_MINIMIZE     = 1 << 3,
        MWM_FUNC_CLOSE        = 1 << 5,
        MWM_DECOR_BORDER      = 1 << 1,
        MWM_DECOR_TITLE       = 1 << 3,
        MWM_DECOR_MENU        = 1 << 4,
        MWM_DECOR_MINIMIZE    = 1 << 5,
    };
    // Flags, functions, decorations, input mode, status
    const uint32_t motifHints[5] = {
        MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS,
        MWM_FUNC_MOVE | MWM_FUNC_MINIMIZE | MWM_FUNC_CLOSE,
        MWM_DECOR_BORDER | MWM_DECOR_TITLE | MWM_DECOR_MENU | MWM_DECOR_MINIMIZE,
    };
    xcb_atom_t motifAtom = intern_atom("_MOTIF_WM_HINTS");
    set_property(motifAtom, motifAtom, 32, CPLUG_ARRLEN(motifHints), motifHints);

    set_fixed_size(width, height);
}

static void close_gui()
{
    if (g_app.gui)
    {
        cplug_setVisible(g_app.gui, false);
        cplug_setParent(g_app.gui, NULL);
        cplug_destroyGUI(g_app.gui);
        g_app.gui = NULL;
    }
}

static void handle_window_events()
{
    xcb_generic_event_t* event;
    while ((event = xcb_poll_for_event(g_app.connection)))
    {
        if ((event->response_type & ~0x80) == XCB_CLIENT_MESSAGE)
        {
            xcb_client_message_event_t* msg = (xcb_client_message_event_t*)event;
            if (msg->type == g_app.wmProtocols && msg->data.data32[0] == g_app.wmDeleteWindow)
            {
                close_gui();
                g_app.quit = 1;
            }
        }
        free(event);
    }
    if (xcb_connection_has_error(g_app.connection))
    {
        fprintf(stderr, "Lost the connection to the X server\n");
        g_app.quit = 1;
    }
}

static void run_loop()
{
    // Each pollfd is either a timer or a user fd. The low bit of 'kind' says which, the rest is the slot index
    struct pollfd pfds[CPLUG_ARRLEN(g_app.timerFds) + CPLUG_ARRLEN(g_app.fds)];
    int           kinds[CPLUG_ARRLEN(pfds)];

    while (!g_app.quit)
    {
        nfds_t n = 0;
        for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        {
            if (g_app.timerFds[i] == -1)
                continue;
            pfds[n]  = (struct pollfd){.fd = g_app.timerFds[i], .events = POLLIN};
            kinds[n] = i << 1;
            n++;
        }
        for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
        {
            if (g_app.fds[i].fd == -1)
                continue;
            short events = 0;
            if (g_app.fds[i].flags & CPLUG_FD_READ)
                events |= POLLIN;
            if (g_app.fds[i].flags & CPLUG_FD_WRITE)
                events |= POLLOUT;
            pfds[n]  = (struct pollfd){.fd = g_app.fds[i].fd, .events = events};
            kinds[n] = (i << 1) | 1;
            n++;
        }

        // SIGINT & SIGTERM interrupt this with EINTR
        int ret = poll(pfds, n, -1);
        if (ret < 0)
        {
            CPLUG_LOG_ASSERT(errno == EINTR);
            continue;
        }

        for (nfds_t i = 0; i < n && !g_app.quit; i++)
        {
            if (pfds[i].revents == 0)
                continue;
            int slot = kinds[i] >> 1;
            // Skip slots that an earlier callback in this loop unregistered
            if ((kinds[i] & 1) == 0)
            {
                if (g_app.timerFds[slot] != pfds[i].fd)
                    continue;
                // Missed ticks are dropped. One callback per wakeup
                uint64_t expirations;
                if (read(pfds[i].fd, &expirations, sizeof(expirations)) == sizeof(expirations))
                    cplug_onTimer(g_app.plugin, (uint32_t)slot);
            }
            else
            {
                if (g_app.fds[slot].fd != pfds[i].fd)
                    continue;
                uint32_t flags = 0;
                if (pfds[i].revents & POLLIN)
                    flags |= CPLUG_FD_READ;
                if (pfds[i].revents & POLLOUT)
                    flags |= CPLUG_FD_WRITE;
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                    flags |= CPLUG_FD_ERROR;
                if (pfds[i].fd == xcb_get_file_descriptor(g_app.connection))
                    handle_window_events();
                else if (pfds[i].fd == g_jack.reconfigureFd)
                    reconfigure_jack();
                else
                    cplug_onFd(g_app.plugin, pfds[i].fd, flags);
            }
        }
    }
}

int main(int argc, char** argv)
{
    CplugHostContext host = {
        .type            = CPLUG_PLUGIN_IS_STANDALONE,
        .sendParamEvent  = Cplug_HostContext_SendParamEvent,
        .rescan          = Cplug_HostContext_Rescan,
        .getHostName     = Cplug_HostContext_GetHostName,
        .requestResize   = Cplug_HostContext_RequestResize,
        .registerTimer   = Cplug_HostContext_RegisterTimer,
        .unregisterTimer = Cplug_HostContext_UnregisterTimer,
        .registerFd      = Cplug_HostContext_RegisterFd,
        .unregisterFd    = Cplug_HostContext_UnregisterFd,
    };

    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        g_app.timerFds[i] = -1;
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.fds); i++)
        g_app.fds[i].fd = -1;

    // No SA_RESTART, so poll() returns and the loop sees 'quit'
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // xcb_connect reads DISPLAY too, but this gives a clearer message for the common case
    if (getenv("DISPLAY") == NULL)
    {
        fprintf(stderr, "DISPLAY is not set. " CPLUG_PLUGIN_NAME " needs an X11 server or XWayland\n");
        return 1;
    }
    g_app.connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(g_app.connection))
    {
        fprintf(stderr, "Failed connecting to the X server on DISPLAY=%s\n", getenv("DISPLAY"));
        xcb_disconnect(g_app.connection);
        return 1;
    }

    cplug_libraryLoad();
    g_app.plugin = cplug_createPlugin(&host);
    if (g_app.plugin)
    {
        // Without a GUI the window stays empty, and closing it still quits
        uint32_t width = 640, height = 360;
        g_app.gui = cplug_createGUI(&host, g_app.plugin);
        if (g_app.gui)
            cplug_getSize(g_app.gui, &width, &height);

        create_window(width, height);
        if (g_app.gui)
        {
            cplug_setParent(g_app.gui, (void*)(uintptr_t)g_app.window);
            cplug_setVisible(g_app.gui, true);
        }
        xcb_map_window(g_app.connection, g_app.window);
        xcb_flush(g_app.connection);

        open_jack();

        int windowFd = xcb_get_file_descriptor(g_app.connection);
        Cplug_HostContext_RegisterFd(&host, windowFd, CPLUG_FD_READ);
        // Atom replies may have queued events that poll() won't see
        handle_window_events();
        run_loop();
        Cplug_HostContext_UnregisterFd(&host, windowFd);

        // Stops the process callback before the plugin goes
        if (g_jack.client)
            g_jack.jack_deactivate(g_jack.client);
        close_jack();
        close_gui();
        xcb_destroy_window(g_app.connection, g_app.window);
        cplug_destroyPlugin(g_app.plugin);
    }
    cplug_libraryUnload();
    xcb_disconnect(g_app.connection);

    // Catch anything the plugin forgot to unregister
    for (size_t i = 0; i < CPLUG_ARRLEN(g_app.timerFds); i++)
        if (g_app.timerFds[i] != -1)
            close(g_app.timerFds[i]);

    return g_app.plugin ? 0 : 1;
}

/*
@@@@@@@%%%%####++++*+++**+++++++==++++++++++*#%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#***+*+++++++++++**+*+++*+*#######%%
@@@@%#%%#####*+**++++++++===========+++++*#######%@@@@@@@@@@@@@@@@@@@@@@@@@%%%##**++++++++++++++**++++++++*#*+**####
@@#*%####***###%#****+++++++=======++++*+++*##%#%%@@@@@@@@@@@@@@@@%%@@@@@%%%##*++++++++=======+++++======+++**#+++**
@%%%%%@@%%%%@%%%%##%%%%##%%###*#*++++*****#*##%%%%@@@@@@@@@@@@@@@@%%@@@@%%###**+++++++==================+=++++++++++
@@@%%@%@@@@@@@@@@@@@@@@@@@%%%%%%%%%######*#####%%%@@@@@@@@@@@@@@@@%%%%%%%##**+++++=======++============++++++++***++
@@%%%%@@%@@@@@@@@@@%#++++++++++++++++===+*++####%%@%@@@@@@@@@@@@@@%%%#####*++===+++++********#*+++###+++++****##****
@%%%%%@@@%@%@%#**###%@@@@@%%%%#*++=====+++===+*##%%%%@@@@@@@@@@@@@%%%###*+++=++=++****++++==+++++=+++===++++++++**#*
@@%%#%%%%%%*+++#%@%#++=========+++*++====++++++=+%%%@@@@@@@@@@@@@@%###**++==+++++**+==+==+++++++++++======+++==+*#*#
%@@@@%%%%++++*##*++*#+++*##==++*+*++**+=+==+++++#++@%@@@@@@@@@@@@@%###*++++=+*++=++++==+**+==========+++++=====+++++
####%%##+*++++=+*%@@%**#++*=-+###@@%+++++++++++**%#=%@@@@@@@@@@@@@%##*++++=+++=*+===+++++==###-=+=-=======++=====+++
++#+%@@@#*##+#**#@@@@#*##**+*####@@@@*+==+****+*%%@@+%@@@@@@@@@@@@%##***+++==#+++++===#@%*++++-=++*==*++==========++
%%%**+++%%%%%%@@%@@@@@%#########@@@@%##%@@#*%@@@@%%@%+@+++#%%#++++++==+==+==#%##+===#%@@@#**+=++++***#%#++========++
@@@@@@+#@@@@@@@@@@@@@@@@@%%%%@@@@@@@@%%%#%%%@@@@@@@@@*=+@@@@@@@@@@%%#*++-==#%%##++*+#%%@@@###*++*#*#%@%#++++++=+++++
@@@@@@+%@@@@@@@@@@@@@@@@@@@@@@@%@@%%%%%%%@@@@@@@@@@@@%=+%%@@@@@@@@@%#*++==+##%%@%%##%#%#%%@%%#*+*#%%%##*##***+++++++
@@@@@@+@@@@@@@@@@@@@@@@@@@@@@%@@@@@%%%%%%%%@@@@@@@*=++=*%@@@@@@@@@@@%#=+==###%%%%%%%#%%%%%%%%%%%%%%%#%%%#########+**
@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@%%%%@%%@@@@@%@@@@@+=====+#@@@@@@@@@@@@%#++==+=+###%%%%%####%##########################
@@@@@@*%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@#+=====#@@@@@@@@@@@@@%##++=+==+*##%%%%%%%%######%%#####*#############
@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@==+==+@@@@@@@@@@@@@@@%#+======##%%%%%%%%%%%%###################%##%%
@@@@@@@#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@%%%*++@@@@@@@@@@@@@@@@%##+====*##%%%%@@@%@@@@@%%%%%%###%%#%%%%%%%%%%%
@@@@@@@@*@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@++@@@@@@@@@@@@@@@@@%##*===**#%%%%%%%%%@%%%@@@@@@@@@@%%%%%%%%%%%%@@
@@@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@+#@@@@@@@@@@@@@@@@@@%%###===%*%%%%%%%%%%%%%%%@@@@@%%%%@%%%%%%%@@@@@
@@@@@@@@@@#%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%=%@@@@@@@@@@@@@@@@@@@%%####===#%%%%%%%%%%##%%%%%%%%%%%%%%%%%%%@%@%@@
@@@@@@@@@@@@=@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@+=@@@@@@@@@@@@@@@@@@@@@@%####*+#=%%#%%%@@%%%%%%%%%%%%%%%%%%%%%@%@@@@%+
@@@@@@@@@@@@@@=%@@@@@@@@@@@@@@@@@@@@@@@@@@@@+=#@@@@@@@@@@@@@@@@@@@@@@@%######+*==#%%@%@@@@%%%%%%%%%%%%%%%%%%@%@@@#+%
@@@@@@@@@@@@@@@@*+%@@@@@@@@@@@@@@@@@@@@@@%+#%%@@@@@@@@@@@@@@@@@@@@@@@@%%%####**##==%%@@%@@%%%%%%%%%%%%@@%%@@@@%#+#%%
@@@@@@@@@@@@@@@@@@@@+=*@@@@@@@@@@@@@@#==%%%#@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####*+*#%+-%%%@@@@@@@@@@%%@%%%%%@%#+=%%#%%
@@@@@@@@@@@@@@@@@@@@@@@@@%##****###%@@%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##########%%#*==+#%@%@@@@@@@@%#+==*%%%####%
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####**#####%%%%%%#++=======+#%%%%%%#######
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%######*####%%%%%%%%%%%%%%%%%%%%%%%%#%####%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#%####*####%%%%%%%%%%%%%%%%%%%%%%%%%%#%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%##%%%%#***####%%%%%%%%%%%%%%%%%%%%%%%%%###
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%####%%%%%#++#####%#%%%%%%%%%%%%%%%%%%%%%%%##
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%######%%%%%#*=+*#####%%%%%%%%%%%@@@%%@%%%%%%##
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#####%%@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##**++**#######=+++*#####%%%%%%%%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%####%%@@@@@@@@@@@@@@@@%%%%%@%@@@@%%##*+++++===+++*##*+++++**###%%%%%%%%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@%%%%#%%%@@@@@@@@@@%%#**+===---==+#%%###*++++==----====++****++++*#######%%%%%%%%%%%%%%%%%%%
@@@@@@@@@@@@@@@@@@@@@@@@%@@%@@@@@@@@@@@@@@@%#*===+=====++++#**++=======-========++**#*############%#%%%%%%%%%%%%%%%#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###*#**#%%%%####*+==+============++++*##################%%%%%%%%%%%####
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#%######%%%%#*++=+==+========++**#######%%%#########%#%%%%%#########
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%#**#%%#*++++++====+++++*+***########%%%###%###%%%#############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@%%%%#####*++++++===+=+++++++***###########%%%####%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%#*++++++++++++**+***+***##################################
@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@#***+*++***+++***#**####%##%##############################
@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%###*+**##############%##*##*#%#**#*#*#####################
@@@@@@@@@@@@@@@@@@@@@@@@%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%#####*#############%###**##*###******+****#################
@@@@@@@@@@@@@@@@@@@@@%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###%%%#########*#####*#*#####****++*++**+*################
@@@@@@@@@@@@@@@@@@@@@%%%##%#%%@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%########**#*****++++++++++++***################
@@@@@@@@@@@@@@@@@@@%%%%#*+=++*#@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@%%%%%%%%%%####****++++++++++****#%%%#############
@@@@@@@@@@@@@@@@@@@@@@@@@%%%@#+==+*#%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@%%%#####%%######**++=====++++**##############*###
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%###**+========++++++++*##%@@%%%##*++++====+=++==++++=======++++**###%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%@@@%%#*++============++++=============------========++**######%%###############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%@@@@@%%%####+++====------========---=====+++++*#######%%%%%%%%###########*#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@@@@@@@@@%%%%####%#############+**#*++***########%%%%%%%%#############*
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@%@@@@%%%%%######**#####**#*########%%%%%%%%##############
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%%%%%%#####**+*****###***############%%%###############**
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%##%%%%%%%#######*****+**+++*+**###############################**
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%@@%#**#***#*++*****+++++++++++++++**####################*#######****+
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%##****+++***+++++++++*+++++****++****################*#*****####*#**+#
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%####****++++++++++++++++*******###################*#********++***#
*/