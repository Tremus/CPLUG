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

#include <VSStyle.h>
#include <Vssym32.h>
#include <dwmapi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

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
// In cpp, this should be a "reference"
#define CPLUG_WTF_IS_A_REFERENCE(obj) obj
#else
// In c, this should be a pointer
#define CPLUG_WTF_IS_A_REFERENCE(obj) &obj
#endif
#define CPLUG_COM_RELEASE(obj)                                                                                         \
    if (obj)                                                                                                           \
    {                                                                                                                  \
        obj->lpVtbl->Release(obj);                                                                                     \
        obj = NULL;                                                                                                    \
    }

HWND  g_hwnd = NULL;
float g_dpi  = 1.0f;

#if defined(HOTRELOAD_WATCH_DIR) || defined(HOTRELOAD_LIB_PATH) || defined(HOTRELOAD_BUILD_COMMAND)
#if !defined(HOTRELOAD_WATCH_DIR) || !defined(HOTRELOAD_LIB_PATH) || !defined(HOTRELOAD_BUILD_COMMAND)
#error You need to define all 3
#endif
#endif

static inline UINT64 Cplug_RoundUp(UINT64 v, UINT64 align)
{
    UINT64 inc = (align - (v % align)) % align;
    return v + inc;
}

// ----==== Plugin ====----

struct Cplug_Plugin
{
    CplugHostContext HostContext;

    void* UserPlugin;
    void* UserGUI;

    void (*libraryLoad)();
    void (*libraryUnload)();
    void* (*createPlugin)(CplugHostContext* ctx);
    void (*destroyPlugin)(void* userPlugin);
    uint32_t (*getOutputBusChannelCount)(void*, uint32_t bus_idx);
    void (*setSampleRateAndBlockSize)(void*, double sampleRate, uint32_t maxBlockSize);
    void (*process)(void*, CplugProcessContext*);
    void (*saveState)(void* userPlugin, const void* stateCtx, cplug_writeProc writeProc);
    void (*loadState)(void* userPlugin, const void* stateCtx, cplug_readProc readProc);

    void* (*createGUI)(CplugHostContext* ctx, void* userPlugin);
    void (*destroyGUI)(void* userGUI);
    void (*setParent)(void* userGUI, void* hwnd_or_nsview);
    void (*setVisible)(void* userGUI, bool visible);
    void (*setScaleFactor)(void* userGUI, float scale);
    void (*getSize)(void* userGUI, uint32_t* width, uint32_t* height);
    void (*checkSize)(void* userGUI, uint32_t* width, uint32_t* height);
    bool (*setSize)(void* userGUI, uint32_t width, uint32_t height);
} g_plugin;
static_assert(sizeof(CplugHostContext) == 40, "You may need to add support for new methods");

void Cplug_HostContext_SendParamEvent(CplugHostContext* ctx, const CplugEvent* e) {}
void Cplug_HostContext_Rescan(CplugHostContext* ctx, uint32_t flags) {}
bool Cplug_HostContext_GetHostName(CplugHostContext* ctx, char* buf, size_t buflen)
{
    snprintf(buf, buflen, "CPLUG Standalone Windows");
    return true;
}

bool Cplug_HostContext_RequestResize(CplugHostContext* ctx, uint32_t width, uint32_t height)
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
                if (ok)
                    return true;
            }
        }
    }
    return false;
}

#pragma region HOTRELOAD

#ifdef HOTRELOAD_LIB_PATH
struct Cplug_Hotreload
{
    HMODULE hPluginDLL;
    UINT    Version;

    HANDLE     hWatchDirectory;
    OVERLAPPED Overlapped;
    BYTE       ReadDirectoryBuffer[1024 * 8];

    INT64 ReloadStartNs;
} g_Hotreload;

struct Cplug_PluginStateContext
{
    BYTE*  Data;
    SIZE_T BytesReserved;
    SIZE_T BytesCommited;

    SIZE_T BytesWritten;
    SIZE_T BytesRead;
} g_PluginState;

int64_t Cplug_WriteStateProc(const void* stateCtx, void* writePos, size_t numBytesToWrite)
{
    cplug_assert(stateCtx != NULL);
    cplug_assert(writePos != NULL);
    cplug_assert(numBytesToWrite > 0);

    // The idea is we reserve heaps of address space up front, and hope we never spill over it.
    // In the rare case your plugin does, simply reserve more address space
    // Some plugins may save big audio files in their state, hence the BIG reserve
    struct Cplug_PluginStateContext* ctx = (struct Cplug_PluginStateContext*)stateCtx;

    if (ctx->Data == NULL)
    {
        const SIZE_T largePageSize  = GetLargePageMinimum();
        SIZE_T       bigreserve     = (SIZE_T)Cplug_RoundUp(numBytesToWrite, largePageSize);
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
        SIZE_T nextcommit = 2 * (numBytesToWrite + ctx->BytesCommited);
        if (nextcommit > ctx->BytesReserved)
            nextcommit = ctx->BytesReserved;
        LPVOID retval = VirtualAlloc(ctx->Data, nextcommit, MEM_COMMIT, PAGE_READWRITE);
        cplug_assert(retval != NULL);
        ctx->BytesCommited = nextcommit;
    }
    memcpy(ctx->Data + ctx->BytesWritten, writePos, numBytesToWrite);
    ctx->BytesWritten += numBytesToWrite;
    return numBytesToWrite;
}

int64_t Cplug_ReadStateProc(const void* stateCtx, void* readPos, size_t maxBytesToRead)
{
    struct Cplug_PluginStateContext* ctx = (struct Cplug_PluginStateContext*)stateCtx;

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

// Get time func taken from here https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
struct
{
    LARGE_INTEGER freq, start;
} g_Timer;

const WCHAR* Cplug_GetFileNameW(const WCHAR* path)
{
    const WCHAR* filename = NULL;
    for (const WCHAR* c = path; *c != 0; c++)
    {
        if (*c == L'\\')
            filename = c + 1;
    }
    if (filename == NULL) // Oh oh, is this even a path?
        filename = path;
    return filename;
}

const WCHAR* Cplug_GetFileExtensionW(const WCHAR* path)
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

// This avoids having to inlcude <Shlwapi.h>, which causes some issues when trying to build in both C & C++
EXTERN_C __declspec(dllimport) BOOL __stdcall PathFileExistsW(_In_ LPCWSTR pszPath);

// Debuggers on Windows have a tough time loading an updated DLL and PDB with the same name of a previously loaded DLL
// So we simply duplicate the file, add a version suffix to the name, then load that
// A quirk of Windows DLLs is the associated PDB will be embedded within the file. We patch this with the new PDB name
void Cplug_DuplicatePatchAndLoadDll()
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
        const WCHAR* Ext = Cplug_GetFileExtensionW(CurrentDllPath);
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
            const WCHAR* NextPdbFilename    = Cplug_GetFileNameW(NextPdbPath);
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

static inline INT64 Cplug_GetNowNS()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    now.QuadPart -= g_Timer.start.QuadPart;
    INT64 q       = now.QuadPart / g_Timer.freq.QuadPart;
    INT64 r       = now.QuadPart % g_Timer.freq.QuadPart;
    return q * 1000000000 + r * 1000000000 / g_Timer.freq.QuadPart;
}

#endif // HOTRELOAD_LIB_PATH

// Loads the DLL + loads symbols for library functions
void Cplug_LoadPlugin()
{
#ifdef HOTRELOAD_WATCH_DIR
    Cplug_DuplicatePatchAndLoadDll();
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
    g_plugin.HostContext.sendParamEvent = Cplug_HostContext_SendParamEvent;
    g_plugin.HostContext.rescan         = Cplug_HostContext_Rescan;
    g_plugin.HostContext.getHostName    = Cplug_HostContext_GetHostName;
    g_plugin.HostContext.requestResize  = Cplug_HostContext_RequestResize;

    g_plugin.UserPlugin = g_plugin.createPlugin(&g_plugin.HostContext);
    cplug_assert(g_plugin.UserPlugin != NULL);
}
#pragma endregion HOTRELOAD

// ----==== MIDI ====----
#pragma region MIDI

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

    // https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/extended-capabilities-from-a-wdm-audio-driver
    UINT         NumDevices;
    MIDIINCAPS2W Devices[16];
    MIDIINCAPS2W ConnectedDevice;
    MIDIINCAPS2W HotplugDevice;

    struct
    {
        volatile LONG writePos;
        volatile LONG readPos;

        MIDIMessage buffer[CPLUG_MIDI_RINGBUFFER_SIZE];
    } RingBuffer;

    struct CplugMidiBuffer
    {
        MIDIHDR Header;
        char    Buffer[CPLUG_MIDI_BUFFER_SIZE];
    } Buffers[CPLUG_MIDI_BUFFER_COUNT];
} g_MIDI = {0};

BOOL Cplug_MIDI_MatchDevice(MIDIINCAPS2W* a, MIDIINCAPS2W* b)
{
    BOOL Val = FALSE;

    const GUID NullGUID = {0};

    BOOL MatchName = 0 == memcmp(a->szPname, b->szPname, sizeof(a->szPname));

    BOOL IsNameGuidNull = 0 == memcmp(&b->NameGuid, &NullGUID, sizeof(GUID));
    BOOL MatchNameGuid  = 0 == memcmp(&b->NameGuid, &a->NameGuid, sizeof(GUID));

    BOOL IsProductGuidNull = 0 == memcmp(&b->ProductGuid, &NullGUID, sizeof(GUID));
    BOOL MatchProductGuid  = 0 == memcmp(&b->ProductGuid, &a->ProductGuid, sizeof(GUID));

    Val |= !IsNameGuidNull && !IsProductGuidNull && MatchNameGuid && MatchProductGuid;
    // Fallback if we are unable to obtain the GUIDs through midiInGetDevCapsW.
    // This API function appears to have started failing to set GUIDs in early 2026 on Windows 11...?
    Val |= IsNameGuidNull && IsProductGuidNull && MatchName;

    return Val;
}

void CALLBACK Cplug_MIDIInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    // https://learn.microsoft.com/en-gb/windows/win32/multimedia/mim-data?redirectedfrom=MSDN
    if (wMsg == MM_MIM_DATA)
    {
        MIDIMessage midi;
        LONG        writePos;

        // take first 3 bytes. remember, the rest are junk, including possibly the ones we're taking
        midi.bytesAsInt  = dwParam1 & 0xffffff;
        midi.timestampMs = (UINT)dwParam2;

        writePos = _InterlockedCompareExchange(&g_MIDI.RingBuffer.writePos, 0, 0);

        g_MIDI.RingBuffer.buffer[writePos] = midi;
        writePos++;
        if (writePos == ARRAYSIZE(g_MIDI.RingBuffer.buffer))
            writePos = 0;
        _InterlockedExchange(&g_MIDI.RingBuffer.writePos, writePos);
    }
    // handle sysex?
    // https://www.midi.org/specifications-old/item/table-4-universal-system-exclusive-messages */
    // else if (wMsg == MIM_LONGDATA) {}
}

// [Main Thread]
void Cplug_MIDI_DisconnectInput()
{
    if (g_MIDI.IsConnected)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinreset
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinstop
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinunprepareheader
        UINT result;
        midiInReset(g_MIDI.hInput);
        midiInStop(g_MIDI.hInput);

        for (int i = 0; i < ARRAYSIZE(g_MIDI.Buffers); i++)
        {
            MIDIHDR* head = &g_MIDI.Buffers[i].Header;
            midiInUnprepareHeader(g_MIDI.hInput, head, sizeof(*head));
        }
        midiInClose(g_MIDI.hInput);
        g_MIDI.hInput      = NULL;
        g_MIDI.IsConnected = 0;
        memset(&g_MIDI.ConnectedDevice, 0, sizeof(g_MIDI.ConnectedDevice));
    }
}

MMRESULT Cplug_MIDI_RescanInputs()
{
    MMRESULT mmr = MMSYSERR_NOERROR;
    // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiingetnumdevs
    UINT N = midiInGetNumDevs(); // Hopefully this triggeres Windows 11 to scan for & cache MIDI devices
    if (N > ARRAYSIZE(g_MIDI.Devices))
        N = ARRAYSIZE(g_MIDI.Devices);

    memset(g_MIDI.Devices, 0, sizeof(g_MIDI.Devices));
    UINT i = 0;
    for (; i < N; i++)
    {
        MIDIINCAPS2W* Caps = &g_MIDI.Devices[i];
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiingetdevcapsw
        mmr = midiInGetDevCapsW(i, (MIDIINCAPSW*)Caps, sizeof(*Caps));
        if (mmr != MMSYSERR_NOERROR)
            break;
    }
    g_MIDI.NumDevices = i;
    return mmr;
}

// [Main Thread]
// Disconnects current device if already connected.
// Sets connected device as preferred "hotplug device" if one is not already set
MMRESULT Cplug_MIDI_ConnectInput(UINT portNum)
{
    MMRESULT result = 0;

    if (g_MIDI.IsConnected)
    {
        Cplug_MIDI_DisconnectInput();
    }

    if (portNum >= g_MIDI.NumDevices)
    {
        result = MMSYSERR_BADDEVICEID;
        goto failed;
    }

    // Set up MIDI reading callback
    cplug_assert(g_MIDI.hInput == NULL);
    // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinopen
    result = midiInOpen(&g_MIDI.hInput, portNum, (DWORD_PTR)Cplug_MIDIInProc, 0, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR)
        goto failed;

    for (int i = 0; i < ARRAYSIZE(g_MIDI.Buffers); i++)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinprepareheader
        result = midiInPrepareHeader(g_MIDI.hInput, &g_MIDI.Buffers[i].Header, sizeof(g_MIDI.Buffers[i].Header));
        if (result != MMSYSERR_NOERROR)
            goto failed;
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinaddbuffer
        result = midiInAddBuffer(g_MIDI.hInput, &g_MIDI.Buffers[i].Header, sizeof(g_MIDI.Buffers[i].Header));
        if (result != MMSYSERR_NOERROR)
            goto failed;
    }

    // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinstart
    result = midiInStart(g_MIDI.hInput);
    if (result != MMSYSERR_NOERROR)
        goto failed;

    g_MIDI.IsConnected = 1;
    fprintf(stderr, "Connected to MIDI input %u\n", portNum);

    g_MIDI.ConnectedDevice = g_MIDI.Devices[portNum];

    if (g_MIDI.HotplugDevice.vDriverVersion == 0)
        g_MIDI.HotplugDevice = g_MIDI.ConnectedDevice; // Make this our default device to hotplug

    return result;

failed:
    if (g_MIDI.hInput)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinclose
        midiInClose(g_MIDI.hInput);
        g_MIDI.hInput = 0;
    }
    return result;
}
#pragma endregion MIDI

// ----==== AUDIO ====----
#pragma region AUDIO

const INT    CPLUG_SAMPLE_RATES[]     = {44100, 48000, 88200, 96000, 192000};
const WCHAR* CPLUG_SAMPLE_RATES_STR[] = {L"44100", L"48000", L"88200", L"96000", L"192000"};
enum
{
    CPLUG_SAMPLE_RATES_COUNT = 5,
};
_STATIC_ASSERT(CPLUG_SAMPLE_RATES_COUNT == ARRAYSIZE(CPLUG_SAMPLE_RATES));

typedef struct CplugAudioDeviceID
{
    WCHAR Buffer[64];
} CplugAudioDeviceID;

struct
{
    UINT NumDevices;
    struct CplugAudioDevice
    {
        WCHAR              Name[128];
        CplugAudioDeviceID DeviceID;
        BOOL               SupportedSampleRates[CPLUG_SAMPLE_RATES_COUNT];
    } Devices[16];

    // Devices
    IMMDeviceEnumerator* pIMMDeviceEnumerator;
    IMMDevice*           pIMMDevice;
    CplugAudioDeviceID   CurrentDeviceID;
    // Process
    IAudioClient*       pIAudioClient;
    IAudioRenderClient* pIAudioRenderClient;
    HANDLE              hAudioEvent;
    HANDLE              hAudioProcessThread;
    volatile UINT32     FlagExitAudioThread;

    SIZE_T ProcessBufferCap;
    BYTE*  ProcessBuffer;
    UINT32 ProcessBufferMaxFrames;
    UINT32 ProcessBufferNumOverprocessedFrames;
    // Config
    UINT32 NumChannels;
    UINT32 SampleRate;
    UINT32 BlockSize;
} g_Audio;

static const PROPERTYKEY _PKEY_Device_FriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14};
static const IID  _IID_IAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const IID _IID_IAudioRenderClient =
    {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};
static const GUID _CLSID_MMDeviceEnumerator =
    {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID _IID_IMMDeviceEnumerator =
    {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};

typedef struct WindowsProcessContext
{
    CplugProcessContext cplugContext;
    float*              output[2];
} WindowsProcessContext;

WAVEFORMATEXTENSIBLE Cplug_Audio_CreateWaveFormat(DWORD NumChannels, DWORD SampleRate)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible
    WAVEFORMATEXTENSIBLE wfmt;
    memset(&wfmt, 0, sizeof(wfmt));
    wfmt.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    wfmt.Format.nChannels            = NumChannels;
    wfmt.Format.nSamplesPerSec       = SampleRate;
    wfmt.Format.wBitsPerSample       = sizeof(float) * 8;
    wfmt.Format.nBlockAlign          = sizeof(float) * NumChannels;
    wfmt.Format.nAvgBytesPerSec      = sizeof(float) * SampleRate * NumChannels;
    wfmt.Format.cbSize               = sizeof(wfmt) - sizeof(wfmt.Format);
    wfmt.Samples.wValidBitsPerSample = sizeof(float) * 8;

    if (wfmt.Format.nChannels == 1)
        wfmt.dwChannelMask = SPEAKER_FRONT_CENTER;
    else
        wfmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;

    wfmt.SubFormat = _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    return wfmt;
}

// [Main Thread]
void Cplug_Audio_ScanDevices()
{
    g_Audio.NumDevices = 0;

    IMMDeviceCollection* pCollection = NULL;

    // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-enumaudioendpoints
    HRESULT hr = g_Audio.pIMMDeviceEnumerator->lpVtbl
                     ->EnumAudioEndpoints(g_Audio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
    cplug_assert(hr == S_OK);
    cplug_assert(pCollection != NULL);

    if (pCollection)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevicecollection-getcount
        // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevicecollection-item
        UINT DeviceCount = 0;
        hr               = pCollection->lpVtbl->GetCount(pCollection, &DeviceCount);
        cplug_assert(hr == S_OK);

        if (DeviceCount > ARRAYSIZE(g_Audio.Devices))
            DeviceCount = ARRAYSIZE(g_Audio.Devices);

        UINT i = 0;

        for (; i < DeviceCount; i++)
        {
            struct CplugAudioDevice* pDevice = &g_Audio.Devices[g_Audio.NumDevices];
            memset(pDevice, 0, sizeof(*pDevice));

            IMMDevice*      pIMMDevice         = NULL;
            WCHAR*          deviceID           = NULL;
            IPropertyStore* pProperties        = NULL;
            PROPVARIANT     varName            = {0};
            int             len                = 0;
            IAudioClient*   pIAudioClient      = NULL;
            BOOL            HasSupportedFormat = FALSE;

            hr = pCollection->lpVtbl->Item(pCollection, i, &pIMMDevice);
            cplug_assert(hr == S_OK);
            if (hr != S_OK || pIMMDevice == NULL)
                goto cleanup;

            hr = pIMMDevice->lpVtbl->GetId(pIMMDevice, &deviceID);
            cplug_assert(hr == S_OK);
            if (hr != S_OK || deviceID == NULL)
                goto cleanup;

            len = _snwprintf(pDevice->DeviceID.Buffer, ARRAYSIZE(pDevice->DeviceID.Buffer), L"%s", deviceID);
            cplug_assert(len < ARRAYSIZE(pDevice->DeviceID.Buffer)); // assert string wasn't truncated

            // https://learn.microsoft.com/en-us/windows/win32/api/propsys/nn-propsys-ipropertystore
            // https://learn.microsoft.com/en-us/windows/win32/api/propsys/nf-propsys-ipropertystore-getvalue
            // https://learn.microsoft.com/en-us/previous-versions/aa912007(v=msdn.10)
            // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-properties
            // https://learn.microsoft.com/en-us/windows/win32/coreaudio/audio-endpoint-properties

            hr = pIMMDevice->lpVtbl->OpenPropertyStore(pIMMDevice, STGM_READ, &pProperties);
            cplug_assert(hr == S_OK);
            if (hr != S_OK || pProperties == NULL)
                goto cleanup;

            hr = pProperties->lpVtbl->GetValue(
                pProperties,
                CPLUG_WTF_IS_A_REFERENCE(_PKEY_Device_FriendlyName),
                &varName);

            cplug_assert(hr == S_OK);
            if (hr != S_OK || varName.pwszVal == NULL || varName.vt == VT_EMPTY)
                goto cleanup;

            _snwprintf(pDevice->Name, ARRAYSIZE(pDevice->Name), L"%s", varName.pwszVal);

            // Unfortunately MS don't just tell you what configurations are supported. We have to jump through COM hoops
            // querying support for all the granular configurations we can offer the user
            // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-activate
            // "Activate" is really just used to create more COM obejcts... It's poorly named.
            hr = pIMMDevice->lpVtbl->Activate(
                pIMMDevice,
                CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioClient),
                CLSCTX_ALL,
                0,
                (void**)&pIAudioClient);
            if (hr != S_OK || pIAudioClient == NULL)
                goto cleanup;

            // query available sample rates
            for (int j = 0; j < ARRAYSIZE(pDevice->SupportedSampleRates); j++)
            {
                int                  SampleRate = CPLUG_SAMPLE_RATES[j];
                WAVEFORMATEXTENSIBLE wfmt       = Cplug_Audio_CreateWaveFormat(g_Audio.NumChannels, SampleRate);

                WAVEFORMATEX* SupportedFormat = NULL;
                // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-isformatsupported
                hr = pIAudioClient->lpVtbl->IsFormatSupported(
                    pIAudioClient,
                    AUDCLNT_SHAREMODE_SHARED,
                    (WAVEFORMATEX*)&wfmt,
                    &SupportedFormat);

                if (hr == S_OK)
                {
                    pDevice->SupportedSampleRates[j] = TRUE;
                    HasSupportedFormat               = TRUE;
                }
                else if (hr == S_FALSE)
                {
                    // TODO: MS want you to check for the "closest match" inside SupportedFormat upon S_FALSE
                }

                if (SupportedFormat)
                    CoTaskMemFree(SupportedFormat);
            }

        cleanup:
            CPLUG_COM_RELEASE(pIAudioClient);

            if (varName.pwszVal)
            {
                PropVariantClear(&varName);
            }
            CPLUG_COM_RELEASE(pProperties);

            if (deviceID)
            {
                CoTaskMemFree(deviceID);
            }
            CPLUG_COM_RELEASE(pIMMDevice);

            if (HasSupportedFormat)
            {
                g_Audio.NumDevices++;
            }
        }
    }

    CPLUG_COM_RELEASE(pCollection);
}

// [Audio Thread]
bool Cplug_Audio_enqueueEvent(struct CplugProcessContext* ctx, const CplugEvent* e, uint32_t frameIdx) { return true; }

// [Audio Thread]
bool Cplug_Audio_dequeueEvent(struct CplugProcessContext* ctx, CplugEvent* event, uint32_t frameIdx)
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

// [Audio Thread]
float** Cplug_Audio_getAudioInput(const struct CplugProcessContext* ctx, uint32_t busIdx) { return NULL; }

// [Audio Thread]
float** Cplug_Audio_getAudioOutput(const struct CplugProcessContext* ctx, uint32_t busIdx)
{
    const WindowsProcessContext* winctx = (const WindowsProcessContext*)ctx;
    if (busIdx == 0)
        return (float**)&winctx->output[0];
    return NULL;
}

// [Audio Thread]
void Cplug_Audio_Process(const UINT32 blockSize)
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
    ctx.cplugContext.numFrames       = g_Audio.BlockSize;
    ctx.cplugContext.numInputBusses  = 0;
    ctx.cplugContext.numOutputBusses = 1;
    ctx.cplugContext.enqueueEvent    = Cplug_Audio_enqueueEvent;
    ctx.cplugContext.dequeueEvent    = Cplug_Audio_dequeueEvent;
    ctx.cplugContext.getAudioInput   = Cplug_Audio_getAudioInput;
    ctx.cplugContext.getAudioOutput  = Cplug_Audio_getAudioOutput;

    SIZE_T processBufferOffset = sizeof(float) * g_Audio.NumChannels * g_Audio.ProcessBufferMaxFrames;
    processBufferOffset        = (SIZE_T)Cplug_RoundUp(processBufferOffset, 32);
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
// [Audio Thread]
DWORD WINAPI Cplug_Audio_RunProcessThread(LPVOID data)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw
    DWORD  TaskIndex             = 0;
    HANDLE ThreadCharacteristics = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);
    cplug_assert(ThreadCharacteristics);

    Cplug_Audio_Process(g_Audio.ProcessBufferMaxFrames);

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

        Cplug_Audio_Process(blockSize);
    }

    // https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avrevertmmthreadcharacteristics
    BOOL ok = AvRevertMmThreadCharacteristics(ThreadCharacteristics);
    cplug_assert(ok);

    return 0;
}
// [Main Thread]
void Cplug_Audio_Stop()
{
    cplug_assert(g_Audio.FlagExitAudioThread == 0);
    cplug_assert(g_Audio.pIAudioRenderClient != NULL);
    cplug_assert(g_Audio.pIAudioClient != NULL);
    cplug_assert(g_Audio.hAudioProcessThread != NULL);

    if (g_Audio.hAudioProcessThread == NULL)
    {
        fprintf(stderr, "[WARNING] Called Cplug_Audio_Stop() when audio is not running\n");
    }
    else
    {
        g_Audio.FlagExitAudioThread = 1;
        cplug_assert(g_Audio.hAudioEvent);
        SetEvent(g_Audio.hAudioEvent);

        WaitForSingleObject(g_Audio.hAudioProcessThread, INFINITE);
        CloseHandle(g_Audio.hAudioProcessThread);
        g_Audio.hAudioProcessThread = NULL;
    }

    if (g_Audio.pIAudioClient)
        g_Audio.pIAudioClient->lpVtbl->Stop(g_Audio.pIAudioClient);

    CPLUG_COM_RELEASE(g_Audio.pIAudioRenderClient);
    CPLUG_COM_RELEASE(g_Audio.pIAudioClient);

    if (g_Audio.hAudioEvent)
    {
        CloseHandle(g_Audio.hAudioEvent);
        g_Audio.hAudioEvent = NULL;
    }
}

// [Main Thread]
void Cplug_Audio_Start()
{
#ifdef HOTRELOAD_WATCH_DIR
    if (g_Hotreload.hPluginDLL == NULL)
    {
        fprintf(stderr, "[FAILED] Called Cplug_Audio_Start when no plugin is loaded\n");
        return;
    }
#endif // Hotreload
    cplug_assert(g_Audio.SampleRate != 0);
    cplug_assert(g_Audio.BlockSize != 0);
    cplug_assert(g_Audio.pIMMDevice != NULL);
    cplug_assert(g_Audio.pIAudioClient == NULL);

    HRESULT hr = g_Audio.pIMMDevice->lpVtbl->Activate(
        g_Audio.pIMMDevice,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioClient),
        CLSCTX_ALL,
        0,
        (void**)&g_Audio.pIAudioClient);
    cplug_assert(!FAILED(hr));

    {
        WAVEFORMATEXTENSIBLE wfmt = Cplug_Audio_CreateWaveFormat(g_Audio.NumChannels, g_Audio.SampleRate);
        REFERENCE_TIME reftime    = (REFERENCE_TIME)((double)g_Audio.BlockSize / ((double)g_Audio.SampleRate * 1.e-7));

        // https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
        hr = g_Audio.pIAudioClient->lpVtbl->Initialize(
            g_Audio.pIAudioClient,
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            reftime,
            0,
            (WAVEFORMATEX*)&wfmt,
            0);
    }
    cplug_assert(hr == S_OK);
    cplug_assert(g_Audio.pIAudioClient != NULL);
    if (hr != S_OK || g_Audio.pIAudioClient == NULL)
        goto fail;

    hr = g_Audio.pIAudioClient->lpVtbl->GetBufferSize(g_Audio.pIAudioClient, &g_Audio.ProcessBufferMaxFrames);
    cplug_assert(hr == S_OK);
    if (hr != S_OK)
        goto fail;

    hr = g_Audio.pIAudioClient->lpVtbl->GetService(
        g_Audio.pIAudioClient,
        CPLUG_WTF_IS_A_REFERENCE(_IID_IAudioRenderClient),
        (void**)&g_Audio.pIAudioRenderClient);
    cplug_assert(hr == S_OK);
    cplug_assert(g_Audio.pIAudioRenderClient != NULL);
    if (hr != S_OK || g_Audio.pIAudioRenderClient == NULL)
        goto fail;

    cplug_assert(g_Audio.hAudioEvent == NULL);
    g_Audio.hAudioEvent = CreateEventW(0, 0, 0, 0);
    cplug_assert(g_Audio.hAudioEvent != NULL);
    if (g_Audio.hAudioEvent == NULL)
        goto fail;

    hr = g_Audio.pIAudioClient->lpVtbl->SetEventHandle(g_Audio.pIAudioClient, g_Audio.hAudioEvent);
    cplug_assert(hr == S_OK);
    if (hr != S_OK)
        goto fail;

    // Allocate buffer
    {
        SIZE_T req_bytes_reserve    = sizeof(float) * g_Audio.NumChannels * g_Audio.ProcessBufferMaxFrames;
        SIZE_T req_bytes_processing = sizeof(float) * g_Audio.NumChannels * g_Audio.BlockSize;
        req_bytes_reserve           = (SIZE_T)Cplug_RoundUp(req_bytes_reserve, 32);
        req_bytes_processing        = (SIZE_T)Cplug_RoundUp(req_bytes_processing, 32);

        // Page sizes are typically 64kb on Windows
        SIZE_T requiredCap = (SIZE_T)Cplug_RoundUp(req_bytes_reserve + req_bytes_processing, 1024 * 64);
        if (requiredCap > g_Audio.ProcessBufferCap)
        {
            if (g_Audio.ProcessBuffer != NULL)
                VirtualFree(g_Audio.ProcessBuffer, g_Audio.ProcessBufferCap, 0);

            g_Audio.ProcessBufferCap = requiredCap;
            g_Audio.ProcessBuffer =
                (BYTE*)VirtualAlloc(NULL, g_Audio.ProcessBufferCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            cplug_assert(g_Audio.ProcessBuffer != NULL);
        }
    }

    g_plugin.setSampleRateAndBlockSize(g_plugin.UserPlugin, g_Audio.SampleRate, g_Audio.BlockSize);

    g_Audio.ProcessBufferNumOverprocessedFrames = 0;
    g_Audio.FlagExitAudioThread                 = 0;

    g_Audio.hAudioProcessThread = CreateThread(NULL, 0, Cplug_Audio_RunProcessThread, NULL, 0, 0);
    cplug_assert(g_Audio.hAudioProcessThread != NULL);

    return;

fail:
    Cplug_Audio_Stop();
}

// [Main Thread]
INT Cplug_Audio_GetActiveDeviceIndex()
{
    // Find active device
    for (INT i = 0; i < g_Audio.NumDevices; i++)
    {
        struct CplugAudioDevice* pDevice = &g_Audio.Devices[i];

        BOOL IsMatch = 0 == memcmp(&pDevice->DeviceID, &g_Audio.CurrentDeviceID, sizeof(g_Audio.CurrentDeviceID));
        if (IsMatch)
            return i;
    }
    // cplug_assert(false);
    return -1;
}

// [Main Thread]
// Pass a deviceIdx < 0 for default device
void Cplug_Audio_SetDevice(int deviceIdx)
{
    cplug_assert(g_Audio.hAudioProcessThread == NULL);
    HRESULT hr = S_OK;

    CPLUG_COM_RELEASE(g_Audio.pIMMDevice);
    g_Audio.CurrentDeviceID = (CplugAudioDeviceID){0};

    if (deviceIdx >= 0)
    {
        IMMDeviceCollection* pCollection = NULL;

        hr = g_Audio.pIMMDeviceEnumerator->lpVtbl
                 ->EnumAudioEndpoints(g_Audio.pIMMDeviceEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection);
        cplug_assert(hr == S_OK);
        cplug_assert(pCollection != NULL);

        UINT numDevices = 0;
        pCollection->lpVtbl->GetCount(pCollection, &numDevices);

        if ((UINT)deviceIdx < numDevices)
            pCollection->lpVtbl->Item(pCollection, (UINT)deviceIdx, &g_Audio.pIMMDevice);

        CPLUG_COM_RELEASE(pCollection);
    }

    if (g_Audio.pIMMDevice == NULL)
    {
        // eConsole or eMultimedia? Microsoft say console is for games, multimedia for playing live music
        // https://learn.microsoft.com/en-us/windows/win32/coreaudio/device-roles
        // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-getdefaultaudioendpoint
        hr = g_Audio.pIMMDeviceEnumerator->lpVtbl
                 ->GetDefaultAudioEndpoint(g_Audio.pIMMDeviceEnumerator, eRender, eMultimedia, &g_Audio.pIMMDevice);
        cplug_assert(hr == S_OK);
    }

    if (g_Audio.pIMMDevice)
    {
        WCHAR* audioDeviceID = NULL;
        // https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdevice-getid
        hr = g_Audio.pIMMDevice->lpVtbl->GetId(g_Audio.pIMMDevice, &audioDeviceID);
        cplug_assert(hr == S_OK);
        cplug_assert(audioDeviceID != NULL);
        if (audioDeviceID)
        {
            _snwprintf(g_Audio.CurrentDeviceID.Buffer, ARRAYSIZE(g_Audio.CurrentDeviceID.Buffer), L"%s", audioDeviceID);
            CoTaskMemFree(audioDeviceID);
        }

        // Check that our last chosen sample rate is supported by this device
        INT DeviceIndex = Cplug_Audio_GetActiveDeviceIndex();

        if (DeviceIndex >= 0 && DeviceIndex < g_Audio.NumDevices)
        {
            struct CplugAudioDevice* pDevice = &g_Audio.Devices[DeviceIndex];

            BOOL IsSupported = FALSE;
            for (int i = 0; i < ARRAYSIZE(CPLUG_SAMPLE_RATES); i++)
            {
                INT SampleRate  = CPLUG_SAMPLE_RATES[i];
                IsSupported    |= (SampleRate == g_Audio.SampleRate) & pDevice->SupportedSampleRates[i];
            }

            if (IsSupported == FALSE)
            {
                // Oh dear, we will have to force a sample rate change here
                for (int i = 0; i < ARRAYSIZE(CPLUG_SAMPLE_RATES); i++)
                {
                    if (pDevice->SupportedSampleRates[i])
                    {
                        INT SampleRate     = CPLUG_SAMPLE_RATES[i];
                        g_Audio.SampleRate = SampleRate;
                        break;
                    }
                }
            }
        }
    }
}

#pragma endregion AUDIO

#pragma region DARKMODE

typedef enum
{
    APPMODE_DEFAULT    = 0,
    APPMODE_ALLOWDARK  = 1,
    APPMODE_FORCEDARK  = 2,
    APPMODE_FORCELIGHT = 3,
} PreferredAppMode;

typedef bool(WINAPI* ShouldAppsUseDarkModeProc)(void);                       // ordinal 132
typedef PreferredAppMode(WINAPI* SetPreferredAppModeProc)(PreferredAppMode); // ordinal 135
typedef void(WINAPI* FlushMenuThemesProc)(void);                             // ordinal 136

// https://github.com/adzm/win32-custom-menubar-aero-theme
#define WM_UAHDRAWMENU        0x0091 // lParam is UAHMENU
#define WM_UAHDRAWMENUITEM    0x0092 // lParam is UAHDRAWMENUITEM
#define WM_UAHMEASUREMENUITEM 0x0094 // lParam is UAHMEASUREMENUITEM

// describes the sizes of the menu bar or menu item
typedef union tagUAHMENUITEMMETRICS
{
    struct
    {
        DWORD cx;
        DWORD cy;
    } rgsizeBar[2];
    struct
    {
        DWORD cx;
        DWORD cy;
    } rgsizePopup[4];
} UAHMENUITEMMETRICS;

// not really used in our case but part of the other structures
typedef struct tagUAHMENUPOPUPMETRICS
{
    DWORD rgcx[4];
    DWORD fUpdateMaxWidths : 2; // from kernel symbols, padded to full dword
} UAHMENUPOPUPMETRICS;

// hmenu is the main window menu; hdc is the context to draw in
typedef struct tagUAHMENU
{
    HMENU hmenu;
    HDC   hdc;
    DWORD dwFlags; // no idea what these mean, in my testing it's either 0x00000a00 or sometimes 0x00000a10
} UAHMENU;

// menu items are always referred to by iPosition here
typedef struct tagUAHMENUITEM
{
    int                 iPosition; // 0-based position of menu item in menubar
    UAHMENUITEMMETRICS  umim;
    UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

// the DRAWITEMSTRUCT contains the states of the menu items, as well as
// the position index of the item in the menu, which is duplicated in
// the UAHMENUITEM's iPosition as well
typedef struct UAHDRAWMENUITEM
{
    DRAWITEMSTRUCT dis; // itemID looks uninitialized
    UAHMENU        um;
    UAHMENUITEM    umi;
} UAHDRAWMENUITEM;

// the MEASUREITEMSTRUCT is intended to be filled with the size of the item
// height appears to be ignored, but width can be modified
typedef struct tagUAHMEASUREMENUITEM
{
    MEASUREITEMSTRUCT mis;
    UAHMENU           um;
    UAHMENUITEM       umi;
} UAHMEASUREMENUITEM;

struct
{
    BOOL   IsDarkMode;
    HTHEME hThemeMenu;

    ShouldAppsUseDarkModeProc ShouldAppsUseDarkMode;
    SetPreferredAppModeProc   SetPreferredAppMode;
    FlushMenuThemesProc       FlushMenuThemes;
} g_DarkMode;

void Cplug_OpenMenuTheme()
{
    // https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-openthemedata
    if (g_hwnd && !g_DarkMode.hThemeMenu)
    {
        // g_DarkMode.hThemeMenu = OpenThemeData(g_hwnd, L"Menu"); // Light mode
        g_DarkMode.hThemeMenu = OpenThemeData(g_hwnd, L"DarkMode::Menu");
    }
}
void Cplug_CloseMenuTheme()
{
    if (g_DarkMode.hThemeMenu)
    {
        CloseThemeData(g_DarkMode.hThemeMenu);
        g_DarkMode.hThemeMenu = NULL;
    }
}

#pragma endregion DARKMODE

// ----==== MENUS ====----
enum
{
    IDM_Noop,
    IDM_SampleRate_44100,
    IDM_SampleRate_48000,
    IDM_SampleRate_88200,
    IDM_SampleRate_96000,
    IDM_SampleRate_192000,
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

    IDM_HandleRemovedAudioDevice,
    IDM_HandleAddedAudioDevice,

    IDM_OFFSET_AUDIO_DEVICES   = 50,
    IDM_RefreshAudioDeviceList = 99,

    IDM_OFFSET_MIDI_DEVICES = 100,
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

#pragma region MENUS

static inline UINT Cplug_MenuFlag(UINT a, UINT b) { return a == b ? (MF_STRING | MF_CHECKED) : MF_STRING; }

void Cplug_Menu_RebuildSampleRateSubmenu()
{
    while (RemoveMenu(g_Menus.hSampleRateSubmenu, 0, MF_BYPOSITION))
    {
    }

    INT DeviceIdx = Cplug_Audio_GetActiveDeviceIndex();
    if (DeviceIdx >= 0 && DeviceIdx < g_Audio.NumDevices)
    {
        struct CplugAudioDevice* pDevice = &g_Audio.Devices[DeviceIdx];

        // Build submenu of supported sample rates
        for (UINT i = 0; i < ARRAYSIZE(pDevice->SupportedSampleRates); i++)
        {
            if (pDevice->SupportedSampleRates[i])
            {
                INT          SampleRate    = CPLUG_SAMPLE_RATES[i];
                const WCHAR* SampleRateStr = CPLUG_SAMPLE_RATES_STR[i];

                UINT Flag = Cplug_MenuFlag(g_Audio.SampleRate, SampleRate);
                AppendMenuW(g_Menus.hSampleRateSubmenu, Flag, IDM_SampleRate_44100 + i, SampleRateStr);
            }
        }
    }
}

void Cplug_Menu_RefreshBlockSizes()
{
    while (RemoveMenu(g_Menus.hBlockSizeSubmenu, 0, MF_BYPOSITION))
    {
    }

    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 128), IDM_BlockSize_128, L"128");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 192), IDM_BlockSize_192, L"192");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 256), IDM_BlockSize_256, L"256");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 384), IDM_BlockSize_384, L"384");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 448), IDM_BlockSize_448, L"448");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 512), IDM_BlockSize_512, L"512");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 768), IDM_BlockSize_768, L"768");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 1024), IDM_BlockSize_1024, L"1024");
    AppendMenuW(g_Menus.hBlockSizeSubmenu, Cplug_MenuFlag(g_Audio.BlockSize, 2048), IDM_BlockSize_2048, L"2048");
}

void Cplug_Menu_RebuildAudioOutputsSubmenu()
{
    while (RemoveMenu(g_Menus.hAudioOutputSubmenu, 0, MF_BYPOSITION))
    {
    }

    for (UINT i = 0; i < g_Audio.NumDevices; i++)
    {
        struct CplugAudioDevice* pDevice = &g_Audio.Devices[i];

        UINT uFlags = MF_STRING;
        _STATIC_ASSERT(sizeof(pDevice->DeviceID) == sizeof(g_Audio.CurrentDeviceID));
        if (0 == memcmp(&pDevice->DeviceID, &g_Audio.CurrentDeviceID, sizeof(g_Audio.CurrentDeviceID)))
            uFlags |= MF_CHECKED;

        AppendMenuW(g_Menus.hAudioOutputSubmenu, uFlags, IDM_OFFSET_AUDIO_DEVICES + i, pDevice->Name);
    }

    AppendMenuW(g_Menus.hAudioOutputSubmenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_Menus.hAudioOutputSubmenu, MF_STRING, IDM_RefreshAudioDeviceList, L"Refresh list");
}

void Cplug_Menu_RebuildMIDIInputSubmenu()
{
    // Remove everything in submenu
    while (RemoveMenu(g_Menus.hMIDIInputsSubMenu, 0, MF_BYPOSITION))
    {
    }

    for (UINT i = 0; i < g_MIDI.NumDevices; i++)
    {
        MIDIINCAPS2W* Caps    = &g_MIDI.Devices[i];
        BOOL          IsMatch = Cplug_MIDI_MatchDevice(Caps, &g_MIDI.ConnectedDevice);
        UINT          uFlags  = MF_STRING;
        if (IsMatch)
            uFlags |= MF_CHECKED;

        AppendMenuW(g_Menus.hMIDIInputsSubMenu, uFlags, IDM_OFFSET_MIDI_DEVICES + i, Caps->szPname);
    }
}

#pragma endregion MENUS

#pragma region HOTPLUGGING

// WARNING: Windows 8+ feature
HCMNOTIFICATION g_hCMNotification;

BOOL Cplug_StartsWith(const WCHAR* str, const WCHAR* prefix)
{
    while (*str != 0 && *prefix != 0 && *str == *prefix)
    {
        str++;
        prefix++;
    }
    return *prefix == 0;
}

// Unknown system thread. Notify Connected/disconnected devices. We only check Audio/MIDI
DWORD CALLBACK Cplug_HandleDeviceChange(
    HCMNOTIFICATION       hNotify,
    PVOID                 hwnd,
    CM_NOTIFY_ACTION      Action,
    PCM_NOTIFY_EVENT_DATA EventData,
    DWORD                 EventDataSize)
{
    if (!EventData)
        return 0;
    const WCHAR* Id = &EventData->u.DeviceInstance.InstanceId[0];
    if (!Id)
        return 0;

    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
        // I've found updating MIDI lists here less reliable than doing a full rescan every time a device is
        // added/removed
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
        // MIDI input instance IDs come in this format:
        // SWD\MMDEVAPI\MIDII_(4 byte hex).P_(2 byte hex)
        // Software device - MMDevice API - MIDI Input
        // Update 2026: Microsoft has been working on their MIDI APIs atm with the new "Windows MIDI Services".
        // MIDI input devices now appear to begin with "SWD\\MIDISRV\\MIDIU_KSA_"
        // Audio devices use the format: L"SWD\MMDEVAPI\{0.0.[0-9].00000000}.{(GUID)}"
        // This appears to match the same device ID string as found with IMMDevice::GetId()
        // Unfortunately the beginning of the audio IDs matches the beginning of the old MIDI IDs, so its critical to
        // check for MIDI IDs before audio IDs
        // MS have recently corrected this confusion, so this shouldn't be an issue in 2040...
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/install/device-instance-ids
        if (Cplug_StartsWith(Id, L"SWD\\MIDISRV\\MIDIU_KSA_") || Cplug_StartsWith(Id, L"SWD\\MMDEVAPI\\MIDII_"))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleRemovedMIDIDevice, 0);
        else if (Cplug_StartsWith(Id, L"SWD\\MMDEVAPI\\"))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleRemovedAudioDevice, 0);
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
        if (Cplug_StartsWith(Id, L"SWD\\MIDISRV\\MIDIU_KSA_") || Cplug_StartsWith(Id, L"SWD\\MMDEVAPI\\MIDII_"))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleAddedMIDIDevice, 0);
        else if (Cplug_StartsWith(Id, L"SWD\\MMDEVAPI\\"))
            PostMessageW((HWND)hwnd, WM_COMMAND, IDM_HandleAddedAudioDevice, 0);
        break;
    default:
        break;
    }
    return 0;
}
#pragma endregion HOTPLUGGING

LRESULT CALLBACK Cplug_WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
            Cplug_Audio_Stop();
        cplug_assert(g_Audio.ProcessBuffer != NULL);
        VirtualFree(g_Audio.ProcessBuffer, g_Audio.ProcessBufferCap, 0);
        CPLUG_COM_RELEASE(g_Audio.pIMMDevice);
        CPLUG_COM_RELEASE(g_Audio.pIMMDeviceEnumerator);

        // Shutdown MIDI
        Cplug_MIDI_DisconnectInput();

        Cplug_CloseMenuTheme();

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
            const WCHAR* Ext                       = Cplug_GetFileExtensionW(CurrentDllPath);
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
    // https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
    case WM_DPICHANGED:
    {
        int Yaxis = HIWORD(wParam);
        int Xaxis = LOWORD(wParam);
        g_dpi     = (float)Yaxis / USER_DEFAULT_SCREEN_DPI;
        g_plugin.setScaleFactor(g_plugin.UserGUI, g_dpi);

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
        case IDM_Noop:
            break;
        case IDM_SampleRate_44100:
        case IDM_SampleRate_48000:
        case IDM_SampleRate_88200:
        case IDM_SampleRate_96000:
        case IDM_SampleRate_192000:
        {
            Cplug_Audio_Stop();
            WCHAR text[8];
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmenustringw
            int numCharsCopied =
                GetMenuStringW(g_Menus.hSampleRateSubmenu, wParam, text, ARRAYSIZE(text), MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            g_Audio.SampleRate = _wtoi(text);
            Cplug_Audio_Start();
            Cplug_Menu_RebuildSampleRateSubmenu();
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
            Cplug_Audio_Stop();
            WCHAR text[8];
            int numCharsCopied = GetMenuStringW(g_Menus.hBlockSizeSubmenu, wParam, text, ARRAYSIZE(text), MF_BYCOMMAND);
            cplug_assert(numCharsCopied > 0);
            g_Audio.BlockSize = _wtoi(text);
            Cplug_Audio_Start();
            Cplug_Menu_RefreshBlockSizes();
            break;
        }
        case IDM_RefreshAudioDeviceList:
            Cplug_Audio_ScanDevices();
            Cplug_Menu_RebuildAudioOutputsSubmenu();
            break;
        case IDM_HandleRemovedMIDIDevice:
        {
            fprintf(stderr, "Callback: Removed MIDI input device\n");
            Cplug_MIDI_RescanInputs();
            if (g_MIDI.IsConnected)
            {
                UINT num = midiInGetNumDevs();
                if (num == 0)
                {
                    Cplug_MIDI_DisconnectInput();
                    fprintf(stderr, "WARNING: Not connected to a MIDI input device\n");
                }
                else
                {
                    // Check it was the connected device which was removed
                    UINT i = 0;
                    for (; i < num; i++)
                    {
                        MIDIINCAPS2W* Caps    = &g_MIDI.Devices[i];
                        BOOL          IsMatch = Cplug_MIDI_MatchDevice(Caps, &g_MIDI.ConnectedDevice);
                        if (IsMatch)
                            break;
                    }
                    // Failed to match our connected device
                    if (i == num)
                    {
                        fprintf(
                            stderr,
                            "Connected MIDI input device was removed. Trying to connecting to the next available "
                            "device\n");
                        Cplug_MIDI_DisconnectInput();
                        Cplug_MIDI_ConnectInput(0);
                    }
                }
            }
            Cplug_Menu_RebuildMIDIInputSubmenu();
            break;
        }
        case IDM_HandleAddedMIDIDevice:
        {
            fprintf(stderr, "Callback: New MIDI input device\n");
            Cplug_MIDI_RescanInputs();

            BOOL HasHotplugDevice = g_MIDI.HotplugDevice.vDriverVersion != 0;
            BOOL IsConnectedToHotplugDevice =
                0 == memcmp(&g_MIDI.ConnectedDevice, &g_MIDI.HotplugDevice, sizeof(g_MIDI.HotplugDevice));
            if (HasHotplugDevice && !IsConnectedToHotplugDevice)
            {
                // Check to see if our last device was just connected
                for (UINT i = 0; i < g_MIDI.NumDevices; i++)
                {
                    MIDIINCAPS2W* Caps    = &g_MIDI.Devices[i];
                    BOOL          IsMatch = Cplug_MIDI_MatchDevice(Caps, &g_MIDI.HotplugDevice);
                    if (IsMatch)
                    {
                        Cplug_MIDI_ConnectInput(i);
                        break;
                    }
                }
            }
            // At least connect to something...
            if (g_MIDI.IsConnected == 0 && g_MIDI.NumDevices)
            {
                fprintf(stderr, "Trying to connect new device\n");
                Cplug_MIDI_ConnectInput(0);
            }

            Cplug_Menu_RebuildMIDIInputSubmenu();
            break;
        }
        case IDM_HandleRemovedAudioDevice:
        {
            Cplug_Audio_ScanDevices();
            INT ActiveIndex = Cplug_Audio_GetActiveDeviceIndex();
            if (ActiveIndex == -1) // failed. Our connceted device got removed...
            {
                Cplug_Audio_Stop();
                Cplug_Audio_SetDevice(-1);
                Cplug_Audio_Start();
            }
            Cplug_Menu_RebuildAudioOutputsSubmenu();
            Cplug_Menu_RebuildSampleRateSubmenu();
            break;
        }
        case IDM_HandleAddedAudioDevice:
        {
            // The OS will determine the "preferred device". Other apps like Chrome automatically switch to the new
            // default on the fly. We will copy that behaviour
            Cplug_Audio_ScanDevices();
            Cplug_Audio_Stop();
            Cplug_Audio_SetDevice(-1);
            Cplug_Audio_Start();
            Cplug_Menu_RebuildAudioOutputsSubmenu();
            Cplug_Menu_RebuildSampleRateSubmenu();
            break;
        }
        default:
        {
            UINT64 AudioDeviceIdx = (UINT64)wParam - IDM_OFFSET_AUDIO_DEVICES;
            UINT64 MidiDeviceIdx  = (UINT64)wParam - IDM_OFFSET_MIDI_DEVICES;
            if (AudioDeviceIdx < g_Audio.NumDevices)
            {
                Cplug_Audio_Stop();
                Cplug_Audio_SetDevice(AudioDeviceIdx);
                Cplug_Audio_Start();
                Cplug_Menu_RebuildAudioOutputsSubmenu();
            }
            if (MidiDeviceIdx < g_MIDI.NumDevices)
            {
                MMRESULT err = Cplug_MIDI_ConnectInput((UINT)MidiDeviceIdx);
                if (err == 0 && g_MIDI.IsConnected)
                {
                    // Set new preferred hotplug device
                    g_MIDI.HotplugDevice = g_MIDI.ConnectedDevice;
                }
                Cplug_Menu_RebuildMIDIInputSubmenu();
            }
        }
        }
        DrawMenuBar(hWnd);
        break;
    }
    // Not sure if we need this...
    // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-erasebkgnd
    // case WM_ERASEBKGND:
    // {
    //     if (g_DarkMode.colBG)
    //     {
    //         HDC  hdc = (HDC)wParam;
    //         RECT rc;
    //         GetClientRect(hWnd, &rc);
    //         FillRect(hdc, &rc, g_DarkMode.hBrushBG);
    //         return 1;
    //     }
    //     break;
    // }
    // https://learn.microsoft.com/en-us/windows/win32/gdi/wm-ncpaint
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    {
        LRESULT ret = DefWindowProcW(hWnd, uMsg, wParam, lParam);
        if (g_DarkMode.IsDarkMode && !!g_Menus.hMain)
        {
            RECT rcClient = {0};
            BOOL ok       = GetClientRect(hWnd, &rcClient);
            cplug_assert(ok);
            MapWindowPoints(hWnd, NULL, (POINT*)&rcClient, 2);

            RECT rcWindow = {0};
            ok            = GetWindowRect(hWnd, &rcWindow);
            cplug_assert(ok);
            ok = OffsetRect(&rcClient, -rcWindow.left, -rcWindow.top);
            cplug_assert(ok);

            // the rcBar is offset by the window rect
            RECT rc   = rcClient;
            rc.bottom = rcClient.top;
            rc.top    = rcClient.top - 1;

            HDC hdc = GetWindowDC(hWnd);
            DrawThemeBackground(g_DarkMode.hThemeMenu, hdc, MENU_POPUPITEM, MPI_NORMAL, &rc, NULL);
            ReleaseDC(hWnd, hdc);
        }

        return ret;
    }
    case WM_UAHDRAWMENU:
    {
        if (g_DarkMode.IsDarkMode)
        {
            Cplug_OpenMenuTheme();

            UAHMENU*    pUDM = (UAHMENU*)lParam;
            RECT        rc   = {0};
            MENUBARINFO mbi  = {sizeof(mbi)};
            RECT        rcWindow;

            GetMenuBarInfo(hWnd, OBJID_MENU, 0, &mbi);
            GetWindowRect(hWnd, &rcWindow);
            rc = mbi.rcBar;
            OffsetRect(&rc, -rcWindow.left, -rcWindow.top);
            rc.top -= 1;

            DrawThemeBackground(g_DarkMode.hThemeMenu, pUDM->hdc, MENU_POPUPITEM, MPI_NORMAL, &rc, NULL);

            return 0;
        }
        break;
    }
    case WM_UAHDRAWMENUITEM:
    {
        if (g_DarkMode.IsDarkMode)
        {
            Cplug_OpenMenuTheme();

            UAHDRAWMENUITEM* pUDMI = (UAHDRAWMENUITEM*)lParam;

            // Get menu title
            WCHAR        StringBuffer[256] = {0};
            MENUITEMINFO mii               = {sizeof(mii), MIIM_STRING};
            mii.dwTypeData                 = StringBuffer;
            mii.cch                        = (sizeof(StringBuffer) / 2) - 1;
            GetMenuItemInfoW(pUDMI->um.hmenu, pUDMI->umi.iPosition, TRUE, &mii);

            // get the item state for drawing
            DWORD dwFlags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;

            int iTextStateID       = 0;
            int iBackgroundStateID = 0;
            if ((pUDMI->dis.itemState & ODS_INACTIVE) | (pUDMI->dis.itemState & ODS_DEFAULT))
            {
                // iTextStateID       = MBI_NORMAL; // normal display
                // iBackgroundStateID = MBI_NORMAL;
                iTextStateID       = MPI_NORMAL; // normal display
                iBackgroundStateID = MPI_NORMAL;
            }

            if ((pUDMI->dis.itemState & ODS_HOTLIGHT) && !(pUDMI->dis.itemState & ODS_INACTIVE))
            {
                // iTextStateID       = MBI_HOT; // hot tracking / hover
                // iBackgroundStateID = MBI_HOT;
                iTextStateID       = MPI_HOT; // hot tracking
                iBackgroundStateID = MPI_HOT;
            }
            if (pUDMI->dis.itemState & ODS_SELECTED)
            {
                // iTextStateID       = MBI_PUSHED; // clicked
                // iBackgroundStateID = MBI_PUSHED;
                iTextStateID       = MPI_HOT; // hot tracking
                iBackgroundStateID = MPI_HOT;
            }
            if ((pUDMI->dis.itemState & ODS_GRAYED) || (pUDMI->dis.itemState & ODS_DISABLED) ||
                (pUDMI->dis.itemState & ODS_INACTIVE))
            {
                // iTextStateID       = MBI_DISABLED; // disabled / grey text/ inactive
                // iBackgroundStateID = MBI_DISABLED;
                iTextStateID       = MPI_DISABLED; // disabled / grey text/ inactive
                iBackgroundStateID = MPI_DISABLED;
            }
            if (pUDMI->dis.itemState & ODS_NOACCEL)
            {
                dwFlags |= DT_HIDEPREFIX;
            }

            // https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-drawthemebackground
            // https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-drawthemetext

            // NOTE: I think 'MENU_BARITEM' is the _correct_ ID to use here, however my Windows 11 machine will
            // _incorrectly_ draw the light mode theme instead! Using 'MENU_POPUPITEM' is a good fallback and has an
            // identical look and feel
            DrawThemeBackground(
                g_DarkMode.hThemeMenu,
                pUDMI->um.hdc,
                MENU_POPUPITEM,
                iBackgroundStateID,
                &pUDMI->dis.rcItem,
                NULL);
            DrawThemeText(
                g_DarkMode.hThemeMenu,
                pUDMI->um.hdc,
                MENU_POPUPITEM,
                // MENU_BARITEM,
                iTextStateID,
                StringBuffer,
                mii.cch,
                dwFlags,
                0,
                &pUDMI->dis.rcItem);
            return 0;
        }
        break;
    }
    case WM_THEMECHANGED:
    {
        Cplug_CloseMenuTheme();
        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        break;
    }
        // Noisy
        // case WM_SETCURSOR:
        // case WM_NCMOUSEMOVE:
        // case WM_NCHITTEST:
        //     break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
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
    memset(&g_DarkMode, 0, sizeof(g_DarkMode));
    memset(&g_Menus, 0, sizeof(g_Menus));

    // Windows 10+ DPI APIs loaded dynamically for compatibility with older Windows versions
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddpiawarenesscontext
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setprocessdpiawarenesscontext
    {
        typedef DPI_AWARENESS_CONTEXT(WINAPI * SetThreadDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        typedef UINT(WINAPI * GetDpiForSystemProc)(void);

        HMODULE                          hUser32 = GetModuleHandleW(L"user32.dll");
        SetThreadDpiAwarenessContextProc pSetThreadDpiCtx =
            (SetThreadDpiAwarenessContextProc)GetProcAddress(hUser32, "SetThreadDpiAwarenessContext");
        SetProcessDpiAwarenessContextProc pSetProcessDpiCtx =
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        GetDpiForSystemProc pGetDpiForSystem = (GetDpiForSystemProc)GetProcAddress(hUser32, "GetDpiForSystem");
        if (pSetThreadDpiCtx)
            pSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        if (pSetProcessDpiCtx)
            pSetProcessDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        g_dpi = pGetDpiForSystem ? (float)pGetDpiForSystem() / (float)USER_DEFAULT_SCREEN_DPI : 1.0f;
    }

    Cplug_LoadPlugin();

    // INIT WINDOW
    {
        g_plugin.UserGUI = g_plugin.createGUI(&g_plugin.HostContext, g_plugin.UserPlugin);
        cplug_assert(g_plugin.UserGUI != NULL);

        g_plugin.setScaleFactor(g_plugin.UserGUI, g_dpi);

        uint32_t guiWidth, guiHeight;
        g_plugin.getSize(g_plugin.UserGUI, &guiWidth, &guiHeight);

        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadiconw
        // Load icon from RC file, if you used one...
        HICON hResIcon = LoadIconW(GetModuleHandleW(0), MAKEINTRESOURCE(1));

        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-wndclassexw
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize      = sizeof(wc);
        wc.lpfnWndProc = Cplug_WindowProc;
        wc.hInstance   = hInst;
        wc.hIcon       = hResIcon != NULL ? hResIcon : LoadIconW(NULL, IDI_APPLICATION); // fallback icon
        wc.hCursor     = LoadCursorW(NULL, IDC_ARROW);
        // wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"CPLUG - " TEXT(CPLUG_PLUGIN_NAME);
        wc.hIconSm       = wc.hIcon;

        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassexw
        if (!RegisterClassExW(&wc))
        {
            fprintf(stderr, "Could not register window class\n");
            return 1;
        }

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

        // May not be required if we set the icons in WndClass?
        // if (hResIcon)
        // {
        //     SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hResIcon);   // set taskbar icon
        //     SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hResIcon); // set window icon
        // }
    }

    // DARK MODE
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmsetwindowattribute
        // https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
        // https://discourse.glfw.org/t/dark-theme-titlebar/2537/2
        // https://github.com/mintty/mintty/issues/983
        // https://gist.github.com/rounk-ctrl/b04e5622e30e0d62956870d5c22b7017

        HMODULE hUXTheme                 = LoadLibraryW(L"uxtheme.dll");
        g_DarkMode.ShouldAppsUseDarkMode = (ShouldAppsUseDarkModeProc)GetProcAddress(hUXTheme, MAKEINTRESOURCEA(132));
        g_DarkMode.SetPreferredAppMode   = (SetPreferredAppModeProc)GetProcAddress(hUXTheme, MAKEINTRESOURCEA(135));
        g_DarkMode.FlushMenuThemes       = (FlushMenuThemesProc)GetProcAddress(hUXTheme, MAKEINTRESOURCEA(136));

        if (g_DarkMode.ShouldAppsUseDarkMode && g_DarkMode.SetPreferredAppMode && g_DarkMode.FlushMenuThemes)
        {
            g_DarkMode.IsDarkMode = g_DarkMode.ShouldAppsUseDarkMode();
            if (g_DarkMode.IsDarkMode)
            {
                DWORD dwDarkModeWindows11 = 20;   // DWMWA_USE_IMMERSIVE_DARK_MODE
                DWORD dwDarkModeWindows10 = 19;   // DWMWA_USE_IMMERSIVE_DARK_MODE (old?)
                BOOL  Dark                = TRUE; // NOTE: requires 4 byte BOOL

                HRESULT hr = DwmSetWindowAttribute(g_hwnd, dwDarkModeWindows11, &Dark, sizeof(Dark));
                if (hr != S_OK)
                    hr = DwmSetWindowAttribute(g_hwnd, dwDarkModeWindows10, &Dark, sizeof(Dark));

                cplug_assert(hr == S_OK);

                g_DarkMode.SetPreferredAppMode(APPMODE_FORCEDARK);
                g_DarkMode.FlushMenuThemes();

                Cplug_OpenMenuTheme();
            }
        }
    }

    // INIT MENU
    {
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

        SetMenu(g_hwnd, g_Menus.hMain);
    }

    // INIT AUDIO
    {
        g_Audio.SampleRate  = CPLUG_DEFAULT_SAMPLE_RATE;
        g_Audio.BlockSize   = CPLUG_DEFAULT_BLOCK_SIZE;
        g_Audio.NumChannels = g_plugin.getOutputBusChannelCount(g_plugin.UserPlugin, 0);
        cplug_assert(g_Audio.NumChannels == 1 || g_Audio.NumChannels == 2); // TODO: supported other configurations

        // Scan for device
        HRESULT hr = CoCreateInstance(
            (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_CLSID_MMDeviceEnumerator),
            0,
            CLSCTX_ALL,
            (REFCLSID)CPLUG_WTF_IS_A_REFERENCE(_IID_IMMDeviceEnumerator),
            (void**)&g_Audio.pIMMDeviceEnumerator);
        cplug_assert(!FAILED(hr));

        Cplug_Audio_ScanDevices();
        Cplug_Audio_SetDevice(-1); // -1 == default device
        Cplug_Audio_Start();
        cplug_assert(g_Audio.ProcessBuffer);
    }

    // INIT MIDI
    {
        for (int i = 0; i < ARRAYSIZE(g_MIDI.Buffers); i++)
        {
            MIDIHDR* head        = &g_MIDI.Buffers[i].Header;
            head->lpData         = &g_MIDI.Buffers[i].Buffer[0];
            head->dwBufferLength = ARRAYSIZE(g_MIDI.Buffers[i].Buffer);
            head->dwUser         = i;
        }
        // For some mysterious reason, possibly in a new update of Windows 11, calling midiInOpen(portnum=0) before
        // midiInOpen() appears to not to work. It's like they recently removed any scanning for
        // devices that used to occur in midiInOpen or something?
        Cplug_MIDI_RescanInputs();
        Cplug_MIDI_ConnectInput(0);
    }

    // Hotplugging
    {
        // Callback to detect connected/disconnected MIDI/Audio devices
        // Must be initialised afer the menu because the callback changes menu items based on new/removed devices
        CM_NOTIFY_FILTER notifyFilter;
        memset(&notifyFilter, 0, sizeof(notifyFilter));
        notifyFilter.cbSize     = sizeof(notifyFilter);
        notifyFilter.Flags      = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
        notifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;

        // Warning: CM_Register_Notification is Windows 8+
        // https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_register_notification
        HRESULT result = CM_Register_Notification(&notifyFilter, g_hwnd, Cplug_HandleDeviceChange, &g_hCMNotification);
        cplug_assert(result == CR_SUCCESS);
        cplug_assert(g_hCMNotification != NULL);
    }

    // Populate submenu items
    Cplug_Menu_RebuildSampleRateSubmenu();
    Cplug_Menu_RefreshBlockSizes();
    Cplug_Menu_RebuildAudioOutputsSubmenu();
    Cplug_Menu_RebuildMIDIInputSubmenu();

    // Window ready
    g_plugin.setParent(g_plugin.UserGUI, g_hwnd);

    ShowWindow(g_hwnd, cmdshow);
    g_plugin.setVisible(g_plugin.UserGUI, true);
    SetForegroundWindow(g_hwnd);

    MSG msg;
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

            INT64 NowNs = Cplug_GetNowNS();
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
        INT64 Now  = Cplug_GetNowNS();
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

                Cplug_Audio_Stop();

                g_PluginState.BytesWritten = 0;
                g_PluginState.BytesRead    = 0;
                g_plugin.saveState(g_plugin.UserPlugin, &g_PluginState, Cplug_WriteStateProc);

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

            const UINT64 buildStart = Cplug_GetNowNS();
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

            const UINT64 buildEnd = Cplug_GetNowNS();

            if (exitCode != 0)
            {
                fprintf(stderr, "[WARNING] Rebuild failed. Exited with code: %lu\n", exitCode);
            }
            else
            {
                Cplug_LoadPlugin();
                g_plugin.loadState(g_plugin.UserPlugin, &g_PluginState, Cplug_ReadStateProc);

                Cplug_Audio_Start();

                // Note: GetClientRect() will set RECT to all zeros if the window is minimised
                RECT size;
                GetClientRect(g_hwnd, &size);
                uint32_t width  = size.right - size.left;
                uint32_t height = size.bottom - size.top;

                g_plugin.UserGUI = g_plugin.createGUI(&g_plugin.HostContext, g_plugin.UserPlugin);
                cplug_assert(g_plugin.UserGUI != NULL);
                g_plugin.setScaleFactor(g_plugin.UserGUI, g_dpi);
                if (width && height)
                    g_plugin.setSize(g_plugin.UserGUI, size.right - size.left, size.bottom - size.top);

                g_plugin.setParent(g_plugin.UserGUI, g_hwnd);
                if (width && height)
                    g_plugin.setVisible(g_plugin.UserGUI, true);
            }

            const UINT64 reloadEnd = Cplug_GetNowNS();

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
