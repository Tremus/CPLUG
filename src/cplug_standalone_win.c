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
#include <avrt.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <mmeapi.h>
#include <synchapi.h>

#include <cplug.h>
#include <stdio.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Avrt.lib")

#define cplug_assert(cond) (cond) ? (void)0 : __debugbreak()

#if !defined(CPLUG_MIDI_BUFFER_COUNT) || !defined(CPLUG_MIDI_BUFFER_SIZE) || !defined(CPLUG_MIDI_RINGBUFFER_SIZE)
#define CPLUG_MIDI_BUFFER_COUNT    4
#define CPLUG_MIDI_BUFFER_SIZE     1024
#define CPLUG_MIDI_RINGBUFFER_SIZE 128
#endif

#if !defined(CPLUG_DEFAULT_BLOCK_SIZE) || !defined(CPLUG_DEFAULT_SAMPLE_RATE)
// WARNING: Only a sample rate of 48000 with a block size of 512 or less appears to work on my machine. Any other
// setting produces stutters. I don't know why this is. Several DAWs I use seem to have this problem too when using
// WASAPI. This may be Microsoft jank, it also may be me not understanding how use the API properly
#define CPLUG_DEFAULT_SAMPLE_RATE 48000
#define CPLUG_DEFAULT_BLOCK_SIZE  512
#endif

#ifdef __cplusplus
#define CPLUG_WTF_IS_A_REFERENCE(obj) obj
#else
#define CPLUG_WTF_IS_A_REFERENCE(obj) &obj
#endif

#if defined(HOTRELOAD_WATCH_DIR) || defined(HOTRELOAD_LIB_PATH) || defined(HOTRELOAD_BUILD_COMMAND)
#if !defined(HOTRELOAD_WATCH_DIR) || !defined(HOTRELOAD_LIB_PATH) || !defined(HOTRELOAD_BUILD_COMMAND)
#error You need to define all 3
#endif
#endif

////////////
// Plugin //
////////////

struct CPWIN_Plugin
{
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
bool CPWIN_HostContext_RequestResize(CplugHostContext* ctx, uint32_t width, uint32_t height);
#ifdef __clang__
_Static_assert(sizeof(CplugHostContext) == 40, "You may need to add support for new methods");
#endif

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

struct CPWIN_Hotreload
{
    HMODULE hPluginDLL;
    UINT    Version;

    HANDLE     hWatchDirectory;
    OVERLAPPED Overlapped;
    BYTE       ReadDirectoryBuffer[1024 * 8];

    INT64 ReloadStartNs;
} g_Hotreload;

const WCHAR* CPWIN_GetFileNameW(const WCHAR* path)
{
    const WCHAR* filename = NULL;
    for (const WCHAR* c = path; *c != 0; c++)
    {
        if (*c == L'\\')
            filename = c + 1;
    }
    return filename;
}

const WCHAR* CPWIN_GetFileExtensionW(const WCHAR* path)
{
    const WCHAR* ext = NULL;
    const WCHAR* c   = path;
    for (c = path; *c != 0; c++)
    {
        if (*c == L'.')
            ext = c;
    }
    if (!ext)
        ext = c;
    return ext;
}

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
HWND             g_hwnd = NULL;

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
    memset(&g_Hotreload, 0, sizeof(g_Hotreload));
#endif

    memset(&g_plugin, 0, sizeof(g_plugin));
    memset(&g_MIDI, 0, sizeof(g_MIDI));
    memset(&g_Audio, 0, sizeof(g_Audio));
    memset(&g_Menus, 0, sizeof(g_Menus));

    CPWIN_LoadPlugin();

    ///////////////
    // INIT MIDI //
    ///////////////

    for (int i = 0; i < ARRAYSIZE(g_MIDI.SystemBuffers); i++)
    {
        MIDIHDR* head        = &g_MIDI.SystemBuffers[i].header;
        head->lpData         = &g_MIDI.SystemBuffers[i].buffer[0];
        head->dwBufferLength = ARRAYSIZE(g_MIDI.SystemBuffers[i].buffer);
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

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    MSG msg;

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

    g_plugin.UserGUI = g_plugin.createGUI(g_plugin.UserPlugin);
    cplug_assert(g_plugin.UserGUI != NULL);

    uint32_t guiWidth, guiHeight;
    g_plugin.getSize(g_plugin.UserGUI, &guiWidth, &guiHeight);

    RECT rect = {0, 0, (LONG)guiWidth, (LONG)guiHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

    g_hwnd = CreateWindowExW(
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
    if (g_hwnd == NULL)
    {
        fprintf(stderr, "Could not create window\n");
        return 1;
    }

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

    SetMenu(g_hwnd, g_Menus.hMain);

    // Callback to detect connected/disconnected MIDI/Audio devices
    // Must be initialised afer the menu because the callback changes menu items based on new/removed devices
    CM_NOTIFY_FILTER notifyFilter;
    memset(&notifyFilter, 0, sizeof(notifyFilter));
    notifyFilter.cbSize     = sizeof(notifyFilter);
    notifyFilter.Flags      = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
    notifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;

    HRESULT result = CM_Register_Notification(&notifyFilter, g_hwnd, CPWIN_HandleDeviceChange, &g_hCMNotification);
    cplug_assert(result == CR_SUCCESS);
    cplug_assert(g_hCMNotification != NULL);

    // Window ready
    g_plugin.setParent(g_plugin.UserGUI, g_hwnd);

    ShowWindow(g_hwnd, cmdshow);
    g_plugin.setVisible(g_plugin.UserGUI, true);
    SetForegroundWindow(g_hwnd);

#ifndef HOTRELOAD_WATCH_DIR
    // Default event loop
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#else  // Hotreloading uses different event loop
    // Setup file watcher
    // Most this code was taken from here: https://gist.github.com/nickav/a57009d4fcc3b527ed0f5c9cf30618f8
    g_Hotreload.hWatchDirectory = CreateFileW(
        TEXT(HOTRELOAD_WATCH_DIR),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    if (g_Hotreload.hWatchDirectory == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Failed to get directory handle\n");
        return 1;
    }
    g_Hotreload.Overlapped.hEvent = CreateEventW(NULL, FALSE, 0, NULL);
    cplug_assert(g_Hotreload.Overlapped.hEvent);

    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
    BOOL ok = ReadDirectoryChangesW(
        g_Hotreload.hWatchDirectory,
        g_Hotreload.ReadDirectoryBuffer,
        sizeof(g_Hotreload.ReadDirectoryBuffer),
        TRUE,
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL,
        &g_Hotreload.Overlapped,
        NULL);
    cplug_assert(ok);
    if (!ok)
    {
        fprintf(stderr, "Failed to queue info buffer\n");
        return 1;
    }
    fprintf(stderr, "Watching folder %s\n", HOTRELOAD_WATCH_DIR);
    BOOL  running          = TRUE;
    INT64 LastFileChangeNs = 0;
    while (running)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }

        // Check file change
        DWORD code = WaitForSingleObject(g_Hotreload.Overlapped.hEvent, 0);
        if (code == WAIT_OBJECT_0)
        {
            DWORD NumberOfBytesTransferred;
            GetOverlappedResult(g_Hotreload.hWatchDirectory, &g_Hotreload.Overlapped, &NumberOfBytesTransferred, TRUE);
            FILE_NOTIFY_INFORMATION* Info = (FILE_NOTIFY_INFORMATION*)g_Hotreload.ReadDirectoryBuffer;

            INT64 NowNs = CPWIN_GetNowNS();
            while (TRUE)
            {
                DWORD name_len = Info->FileNameLength / sizeof(wchar_t);

                if (Info->Action == FILE_ACTION_MODIFIED)
                {
                    if (g_Hotreload.ReloadStartNs == 0)
                        g_Hotreload.ReloadStartNs = NowNs;
                    LastFileChangeNs = NowNs;
                    fwprintf(stderr, L"File changed at %lld: %.*s\n", NowNs, name_len, Info->FileName);
                }

                // Iterate events
                if (Info->NextEntryOffset)
                    *((BYTE**)&Info) += Info->NextEntryOffset;
                else
                    break;
            }

            // Queue next event
            ok = ReadDirectoryChangesW(
                g_Hotreload.hWatchDirectory,
                g_Hotreload.ReadDirectoryBuffer,
                sizeof(g_Hotreload.ReadDirectoryBuffer),
                TRUE,
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL,
                &g_Hotreload.Overlapped,
                NULL);

            if (!ok)
            {
                fprintf(stderr, "Failed to queue info buffer\n");
                return 1;
            }
        }

        // Throttle hotreload
        INT64 Now  = CPWIN_GetNowNS();
        INT64 diff = Now - LastFileChangeNs;
        // Add 50ms latency to account for multiple files being changed in quick succession
        // This accounts for things like mass renaming of variables by an IDE, clang-format, etc.
        if (LastFileChangeNs && diff > 50000000)
        {
            LastFileChangeNs = 0;

            if (g_Hotreload.hPluginDLL)
            {
                // Deinit
                g_plugin.setVisible(g_plugin.UserGUI, false);
                g_plugin.setParent(g_plugin.UserGUI, NULL);
                g_plugin.destroyGUI(g_plugin.UserGUI);

                DefWindowProcA(
                    g_hwnd,
                    WM_CHANGEUISTATE,
                    UIS_INITIALIZE | UISF_ACTIVE | UISF_HIDEACCEL | UISF_HIDEFOCUS,
                    0);

                CPWIN_Audio_Stop();

                g_PluginState.BytesWritten = 0;
                g_PluginState.BytesRead    = 0;
                g_plugin.saveState(g_plugin.UserPlugin, &g_PluginState, CPWIN_WriteStateProc);

                g_plugin.destroyPlugin(g_plugin.UserPlugin);
                g_plugin.libraryUnload();
                ok = FreeLibrary(g_Hotreload.hPluginDLL);
                cplug_assert(ok);
                g_Hotreload.hPluginDLL = NULL;
                memset(&g_plugin, 0, sizeof(g_plugin));
            }

            // Using 'system()' to call our build command is way simpler, but creates some stdout buffering problems...
            // Windows prefer that you use CreateProcessW.
            // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
            // https://learn.microsoft.com/en-au/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output?redirectedfrom=MSDN
            STARTUPINFO         si;
            PROCESS_INFORMATION pi;
            SECURITY_ATTRIBUTES sa;
            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));
            memset(&sa, 0, sizeof(sa));

            sa.nLength              = sizeof(sa);
            sa.bInheritHandle       = TRUE;
            sa.lpSecurityDescriptor = NULL;

            HANDLE hChildStdoutRd, hChildStdoutWr;
            if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0) ||
                !SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0))
            {
                fprintf(stderr, "Failed to create pipes.");
                cplug_assert(false);
                return -1;
            }

            si.cb          = sizeof(si);
            si.dwFlags    |= STARTF_USESHOWWINDOW; // Stops a terminal window popping up as it runs the command
            si.hStdOutput  = hChildStdoutWr;
            si.dwFlags    |= STARTF_USESTDHANDLES; // Lets us use the stdout pipe

            const UINT64 buildStart = CPWIN_GetNowNS();
            // Run build command in child process.
            WCHAR cmdbuf[512];
            _snwprintf(cmdbuf, ARRAYSIZE(cmdbuf), L"%s", TEXT(HOTRELOAD_BUILD_COMMAND));
            if (!CreateProcessW(0, cmdbuf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                fprintf(stderr, "CreateProcess failed (%lu).\n", GetLastError());
                return 1;
            }

            // Wait until child process exits
            WaitForSingleObject(pi.hProcess, INFINITE);

            char  buffer[4096] = {0};
            DWORD bytesRead    = 0;

            do
            {
                ok = ReadFile(hChildStdoutRd, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
                if (ok)
                    fwrite(buffer, 1, bytesRead, stderr);
            }
            while (bytesRead == sizeof(buffer) - 1);
            // TODO: get stderr also working. Currently it hangs forever when you call ReadFile

            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            // Cleanup build process
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hChildStdoutWr);

            const UINT64 buildEnd = CPWIN_GetNowNS();

            if (exitCode != 0)
            {
                fprintf(stderr, "[WARNING] Rebuild failed. Exited with code: %lu\n", exitCode);
            }
            else
            {
                CPWIN_LoadPlugin();
                g_plugin.loadState(g_plugin.UserPlugin, &g_PluginState, CPWIN_ReadStateProc);

                CPWIN_Audio_Start();

                // Note: GetClientRect() will set RECT to all zeros if the window is minimised
                RECT size;
                GetClientRect(g_hwnd, &size);
                uint32_t width  = size.right - size.left;
                uint32_t height = size.bottom - size.top;

                g_plugin.UserGUI = g_plugin.createGUI(g_plugin.UserPlugin);
                cplug_assert(g_plugin.UserGUI != NULL);
                if (width && height)
                    g_plugin.setSize(g_plugin.UserGUI, size.right - size.left, size.bottom - size.top);

                g_plugin.setParent(g_plugin.UserGUI, g_hwnd);
                if (width && height)
                    g_plugin.setVisible(g_plugin.UserGUI, true);
            }

            const UINT64 reloadEnd = CPWIN_GetNowNS();

            double rebuild_ms = (double)(buildEnd - buildStart) / 1.e6;
            double reload_ms  = (double)(reloadEnd - g_Hotreload.ReloadStartNs) / 1.e6;
            fprintf(stderr, "Rebuild time %.2fms\n", rebuild_ms);
            fprintf(stderr, "Reload time %.2fms\n", reload_ms);
            g_Hotreload.ReloadStartNs = 0;
        }

        Sleep(5);
    }
    CloseHandle(g_Hotreload.Overlapped.hEvent);
    CloseHandle(g_Hotreload.hWatchDirectory);
#endif // Main loop

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
        if (g_Hotreload.hPluginDLL)
        {
#endif
            g_plugin.setVisible(g_plugin.UserGUI, false);
            g_plugin.setParent(g_plugin.UserGUI, NULL);
            g_plugin.destroyGUI(g_plugin.UserGUI);
            g_plugin.destroyPlugin(g_plugin.UserPlugin);
            g_plugin.libraryUnload();
#ifdef HOTRELOAD_WATCH_DIR
            FreeLibrary(g_Hotreload.hPluginDLL);
        }
        if (g_PluginState.Data)
            VirtualFree(g_PluginState.Data, g_PluginState.BytesReserved, 0);

        // Cleanup old versions
        // Debuggers appear to release their lock on previously loaded DLLs after the WM_CLOSE message is sent
        for (UINT PrevVersion = g_Hotreload.Version; PrevVersion > 0; PrevVersion--)
        {
            const WCHAR* CurrentDllPath            = TEXT(HOTRELOAD_LIB_PATH);
            const WCHAR* Ext                       = CPWIN_GetFileExtensionW(CurrentDllPath);
            WCHAR        PrevVersionPath[MAX_PATH] = {0};

            int len = (int)(Ext - CurrentDllPath);
            _snwprintf(PrevVersionPath, MAX_PATH, L"%.*s%u.dll", len, CurrentDllPath, PrevVersion);
            BOOL ok = DeleteFileW(PrevVersionPath);
            cplug_assert(ok);
            _snwprintf(PrevVersionPath, MAX_PATH, L"%.*s%u.pdb", len, CurrentDllPath, PrevVersion);
            // Some (not all) debuggers hold a lock on pdb files which causes deleting the file to fail
            DeleteFileW(PrevVersionPath);
        }
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
        cplug_assert(w >= 0);
        cplug_assert(h >= 0);
        g_plugin.checkSize(g_plugin.UserGUI, &w, &h);
        width   = w;
        height  = h;
        width  += padding_x;
        height += padding_y;

        // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing
        if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
            parent->left = parent->right - width;
        else
            parent->right = parent->left + width;

        if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
            parent->top = parent->bottom - height;
        else
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
        case IDM_SampleRate_44100:
        case IDM_SampleRate_48000:
        case IDM_SampleRate_88200:
        case IDM_SampleRate_96000:
        {
            CPWIN_Audio_Stop();
            WCHAR text[8];
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmenustringw
            int numCharsCopied =
                GetMenuStringW(g_Menus.hSampleRateSubmenu, wParam, text, ARRAYSIZE(text), MF_BYCOMMAND);
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
            WCHAR text[8];
            int numCharsCopied = GetMenuStringW(g_Menus.hBlockSizeSubmenu, wParam, text, ARRAYSIZE(text), MF_BYCOMMAND);
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

// This avoids having to inlcude <Shlwapi.h>, which causes some issues when trying to build in both C & C++
EXTERN_C __declspec(dllimport) BOOL __stdcall PathFileExistsW(_In_ LPCWSTR pszPath);

// Debuggers on Windows have a tough time loading an updated DLL and PDB with the same name of a previously loaded DLL
// So we simply duplicate the file, add a version suffix to the name, then load that
// A quirk of Windows DLLs is the associated PDB will be embedded within the file. We patch this with the new PDB name
void CPWIN_DuplicatePatchAndLoadDll()
{
    cplug_assert(g_Hotreload.hPluginDLL == NULL);

    const WCHAR* CurrentDllPath        = TEXT(HOTRELOAD_LIB_PATH);
    WCHAR        NextDllPath[MAX_PATH] = {0};
    WCHAR        NextPdbPath[MAX_PATH] = {0};

    HANDLE hFile        = NULL;
    HANDLE hFileMapping = NULL;
    LPVOID pFileView    = 0;
    BOOL   PdbPatched   = FALSE;

    g_Hotreload.Version++;

    // Build paths
    {
        cplug_assert(PathFileExistsW(CurrentDllPath));
        const WCHAR* Ext = CPWIN_GetFileExtensionW(CurrentDllPath);
        cplug_assert(Ext != NULL);

        // Create paths with the same name, but with a version suffix
        int len = (int)(Ext - CurrentDllPath);
        _snwprintf(NextDllPath, MAX_PATH, L"%.*s%u.dll", len, CurrentDllPath, g_Hotreload.Version);
        _snwprintf(NextPdbPath, MAX_PATH, L"%.*s%u.pdb", len, CurrentDllPath, g_Hotreload.Version);

        // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfilew
        BOOL ok = CopyFileW(CurrentDllPath, NextDllPath, FALSE);
        cplug_assert(ok != 0);
        // User has only supplied the DLL path with HOTRELOAD_LIB_PATH. The .pdb cannot be guaranteed to share the same
        // filename. We will find that path in the DLL, duplicate the file at that path, then patch the DLL
    }

    // Get file mapping
    {
        hFile = CreateFileW(
            NextDllPath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            hFile = NULL;
        if (hFile)
            hFileMapping = CreateFileMappingW(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);

        if (hFileMapping)
            pFileView = MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        cplug_assert(pFileView);
    }

    // Algorithm for finding the correct spot to patch the DLL is taken from github.com/fungos.cr, only translated from
    // C++ to C, refactored, and with more comments.
    // Further reading: https://github.com/fungos/cr
    // https://fungos.github.io/cr-simple-c-hot-reload/
    // http://www.godevtool.com/Other/pdb.htm
    // https://www.debuginfo.com/articles/debuginfomatch.html
    // https://learn.microsoft.com/en-us/previous-versions/ms809762(v=msdn.10)
    if (pFileView)
    {
        // https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/System/SystemServices/struct.IMAGE_DOS_HEADER.html
        const PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)pFileView;
        cplug_assert(DosHeader->e_magic == IMAGE_DOS_SIGNATURE);

        // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_nt_headers64
        // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_file_header
        // e_lfanew = offset of exe in file header
        const PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)((BYTE*)DosHeader + DosHeader->e_lfanew);
        cplug_assert(pNTHeader->Signature == IMAGE_NT_SIGNATURE);
        // Clang appears to use HDR64, while MSVC appears to use HDR32
        cplug_assert(
            pNTHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            pNTHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);

        // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_optional_header32
        // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_data_directory
        const DWORD RVA = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
        // Clang will create an entry of size sizeof(IMAGE_DEBUG_DIRECTORY)
        // MSVC will create an entry of size 2 * sizeof(IMAGE_DEBUG_DIRECTORY)
        cplug_assert(RVA);
        cplug_assert(
            pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress >=
            sizeof(IMAGE_DEBUG_DIRECTORY));

        enum
        {
            PDB2_SIGNATURE = '01BN',
            PDB7_SIGNATURE = 'SDSR',
        };
        struct CV_INFO_PDB70
        {
            DWORD CvSignature;
            GUID  Signature;
            DWORD Age;
            BYTE  PdbFileName[];
        };

        struct CV_INFO_PDB70* Info = NULL;

        // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_section_header
        // https://learn.microsoft.com/en-us/previous-versions/ms809762(v=msdn.10)#the-section-table
        PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(pNTHeader);
        for (WORD i = 0; i < pNTHeader->FileHeader.NumberOfSections; i++, section++)
        {
            // https://learn.microsoft.com/en-us/previous-versions/ms809762(v=msdn.10)#win32-and-pe-basic-concepts
            if (RVA >= section->VirtualAddress && RVA < (section->VirtualAddress + section->Misc.VirtualSize))
            {
                const DWORD diff   = section->VirtualAddress - section->PointerToRawData;
                const DWORD offset = RVA - diff;

                // https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_debug_directory
                const PIMAGE_DEBUG_DIRECTORY dir = (PIMAGE_DEBUG_DIRECTORY)((BYTE*)pFileView + offset);

                cplug_assert(dir->Type == IMAGE_DEBUG_TYPE_CODEVIEW);
                cplug_assert(dir->SizeOfData >= sizeof(*Info));

                Info = (struct CV_INFO_PDB70*)((BYTE*)pFileView + dir->PointerToRawData);
                cplug_assert(Info->CvSignature == PDB7_SIGNATURE);

                break;
            }
        }
        cplug_assert(Info);
        if (Info)
        {
            WCHAR CurrentPdbPath[MAX_PATH] = {0};

            // This is the path embedded in the DLL by your compiler (eg. C:\\path\\to\\plugin.pdb)
            char* EmbeddedPdbPath = (char*)Info->PdbFileName;

            // Duplicate PDB
            int ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, EmbeddedPdbPath, -1, CurrentPdbPath, MAX_PATH);
            cplug_assert(ok);
            ok = CopyFileW(CurrentPdbPath, NextPdbPath, FALSE);
            cplug_assert(ok);

            // Patch DLL with new path
            // Because we append a version number to the file name, the string length will be longer.
            // Replacing the path with only the new filename (no directory) appears to work fine
            const WCHAR* NextPdbFilename    = CPWIN_GetFileNameW(NextPdbPath);
            const size_t EmbeddedPdbPathLen = 1 + strlen(EmbeddedPdbPath);
            cplug_assert(EmbeddedPdbPathLen >= wcslen(NextPdbFilename));
            ok = snprintf(EmbeddedPdbPath, EmbeddedPdbPathLen, "%ls", NextPdbFilename);
            cplug_assert(ok);

            // Loading the DLL right here doesn't appear to work.
            // After unmapping the file & closing all the handles, it works fine
            PdbPatched = TRUE;
        }
    }

    if (pFileView)
        UnmapViewOfFile(pFileView);
    if (hFileMapping)
        CloseHandle(hFileMapping);
    if (hFile)
        CloseHandle(hFile);

    if (PdbPatched)
    {
        g_Hotreload.hPluginDLL = LoadLibraryW(NextDllPath);
        // DWORD err              = GetLastError();
        cplug_assert(g_Hotreload.hPluginDLL != NULL);
    }
}
#endif // HOTRELOAD

void CPWIN_LoadPlugin()
{
#ifdef HOTRELOAD_WATCH_DIR
    CPWIN_DuplicatePatchAndLoadDll();
    cplug_assert(g_Hotreload.hPluginDLL != NULL);
#define CPLUG_GET_PROC(name) GetProcAddress(g_Hotreload.hPluginDLL, #name)
#else // not a hotrealoding build
#define CPLUG_GET_PROC(func) func
#endif // Hotreload

    // This looks ugly because of the strict types in C++. C is ironically more elegant
    *(LONG_PTR*)&g_plugin.libraryLoad               = (LONG_PTR)CPLUG_GET_PROC(cplug_libraryLoad);
    *(LONG_PTR*)&g_plugin.libraryUnload             = (LONG_PTR)CPLUG_GET_PROC(cplug_libraryUnload);
    *(LONG_PTR*)&g_plugin.createPlugin              = (LONG_PTR)CPLUG_GET_PROC(cplug_createPlugin);
    *(LONG_PTR*)&g_plugin.destroyPlugin             = (LONG_PTR)CPLUG_GET_PROC(cplug_destroyPlugin);
    *(LONG_PTR*)&g_plugin.getOutputBusChannelCount  = (LONG_PTR)CPLUG_GET_PROC(cplug_getOutputBusChannelCount);
    *(LONG_PTR*)&g_plugin.setSampleRateAndBlockSize = (LONG_PTR)CPLUG_GET_PROC(cplug_setSampleRateAndBlockSize);
    *(LONG_PTR*)&g_plugin.process                   = (LONG_PTR)CPLUG_GET_PROC(cplug_process);
    *(LONG_PTR*)&g_plugin.saveState                 = (LONG_PTR)CPLUG_GET_PROC(cplug_saveState);
    *(LONG_PTR*)&g_plugin.loadState                 = (LONG_PTR)CPLUG_GET_PROC(cplug_loadState);

    *(LONG_PTR*)&g_plugin.createGUI      = (LONG_PTR)CPLUG_GET_PROC(cplug_createGUI);
    *(LONG_PTR*)&g_plugin.destroyGUI     = (LONG_PTR)CPLUG_GET_PROC(cplug_destroyGUI);
    *(LONG_PTR*)&g_plugin.setParent      = (LONG_PTR)CPLUG_GET_PROC(cplug_setParent);
    *(LONG_PTR*)&g_plugin.setVisible     = (LONG_PTR)CPLUG_GET_PROC(cplug_setVisible);
    *(LONG_PTR*)&g_plugin.setScaleFactor = (LONG_PTR)CPLUG_GET_PROC(cplug_setScaleFactor);
    *(LONG_PTR*)&g_plugin.getSize        = (LONG_PTR)CPLUG_GET_PROC(cplug_getSize);
    *(LONG_PTR*)&g_plugin.checkSize      = (LONG_PTR)CPLUG_GET_PROC(cplug_checkSize);
    *(LONG_PTR*)&g_plugin.setSize        = (LONG_PTR)CPLUG_GET_PROC(cplug_setSize);

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
    g_plugin.HostContext.requestResize  = CPWIN_HostContext_RequestResize;

    g_plugin.UserPlugin = g_plugin.createPlugin(&g_plugin.HostContext);
    cplug_assert(g_plugin.UserPlugin != NULL);
}

bool CPWIN_HostContext_RequestResize(CplugHostContext* ctx, uint32_t width, uint32_t height)
{
    if (g_plugin.UserGUI)
    {
        RECT parent;
        BOOL ok = 0;
        ok      = GetWindowRect(g_hwnd, &parent);
        cplug_assert(ok == 1);
        if (ok)
        {
            LONG parent_width  = parent.right - parent.left;
            LONG parent_height = parent.bottom - parent.top;
            RECT child         = parent;
            ok                 = AdjustWindowRect(&child, WS_OVERLAPPEDWINDOW, TRUE);
            cplug_assert(ok == 1);
            if (ok)
            {
                LONG diff_x = (child.right - child.left) - parent_width;
                LONG diff_y = (child.bottom - child.top) - parent_height;

                LONG next_parent_width  = width + diff_x;
                LONG next_parent_height = height + diff_y;

                // This should trigger WM_SIZE - SIZE_RESTORED
                // https://learn.microsoft.com/en-gb/windows/win32/api/winuser/nf-winuser-setwindowpos
                ok = SetWindowPos(g_hwnd, NULL, parent.left, parent.top, next_parent_width, next_parent_height, 0);
                cplug_assert(ok == 1);
            }
        }
    }
    return false;
}

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
                if (0 == wcsncmp(deviceID, g_Audio.DeviceIDBuffer, ARRAYSIZE(g_Audio.DeviceIDBuffer)))
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
        if (writePos == ARRAYSIZE(g_MIDI.RingBuffer.buffer))
            writePos = 0;
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

    for (int i = 0; i < ARRAYSIZE(g_MIDI.SystemBuffers); i++)
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

        for (int i = 0; i < ARRAYSIZE(g_MIDI.SystemBuffers); i++)
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
        if (tail == CPLUG_MIDI_RINGBUFFER_SIZE)
            tail = 0;

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
    ctx.cplugContext.numInputs      = 0;
    ctx.cplugContext.numOutputs     = 2;
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
    CPWIN_Audio_Process(g_Audio.ProcessBufferMaxFrames);

    // https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw
    DWORD  TaskIndex             = 0;
    HANDLE ThreadCharacteristics = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
    cplug_assert(ThreadCharacteristics);

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

    // https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avrevertmmthreadcharacteristics
    BOOL ok = AvRevertMmThreadCharacteristics(ThreadCharacteristics);
    cplug_assert(ok);

    return 0;
}

void CPWIN_Audio_Stop()
{
    if (g_Audio.hAudioProcessThread == NULL)
    {
        fprintf(stderr, "[WARNING] Called CPWIN_Audio_Stop() when audio is not running\n");
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
    wcscpy_s(g_Audio.DeviceIDBuffer, ARRAYSIZE(g_Audio.DeviceIDBuffer), audioDeviceID);
    g_Audio.DeviceIDBuffer[ARRAYSIZE(g_Audio.DeviceIDBuffer) - 1] = 0;
    CoTaskMemFree(audioDeviceID);
}

void CPWIN_Audio_Start()
{
#ifdef HOTRELOAD_WATCH_DIR
    if (g_Hotreload.hPluginDLL == NULL)
    {
        fprintf(stderr, "[FAILED] Called CPWIN_Audio_Start when no plugin is loaded\n");
        return;
    }
#endif // Hotreload
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
    fmtex.Format.wBitsPerSample       = sizeof(float) * 8;
    fmtex.Format.nBlockAlign          = sizeof(float) * g_Audio.NumChannels;
    fmtex.Format.nAvgBytesPerSec      = sizeof(float) * g_Audio.SampleRate * g_Audio.NumChannels;
    fmtex.Format.cbSize               = sizeof(fmtex) - sizeof(fmtex.Format);
    fmtex.Samples.wValidBitsPerSample = sizeof(float) * 8;

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