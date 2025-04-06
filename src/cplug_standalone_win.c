/* Released into the public domain by Tré Dudman - 2024
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define CINTERFACE
#define COBJMACROS

#include <Windows.h>

#include <audioclient.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <mmeapi.h>
#include <synchapi.h>

#include <cplug.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "cfgmgr32.lib")

#ifndef ARRSIZE
#define ARRSIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define cplug_assert(cond) (cond) ? (void)0 : __debugbreak()

#if !defined(CPLUG_MIDI_BUFFER_COUNT) || !defined(CPLUG_MIDI_BUFFER_SIZE) || !defined(CPLUG_MIDI_RINGBUFFER_SIZE)
#define CPLUG_MIDI_BUFFER_COUNT    4
#define CPLUG_MIDI_BUFFER_SIZE     1024
#define CPLUG_MIDI_RINGBUFFER_SIZE 128
#endif

#if !defined(CPLUG_DEFAULT_BLOCK_SIZE) || !defined(CPLUG_DEFAULT_SAMPLE_RATE)
// WARNING: using 44100 is currently glitchy, don't know why. It's not a default for now
#define CPLUG_DEFAULT_SAMPLE_RATE 48000
#define CPLUG_DEFAULT_BLOCK_SIZE  512
#endif

#ifdef __cplusplus
#define CPLUG_WTF_IS_A_REFERENCE(obj) obj
#else
#define CPLUG_WTF_IS_A_REFERENCE(obj) &obj
#endif

////////////
// Plugin //
////////////

struct CPWIN_Plugin
{
#ifdef HOTRELOAD_LIB_PATH
    HMODULE Library;
#endif
    CplugHostContext HostContext;

    void* UserPlugin;
    void* UserGUI;

    void (*libraryLoad)();
    void (*libraryUnload)();
    void* (*createPlugin)(CplugHostContext*);
    void (*destroyPlugin)(void* userPlugin);
    uint32_t (*getOutputBusChannelCount)(void*, uint32_t bus_idx);
    void (*setSampleRateAndBlockSize)(void*, double sampleRate, uint32_t maxBlockSize);
    void (*process)(void*, CplugProcessContext*);
    void (*saveState)(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc);
    void (*loadState)(void* userPlugin, const void* stateCtx, cplug_readProc readProc);

    void* (*createGUI)(void* userPlugin);
    void (*destroyGUI)(void* userGUI);
    void (*setParent)(void* userGUI, void* hwnd_or_nsview);
    void (*setVisible)(void* userGUI, bool visible);
    void (*setScaleFactor)(void* userGUI, float scale);
    void (*getSize)(void* userGUI, uint32_t* width, uint32_t* height);
    void (*checkSize)(void* userGUI, uint32_t* width, uint32_t* height);
    bool (*setSize)(void* userGUI, uint32_t width, uint32_t height);
} g_plugin;
// Loads the DLL + loads symbols for library functions
void CPWIN_LoadPlugin();
void CPWIN_HostContext_SendParamEvent(CplugHostContext* ctx, const CplugEvent* e) {}
void CPWIN_HostContext_Rescan(CplugHostContext* ctx, uint32_t flags) {}
bool CPWIN_HostContext_GetHostName(CplugHostContext* ctx, char* buf, size_t buflen)
{
    snprintf(buf, buflen, "CPLUG Standalone Windows");
    return true;
}
_Static_assert(sizeof(CplugHostContext) == 32, "You may need to add support for new methods");

#ifdef HOTRELOAD_WATCH_DIR
struct CPWIN_PluginStateContext
{
    BYTE*  Data;
    SIZE_T BytesReserved;
    SIZE_T BytesCommited;

    SIZE_T BytesWritten;
    SIZE_T BytesRead;
} g_PluginState;
int64_t CPWIN_WriteStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite);
int64_t CPWIN_ReadStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead);

// File watch thread
DWORD WINAPI CPWIN_WatchFileChangesProc(LPVOID hwnd);
int          g_FlagExitFileWatchThread;
HANDLE       g_hFileWatchThread;

// Get time func taken from here https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
struct
{
    LARGE_INTEGER freq, start;
} g_Timer;
static inline INT64 CPWIN_GetNowNS()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    now.QuadPart -= g_Timer.start.QuadPart;
    INT64 q       = now.QuadPart / g_Timer.freq.QuadPart;
    INT64 r       = now.QuadPart % g_Timer.freq.QuadPart;
    return q * 1000000000 + r * 1000000000 / g_Timer.freq.QuadPart;
}
#endif // HOTRELOAD_WATCH_DIR

//////////
// MIDI //
//////////

typedef struct MIDIMessage
{
    union
    {
        struct
        {
            BYTE status;
            BYTE data1;
            BYTE data2;
        };
        BYTE bytes[4];
        UINT bytesAsInt;
    };
    /* Milliseconds since first connected to MIDI port */
    UINT timestampMs;
} MIDIMessage;

struct
{
    HMIDIIN hInput;
    int     IsConnected;

    MIDIINCAPS2W LastConnectedInput;

    struct
    {
        volatile LONG writePos;
        volatile LONG readPos;

        MIDIMessage buffer[CPLUG_MIDI_RINGBUFFER_SIZE];
    } RingBuffer;

    struct
    {
        MIDIHDR header;
        char    buffer[CPLUG_MIDI_BUFFER_SIZE];
    } SystemBuffers[CPLUG_MIDI_BUFFER_COUNT];
} g_MIDI;
// Main Thread
UINT CPWIN_MIDI_ConnectInput(UINT portNum);
void CPWIN_MIDI_DisconnectInput();

///////////
// AUDIO //
///////////

struct
{
    // Devices
    IMMDeviceEnumerator* pIMMDeviceEnumerator;
    IMMDevice*           pIMMDevice;
    WCHAR                DeviceIDBuffer[64];
    // Process
    IAudioClient*       pIAudioClient;
    IAudioRenderClient* pIAudioRenderClient;
    HANDLE              hAudioEvent;
    HANDLE              hAudioProcessThread;
    UINT32              FlagExitAudioThread;

    SIZE_T ProcessBufferCap;
    BYTE*  ProcessBuffer;
    UINT32 ProcessBufferMaxFrames;
    UINT32 ProcessBufferNumOverprocessedFrames;
    // Config
    UINT32 NumChannels;
    UINT32 SampleRate;
    UINT32 BlockSize;
} g_Audio;
// Audio Thread
DWORD WINAPI CPWIN_Audio_RunProcessThread(LPVOID data);
// Main Thread
void CPWIN_Audio_Stop();
void CPWIN_Audio_Start();
// Pass a deviceIdx < 0 for default device
void CPWIN_Audio_SetDevice(int deviceIdx);

///////////
// MENUS //
///////////

enum
{
    IDM_SampleRate_44100,
    IDM_SampleRate_48000,
    IDM_SampleRate_88200,
    IDM_SampleRate_96000,
    IDM_BlockSize_128,
    IDM_BlockSize_192,
    IDM_BlockSize_256,
    IDM_BlockSize_384,
    IDM_BlockSize_448,
    IDM_BlockSize_512,
    IDM_BlockSize_768,
    IDM_BlockSize_1024,
    IDM_BlockSize_2048,

    IDM_HandleRemovedMIDIDevice,
    IDM_HandleAddedMIDIDevice,

    IDM_Hotreload,

    IDM_OFFSET_AUDIO_DEVICES   = 50,
    IDM_RefreshAudioDeviceList = 99,

    IDM_OFFSET_MIDI_DEVICES   = 100,
    IDM_RefreshMIDIDeviceList = 149,
};

struct
{
    HMENU hMain;

    HMENU hAudioMenu;
    HMENU hSampleRateSubmenu;
    HMENU hBlockSizeSubmenu;
    HMENU hAudioOutputSubmenu;
    UINT  numAudioOutputs;

    HMENU hMIDIMenu;
    HMENU hMIDIInputsSubMenu;
} g_Menus;
void CPWIN_Menu_RefreshSampleRates();
void CPWIN_Menu_RefreshBlockSizes();
void CPWIN_Menu_RefreshAudioOutputs();
void CPWIN_Menu_RefreshMIDIInputs();

// Unknown system thread. Notify Connected/disconnected devices. We only check Audio/MIDI
DWORD CALLBACK CPWIN_HandleDeviceChange(
    HCMNOTIFICATION       hNotify,
    PVOID                 Context,
    CM_NOTIFY_ACTION      Action,
    PCM_NOTIFY_EVENT_DATA EventData,
    DWORD                 EventDataSize);
HCMNOTIFICATION g_hCMNotification;

// Main Thread
LRESULT CALLBACK CPWIN_WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static inline UINT64 CPWIN_RoundUp(UINT64 v, UINT64 align)
{
    UINT64 inc = (align - (v % align)) % align;
    return v + inc;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdline, int cmdshow)
{
    // https://stackoverflow.com/questions/171213/how-to-block-running-two-instances-of-the-same-program
    HANDLE hMutexOneInstance = CreateMutexW(NULL, TRUE, L"Single instance - " TEXT(CPLUG_PLUGIN_NAME));
    if (hMutexOneInstance == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hMutexOneInstance)
        {
            ReleaseMutex(hMutexOneInstance);
            CloseHandle(hMutexOneInstance);
        }
        return 1;
    }

    if (FAILED(OleInitialize(NULL)))
    {
        fprintf(stderr, "Failed initialising COM\n");
        return 1;
    }

#ifdef HOTRELOAD_WATCH_DIR
    QueryPerformanceFrequency(&g_Timer.freq);
    QueryPerformanceCounter(&g_Timer.start);
    memset(&g_PluginState, 0, sizeof(g_PluginState));
#endif

    memset(&g_plugin, 0, sizeof(g_plugin));
    memset(&g_MIDI, 0, sizeof(g_MIDI));
    memset(&g_Audio, 0, sizeof(g_Audio));
    memset(&g_Menus, 0, sizeof(g_Menus));

    CPWIN_LoadPlugin();

    ///////////////
    // INIT MIDI //
    ///////////////

    for (int i = 0; i < ARRSIZE(g_MIDI.SystemBuffers); i++)
    {
        MIDIHDR* head        = &g_MIDI.SystemBuffers[i].header;
        head->lpData         = &g_MIDI.SystemBuffers[i].buffer[0];
        head->dwBufferLength = ARRSIZE(g_MIDI.SystemBuffers[i].buffer);
        head->dwUser         = i;
    }
    CPWIN_MIDI_ConnectInput(0);

    ////////////////
    // INIT AUDIO //
    ////////////////

    g_Audio.SampleRate  = CPLUG_DEFAULT_SAMPLE_RATE;
    g_Audio.BlockSize   = CPLUG_DEFAULT_BLOCK_SIZE;
    g_Audio.NumChannels = g_plugin.getOutputBusChannelCount(g_plugin.UserPlugin, 0);
    cplug_assert(g_Audio.NumChannels == 1 || g_Audio.NumChannels == 2); // TODO: supported other configurations

    // Scan for device
    static const GUID _CLSID_MMDeviceEnumerator =
        {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
    static const GUID _IID_IMMDeviceEnumerator =
        {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
    HRESULT hr = CoCreateInstance(
        (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_CLSID_MMDeviceEnumerator),
        0,
        CLSCTX_ALL,
        (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_IID_IMMDeviceEnumerator),
        (void**)&g_Audio.pIMMDeviceEnumerator);
    cplug_assert(!FAILED(hr));

    CPWIN_Audio_SetDevice(-1); // -1 == default device
    CPWIN_Audio_Start();
    cplug_assert(g_Audio.ProcessBuffer);

    /////////////////
    // INIT WINDOW //
    /////////////////

    MSG  msg;
    HWND hWindow;

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = CPWIN_WindowProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"CPLUG - " TEXT(CPLUG_PLUGIN_NAME);
    wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc))
    {
        fprintf(stderr, "Could not register window class\n");
        return 1;
    }

    DPI_AWARENESS_CONTEXT prevDpiCtx = GetThreadDpiAwarenessContext();
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    g_plugin.UserGUI = g_plugin.createGUI(g_plugin.UserPlugin);
    cplug_assert(g_plugin.UserGUI != NULL);

    uint32_t guiWidth, guiHeight;
    g_plugin.getSize(g_plugin.UserGUI, &guiWidth, &guiHeight);

    RECT rect = {0, 0, (LONG)guiWidth, (LONG)guiHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

    hWindow = CreateWindowExW(
        0L,
        wc.lpszClassName,
        TEXT(CPLUG_PLUGIN_NAME),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        hInst,
        NULL);
    if (hWindow == NULL)
    {
        fprintf(stderr, "Could not create window\n");
        return 1;
    }
    if (prevDpiCtx)
        SetThreadDpiAwarenessContext(prevDpiCtx);

    ///////////////
    // INIT MENU //
    ///////////////

    g_Menus.hMain = CreateMenu();

    g_Menus.hAudioMenu          = CreatePopupMenu();
    g_Menus.hSampleRateSubmenu  = CreatePopupMenu();
    g_Menus.hBlockSizeSubmenu   = CreatePopupMenu();
    g_Menus.hAudioOutputSubmenu = CreatePopupMenu();
    g_Menus.hMIDIMenu           = CreatePopupMenu();
    g_Menus.hMIDIInputsSubMenu  = CreatePopupMenu();

    AppendMenuW(g_Menus.hMain, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hAudioMenu, L"Audio");
    AppendMenuW(g_Menus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hSampleRateSubmenu, L"Sample Rate");
    AppendMenuW(g_Menus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hBlockSizeSubmenu, L"Block Size");
    AppendMenuW(g_Menus.hAudioMenu, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hAudioOutputSubmenu, L"Outputs");

    AppendMenuW(g_Menus.hMain, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hMIDIMenu, L"MIDI");
    AppendMenuW(g_Menus.hMIDIMenu, MF_STRING | MF_POPUP, (UINT_PTR)g_Menus.hMIDIInputsSubMenu, L"Inputs");

    CPWIN_Menu_RefreshSampleRates();
    CPWIN_Menu_RefreshBlockSizes();
    CPWIN_Menu_RefreshAudioOutputs();
    CPWIN_Menu_RefreshMIDIInputs();

    SetMenu(hWindow, g_Menus.hMain);

    // Callback to detect connected/disconnected MIDI/Audio devices
    // Must be initialised afer the menu because the callback changes menu items based on new/removed devices
    CM_NOTIFY_FILTER notifyFilter;
    memset(&notifyFilter, 0, sizeof(notifyFilter));
    notifyFilter.cbSize     = sizeof(notifyFilter);
    notifyFilter.Flags      = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
    notifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;

    HRESULT result = CM_Register_Notification(&notifyFilter, hWindow, CPWIN_HandleDeviceChange, &g_hCMNotification);
    cplug_assert(result == CR_SUCCESS);
    cplug_assert(g_hCMNotification != NULL);

#ifdef HOTRELOAD_WATCH_DIR
    // Setup file watcher
    g_FlagExitFileWatchThread = 0;
    g_hFileWatchThread        = CreateThread(NULL, 0, &CPWIN_WatchFileChangesProc, hWindow, 0, 0);
    cplug_assert(g_hFileWatchThread != NULL);
#endif

    // Window ready
    g_plugin.setParent(g_plugin.UserGUI, hWindow);

    ShowWindow(hWindow, cmdshow);
    g_plugin.setVisible(g_plugin.UserGUI, true);
    SetForegroundWindow(hWindow);

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    ReleaseMutex(hMutexOneInstance);
    CloseHandle(hMutexOneInstance);
    return (int)msg.wParam;
}

LRESULT CALLBACK CPWIN_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE: // User pressed the window X/Close button
        // Shutdown device notifications
        CM_Unregister_Notification(g_hCMNotification);

        // Shutdown audio
        if (g_Audio.hAudioEvent)
            CPWIN_Audio_Stop();
        cplug_assert(g_Audio.ProcessBuffer != NULL);
        VirtualFree(g_Audio.ProcessBuffer, g_Audio.ProcessBufferCap, 0);
        cplug_assert(g_Audio.pIMMDevice != NULL);
        g_Audio.pIMMDevice->lpVtbl->Release(g_Audio.pIMMDevice);
        cplug_assert(g_Audio.pIMMDeviceEnumerator != NULL);
        g_Audio.pIMMDeviceEnumerator->lpVtbl->Release(g_Audio.pIMMDeviceEnumerator);

        // Shutdown MIDI
        CPWIN_MIDI_DisconnectInput();

        // Destroy plugin
#ifdef HOTRELOAD_WATCH_DIR
        // Stop file watcher
        g_FlagExitFileWatchThread = 1;
        WaitForSingleObject(g_hFileWatchThread, INFINITE);
        CloseHandle(g_hFileWatchThread);
        if (g_plugin.Library)
        {
#endif
            g_plugin.setVisible(g_plugin.UserGUI, false);
            g_plugin.setParent(g_plugin.UserGUI, NULL);
            g_plugin.destroyGUI(g_plugin.UserGUI);
            g_plugin.destroyPlugin(g_plugin.UserPlugin);
            g_plugin.libraryUnload();
#ifdef HOTRELOAD_WATCH_DIR
            FreeLibrary(g_plugin.Library);
        }
        if (g_PluginState.Data)
            VirtualFree(g_PluginState.Data, g_PluginState.BytesReserved, 0);
#endif
        DestroyWindow(hWnd);
        return 0;
    // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing
    case WM_SIZING: // User is resizing
    {
        // Note: The size of the child window is different to the size of our window.
        // The area of (RECT*)lParam below includes the toolbar, window title, window border, etc.
        RECT* parent = (RECT*)lParam;
        LONG  width  = parent->right - parent->left;
        LONG  height = parent->bottom - parent->top;

        // Calculate the size of the child window
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-adjustwindowrect
        RECT child = *parent;
        AdjustWindowRect(&child, WS_OVERLAPPEDWINDOW, TRUE);
        LONG padding_x = (child.right - child.left) - width;
        LONG padding_y = (child.bottom - child.top) - height;

        width      -= padding_x;
        height     -= padding_y;
        uint32_t w  = width < 0 ? 0 : width;
        uint32_t h  = height < 0 ? 0 : height;
        g_plugin.checkSize(g_plugin.UserGUI, &w, &h);
        width   = w;
        height  = h;
        width  += padding_x;
        height += padding_y;

        parent->right  = parent->left + width;
        parent->bottom = parent->top + height;

        return TRUE;
    }
    // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size
    case WM_SIZE: // Window has resized, minimised, maximised, or unminimised/unmaximised?
    {
        UINT Width  = LOWORD(lParam);
        UINT Height = HIWORD(lParam);

        switch (wParam)
        {
        case SIZE_RESTORED:
        case SIZE_MAXSHOW:
            g_plugin.setSize(g_plugin.UserGUI, Width, Height);
            g_plugin.setVisible(g_plugin.UserGUI, true);
            break;
        case SIZE_MINIMIZED:
        case SIZE_MAXHIDE:
            g_plugin.setVisible(g_plugin.UserGUI, false);
            break;
        case SIZE_MAXIMIZED:
            g_plugin.checkSize(g_plugin.UserGUI, &Width, &Height);
            g_plugin.setSize(g_plugin.UserGUI, Width, Height);
            break;
        }
        return 0;
    }
    case WM_DPICHANGED:
    {
        int   g_dpi  = HIWORD(wParam);
        FLOAT fscale = (float)g_dpi / USER_DEFAULT_SCREEN_DPI;
        g_plugin.setScaleFactor(g_plugin.UserGUI, fscale);

        RECT* const prcNewWindow = (RECT*)lParam;
        SetWindowPos(
            hWnd,
            NULL,
            prcNewWindow->left,
            prcNewWindow->top,
            prcNewWindow->right - prcNewWindow->left,
            prcNewWindow->bottom - prcNewWindow->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    case WM_COMMAND: // clicking nav menu items triggers commands. You can also send commands for other things
    {
        switch (wParam)
        {
#ifdef HOTRELOAD_BUILD_COMMAND
        case IDM_Hotreload:
        {
            UINT64 reloadStart = CPWIN_GetNowNS();

            if (g_plugin.Library)
            {
                // Deinit
                g_plugin.setVisible(g_plugin.UserGUI, false);
                g_plugin.setParent(g_plugin.UserGUI, NULL);
                g_plugin.destroyGUI(g_plugin.UserGUI);

                CPWIN_Audio_Stop();

                g_PluginState.BytesWritten = 0;
                g_PluginState.BytesRead    = 0;
                g_plugin.saveState(g_plugin.UserPlugin, &g_PluginState, CPWIN_WriteStateProc);

                g_plugin.destroyPlugin(g_plugin.UserPlugin);
                g_plugin.libraryUnload();
                BOOL ok = FreeLibrary(g_plugin.Library);
                cplug_assert(ok);
                memset(&g_plugin, 0, sizeof(g_plugin));
            }

            // Using 'system()' to call our build command is way simpler, but creates some stdout buffering problems...
            // Windows prefer that you use CreateProcessA.
            // The idea is we create a new process using CREATE_NEW_CONSOLE while setting HIDE falgs in the STARTUPINFO
            // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
            STARTUPINFO         si;
            PROCESS_INFORMATION pi;
            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));

            si.cb      = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW; // These flags are necessarry to stop a terminal window popping up as it
            si.wShowWindow = SW_HIDE;          // runs the command

            UINT64 buildStart = CPWIN_GetNowNS();
            // Run build command in child process.
            const LPWSTR cmd = TEXT(HOTRELOAD_BUILD_COMMAND);
            if (!CreateProcessW(0, cmd, 0, 0, FALSE, CREATE_NEW_CONSOLE, 0, 0, &si, &pi))
            {
                printf("CreateProcess failed (%lu).\n", GetLastError());
                return 1;
            }

            // Wait until child process exits
            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            UINT64 buildEnd = CPWIN_GetNowNS();
            // Cleanup build process
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (exitCode != 0)
            {
                cplug_log("[WARNING] Rebuild failed. Exited with code: %lu", exitCode);
            }
            else
            {
                CPWIN_LoadPlugin();
                g_plugin.loadState(g_plugin.UserPlugin, &g_PluginState, CPWIN_ReadStateProc);

                CPWIN_Audio_Start();

                g_plugin.UserGUI = g_plugin.createGUI(g_plugin.UserPlugin);
                cplug_assert(g_plugin.UserGUI != NULL);

                RECT size;
                GetClientRect(hWnd, &size);
                g_plugin.setSize(g_plugin.UserGUI, size.right - size.left, size.bottom - size.top);

                g_plugin.setParent(g_plugin.UserGUI, hWnd);
                g_plugin.setVisible(g_plugin.UserGUI, true);
            }

            UINT64 reloadEnd = CPWIN_GetNowNS();

            double rebuild_ms = (double)(buildEnd - buildStart) / 1.e6;
            double reload_ms  = (double)(reloadEnd - reloadStart) / 1.e6;
            fprintf(stderr, "Rebuild time %.2fms\n", rebuild_ms);
            fprintf(stderr, "Reload time %.2fms\n", reload_ms);
            break;
        }
#endif // HOTRELOAD_BUILD_COMMAND
        case IDM_SampleRate_44100:
        case IDM_SampleRate_48000:
        case IDM_SampleRate_88200:
        case IDM_SampleRate_96000:
        {
            CPWIN_Audio_Stop();
            const SIZE_T MaxChars = 8;
            WCHAR        text[MaxChars];
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmenustringw
            int numCharsCopied = GetMenuStringW(g_Menus.hSampleRateSubmenu, wParam, text, MaxChars, MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            g_Audio.SampleRate = _wtoi(text);
            CPWIN_Audio_Start();
            CPWIN_Menu_RefreshSampleRates();
            break;
        }
        case IDM_BlockSize_128:
        case IDM_BlockSize_192:
        case IDM_BlockSize_256:
        case IDM_BlockSize_384:
        case IDM_BlockSize_448:
        case IDM_BlockSize_512:
        case IDM_BlockSize_768:
        case IDM_BlockSize_1024:
        case IDM_BlockSize_2048:
        {
            CPWIN_Audio_Stop();
            const SIZE_T MaxChars = 8;
            WCHAR        text[MaxChars];
            int numCharsCopied = GetMenuStringW(g_Menus.hBlockSizeSubmenu, wParam, text, MaxChars, MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            g_Audio.BlockSize = _wtoi(text);
            CPWIN_Audio_Start();
            CPWIN_Menu_RefreshBlockSizes();
            break;
        }
        case IDM_RefreshAudioDeviceList:
            CPWIN_Menu_RefreshAudioOutputs();
            break;
        case IDM_RefreshMIDIDeviceList:
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        case IDM_HandleRemovedMIDIDevice:
        {
            fprintf(stderr, "Callback: Removed MIDI input device\n");
            if (g_MIDI.IsConnected)
            {
                UINT num = midiInGetNumDevs();
                if (num == 0)
                {
                    CPWIN_MIDI_DisconnectInput();
                    fprintf(stderr, "WARNING: Not connected to a MIDI input device\n");
                }
                else
                {
                    // Check it was the connected device which was removed
                    MIDIINCAPS2W caps;
                    UINT         i      = 0;
                    MMRESULT     result = 0;
                    for (; i < num; i++)
                    {
                        result = midiInGetDevCapsW(i, (MIDIINCAPSW*)&caps, sizeof(caps));
                        if (result == MMSYSERR_NOERROR &&
                            memcmp(&caps.NameGuid, &g_MIDI.LastConnectedInput.NameGuid, sizeof(caps.NameGuid)) &&
                            memcmp(&caps.ProductGuid, &g_MIDI.LastConnectedInput.ProductGuid, sizeof(caps.ProductGuid)))
                            break;
                    }
                    // Failed to match our connected device
                    if (i == num)
                    {
                        fprintf(
                            stderr,
                            "Connected MIDI input device was removed. Trying to connecting to the next available "
                            "device\n");
                        CPWIN_MIDI_DisconnectInput();
                        CPWIN_MIDI_ConnectInput(0);
                    }
                }
            }
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        }
        case IDM_HandleAddedMIDIDevice:
            fprintf(stderr, "Callback: New MIDI input device\n");
            if (g_MIDI.IsConnected == 0)
            {
                fprintf(stderr, "Trying to connect new device\n");
                CPWIN_MIDI_ConnectInput(0);
            }
            CPWIN_Menu_RefreshMIDIInputs();
            break;
        default:
        {
            if (wParam >= IDM_OFFSET_AUDIO_DEVICES && wParam < IDM_RefreshAudioDeviceList)
            {
                WPARAM idx = wParam - IDM_OFFSET_AUDIO_DEVICES;
                CPWIN_Audio_Stop();
                CPWIN_Audio_SetDevice((int)idx);
                CPWIN_Audio_Start();
                CPWIN_Menu_RefreshAudioOutputs();
            }
            if (wParam >= IDM_OFFSET_MIDI_DEVICES && wParam < IDM_RefreshMIDIDeviceList)
            {
                WPARAM idx = wParam - IDM_OFFSET_MIDI_DEVICES;
                CPWIN_MIDI_DisconnectInput();
                CPWIN_MIDI_ConnectInput((UINT)idx);
                CPWIN_Menu_RefreshMIDIInputs();
            }
        }
        }
        DrawMenuBar(hWnd);
        break;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void CPWIN_LoadPlugin()
{
#ifdef HOTRELOAD_LIB_PATH
    cplug_assert(g_plugin.Library == NULL);
#define CPLUG_GET_PROC_ADDR(name) GetProcAddress(g_plugin.Library, #name)
    g_plugin.Library = LoadLibraryW(TEXT(HOTRELOAD_LIB_PATH));
    cplug_assert(g_plugin.Library != NULL);
#else // not a hotrealoding build
#define CPLUG_GET_PROC_ADDR(func) func
#endif

    // This looks ugly because of the strict types in C++. C is ironically more elegant
    *(LONG_PTR*)&g_plugin.libraryLoad               = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_libraryLoad);
    *(LONG_PTR*)&g_plugin.libraryUnload             = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_libraryUnload);
    *(LONG_PTR*)&g_plugin.createPlugin              = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_createPlugin);
    *(LONG_PTR*)&g_plugin.destroyPlugin             = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_destroyPlugin);
    *(LONG_PTR*)&g_plugin.getOutputBusChannelCount  = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_getOutputBusChannelCount);
    *(LONG_PTR*)&g_plugin.setSampleRateAndBlockSize = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setSampleRateAndBlockSize);
    *(LONG_PTR*)&g_plugin.process                   = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_process);
    *(LONG_PTR*)&g_plugin.saveState                 = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_saveState);
    *(LONG_PTR*)&g_plugin.loadState                 = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_loadState);

    *(LONG_PTR*)&g_plugin.createGUI      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_createGUI);
    *(LONG_PTR*)&g_plugin.destroyGUI     = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_destroyGUI);
    *(LONG_PTR*)&g_plugin.setParent      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setParent);
    *(LONG_PTR*)&g_plugin.setVisible     = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setVisible);
    *(LONG_PTR*)&g_plugin.setScaleFactor = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setScaleFactor);
    *(LONG_PTR*)&g_plugin.getSize        = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_getSize);
    *(LONG_PTR*)&g_plugin.checkSize      = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_checkSize);
    *(LONG_PTR*)&g_plugin.setSize        = (LONG_PTR)CPLUG_GET_PROC_ADDR(cplug_setSize);

    cplug_assert(NULL != g_plugin.libraryLoad);
    cplug_assert(NULL != g_plugin.libraryUnload);
    cplug_assert(NULL != g_plugin.createPlugin);
    cplug_assert(NULL != g_plugin.destroyPlugin);
    cplug_assert(NULL != g_plugin.getOutputBusChannelCount);
    cplug_assert(NULL != g_plugin.setSampleRateAndBlockSize);
    cplug_assert(NULL != g_plugin.process);
    cplug_assert(NULL != g_plugin.saveState);
    cplug_assert(NULL != g_plugin.loadState);

    cplug_assert(NULL != g_plugin.createGUI);
    cplug_assert(NULL != g_plugin.destroyGUI);
    cplug_assert(NULL != g_plugin.setParent);
    cplug_assert(NULL != g_plugin.setVisible);
    cplug_assert(NULL != g_plugin.setScaleFactor);
    cplug_assert(NULL != g_plugin.getSize);
    cplug_assert(NULL != g_plugin.checkSize);
    cplug_assert(NULL != g_plugin.setSize);

    g_plugin.libraryLoad();
    g_plugin.HostContext.type           = CPLUG_PLUGIN_IS_STANDALONE;
    g_plugin.HostContext.sendParamEvent = CPWIN_HostContext_SendParamEvent;
    g_plugin.HostContext.rescan         = CPWIN_HostContext_Rescan;
    g_plugin.HostContext.getHostName    = CPWIN_HostContext_GetHostName;

    g_plugin.UserPlugin = g_plugin.createPlugin(&g_plugin.HostContext);
    cplug_assert(g_plugin.UserPlugin != NULL);
}

#ifdef HOTRELOAD_WATCH_DIR
#pragma region PLUGIN_STATE
int64_t CPWIN_WriteStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    cplug_assert(stateCtx != NULL);
    cplug_assert(writePos != NULL);
    cplug_assert(numBytesToWrite > 0);

    // The idea is we reserve heaps of address space up front, and hope we never spill over it.
    // In the rare case your plugin does, simply reserve more address space
    // Some plugins may save big audio files in their state, hence the BIG reserve
    struct CPWIN_PluginStateContext* ctx = (struct CPWIN_PluginStateContext*)stateCtx;

    if (ctx->Data == NULL)
    {
        const SIZE_T largePageSize  = GetLargePageMinimum();
        SIZE_T       bigreserve     = (SIZE_T)CPWIN_RoundUp(numBytesToWrite, largePageSize);
        bigreserve                 *= 8;
        ctx->Data                   = (BYTE*)VirtualAlloc(NULL, bigreserve, MEM_RESERVE, PAGE_READWRITE);
        cplug_assert(ctx->Data != NULL);
        ctx->BytesReserved = bigreserve;

        SIZE_T bigcommit = numBytesToWrite * 4;
        LPVOID retval    = VirtualAlloc(ctx->Data, bigcommit, MEM_COMMIT, PAGE_READWRITE);
        cplug_assert(retval != NULL);
        ctx->BytesCommited = bigcommit;
    }
    // If you hit this assertion, you need to reserve more address space above!
    cplug_assert(numBytesToWrite < (ctx->BytesReserved - ctx->BytesCommited));
    if (numBytesToWrite > (ctx->BytesCommited - ctx->BytesWritten))
    {
        SIZE_T nextcommit = 2 * ctx->BytesCommited;
        LPVOID retval     = VirtualAlloc(ctx->Data, nextcommit, MEM_COMMIT, PAGE_READWRITE);
        cplug_assert(retval != NULL);
        ctx->BytesCommited = nextcommit;
    }
    memcpy(ctx->Data + ctx->BytesWritten, writePos, numBytesToWrite);
    ctx->BytesWritten += numBytesToWrite;
    return numBytesToWrite;
}

int64_t CPWIN_ReadStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    struct CPWIN_PluginStateContext* ctx = (struct CPWIN_PluginStateContext*)stateCtx;

    cplug_assert(stateCtx != NULL);
    cplug_assert(readPos != NULL);
    cplug_assert(maxBytesToRead > 0);

    SIZE_T remainingBytes     = ctx->BytesWritten - ctx->BytesRead;
    SIZE_T bytesToActualyRead = maxBytesToRead > remainingBytes ? remainingBytes : maxBytesToRead;

    if (bytesToActualyRead)
    {
        memcpy(readPos, ctx->Data + ctx->BytesRead, bytesToActualyRead);
        ctx->BytesRead += bytesToActualyRead;
    }

    return bytesToActualyRead;
}
#pragma endregion PLUGIN_STATE

DWORD WINAPI CPWIN_WatchFileChangesProc(LPVOID hwnd)
{
    // Most this code was taken from here: https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8
    fprintf(stderr, "Watching folder %s\n", HOTRELOAD_WATCH_DIR);

    HANDLE hDirectory = CreateFileW(
        TEXT(HOTRELOAD_WATCH_DIR),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    if (hDirectory == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to get directory handle\n");
        return 1;
    }
    OVERLAPPED overlapped;
    overlapped.hEvent = CreateEventW(NULL, FALSE, 0, NULL);

    BYTE infobuffer[1024];
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
    BOOL success = ReadDirectoryChangesW(
        hDirectory,
        infobuffer,
        sizeof(infobuffer),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &overlapped,
        NULL);
    if (!success)
    {
        fprintf(stderr, "Failed to queue info buffer\n");
        return 1;
    }

    int throttlereload = 0;
    while (g_FlagExitFileWatchThread == 0)
    {
        DWORD result = WaitForSingleObject(overlapped.hEvent, 50);

        if (result == WAIT_TIMEOUT)
        {
            if (throttlereload != 0)
                PostMessageW((HWND)hwnd, WM_COMMAND, IDM_Hotreload, 0);
            throttlereload = 0;
        }
        else if (result == WAIT_OBJECT_0)
        {
            DWORD bytes_transferred;
            GetOverlappedResult(hDirectory, &overlapped, &bytes_transferred, TRUE);

            FILE_NOTIFY_INFORMATION* event = (FILE_NOTIFY_INFORMATION*)infobuffer;

            while (TRUE)
            {
                DWORD name_len = event->FileNameLength / sizeof(wchar_t);

                if (event->Action == FILE_ACTION_MODIFIED)
                {
                    fwprintf(stderr, L"File changed: %.*s\n", name_len, event->FileName);
                    throttlereload++;
                }

                // Iterate events
                if (event->NextEntryOffset)
                    *((BYTE**)&event) += event->NextEntryOffset;
                else
                    break;
            }

            // Queue next event
            success = ReadDirectoryChangesW(
                hDirectory,
                infobuffer,
                sizeof(infobuffer),
                TRUE,
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL,
                &overlapped,
                NULL);

            if (!success)
            {
                fprintf(stderr, "Failed to queue info buffer\n");
                return 1;
            }
        }
    }
    return 0;
}
#endif // HOTRELOAD_WATCH_DIR

#pragma region MENUS

static inline UINT CPWIN_MenuFlag(UINT a, UINT b) { return a == b ? (MF_STRING | MF_CHECKED) : MF_STRING; }

void CPWIN_Menu_RefreshSampleRates()
{
    while (RemoveMenu(g_Menus.hSampleRateSubmenu, 0, MF_BYPOSITION))
    {
    }

    AppendMenuW(g_Menus.hSampleRateSubmenu, CPWIN_MenuFlag(g_Audio.SampleRate, 44100), IDM_SampleRate_44100, L"44100");
    AppendMenuW(g_Menus.hSampleRateSubmenu, CPWIN_MenuFlag(g_Audio.SampleRate, 48000), IDM_SampleRate_48000, L"48000");
    AppendMenuW(g_Menus.hSampleRateSubmenu, CPWIN_MenuFlag(g_Audio.SampleRate, 88200), IDM_SampleRate_88200, L"88200");
    AppendMenuW(g_Menus.hSampleRateSubmenu, CPWIN_MenuFlag(g_Audio.SampleRate, 96000), IDM_SampleRate_96000, L"96000");
}

void CPWIN_Menu_RefreshBlockSizes()
{
    while (RemoveMenu(g_Menus.hBlockSizeSubmenu, 0, MF_BYPOSITION))
    {
    }

    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 128), IDM_BlockSize_128, L"128");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 192), IDM_BlockSize_192, L"192");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 256), IDM_BlockSize_256, L"256");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 384), IDM_BlockSize_384, L"384");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 448), IDM_BlockSize_448, L"448");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 512), IDM_BlockSize_512, L"512");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 768), IDM_BlockSize_768, L"768");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 1024), IDM_BlockSize_1024, L"1024");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, CPWIN_MenuFlag(g_Audio.BlockSize, 2048), IDM_BlockSize_2048, L"2048");
}

void CPWIN_Menu_RefreshAudioOutputs()
{
    while (RemoveMenu(g_Menus.hAudioOutputSubmenu, 0, MF_BYPOSITION))
    {
    }

    IMMDeviceCollection* pCollection = NULL;
    g_Audio.pIMMDeviceEnumerator->lpVtbl
        ->EnumAudioEndpoints(g_Audio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
    cplug_assert(pCollection != NULL);

    pCollection->lpVtbl->GetCount(pCollection, &g_Menus.numAudioOutputs);

    for (UINT i = 0; i < g_Menus.numAudioOutputs; i++)
    {
        IMMDevice* pDevice = NULL;

        pCollection->lpVtbl->Item(pCollection, i, &pDevice);
        if (pDevice != NULL)
        {
            WCHAR* deviceID = NULL;
            pDevice->lpVtbl->GetId(pDevice, &deviceID);

            static const PROPERTYKEY _PKEY_Device_FriendlyName = {
                {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
                14};

            IPropertyStore* pProperties = NULL;
            HRESULT         hr          = pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pProperties);
            cplug_assert(!FAILED(hr));

            PROPVARIANT varName;
            pProperties->lpVtbl->GetValue(pProperties, CPLUG_WTF_IS_A_REFERENCE(_PKEY_Device_FriendlyName), &varName);

            if (varName.vt != VT_EMPTY)
            {
                UINT uFlags = MF_STRING;
                if (0 == wcsncmp(deviceID, g_Audio.DeviceIDBuffer, ARRSIZE(g_Audio.DeviceIDBuffer)))
                    uFlags |= MF_CHECKED;

                AppendMenuW(g_Menus.hAudioOutputSubmenu, uFlags, IDM_OFFSET_AUDIO_DEVICES + i, varName.pwszVal);
            }

            PropVariantClear(&varName);

            pProperties->lpVtbl->Release(pProperties);
            pDevice->lpVtbl->Release(pDevice);
            CoTaskMemFree(deviceID);
        }
    }

    pCollection->lpVtbl->Release(pCollection);

    AppendMenuW(g_Menus.hAudioOutputSubmenu, MF_SEPARATOR, IDM_RefreshAudioDeviceList - 1, NULL);
    AppendMenuW(g_Menus.hAudioOutputSubmenu, MF_STRING, IDM_RefreshAudioDeviceList, L"Refresh list");
}

void CPWIN_Menu_RefreshMIDIInputs()
{
    while (RemoveMenu(g_Menus.hMIDIInputsSubMenu, 0, MF_BYPOSITION))
    {
    }

    MIDIINCAPS2W caps;

    int numMidiIn = midiInGetNumDevs();
    for (int i = 0; i < numMidiIn; i++)
    {
        MMRESULT result = midiInGetDevCapsW(i, (MIDIINCAPSW*)&caps, sizeof(caps));
        cplug_assert(result == MMSYSERR_NOERROR);

        if (result == MMSYSERR_NOERROR)
        {
            UINT uFlags = MF_STRING;
            if (0 == memcmp(&caps.NameGuid, &g_MIDI.LastConnectedInput.NameGuid, sizeof(caps.NameGuid)) &&
                0 == memcmp(&caps.ProductGuid, &g_MIDI.LastConnectedInput.ProductGuid, sizeof(caps.ProductGuid)))
                uFlags |= MF_CHECKED;

            AppendMenuW(g_Menus.hMIDIInputsSubMenu, uFlags, IDM_OFFSET_MIDI_DEVICES + i, caps.szPname);
        }
    }
}

#pragma endregion MENUS

DWORD CALLBACK CPWIN_HandleDeviceChange(
    HCMNOTIFICATION       hNotify,
    PVOID                 hwnd,
    CM_NOTIFY_ACTION      Action,
    PCM_NOTIFY_EVENT_DATA EventData,
    DWORD                 EventDataSize)
{
    WCHAR* InstanceId = &EventData->u.DeviceInstance.InstanceId[0];

    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
        // I've found updating MIDI lists here less reliable than in the following 2 enums
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
        // MIDI input instance IDs come in this format:
        // SWD\MMDEVAPI\MIDII_(4 byte hex).P_(2 byte hex)
        // Software device - MMDevice API - MIDI Input
        // For audio devices I'm less sure of thier format.
        // The format I have seen on my own PC is: L"SWD\MMDEVAPI\{0.0.0.00000000}.{(GUID)}""
        // TODO: test for patten used by audio devices
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/install/device-instance-ids
        if (0 == wcsncmp(L"SWD\\MMDEVAPI\\MIDII_", InstanceId, 19))
        {
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleRemovedMIDIDevice, 0);
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_RefreshMIDIDeviceList, 0);
        }
        else if (0 == wcsncmp(L"SWD\\MMDEVAPI\\", InstanceId, 13))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_RefreshAudioDeviceList, 0);
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
        if (0 == wcsncmp(L"SWD\\MMDEVAPI\\MIDII_", InstanceId, 19))
        {
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleAddedMIDIDevice, 0);
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_RefreshMIDIDeviceList, 0);
        }
        else if (0 == wcsncmp(L"SWD\\MMDEVAPI\\", InstanceId, 13))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_RefreshAudioDeviceList, 0);
        break;
    default:
        break;
    }
    return 0;
}

#pragma region MIDI

void CALLBACK CPWIN_MIDIInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    /* https://learn.microsoft.com/en-gb/windows/win32/multimedia/mim-data?redirectedfrom=MSDN */
    if (wMsg == MM_MIM_DATA)
    {
        MIDIMessage midi;
        LONG        writePos;

        /* take first 3 bytes. remember, the rest are junk, including possibly the ones we're taking */
        midi.bytesAsInt  = dwParam1 & 0xffffff;
        midi.timestampMs = (UINT)dwParam2;

        writePos = _InterlockedCompareExchange(&g_MIDI.RingBuffer.writePos, 0, 0);

        g_MIDI.RingBuffer.buffer[writePos] = midi;
        writePos++;
        writePos = writePos % ARRSIZE(g_MIDI.RingBuffer.buffer);
        _InterlockedExchange(&g_MIDI.RingBuffer.writePos, writePos);
    }
    /* handle sysex*/
    /* https://www.midi.org/specifications-old/item/table-4-universal-system-exclusive-messages */
    /* else if (wMsg == MIM_LONGDATA) {} */
}

UINT CPWIN_MIDI_ConnectInput(UINT portNum)
{
    UINT result;

    // Set up are MIDI reading callback
    cplug_assert(g_MIDI.hInput == NULL);
    result = midiInOpen(&g_MIDI.hInput, portNum, (DWORD_PTR)&CPWIN_MIDIInProc, 0, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR)
        goto failed;

    memset(&g_MIDI.LastConnectedInput, 0, sizeof(g_MIDI.LastConnectedInput));
    result = midiInGetDevCapsW(0, (MIDIINCAPSW*)&g_MIDI.LastConnectedInput, sizeof(g_MIDI.LastConnectedInput));
    cplug_assert(result == MMSYSERR_NOERROR);

    for (int i = 0; i < ARRSIZE(g_MIDI.SystemBuffers); i++)
    {
        result =
            midiInPrepareHeader(g_MIDI.hInput, &g_MIDI.SystemBuffers[i].header, sizeof(g_MIDI.SystemBuffers[i].header));
        if (result != MMSYSERR_NOERROR)
            goto failed;
        result = midiInAddBuffer(g_MIDI.hInput, &g_MIDI.SystemBuffers[i].header, sizeof(MIDIHDR));
        if (result != MMSYSERR_NOERROR)
            goto failed;
    }

    result = midiInStart(g_MIDI.hInput);
    if (result != MMSYSERR_NOERROR)
        goto failed;

    g_MIDI.IsConnected = 1;
    fprintf(stderr, "Connected to MIDI input %u\n", portNum);

    return result;

failed:
    if (g_MIDI.hInput)
    {
        midiInClose(g_MIDI.hInput);
        g_MIDI.hInput = 0;
    }
    return result;
}

void CPWIN_MIDI_DisconnectInput()
{
    if (g_MIDI.IsConnected)
    {
        UINT result;
        midiInReset(g_MIDI.hInput);
        midiInStop(g_MIDI.hInput);

        for (int i = 0; i < ARRSIZE(g_MIDI.SystemBuffers); i++)
        {
            MIDIHDR* head = &g_MIDI.SystemBuffers[i].header;
            result        = midiInUnprepareHeader(g_MIDI.hInput, head, sizeof(*head));

            if (result != MMSYSERR_NOERROR)
                break;
        }
        midiInClose(g_MIDI.hInput);
        g_MIDI.hInput      = NULL;
        g_MIDI.IsConnected = 0;
        memset(&g_MIDI.LastConnectedInput, 0, sizeof(g_MIDI.LastConnectedInput));
    }
}

#pragma endregion MIDI

#pragma region AUDIO

typedef struct WindowsProcessContext
{
    CplugProcessContext cplugContext;
    float*              output[2];
} WindowsProcessContext;

bool CPWIN_Audio_enqueueEvent(struct CplugProcessContext* ctx, const CplugEvent* e, uint32_t frameIdx) { return true; }

bool CPWIN_Audio_dequeueEvent(struct CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
{
    if (frameIdx >= ctx->numFrames)
        return false;

    LONG head = _InterlockedCompareExchange(&g_MIDI.RingBuffer.writePos, 0, 0);
    LONG tail = _InterlockedCompareExchange(&g_MIDI.RingBuffer.readPos, 0, 0);
    if (head != tail)
    {
        MIDIMessage* msg       = &g_MIDI.RingBuffer.buffer[tail];
        event->midi.type       = CPLUG_EVENT_MIDI;
        event->midi.bytesAsInt = msg->bytesAsInt;

        tail++;
        tail %= CPLUG_MIDI_RINGBUFFER_SIZE;

        g_MIDI.RingBuffer.readPos = tail;
        return true;
    }

    event->processAudio.type     = CPLUG_EVENT_PROCESS_AUDIO;
    event->processAudio.endFrame = ctx->numFrames;
    return true;
}

float** CPWIN_Audio_getAudioInput(const struct CplugProcessContext* ctx, uint32_t busIdx) { return NULL; }

float** CPWIN_Audio_getAudioOutput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const WindowsProcessContext* winctx = (const WindowsProcessContext*)ctx;
    if (busIdx == 0)
        return (float**)&winctx->output[0];
    return NULL;
}

void CPWIN_Audio_Process(const UINT32 blockSize)
{
    BYTE*   outBuffer            = NULL;
    UINT32  remainingBlockFrames = blockSize;
    HRESULT hr = g_Audio.pIAudioRenderClient->lpVtbl->GetBuffer(g_Audio.pIAudioRenderClient, blockSize, &outBuffer);
    cplug_assert(outBuffer != NULL);
    if (FAILED(hr))
        return;

    if (g_Audio.ProcessBufferNumOverprocessedFrames)
    {
        // Our remaining samples are already in a deinterleaved format
        UINT32 framesToCopy = g_Audio.ProcessBufferNumOverprocessedFrames < remainingBlockFrames
                                  ? g_Audio.ProcessBufferNumOverprocessedFrames
                                  : remainingBlockFrames;
        SIZE_T bytesToCopy  = sizeof(float) * g_Audio.NumChannels * framesToCopy;
        memcpy(outBuffer, g_Audio.ProcessBuffer, bytesToCopy);

        remainingBlockFrames                        -= framesToCopy;
        g_Audio.ProcessBufferNumOverprocessedFrames -= framesToCopy;
        outBuffer                                   += bytesToCopy;
        cplug_assert(remainingBlockFrames < blockSize); // check overflow
    }

    WindowsProcessContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cplugContext.numFrames      = g_Audio.BlockSize;
    ctx.cplugContext.enqueueEvent   = CPWIN_Audio_enqueueEvent;
    ctx.cplugContext.dequeueEvent   = CPWIN_Audio_dequeueEvent;
    ctx.cplugContext.getAudioInput  = CPWIN_Audio_getAudioInput;
    ctx.cplugContext.getAudioOutput = CPWIN_Audio_getAudioOutput;

    SIZE_T processBufferOffset = sizeof(float) * g_Audio.NumChannels * g_Audio.ProcessBufferMaxFrames;
    processBufferOffset        = (SIZE_T)CPWIN_RoundUp(processBufferOffset, 32);
    ctx.output[0]              = (float*)(g_Audio.ProcessBuffer + processBufferOffset);
    ctx.output[1]              = ctx.output[0] + g_Audio.BlockSize;

    while (remainingBlockFrames > 0)
    {
        cplug_assert(g_Audio.ProcessBufferNumOverprocessedFrames == 0);

        g_plugin.process(g_plugin.UserPlugin, &ctx.cplugContext);

        UINT32 framesToCopy = remainingBlockFrames < g_Audio.BlockSize ? remainingBlockFrames : g_Audio.BlockSize;
        SIZE_T bytesToCopy  = sizeof(float) * g_Audio.NumChannels * framesToCopy;

        UINT32 i                 = 0;
        float* outputInterleaved = (float*)outBuffer;
        for (; i < framesToCopy; i++)
            for (UINT32 ch = 0; ch < g_Audio.NumChannels; ch++)
                *outputInterleaved++ = ctx.output[ch][i];

        float* remainingInterleaved = (float*)g_Audio.ProcessBuffer;
        for (; i < g_Audio.BlockSize; i++)
            for (UINT32 ch = 0; ch < g_Audio.NumChannels; ch++)
                *remainingInterleaved++ = ctx.output[ch][i];
        g_Audio.ProcessBufferNumOverprocessedFrames = g_Audio.BlockSize - framesToCopy;

        remainingBlockFrames -= framesToCopy;
        outBuffer            += bytesToCopy;

        cplug_assert(remainingBlockFrames < blockSize); // check overflow
    }

    // This has a scary name 'Release', however I don't think any resources are deallocated,
    // rather space within a preallocated block is marked reserved/unreserved
    // This is just how you hand the buffer back to windows
    g_Audio.pIAudioRenderClient->lpVtbl->ReleaseBuffer(g_Audio.pIAudioRenderClient, blockSize, 0);
}

DWORD WINAPI CPWIN_Audio_RunProcessThread(LPVOID data)
{
    // NOTE: requested sizes do not come in the size requested, or even in a multiple of 32
    // On my machine, requesting a block size of 512 at 44100Hz gives me a max frame size of 1032 and variable
    // block sizes, usually consisting of 441 frames.The windows docs say this to guarantee enough audio in reserve to
    // prevent audible glicthes:
    // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
    // Unfortunately for us, this means we need to play silly games caching audio within a preallocated buffer to
    // make sure the users App recieves a sensible block size
    CPWIN_Audio_Process(g_Audio.ProcessBufferMaxFrames);

    g_Audio.pIAudioClient->lpVtbl->Start(g_Audio.pIAudioClient);

    while (!g_Audio.FlagExitAudioThread)
    {
        WaitForSingleObject(g_Audio.hAudioEvent, INFINITE);

        UINT32  padding = 0;
        HRESULT hr      = g_Audio.pIAudioClient->lpVtbl->GetCurrentPadding(g_Audio.pIAudioClient, &padding);

        if (FAILED(hr))
            continue;

        cplug_assert(g_Audio.ProcessBufferMaxFrames >= padding);
        UINT32 blockSize = g_Audio.ProcessBufferMaxFrames - padding;
        if (blockSize == 0)
            continue;

        CPWIN_Audio_Process(blockSize);
    }

    return 0;
}

void CPWIN_Audio_Stop()
{
    if (g_Audio.hAudioProcessThread == NULL)
    {
        cplug_log("[WARNING] Called CPWIN_Audio_Stop() when audio is not running");
        return;
    }
    cplug_assert(g_Audio.FlagExitAudioThread == 0);
    g_Audio.FlagExitAudioThread = 1;
    cplug_assert(g_Audio.hAudioEvent);
    SetEvent(g_Audio.hAudioEvent);

    cplug_assert(g_Audio.hAudioProcessThread != NULL);
    WaitForSingleObject(g_Audio.hAudioProcessThread, INFINITE);
    CloseHandle(g_Audio.hAudioProcessThread);
    g_Audio.hAudioProcessThread = NULL;

    cplug_assert(g_Audio.pIAudioClient != NULL);
    g_Audio.pIAudioClient->lpVtbl->Stop(g_Audio.pIAudioClient);
    cplug_assert(g_Audio.pIAudioRenderClient != NULL);
    g_Audio.pIAudioRenderClient->lpVtbl->Release(g_Audio.pIAudioRenderClient);
    cplug_assert(g_Audio.pIAudioClient != NULL);
    g_Audio.pIAudioClient->lpVtbl->Release(g_Audio.pIAudioClient);
    g_Audio.pIAudioClient       = NULL;
    g_Audio.pIAudioRenderClient = NULL;

    cplug_assert(g_Audio.hAudioEvent != NULL);
    CloseHandle(g_Audio.hAudioEvent);
    g_Audio.hAudioEvent = NULL;
}

void CPWIN_Audio_SetDevice(int deviceIdx)
{
    cplug_assert(g_Audio.hAudioProcessThread == NULL);

    if (g_Audio.pIMMDevice != NULL)
        g_Audio.pIMMDevice->lpVtbl->Release(g_Audio.pIMMDevice);

    if (deviceIdx >= 0)
    {
        IMMDeviceCollection* pCollection = NULL;
        g_Audio.pIMMDeviceEnumerator->lpVtbl
            ->EnumAudioEndpoints(g_Audio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
        cplug_assert(pCollection != NULL);

        UINT numDevices = 0;
        pCollection->lpVtbl->GetCount(pCollection, &numDevices);

        if ((UINT)deviceIdx < numDevices)
            pCollection->lpVtbl->Item(pCollection, (UINT)deviceIdx, &g_Audio.pIMMDevice);

        pCollection->lpVtbl->Release(pCollection);
    }

    if (g_Audio.pIMMDevice == NULL)
    {
        // eConsole or eMultimedia? Microsoft say console is for games, multimedia for playing live music
        // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles
        HRESULT hr = g_Audio.pIMMDeviceEnumerator->lpVtbl->GetDefaultAudioEndpoint(
            g_Audio.pIMMDeviceEnumerator,
            eRender,
            eMultimedia,
            &g_Audio.pIMMDevice);
        cplug_assert(!FAILED(hr));
    }

    WCHAR* audioDeviceID = NULL;
    // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-getid
    g_Audio.pIMMDevice->lpVtbl->GetId(g_Audio.pIMMDevice, &audioDeviceID);
    wcscpy_s(g_Audio.DeviceIDBuffer, ARRSIZE(g_Audio.DeviceIDBuffer), audioDeviceID);
    g_Audio.DeviceIDBuffer[ARRSIZE(g_Audio.DeviceIDBuffer) - 1] = 0;
    CoTaskMemFree(audioDeviceID);
}

void CPWIN_Audio_Start()
{
#ifdef HOTRELOAD_BUILD_COMMAND
    if (g_plugin.Library == NULL)
    {
        cplug_log("[FAILED] Called CPWIN_Audio_Start when no plugin is loaded");
        return;
    }
#endif
    cplug_assert(g_Audio.SampleRate != 0);
    cplug_assert(g_Audio.BlockSize != 0);
    static const IID _IID_IAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
    static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
        {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    static const IID _IID_IAudioRenderClient =
        {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};

    cplug_assert(g_Audio.pIMMDevice != NULL);
    cplug_assert(g_Audio.pIAudioClient == NULL);
    HRESULT hr = g_Audio.pIMMDevice->lpVtbl->Activate(
        g_Audio.pIMMDevice,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioClient),
        CLSCTX_ALL,
        0,
        (void**)&g_Audio.pIAudioClient);
    cplug_assert(!FAILED(hr));

    // https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible
    WAVEFORMATEXTENSIBLE fmtex;
    memset(&fmtex, 0, sizeof(fmtex));
    fmtex.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    fmtex.Format.nChannels            = g_Audio.NumChannels;
    fmtex.Format.nSamplesPerSec       = g_Audio.SampleRate;
    fmtex.Format.wBitsPerSample       = 32;
    fmtex.Format.nBlockAlign          = (fmtex.Format.nChannels * fmtex.Format.wBitsPerSample) / 8;
    fmtex.Format.nAvgBytesPerSec      = fmtex.Format.nSamplesPerSec * fmtex.Format.nBlockAlign;
    fmtex.Format.cbSize               = 22;
    fmtex.Samples.wValidBitsPerSample = 32;

    if (fmtex.Format.nChannels == 1)
        fmtex.dwChannelMask = SPEAKER_FRONT_CENTER;
    else
        fmtex.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

    fmtex.SubFormat = _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    REFERENCE_TIME reftime = (REFERENCE_TIME)((double)g_Audio.BlockSize / ((double)g_Audio.SampleRate * 1.e-7));

    // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
    hr = g_Audio.pIAudioClient->lpVtbl->Initialize(
        g_Audio.pIAudioClient,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        reftime,
        0,
        (WAVEFORMATEX*)&fmtex,
        0);
    cplug_assert(!FAILED(hr));

    hr = g_Audio.pIAudioClient->lpVtbl->GetBufferSize(g_Audio.pIAudioClient, &g_Audio.ProcessBufferMaxFrames);
    cplug_assert(!FAILED(hr));

    g_Audio.pIAudioClient->lpVtbl->GetService(
        g_Audio.pIAudioClient,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioRenderClient),
        (void**)&g_Audio.pIAudioRenderClient);

    cplug_assert(g_Audio.hAudioEvent == NULL);
    g_Audio.hAudioEvent = CreateEventW(0, 0, 0, 0);
    cplug_assert(g_Audio.hAudioEvent != NULL);
    g_Audio.pIAudioClient->lpVtbl->SetEventHandle(g_Audio.pIAudioClient, g_Audio.hAudioEvent);

    SIZE_T req_bytes_reserve    = sizeof(float) * g_Audio.NumChannels * g_Audio.ProcessBufferMaxFrames;
    SIZE_T req_bytes_processing = sizeof(float) * g_Audio.NumChannels * g_Audio.BlockSize;
    req_bytes_reserve           = (SIZE_T)CPWIN_RoundUp(req_bytes_reserve, 32);
    req_bytes_processing        = (SIZE_T)CPWIN_RoundUp(req_bytes_processing, 32);

    SIZE_T requiredCap = (SIZE_T)CPWIN_RoundUp(req_bytes_reserve + req_bytes_processing, 4096);
    if (requiredCap > g_Audio.ProcessBufferCap)
    {
        if (g_Audio.ProcessBuffer != NULL)
            VirtualFree(g_Audio.ProcessBuffer, g_Audio.ProcessBufferCap, 0);

        g_Audio.ProcessBufferCap = requiredCap;
        g_Audio.ProcessBuffer =
            (BYTE*)VirtualAlloc(NULL, g_Audio.ProcessBufferCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        cplug_assert(g_Audio.ProcessBuffer != NULL);
    }

    g_plugin.setSampleRateAndBlockSize(g_plugin.UserPlugin, g_Audio.SampleRate, g_Audio.BlockSize);

    g_Audio.ProcessBufferNumOverprocessedFrames = 0;
    g_Audio.FlagExitAudioThread                 = 0;

    g_Audio.hAudioProcessThread = CreateThread(NULL, 0, CPWIN_Audio_RunProcessThread, NULL, 0, 0);
    cplug_assert(g_Audio.hAudioProcessThread != NULL);
}
#pragma endregion AUDIO