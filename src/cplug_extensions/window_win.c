/* Released into the public domain by Tré Dudman - 2025
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

#include <commdlg.h>
#include <ole2.h>
#include <shellapi.h>
#include <stdio.h>

#include <Shlobj_core.h>

#ifdef PW_DX11
#include <d3d11.h>
#include <dxgi.h>

#define PW_DX11_RELEASE(ptr)                                                                                           \
    if (ptr)                                                                                                           \
    {                                                                                                                  \
        ptr->lpVtbl->Release(ptr);                                                                                     \
        ptr = NULL;                                                                                                    \
    }
#endif

#include <cplug.h>
#include <cplug_extensions/window.h>

#if !defined(GET_X_LPARAM) && !defined(GET_Y_LPARAM)
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#define PW_TIMER_ID 1

static UINT_PTR PW_UNIQUE_INT_ID = 0;

static PWResizeDirection g_ResizeDirection = PW_RESIZE_UNKNOWN;

enum PW_WM_COMMAND
{
    PW_WM_COMMAND_ChooseFile = 69 // Magic number
};

typedef struct CplugWindow
{
    void* gui;
    void* plugin;

    WCHAR ClassName[48];
    HWND  hwnd;
    HHOOK hGetMessageHook;
    HHOOK hCallWndHook;
    HWND  hPrevKeyboardFocus;

    HCURSOR hCursorClosedHand;
    HCURSOR hCursorOpenHand;

    // Windows has no WM_MOUSEMOVE event, so we have to do this.
    BOOL MouseIsOver;

    float dpi;

    struct
    {
        IDropTargetVtbl* lpVtbl;
        IDropTargetVtbl  Vtbl;
        volatile LONG    RefCount;

        UINT   NumPaths;   // Out
        char** pFilePaths; // Out
    } DropTarget;

    struct
    {
        IDropSourceVtbl* lpVtbl;
        IDropSourceVtbl  Vtbl;
        volatile LONG    RefCount;
    } DropSource;

#ifdef PW_DX11
    BOOL IsWindows10OrGreater;

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    IDXGISwapChain*      pSwapchain;
    ID3D11Device*        pDevice;
    ID3D11DeviceContext* pDeviceContext;

    ID3D11Texture2D*        pRenderTarget;
    ID3D11RenderTargetView* pRenderTargetView;
    ID3D11Texture2D*        pDepthStencil;
    ID3D11DepthStencilView* pDepthStencilView;
#endif

    struct
    {
        UINT   NumPaths;   // Out
        char** pFilePaths; // Out

        void*                   callback_data; // In
        pw_choose_file_callback callback;      // In

        bool IsSave;      // In
        bool IsFolder;    // In
        bool Mutliselect; // In

        UINT               NumTypes;   // In
        COMDLG_FILTERSPEC* pFileTypes; // In

        WCHAR* pTitle;  // In
        WCHAR* pFolder; // In
        WCHAR* pName;   // In

        HANDLE hThread;
    } ChooseFile;
} CplugWindow;

#ifndef CPLUG_BUILD_STANDALONE
HINSTANCE g_DLL = NULL;

__declspec(dllexport) BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD     fdwReason, // reason for calling function
    LPVOID    lpvReserved)  // reserved
{
    // Perform actions based on the reason for calling.
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Initialize once for each new process.
        // Return FALSE to fail DLL load.
        g_DLL = hinstDLL;
        break;

    case DLL_THREAD_ATTACH:
        // Do thread-specific initialization.
        break;

    case DLL_THREAD_DETACH:
        // Do thread-specific cleanup.
        break;

    case DLL_PROCESS_DETACH:
        // Perform any necessary cleanup.
        g_DLL = NULL;
        break;
    }
    return TRUE; // Successful DLL_PROCESS_ATTACH.
}
#endif // CPLUG_BUILD_STANDALONE

LPSTR PWMakeUTF8String(PWCHAR utf16)
{
    LPSTR utf8 = NULL;
    int   num  = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, -1, NULL, 0, 0, 0);

    utf8 = PW_MALLOC((num + 1));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, -1, utf8, num, 0, 0);
    utf8[num] = 0;
    return utf8;
}

void PWFreeChooseFile(CplugWindow* pw)
{
    if (pw->ChooseFile.pFilePaths)
    {
        for (int i = 0; i < pw->ChooseFile.NumPaths; i++)
        {
            if (pw->ChooseFile.pFilePaths[i])
            {
                PW_FREE(pw->ChooseFile.pFilePaths[i]);
            }
        }
        PW_FREE(pw->ChooseFile.pFilePaths);
        pw->ChooseFile.pFilePaths = NULL;
    }
    if (pw->ChooseFile.pFileTypes)
    {
        for (int i = 0; i < pw->ChooseFile.NumTypes; i++)
        {
            COMDLG_FILTERSPEC* pFilterSpec = &pw->ChooseFile.pFileTypes[i];
            if (pFilterSpec->pszName)
            {
                PW_FREE((LPWSTR)pFilterSpec->pszName);
            }
            if (pFilterSpec->pszSpec)
            {
                PW_FREE((LPWSTR)pFilterSpec->pszSpec);
            }
        }

        PW_FREE(pw->ChooseFile.pFileTypes);
        pw->ChooseFile.pFileTypes = NULL;
    }
    if (pw->ChooseFile.pTitle)
    {
        PW_FREE(pw->ChooseFile.pTitle);
        pw->ChooseFile.pTitle = NULL;
    }
    if (pw->ChooseFile.pFolder)
    {
        PW_FREE(pw->ChooseFile.pFolder);
        pw->ChooseFile.pFolder = NULL;
    }
    if (pw->ChooseFile.pName)
    {
        PW_FREE(pw->ChooseFile.pName);
        pw->ChooseFile.pName = NULL;
    }
}

void PWFreeDropTarget(CplugWindow* pw)
{
    if (pw->DropTarget.pFilePaths)
    {
        PW_FREE(pw->DropTarget.pFilePaths);
        pw->DropTarget.NumPaths   = 0;
        pw->DropTarget.pFilePaths = NULL;
    }
}

static inline CplugWindow* PWDropTargetShiftPtr(IDropTarget* pDropTarget)
{
    CplugWindow* pw = (CplugWindow*)((char*)pDropTarget - offsetof(CplugWindow, DropTarget));
    return pw;
}

// https://learn.microsoft.com/en-us/windows/win32/com/component-object-model--com--portal
HRESULT STDMETHODCALLTYPE PWDropTarget_QueryInterface(IDropTarget* This, REFIID riid, void** ppvObject)
{
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PWDropTarget_AddRef(IDropTarget* This)
{
    // NOTE: called after RegisterDragDrop()
    CplugWindow* pw = PWDropTargetShiftPtr(This);
    // https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-_inlineinterlockedadd
    return _InlineInterlockedAdd(&pw->DropTarget.RefCount, 1);
}

ULONG STDMETHODCALLTYPE PWDropTarget_Release(IDropTarget* This)
{
    // NOTE: Should be called after RevokeDragDrop(), that's what the docs say, but my testing shows it doesn't get
    // called if you first clicked the windows X close button.
    CplugWindow* pw = PWDropTargetShiftPtr(This);
    // https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-_inlineinterlockedadd
    return _InlineInterlockedAdd(&pw->DropTarget.RefCount, -1);
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-dragenter
// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-idataobject
HRESULT STDMETHODCALLTYPE
PWDropTarget_DragEnter(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    CplugWindow* pw = PWDropTargetShiftPtr(This);

    FORMATETC Format   = {0};
    STGMEDIUM medium   = {0};
    HDROP     hDrop    = NULL;
    UINT      NumPaths = 0;
    HRESULT   hr       = S_OK;

    PW_ASSERT(pw->DropTarget.NumPaths == 0);
    PW_ASSERT(pw->DropTarget.pFilePaths == NULL);

    // Check metadata of dragged object to make sure its a file and not something else

    // https://learn.microsoft.com/en-us/windows/win32/api/objidl/ns-objidl-formatetc
    // https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
    // https://learn.microsoft.com/en-us/windows/win32/shell/clipboard#cf_hdrop
    // Format.cfFormat = CF_TEXT;
    Format.cfFormat = CF_HDROP;
    Format.ptd      = NULL;
    Format.dwAspect = DVASPECT_CONTENT;
    Format.lindex   = -1;
    Format.tymed    = TYMED_HGLOBAL;

    // https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-querygetdata
    hr = pDataObj->lpVtbl->QueryGetData(pDataObj, &Format);
    PW_ASSERT(SUCCEEDED(hr));
    if (FAILED(hr))
        goto error;

    // https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getdata
    hr = pDataObj->lpVtbl->GetData(pDataObj, &Format, &medium);
    PW_ASSERT(SUCCEEDED(hr));
    if (FAILED(hr))
        goto error;

    hDrop = GlobalLock(medium.hGlobal);

    // https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-dragqueryfilew
    NumPaths = DragQueryFileW(hDrop, 0xffffffff, NULL, 0);
    if (NumPaths)
    {
        // Rather than bother with several small mallocs, we'll just pool everything
        const SIZE_T PathArrSize = NumPaths * sizeof(char*);
        const SIZE_T Stride      = MAX_PATH * 2;
        const SIZE_T AllocSize   = PathArrSize + NumPaths * Stride;

        char** PathArr = PW_MALLOC(AllocSize);
        char*  Path    = (char*)(PathArr + NumPaths);
        memset(PathArr, 0, AllocSize);

        for (UINT i = 0; i < NumPaths; i++)
        {
            WCHAR WPath[MAX_PATH];
            int   numchars = (int)DragQueryFileW(hDrop, i, WPath, ARRAYSIZE(WPath));
            PW_ASSERT(numchars);

            if (!numchars)
                break;

            // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
            numchars = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, WPath, numchars, Path, Stride - 1, 0, 0);
            if (numchars <= 0)
                break;

            PathArr[i] = Path;
            pw->DropTarget.NumPaths++;
            Path += Stride;
        }
        pw->DropTarget.pFilePaths = PathArr;
    }

    // if (medium.pUnkForRelease == NULL)
    //     GlobalFree(medium.hGlobal);
    // else
    //     medium.pUnkForRelease->lpVtbl->Release(medium.pUnkForRelease);

    if (medium.hGlobal)
        GlobalUnlock(medium.hGlobal);

    if (pw->DropTarget.pFilePaths)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-screentoclient
        POINT Point = {pt.x, pt.y};
        ScreenToClient(pw->hwnd, &Point);

        PWEvent event = {
            .type           = PW_EVENT_FILE_ENTER,
            .gui            = pw->gui,
            .file.x         = Point.x,
            .file.y         = Point.y,
            .file.paths     = (const char* const*)pw->DropTarget.pFilePaths,
            .file.num_paths = pw->DropTarget.NumPaths,
        };

        bool ok = pw_event(&event);

        *pdwEffect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return ok ? 0 : -1;
    }

error:
    *pdwEffect = DROPEFFECT_NONE;
    return E_UNEXPECTED;
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-dragover
HRESULT STDMETHODCALLTYPE PWDropTarget_DragOver(IDropTarget* This, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    CplugWindow* pw = PWDropTargetShiftPtr(This);
    PW_ASSERT(pw->DropTarget.NumPaths);
    PW_ASSERT(pw->DropTarget.pFilePaths);

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-screentoclient
    POINT Point = {pt.x, pt.y};
    ScreenToClient(pw->hwnd, &Point);

    PWEvent event = {
        .type           = PW_EVENT_FILE_MOVE,
        .gui            = pw->gui,
        .file.x         = Point.x,
        .file.y         = Point.y,
        .file.paths     = (const char* const*)pw->DropTarget.pFilePaths,
        .file.num_paths = pw->DropTarget.NumPaths,
    };
    bool ok = pw_event(&event);

    // TODO: make this change cursor. Currently not working?
    *pdwEffect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;

    return S_OK;
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-dragleave
HRESULT STDMETHODCALLTYPE PWDropTarget_DragLeave(IDropTarget* This)
{
    CplugWindow* pw = PWDropTargetShiftPtr(This);
    PW_ASSERT(pw->DropTarget.NumPaths);
    PW_ASSERT(pw->DropTarget.pFilePaths);

    PWEvent event = {
        .type = PW_EVENT_FILE_EXIT,
        .gui  = pw->gui,
    };
    pw_event(&event);

    PWFreeDropTarget(pw);
    return 0;
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-drop
HRESULT STDMETHODCALLTYPE
PWDropTarget_Drop(IDropTarget* This, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    CplugWindow* pw = PWDropTargetShiftPtr(This);
    PW_ASSERT(pw->DropTarget.NumPaths);
    PW_ASSERT(pw->DropTarget.pFilePaths);

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-screentoclient
    POINT Point = {pt.x, pt.y};
    ScreenToClient(pw->hwnd, &Point);

    PWEvent event = {
        .type           = PW_EVENT_FILE_DROP,
        .gui            = pw->gui,
        .file.x         = Point.x,
        .file.y         = Point.y,
        .file.num_paths = pw->DropTarget.NumPaths,
        .file.paths     = (const char* const*)pw->DropTarget.pFilePaths,
    };
    bool ok = pw_event(&event);

    PWFreeDropTarget(pw);
    return ok ? 0 : -1;
}

typedef struct PWDropSource
{
    IDropSourceVtbl* lpVtbl;
    IDropSourceVtbl  Vtbl;
    volatile LONG    RefCount;
} PWDropSource;

static inline CplugWindow* PWDropSourceShiftPtr(IDropSource* pDropSource)
{
    CplugWindow* pw = (CplugWindow*)((char*)pDropSource - offsetof(CplugWindow, DropSource));
    return pw;
}

HRESULT STDMETHODCALLTYPE PWDropSource_QueryInterface(IDropSource* This, REFIID riid, void** ppvObject)
{
    if (0 == memcmp(riid, &IID_IDropSource, sizeof(*riid)) || 0 == memcmp(riid, &IID_IUnknown, sizeof(*riid)))
    {
        This->lpVtbl->AddRef(This);
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PWDropSource_AddRef(IDropSource* This)
{
    CplugWindow* pw = PWDropSourceShiftPtr(This);
    return _InlineInterlockedAdd(&pw->DropSource.RefCount, 1);
}

ULONG STDMETHODCALLTYPE PWDropSource_Release(IDropSource* This)
{
    CplugWindow* pw = PWDropSourceShiftPtr(This);
    return _InlineInterlockedAdd(&pw->DropSource.RefCount, -1);
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idropsource-querycontinuedrag
HRESULT STDMETHODCALLTYPE
PWDropSource_QueryContinueDrag(IDropSource* This, _In_ BOOL fEscapePressed, _In_ DWORD grfKeyState)
{
    if (fEscapePressed)
        return DRAGDROP_S_CANCEL;

    if ((grfKeyState & MK_LBUTTON) == 0)
        return DRAGDROP_S_DROP;

    return S_OK;
}

// https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idropsource-givefeedback
HRESULT STDMETHODCALLTYPE PWDropSource_GiveFeedback(IDropSource* This, _In_ DWORD dwEffect)
{
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

void pw_set_mouse_cursor(void* _pw, enum PWCursorType type)
{
    CplugWindow* pw = _pw;

    HCURSOR cursor = NULL;
    // https://learn.microsoft.com/en-us/windows/win32/menurc/about-cursors
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadcursorw
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor
    // https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw
    // https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary
    switch (type)
    {
    case PW_CURSOR_ARROW:
        cursor = LoadCursorW(NULL, IDC_ARROW);
        break;
    case PW_CURSOR_IBEAM:
        cursor = LoadCursorW(NULL, IDC_IBEAM);
        break;
    case PW_CURSOR_NO:
    {
        HMODULE ole32 = LoadLibraryW(L"ole32.dll");
        cursor        = LoadCursorW(ole32, MAKEINTRESOURCEW(1));
        PW_ASSERT(cursor != NULL);
        FreeLibrary(ole32);
        break;
    }
    case PW_CURSOR_CROSS:
        cursor = LoadCursorW(NULL, IDC_CROSS);
        break;

    case PW_CURSOR_ARROW_DRAG:
    {
        // https://stackoverflow.com/questions/49485890/using-the-windows-drag-copy-cursor
        HMODULE ole32 = LoadLibraryW(L"ole32.dll");
        cursor        = LoadCursorW(ole32, MAKEINTRESOURCEW(2));
        PW_ASSERT(cursor != NULL);
        FreeLibrary(ole32);
        break;
    }
    case PW_CURSOR_HAND_POINT:
        cursor = LoadCursorW(NULL, IDC_HAND);
        break;
    case PW_CURSOR_HAND_DRAGGABLE:
        // clang-format off
        static const uint8_t OPEN_HAND_AND_MASK[] = {
            0xFE, 0x7F, 0xE4, 0x0F, 0xC0, 0x07, 0xC0, 0x05,
            0xE0, 0x00, 0xE0, 0x00, 0x90, 0x00, 0x00, 0x00,
            0x00, 0x01, 0x80, 0x01, 0xC0, 0x01, 0xC0, 0x03,
            0xE0, 0x03, 0xF0, 0x07, 0xF8, 0x07, 0xF8, 0x07,
        };
        static const uint8_t OPEN_HAND_XOR_MASK[] = {
            0x00, 0x00, 0x01, 0x80, 0x19, 0xB0, 0x19, 0xB0,
            0x0D, 0xB2, 0x0D, 0xB6, 0x07, 0xF6, 0x67, 0xFE,
            0x77, 0xFC, 0x63, 0xFC, 0x3F, 0xFC, 0x1F, 0xF8,
            0x0F, 0xF8, 0x07, 0xF0, 0x03, 0xF0, 0x03, 0xF0,
        };
        _Static_assert(sizeof(OPEN_HAND_AND_MASK) == 32, "");
        _Static_assert(sizeof(OPEN_HAND_XOR_MASK) == 32, "");
        // clang-format on
        if (pw->hCursorOpenHand == NULL)
            pw->hCursorOpenHand = CreateCursor(NULL, 8, 8, 16, 16, OPEN_HAND_AND_MASK, OPEN_HAND_XOR_MASK);
        cursor = pw->hCursorOpenHand;
        break;
    case PW_CURSOR_HAND_DRAGGING:
        // clang-format off
        static const uint8_t CLOSED_HAND_AND_MASK[] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF2, 0x4F,
            0xE0, 0x03, 0xE0, 0x01, 0xF0, 0x01, 0xE0, 0x01,
            0xC0, 0x01, 0xC0, 0x03, 0xE0, 0x03, 0xF0, 0x07,
            0xF8, 0x07, 0xF8, 0x07, 0xFF, 0xFF, 0xFF, 0xFF,
        };
        static const uint8_t CLOSED_HAND_XOR_MASK[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x0D, 0xB0, 0x0F, 0xF4, 0x07, 0xFC, 0x07, 0xFC,
            0x1F, 0xFC, 0x1F, 0xFC, 0x0F, 0xF8, 0x07, 0xF0,
            0x03, 0xF0, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x00,
        };
        _Static_assert(sizeof(CLOSED_HAND_AND_MASK) == 32, "");
        _Static_assert(sizeof(CLOSED_HAND_XOR_MASK) == 32, "");
        // clang-format on
        if (pw->hCursorClosedHand == NULL)
            pw->hCursorClosedHand = CreateCursor(NULL, 8, 8, 16, 16, CLOSED_HAND_AND_MASK, CLOSED_HAND_XOR_MASK);
        cursor = pw->hCursorClosedHand;
        break;

    case PW_CURSOR_RESIZE_WE:
        cursor = LoadCursorW(NULL, IDC_SIZEWE);
        break;
    case PW_CURSOR_RESIZE_NS:
        cursor = LoadCursorW(NULL, IDC_SIZENS);
        break;
    case PW_CURSOR_RESIZE_NESW:
        cursor = LoadCursorW(NULL, IDC_SIZENESW);
        break;
    case PW_CURSOR_RESIZE_NWSE:
        cursor = LoadCursorW(NULL, IDC_SIZENWSE);
        break;
    }

    SetCursor(cursor);
}

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-messagebeep
void pw_beep() { MessageBeep(MB_OK); }

PWEvent PWTranslateMouseEvent(CplugWindow* pw, WPARAM wParam, LPARAM lParam)
{
    PWEvent e = {
        .gui                            = pw->gui,
        .mouse.x                        = GET_X_LPARAM(lParam),
        .mouse.y                        = GET_Y_LPARAM(lParam),
        .mouse.modifiers                = 0,
        .mouse.time_ms                  = GetMessageTime(),
        .mouse.double_click_interval_ms = GetDoubleClickTime(),
    };
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove
    if (wParam & MK_CONTROL)
        e.mouse.modifiers |= PW_MOD_KEY_CTRL;
    if (wParam & MK_LBUTTON)
        e.mouse.modifiers |= PW_MOD_LEFT_BUTTON;
    if (wParam & MK_MBUTTON)
        e.mouse.modifiers |= PW_MOD_MIDDLE_BUTTON;
    if (wParam & MK_RBUTTON)
        e.mouse.modifiers |= PW_MOD_RIGHT_BUTTON;
    if (wParam & MK_SHIFT)
        e.mouse.modifiers |= PW_MOD_KEY_SHIFT;
    if (wParam & MK_CONTROL)
        e.mouse.modifiers |= PW_MOD_KEY_CTRL;
    // if (GetAsyncKeyState(VK_MENU) & 0x8000)
    if (GetKeyState(VK_MENU) & 0x8000)
        e.mouse.modifiers |= PW_MOD_KEY_ALT;

    return e;
}

uint32_t PWGetKeyModifiers()
{
    uint32_t modifiers = 0;
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeystate
    if (GetKeyState(VK_LBUTTON) & 0x8000)
        modifiers |= PW_MOD_LEFT_BUTTON;
    if (GetKeyState(VK_RBUTTON) & 0x8000)
        modifiers |= PW_MOD_RIGHT_BUTTON;
    if (GetKeyState(VK_MBUTTON) & 0x8000)
        modifiers |= PW_MOD_MIDDLE_BUTTON;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        modifiers |= PW_MOD_KEY_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        modifiers |= PW_MOD_KEY_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= PW_MOD_KEY_ALT;
    return modifiers;
}

void pw_set_clipboard_text(void* _pw, const char* text)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closeclipboard
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-emptyclipboard
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata
    CplugWindow* pw = _pw;
    // https://devblogs.microsoft.com/oldnewthing/20210526-00/?p=105252
    BOOL ok = OpenClipboard(pw->hwnd);
    PW_ASSERT(ok);
    if (!ok)
        return;

    ok = EmptyClipboard();
    PW_ASSERT(ok);
    if (!ok)
    {
        CloseClipboard();
        return;
    }

    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalalloc
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalunlock
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalfree
    // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
    int     text_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    HGLOBAL hMem     = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * (text_len + 1));
    if (hMem == NULL)
    {
        CloseClipboard();
        return;
    }
    WCHAR* utf16 = GlobalLock(hMem);
    if (utf16 == NULL)
    {
        CloseClipboard();
        GlobalFree(hMem);
        return;
    }

    MultiByteToWideChar(CP_UTF8, 0, text, text_len, utf16, text_len);
    utf16[text_len] = 0;
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, utf16);
    CloseClipboard();
}

bool pw_get_clipboard_text(void* _pw, char** ptext, size_t* len)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closeclipboard
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globallock
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globalunlock
    // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte

    CplugWindow* pw = _pw;

    BOOL    ClipboardOpen     = 0;
    HGLOBAL hGlobalData       = NULL;
    WCHAR*  ClipboardContents = NULL;
    char*   out               = NULL;
    size_t  outlen            = 0;

    *ptext = NULL;
    *len   = 0;

    ClipboardOpen = OpenClipboard(pw->hwnd);
    if (ClipboardOpen == FALSE) // Clipboard may be empty. eg. PC has started up, nothing copied to clipboard yet
        goto cleanup;

    hGlobalData = GetClipboardData(CF_UNICODETEXT);
    if (hGlobalData == NULL)
        goto cleanup;

    ClipboardContents = (WCHAR*)GlobalLock(hGlobalData);
    if (ClipboardContents == NULL)
        goto cleanup;

    outlen = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ClipboardContents, -1, NULL, 0, NULL, NULL);
    PW_ASSERT(outlen);
    out = PW_MALLOC((outlen + 1));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, ClipboardContents, -1, out, outlen, NULL, NULL);
    out[outlen] = 0;

    *ptext = out;
    *len   = outlen;

cleanup:
    if (ClipboardContents)
        GlobalUnlock(hGlobalData);
    if (ClipboardOpen)
        CloseClipboard();

    return out != NULL;
}

void pw_free_clipboard_text(char* ptr)
{
    PW_ASSERT(ptr != NULL);
    PW_FREE(ptr);
}

void pw_get_screen_size(uint32_t* width, uint32_t* height)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdesktopwindow
    RECT Rect;
    HWND Desktop = GetDesktopWindow();
    GetWindowRect(Desktop, &Rect);
    *width  = Rect.right - Rect.left;
    *height = Rect.bottom - Rect.top;
}

float pw_get_dpi(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->dpi;
}

void* pw_get_native_window(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->hwnd;
}

#ifdef PW_DX11
void* pw_get_dx11_device(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->pDevice;
}

void* pw_get_dx11_device_context(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->pDeviceContext;
}
void* pw_get_dx11_render_target_view(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->pRenderTargetView;
}
void* pw_get_dx11_depth_stencil_view(void* _pw)
{
    CplugWindow* pw = _pw;
    return pw->pDepthStencilView;
}

HRESULT pw_dx11_create_render_target(CplugWindow* pw)
{
    PW_ASSERT(pw->pSwapchain);

    HRESULT hr = pw->pSwapchain->lpVtbl->GetBuffer(pw->pSwapchain, 0, &IID_ID3D11Texture2D, (void**)&pw->pRenderTarget);
    PW_ASSERT(SUCCEEDED(hr));
    PW_ASSERT(pw->pRenderTarget);
    if (pw->pRenderTarget)
    {
        D3D11_RENDER_TARGET_VIEW_DESC ViewDesc;
        memset(&ViewDesc, 0, sizeof(ViewDesc));
        ViewDesc.Format = pw->SwapChainDesc.BufferDesc.Format;
        ViewDesc.ViewDimension =
            (pw->SwapChainDesc.SampleDesc.Count > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = pw->pDevice->lpVtbl->CreateRenderTargetView(
            pw->pDevice,
            (ID3D11Resource*)pw->pRenderTarget,
            &ViewDesc,
            &pw->pRenderTargetView);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pw->pRenderTargetView);
    }

    D3D11_TEXTURE2D_DESC DepthStencilDesc;
    memset(&DepthStencilDesc, 0, sizeof(DepthStencilDesc));
    DepthStencilDesc.ArraySize          = 1;
    DepthStencilDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    DepthStencilDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DepthStencilDesc.Width              = pw->SwapChainDesc.BufferDesc.Width;
    DepthStencilDesc.Height             = pw->SwapChainDesc.BufferDesc.Height;
    DepthStencilDesc.MipLevels          = 1;
    DepthStencilDesc.SampleDesc.Count   = pw->SwapChainDesc.SampleDesc.Count;
    DepthStencilDesc.SampleDesc.Quality = pw->SwapChainDesc.SampleDesc.Quality;

    hr = pw->pDevice->lpVtbl->CreateTexture2D(pw->pDevice, &DepthStencilDesc, NULL, &pw->pDepthStencil);
    PW_ASSERT(SUCCEEDED(hr));
    PW_ASSERT(pw->pDepthStencil);
    if (pw->pDepthStencil)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC DepthViewDesc;
        memset(&DepthViewDesc, 0, sizeof(DepthViewDesc));
        DepthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DepthViewDesc.ViewDimension =
            (pw->SwapChainDesc.SampleDesc.Count > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = pw->pDevice->lpVtbl->CreateDepthStencilView(
            pw->pDevice,
            (ID3D11Resource*)pw->pDepthStencil,
            &DepthViewDesc,
            &pw->pDepthStencilView);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pw->pDepthStencilView);
    }
    return hr;
}
#endif

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nc-winuser-wndproc
LRESULT CALLBACK PWWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowlongptrw
    // NOTE: Might be NULL during initialisation
    CplugWindow* pw = (void*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_PAINT:
        break;
    case WM_DESTROY:
        break;
    case WM_SETCURSOR:
        return 0;
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-killfocus
    case WM_KILLFOCUS:
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unhookwindowshookex
        if (pw->hGetMessageHook)
            UnhookWindowsHookEx(pw->hGetMessageHook);
        pw->hGetMessageHook    = NULL;
        pw->hPrevKeyboardFocus = NULL;
        pw_event(&(PWEvent){.type = PW_EVENT_KEY_FOCUS_LOST, .gui = pw->gui});
        return 0;
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttondown
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mbuttondown
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-rbuttondown
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture
        SetCapture(hwnd);
        PWEvent e = PWTranslateMouseEvent(pw, wParam, lParam);
        if (uMsg == WM_LBUTTONDOWN)
            e.type = PW_EVENT_MOUSE_LEFT_DOWN;
        if (uMsg == WM_MBUTTONDOWN)
            e.type = PW_EVENT_MOUSE_MIDDLE_DOWN;
        if (uMsg == WM_RBUTTONDOWN)
            e.type = PW_EVENT_MOUSE_RIGHT_DOWN;
        pw_event(&e);
        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-lbuttonup
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mbuttonup
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-rbuttonup
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-releasecapture
        ReleaseCapture();
        PWEvent e = PWTranslateMouseEvent(pw, wParam, lParam);

        if (uMsg == WM_LBUTTONUP)
            e.type = PW_EVENT_MOUSE_LEFT_UP;
        if (uMsg == WM_MBUTTONUP)
            e.type = PW_EVENT_MOUSE_MIDDLE_UP;
        if (uMsg == WM_RBUTTONUP)
            e.type = PW_EVENT_MOUSE_RIGHT_UP;
        pw_event(&e);
        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove
    case WM_MOUSEMOVE:
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-trackmouseevent
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, HOVER_DEFAULT};
        TrackMouseEvent(&tme);

        PWEvent e = PWTranslateMouseEvent(pw, wParam, lParam);
        // Windows has no WM_MOUSEMOVE event, so we have to do this.
        if (pw->MouseIsOver)
        {
            e.type = PW_EVENT_MOUSE_MOVE;
        }
        else
        {
            e.type = PW_EVENT_MOUSE_ENTER;

            pw->MouseIsOver = TRUE;
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
        }
        pw_event(&e);

        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel
    case WM_MOUSEWHEEL:
    {
        // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousewheel
        PWEvent e = PWTranslateMouseEvent(pw, wParam, lParam);
        e.type    = PW_EVENT_MOUSE_SCROLL_WHEEL;
        e.mouse.x = 0;
        e.mouse.y = GET_WHEEL_DELTA_WPARAM(wParam);
        pw_event(&e);
        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mouseleave
    case WM_MOUSELEAVE:
    {
        pw->MouseIsOver = FALSE;

        PWEvent e = {0};
        e.type    = PW_EVENT_MOUSE_EXIT;
        e.gui     = pw->gui;
        pw_event(&e);
        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-char
    case WM_CHAR:
    {
        PWEvent e        = {0};
        e.type           = PW_EVENT_TEXT;
        e.gui            = pw->gui;
        e.text.modifiers = PWGetKeyModifiers();

        LPCWCH utf16 = (LPCWCH)&wParam;
        LPSTR  utf8  = (LPSTR)&e.text.codepoint;

        // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
        int num = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, utf16, -1, utf8, sizeof(e.text.codepoint), 0, 0);
        PW_ASSERT(num);

        if (e.text.codepoint == 127) // DEL ASCII. Not considered text
            return 0;

        pw_event(&e);
        return 0;
    }
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-syskeydown
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-syskeyup
    // case WM_SYSKEYDOWN: // not supported or needed
    // case WM_SYSKEYUP:
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keydown
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keyup
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        // https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#keystroke-message-flags
        // https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
        WORD keyFlags      = HIWORD(lParam);
        BOOL KeyIsReleased = (keyFlags & KF_UP) == KF_UP;

        PWEvent e = {
            .type            = KeyIsReleased ? PW_EVENT_KEY_UP : PW_EVENT_KEY_DOWN,
            .gui             = pw->gui,
            .key.virtual_key = wParam,
            .key.modifiers   = PWGetKeyModifiers(),
        };

        pw_event(&e);
        return 0;
    }
    case WM_COMMAND: // clicking nav menu items triggers commands. You can also send commands for other things
    {
        if (wParam == PW_WM_COMMAND_ChooseFile)
        {
            if (pw->ChooseFile.callback)
                pw->ChooseFile.callback(
                    pw->ChooseFile.callback_data,
                    (const char* const*)pw->ChooseFile.pFilePaths,
                    pw->ChooseFile.NumPaths);

            if (pw->ChooseFile.hThread)
            {
                WaitForSingleObject(pw->ChooseFile.hThread, INFINITE);
                CloseHandle(pw->ChooseFile.hThread);
                pw->ChooseFile.hThread = NULL;
            }

            PWFreeChooseFile(pw);
        }
        break;
    }
    // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-timer
    case WM_TIMER:
    {
        PW_ASSERT(pw->gui != NULL);
        pw_tick(pw->gui);

#ifdef PW_DX11
        // https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
        UINT Flags = 0;
        if (pw->IsWindows10OrGreater)
            Flags |= DXGI_PRESENT_DO_NOT_WAIT;
        pw->pSwapchain->lpVtbl->Present(pw->pSwapchain, 0, Flags);
#endif
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms644981(v=vs.85)
LRESULT CALLBACK PWGetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    MSG* msg = (MSG*)lParam;

    // Check application is trying to dequeue a message
    if (nCode == HC_ACTION && wParam == PM_REMOVE)
    {
        if ((msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_CHAR) &&
            GetWindowLongPtrW(msg->hwnd, GWLP_ID) == PW_UNIQUE_INT_ID)
        {
            PWWndProc(msg->hwnd, msg->message, msg->wParam, msg->lParam);

            // Calling TranslateMessage here immediately triggers this GetMsgProc callback
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translatemessage#remarks
            TranslateMessage(msg);

            // JUCE use this to remove duplicate WM_CHAR messages in the queue.
            // REUK mentioned on Discord that Japanese characters have this problem
            MSG NextMsg;
            memset(&NextMsg, 0, sizeof(NextMsg));
            PeekMessageW(&NextMsg, msg->hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE);

            // Overwite the message so nasty hosts like Reaper & Ableton can't consume it eg. the spacebar
            // https://forum.cockos.com/showthread.php?t=236843
            memset(msg, 0, sizeof(*msg));
            msg->message = WM_USER;

            return 1;
        }
    }

    return 0;
}

// https://learn.microsoft.com/en-us/windows/win32/winmsg/callwndproc
LRESULT CALLBACK PWCallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        CWPSTRUCT* cwp = (CWPSTRUCT*)lParam;

        if (cwp->message == WM_SIZING)
        {
            // I've spotted Windows 11 sending cwp->wParam == 9, which is undocumented and possibly a bug
            // https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing
            if (cwp->wParam > PW_RESIZE_UNKNOWN && cwp->wParam <= PW_RESIZE_BOTTOMRIGHT)
                g_ResizeDirection = cwp->wParam;
            else
                g_ResizeDirection = PW_RESIZE_UNKNOWN;
        }
    }
    return 0;
}

void pw_get_keyboard_focus(void* _pw)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw
    CplugWindow* pw = _pw;
    if (pw->hPrevKeyboardFocus == NULL)
        pw->hPrevKeyboardFocus = SetFocus(pw->hwnd);

#ifndef CPLUG_BUILD_STANDALONE
    // This is a hack to deal with DAWs that use questionable win32 tricks
    // https://forum.juce.com/t/vst-plugin-isnt-getting-keystrokes/1633/71
    PW_ASSERT(pw->hGetMessageHook == NULL);
    if (pw->hGetMessageHook == NULL)
        pw->hGetMessageHook = SetWindowsHookExW(WH_GETMESSAGE, PWGetMsgProc, g_DLL, 0);
    PW_ASSERT(pw->hGetMessageHook != NULL);
#endif
}

bool pw_check_keyboard_focus(const void* _pw)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus
    const CplugWindow* pw = _pw;
    return GetFocus() == pw->hwnd;
}

void pw_release_keyboard_focus(void* _pw)
{
    CplugWindow* pw = _pw;
#ifndef CPLUG_BUILD_STANDALONE
    PW_ASSERT(pw->hGetMessageHook != NULL);
    if (pw->hGetMessageHook)
    {
        UnhookWindowsHookEx(pw->hGetMessageHook);
        pw->hGetMessageHook = NULL;
    }
#endif

    if (pw->hPrevKeyboardFocus)
    {
        SetFocus(pw->hPrevKeyboardFocus);
        pw->hPrevKeyboardFocus = NULL;
    }
}

typedef struct PWDraggedFiles
{
    struct IDataObjectVtbl* lpVtbl;
    struct IDataObjectVtbl  Vtbl;
    volatile LONG           RefCount;

    HGLOBAL hMem;
} PWDraggedFiles;

HRESULT STDMETHODCALLTYPE PWDraggedFiles_QueryInterface(IDataObject* This, REFIID riid, _COM_Outptr_ void** ppvObject)
{
    if (0 == memcmp(riid, &IID_IDataObject, sizeof(*riid)) || 0 == memcmp(riid, &IID_IUnknown, sizeof(*riid)))
    {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE PWDraggedFiles_AddRef(IDataObject* This)
{
    PWDraggedFiles* obj = (PWDraggedFiles*)This;
    return _InlineInterlockedAdd(&obj->RefCount, 1);
}

ULONG STDMETHODCALLTYPE PWDraggedFiles_Release(IDataObject* This)
{
    PWDraggedFiles* obj       = (PWDraggedFiles*)This;
    LONG            NextCount = _InlineInterlockedAdd(&obj->RefCount, -1);
    if (NextCount == 0)
    {
        if (obj->hMem)
            GlobalFree(obj->hMem);

        PW_FREE(obj);
    }
    return NextCount;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getdata
HRESULT STDMETHODCALLTYPE
PWDraggedFiles_GetData(IDataObject* This, _In_ FORMATETC* pformatetcIn, _Out_ STGMEDIUM* pmedium)
{
    BOOL ok = pformatetcIn->cfFormat == CF_HDROP && (pformatetcIn->dwAspect & DVASPECT_CONTENT) &&
              (pformatetcIn->tymed & TYMED_HGLOBAL);
    if (!ok)
        return DV_E_FORMATETC;

    pmedium->tymed          = TYMED_HGLOBAL;
    pmedium->pUnkForRelease = 0;

    PWDraggedFiles* obj = (PWDraggedFiles*)This;

    SIZE_T Size = GlobalSize(obj->hMem);
    void*  Src  = GlobalLock(obj->hMem);
    void*  Dst  = NULL;
    if (Src)
    {
        Dst = GlobalAlloc(GMEM_FIXED, Size);
        if (Dst)
        {
            memcpy(Dst, Src, Size);
            pmedium->hGlobal = Dst;
        }
        GlobalUnlock(obj->hMem);
    }

    return Dst ? S_OK : ENOMEM;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getdatahere
HRESULT STDMETHODCALLTYPE PWDraggedFiles_GetDataHere(IDataObject* This, FORMATETC* pformatetc, STGMEDIUM* pmedium)
{
    return E_NOTIMPL;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-querygetdata
HRESULT STDMETHODCALLTYPE PWDraggedFiles_QueryGetData(IDataObject* This, FORMATETC* pformatetc)
{
    BOOL ok = pformatetc->cfFormat == CF_HDROP && (pformatetc->dwAspect & DVASPECT_CONTENT) &&
              (pformatetc->tymed & TYMED_HGLOBAL);
    return ok ? S_OK : DV_E_FORMATETC;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-getcanonicalformatetc
HRESULT STDMETHODCALLTYPE
PWDraggedFiles_GetCanonicalFormatEtc(IDataObject* This, FORMATETC* pformatectIn, FORMATETC* pformatetcOut)
{
    pformatetcOut->ptd = NULL;
    return E_NOTIMPL;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-setdata
HRESULT STDMETHODCALLTYPE
PWDraggedFiles_SetData(IDataObject* This, FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease)
{
    return E_NOTIMPL;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-enumformatetc
HRESULT STDMETHODCALLTYPE
PWDraggedFiles_EnumFormatEtc(IDataObject* This, DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc)
{
    if (dwDirection == DATADIR_GET)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ienumformatetc
        // Other implementations of IDataObject I've seem to implement and return IEnumFORMATETC here, however  apps
        // I've tested like File Explorer, Abelton Live 12, Bitwig 5, Reaper, and likely many more all don't seem to
        // care if I skip it...
    }
    return E_NOTIMPL;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-dadvise
HRESULT STDMETHODCALLTYPE PWDraggedFiles_DAdvise(
    IDataObject* This,
    FORMATETC*   pformatetc,
    DWORD        advf,
    IAdviseSink* pAdvSink,
    DWORD*       pdwConnection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-dunadvise
HRESULT STDMETHODCALLTYPE PWDraggedFiles_DUnadvise(IDataObject* This, DWORD dwConnection)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

// https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-idataobject-enumdadvise
HRESULT STDMETHODCALLTYPE PWDraggedFiles_EnumDAdvise(IDataObject* This, IEnumSTATDATA** ppenumAdvise)
{
    return OLE_E_ADVISENOTSUPPORTED;
}

void pw_drag_files(void* _pw, const char* const* paths, uint32_t num_paths)
{
    // https://devblogs.microsoft.com/oldnewthing/20041206-00/?p=37133
    // https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop
    // https://learn.microsoft.com/en-us/windows/win32/com/dropeffect-constants
    // https://www.catch22.net/tuts/ole/
    // https://www.codeproject.com/Articles/840/How-to-Implement-Drag-and-Drop-Between-Your-Progra

    CplugWindow* pw = _pw;

    PW_ASSERT(paths);
    PW_ASSERT(num_paths);

    // This is a pretty hacky data structure
    // https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/ns-shlobj_core-dropfiles
    // Calc required WCHARs for double null terminated array of paths
    UINT NumChars = 2;
    for (uint32_t i = 0; i < num_paths; i++)
    {
        // MultiByteToWideChar returns a count including the null terminating byte
        int num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, paths[i], -1, NULL, 0);
        PW_ASSERT(num);
        NumChars += num;
    }

    HGLOBAL hMem = GlobalAlloc(GHND | GMEM_SHARE, sizeof(DROPFILES) + NumChars * sizeof(WCHAR));
    if (hMem)
    {
        DROPFILES* pDrop = GlobalLock(hMem);
        if (pDrop)
        {
            pDrop->pFiles = sizeof(DROPFILES);
            pDrop->fWide  = TRUE;

            WCHAR* Path = (WCHAR*)(pDrop + 1);
            for (uint32_t i = 0; i < num_paths; i++)
            {
                // We lie about the true capacity of Path. We've already calculated & reserved enough capacity
                int stride = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, paths[i], -1, Path, MAX_PATH);

                Path += stride;
            }

            GlobalUnlock(hMem);
        }
        else
        {
            GlobalFree(hMem);
            hMem = NULL;
        }
    }

    if (hMem)
    {
        PWDraggedFiles* obj = PW_MALLOC(sizeof(PWDraggedFiles));

        obj->lpVtbl                     = &obj->Vtbl;
        obj->Vtbl.QueryInterface        = PWDraggedFiles_QueryInterface;
        obj->Vtbl.AddRef                = PWDraggedFiles_AddRef;
        obj->Vtbl.Release               = PWDraggedFiles_Release;
        obj->Vtbl.GetData               = PWDraggedFiles_GetData;
        obj->Vtbl.GetDataHere           = PWDraggedFiles_GetDataHere;
        obj->Vtbl.QueryGetData          = PWDraggedFiles_QueryGetData;
        obj->Vtbl.GetCanonicalFormatEtc = PWDraggedFiles_GetCanonicalFormatEtc;
        obj->Vtbl.SetData               = PWDraggedFiles_SetData;
        obj->Vtbl.EnumFormatEtc         = PWDraggedFiles_EnumFormatEtc;
        obj->Vtbl.DAdvise               = PWDraggedFiles_DAdvise;
        obj->Vtbl.DUnadvise             = PWDraggedFiles_DUnadvise;
        obj->Vtbl.EnumDAdvise           = PWDraggedFiles_EnumDAdvise;
        obj->RefCount                   = 1;
        obj->hMem                       = hMem;

        DWORD OKEffects = DROPEFFECT_COPY; // TODO: support move
        // DWORD OKEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        DWORD Effect = 0;
        ULONG hr     = DoDragDrop((IDataObject*)obj, (IDropSource*)&pw->DropSource, OKEffects, &Effect);

        PW_ASSERT((hr == DRAGDROP_S_DROP || hr == DRAGDROP_S_CANCEL));
        obj->lpVtbl->Release((IDataObject*)obj);
    }
}

void* cplug_createGUI(void* userPlugin)
{
    CplugWindow* pw = (void*)PW_MALLOC(sizeof(CplugWindow));
    memset(pw, 0, sizeof(*pw));
    pw->plugin = userPlugin;

    pw->DropTarget.lpVtbl              = &pw->DropTarget.Vtbl;
    pw->DropTarget.Vtbl.QueryInterface = PWDropTarget_QueryInterface;
    pw->DropTarget.Vtbl.AddRef         = PWDropTarget_AddRef;
    pw->DropTarget.Vtbl.Release        = PWDropTarget_Release;
    pw->DropTarget.Vtbl.DragEnter      = PWDropTarget_DragEnter;
    pw->DropTarget.Vtbl.DragOver       = PWDropTarget_DragOver;
    pw->DropTarget.Vtbl.DragLeave      = PWDropTarget_DragLeave;
    pw->DropTarget.Vtbl.Drop           = PWDropTarget_Drop;
    pw->DropTarget.RefCount            = 1;

    pw->DropSource.lpVtbl                 = &pw->DropSource.Vtbl;
    pw->DropSource.Vtbl.QueryInterface    = PWDropSource_QueryInterface;
    pw->DropSource.Vtbl.AddRef            = PWDropSource_AddRef;
    pw->DropSource.Vtbl.Release           = PWDropSource_Release;
    pw->DropSource.Vtbl.QueryContinueDrag = PWDropSource_QueryContinueDrag;
    pw->DropSource.Vtbl.GiveFeedback      = PWDropSource_GiveFeedback;
    pw->DropSource.RefCount               = 1;

    // https://stackoverflow.com/questions/1695288/getting-the-current-time-in-milliseconds-from-the-system-clock-in-windows#1695332
    UINT64   EpochTimeMs = 0;
    FILETIME filetime;
    GetSystemTimeAsFileTime(&filetime);
    EpochTimeMs = (UINT64)filetime.dwLowDateTime + ((UINT64)(filetime.dwHighDateTime) << 32LL);
    EpochTimeMs = EpochTimeMs / 10000;             // convert units 100 nanosecods > ms
    EpochTimeMs = EpochTimeMs - 11644473600000ULL; // convert date from Jan 1, 1601 to Jan 1 1970.

    swprintf_s(pw->ClassName, ARRAYSIZE(pw->ClassName), L"%ls-%llx", TEXT(CPLUG_PLUGIN_NAME), EpochTimeMs);

    WNDCLASSEXW wc   = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = PWWndProc;
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = pw->ClassName;

    ATOM ok = RegisterClassExW(&wc);
    PW_ASSERT(ok != 0);

    PWGetInfo Info = {.type = PW_INFO_INIT_SIZE, .init_size.plugin = userPlugin};
    pw_get_info(&Info);
    PW_ASSERT(Info.init_size.width > 0);
    PW_ASSERT(Info.init_size.height > 0);

    pw->hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        pw->ClassName,
        TEXT(CPLUG_PLUGIN_NAME),
        WS_CHILD | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Info.init_size.width,
        Info.init_size.height,
        GetDesktopWindow(),
        NULL,
        wc.hInstance,
        NULL);
    PW_ASSERT(pw->hwnd != NULL);

    SetWindowLongPtrW(pw->hwnd, GWLP_USERDATA, (LONG_PTR)pw);

    // When using Hooks, HWNDs could belong to anyone else in the same process, such as the hosts window.
    // This trick tags the HWND as one of our own.
    if (PW_UNIQUE_INT_ID == 0)
        PW_UNIQUE_INT_ID = EpochTimeMs;
    SetWindowLongPtrW(pw->hwnd, GWLP_ID, PW_UNIQUE_INT_ID);

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdpiforwindow
    pw->dpi = GetDpiForWindow(pw->hwnd) / 96.0f;

    // https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-oleinitialize
    // https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-registerdragdrop
    HRESULT hr = OleInitialize(NULL);
    PW_ASSERT(SUCCEEDED(hr));
    hr = RegisterDragDrop(pw->hwnd, (IDropTarget*)&pw->DropTarget);
    PW_ASSERT(SUCCEEDED(hr));

#ifdef PW_DX11
    IDXGIOutput*  pOutput     = NULL;
    IDXGIAdapter* pAdapter    = NULL;
    IDXGIFactory* pFactory    = NULL;
    IDXGIDevice1* pDXGIDevice = NULL;

    static const D3D_DRIVER_TYPE DriverTypes[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };

    UINT Flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    for (int i = 0; i < ARRAYSIZE(DriverTypes); i++)
    {
        static const D3D_FEATURE_LEVEL levelAttempts[] = {
            D3D_FEATURE_LEVEL_12_1, // Direct3D 12.1 SM 6
            D3D_FEATURE_LEVEL_12_0, // Direct3D 12.0 SM 5.1
            D3D_FEATURE_LEVEL_11_1, // Direct3D 11.1 SM 5
            D3D_FEATURE_LEVEL_11_0, // Direct3D 11.0 SM 5
            D3D_FEATURE_LEVEL_10_1, // Direct3D 10.1 SM 4
            D3D_FEATURE_LEVEL_10_0, // Direct3D 10.0 SM 4
            D3D_FEATURE_LEVEL_9_3,  // Direct3D 9.3  SM 3
            D3D_FEATURE_LEVEL_9_2,  // Direct3D 9.2  SM 2
        };

        hr = D3D11CreateDevice(
            NULL,
            DriverTypes[i],
            NULL,
            Flags,
            levelAttempts,
            ARRAYSIZE(levelAttempts),
            D3D11_SDK_VERSION,
            &pw->pDevice,
            NULL,
            &pw->pDeviceContext);

        if (SUCCEEDED(hr))
            break;
    }
    PW_ASSERT(pw->pDevice);
    PW_ASSERT(pw->pDeviceContext);

    if (pw->pDevice)
    {
        hr = pw->pDevice->lpVtbl->QueryInterface(pw->pDevice, &IID_IDXGIDevice1, (void**)&pDXGIDevice);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pDXGIDevice);
    }

    if (pDXGIDevice)
    {
        hr = pDXGIDevice->lpVtbl->SetMaximumFrameLatency(pDXGIDevice, 1);
        PW_ASSERT(SUCCEEDED(hr));
        hr = pDXGIDevice->lpVtbl->GetAdapter(pDXGIDevice, &pAdapter);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pAdapter);
        if (pAdapter)
        {
            hr = pAdapter->lpVtbl->GetParent(pAdapter, &IID_IDXGIFactory, (void**)&pFactory);
            PW_ASSERT(SUCCEEDED(hr));
            PW_ASSERT(pFactory);
            hr = pAdapter->lpVtbl->EnumOutputs(pAdapter, 0, &pOutput);
            PW_ASSERT(SUCCEEDED(hr));
            PW_ASSERT(pOutput);
        }
    }

    if (pFactory)
    {
        // Get current refresh rate
        DWORD DisplayFrequency = 60;
        if (pOutput)
        {
            // https://stackoverflow.com/questions/15583294/how-to-get-current-display-mode-resolution-refresh-rate-of-a-monitor-output-i
            DXGI_OUTPUT_DESC OutputDesc;
            MONITORINFOEXW   MonitorInfo;
            DEVMODE          DevMode;

            DevMode.dmSize        = sizeof(DEVMODE);
            DevMode.dmDriverExtra = 0;
            MonitorInfo.cbSize    = sizeof(MONITORINFOEX);

            hr = pOutput->lpVtbl->GetDesc(pOutput, &OutputDesc);
            PW_ASSERT(SUCCEEDED(hr));

            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmonitorinfow
            ok = GetMonitorInfoW(OutputDesc.Monitor, (LPMONITORINFO)&MonitorInfo);
            PW_ASSERT(ok);

            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsw
            ok = EnumDisplaySettingsW(MonitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &DevMode);
            PW_ASSERT(ok);

            DisplayFrequency = DevMode.dmDisplayFrequency;

            // Makes any 144/288 fps monitor refresh at 72 fps
            // High refresh rates look great in games, but will often be wasted in audio software.
            // Metering of live audio benefits from high refresh rates, but it depends on new audio data
            // Consider common audio settings of sample rate = 48k & block size = 512 samples
            // With these settings, new audio can only be sent to the gui at a rate of 93.75/s (48000 / 512)
            // Also we lazily render on the main thread and we don't want to hog it too much.
            while (DisplayFrequency >= 100)
            {
                DisplayFrequency /= 2;
            }
        }

        // https://stackoverflow.com/questions/29944745/get-osversion-in-windows-using-c
        // https://stackoverflow.com/questions/71250924/how-to-get-osversioninfo-for-windows-11
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_osversioninfoexw
        HRESULT(__stdcall * RtlGetVersion)(OSVERSIONINFOEXW*);
        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleW(L"ntdll"), "RtlGetVersion");

        if (RtlGetVersion != NULL)
        {
            OSVERSIONINFOEXW osInfo;
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            RtlGetVersion(&osInfo);

            pw->IsWindows10OrGreater = osInfo.dwMajorVersion >= 10;
        }

        DXGI_SWAP_CHAIN_DESC* pSwapDesc               = &pw->SwapChainDesc;
        pSwapDesc->BufferDesc.Width                   = Info.init_size.width;
        pSwapDesc->BufferDesc.Height                  = Info.init_size.height;
        pSwapDesc->BufferDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
        pSwapDesc->BufferDesc.RefreshRate.Numerator   = DisplayFrequency;
        pSwapDesc->BufferDesc.RefreshRate.Denominator = 1;
        // Flip discard is the recommended setting for optimal performance. IIRC it helps to remove any waiting for the
        // backbuffer to become available. This was introduced in Windows 10.
        if (pw->IsWindows10OrGreater)
        {
            pSwapDesc->BufferCount = 2;
            pSwapDesc->SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        }
        else
        {
            pSwapDesc->BufferCount = 1;
            pSwapDesc->SwapEffect  = DXGI_SWAP_EFFECT_DISCARD;
        }
        pSwapDesc->SampleDesc.Count   = 1;
        pSwapDesc->SampleDesc.Quality = 0;
        pSwapDesc->BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        pSwapDesc->OutputWindow       = pw->hwnd;
        pSwapDesc->Windowed           = true;

        hr = pFactory->lpVtbl->CreateSwapChain(pFactory, (IUnknown*)pw->pDevice, pSwapDesc, &pw->pSwapchain);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pw->pSwapchain);
    }

    if (pw->pSwapchain)
    {
        hr = pw_dx11_create_render_target(pw);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pw->pRenderTarget);
        PW_ASSERT(pw->pRenderTargetView);
        PW_ASSERT(pw->pDepthStencil);
        PW_ASSERT(pw->pDepthStencilView);
    }

    PW_DX11_RELEASE(pFactory)
    PW_DX11_RELEASE(pAdapter)
    PW_DX11_RELEASE(pOutput)
    PW_DX11_RELEASE(pDXGIDevice)
#endif

    pw->gui = pw_create_gui(pw->plugin, pw);
    PW_ASSERT(pw->gui);

    return pw;
}

void cplug_destroyGUI(void* userGUI)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-iswindow
    CplugWindow* pw = userGUI;
    PW_ASSERT(IsWindow(pw->hwnd));
    pw_destroy_gui(pw->gui);

    // https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-revokedragdrop
    // https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-oleuninitialize
    HRESULT result = RevokeDragDrop(pw->hwnd);
    PW_ASSERT(result == S_OK);
    OleUninitialize();

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unhookwindowshookex
    if (pw->hGetMessageHook)
    {
        BOOL ok = UnhookWindowsHookEx(pw->hGetMessageHook);
        PW_ASSERT(ok);
    }
    if (pw->hCallWndHook)
    {
        BOOL ok = UnhookWindowsHookEx(pw->hCallWndHook);
        PW_ASSERT(ok);
    }

#ifdef PW_DX11
    PW_DX11_RELEASE(pw->pRenderTarget)
    PW_DX11_RELEASE(pw->pRenderTargetView)
    PW_DX11_RELEASE(pw->pDepthStencil)
    PW_DX11_RELEASE(pw->pDepthStencilView)
    PW_DX11_RELEASE(pw->pSwapchain)
    PW_DX11_RELEASE(pw->pDeviceContext)
    PW_DX11_RELEASE(pw->pDevice)
#endif

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterclassw
    BOOL ok = DestroyWindow(pw->hwnd);
    PW_ASSERT(ok);
    ok = UnregisterClassW(pw->ClassName, NULL);
    PW_ASSERT(ok);

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroycursor
    if (pw->hCursorClosedHand)
    {
        ok = DestroyCursor(pw->hCursorClosedHand);
        PW_ASSERT(ok);
    }
    if (pw->hCursorOpenHand)
    {
        ok = DestroyCursor(pw->hCursorOpenHand);
        PW_ASSERT(ok);
    }

    PW_FREE(pw);
}

void cplug_setParent(void* userGUI, void* newParent)
{
    CplugWindow* pw = userGUI;

    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getparent
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-defwindowprocw
    HWND oldParent = GetParent(pw->hwnd);
    if (oldParent)
    {
        KillTimer(pw->hwnd, PW_TIMER_ID);

        SetParent(pw->hwnd, NULL);
        DefWindowProcW(pw->hwnd, WM_UPDATEUISTATE, UIS_CLEAR, WS_CHILD);
        DefWindowProcW(pw->hwnd, WM_UPDATEUISTATE, UIS_SET, WS_POPUP);
    }

    if (newParent)
    {
        SetParent(pw->hwnd, (HWND)newParent);

        DefWindowProcW(pw->hwnd, WM_UPDATEUISTATE, UIS_CLEAR, WS_POPUP);
        DefWindowProcW(pw->hwnd, WM_UPDATEUISTATE, UIS_SET, WS_CHILD);

        if (pw->hCallWndHook == NULL)
        {
            // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessid
            // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid
            DWORD pid = GetCurrentProcessId();
            DWORD tid = GetWindowThreadProcessId(newParent, &pid);

            HMODULE module   = GetModuleHandleW(NULL);
            pw->hCallWndHook = SetWindowsHookExW(WH_CALLWNDPROC, PWCallWndProc, module, tid);
            PW_ASSERT(pw->hCallWndHook);
        }

        SetTimer(pw->hwnd, PW_TIMER_ID, 10, NULL);
    }
}

void cplug_setVisible(void* userGUI, bool visible)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow
    CplugWindow* pw = userGUI;
    ShowWindow(pw->hwnd, visible ? SW_SHOW : SW_HIDE);
}

void cplug_setScaleFactor(void* userGUI, float scale)
{
    CplugWindow* pw = userGUI;
    pw->dpi         = scale;

    PWEvent e = {
        .type = PW_EVENT_DPI_CHANGED,
        .gui  = pw->gui,
        .dpi  = scale,
    };
    pw_event(&e);
}

void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    CplugWindow* pw = userGUI;
    RECT         rect;
    GetWindowRect(pw->hwnd, &rect);
    *width  = rect.right - rect.left;
    *height = rect.bottom - rect.top;
}

void cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    CplugWindow* pw   = (CplugWindow*)userGUI;
    PWGetInfo    Info = {
           .type                     = PW_INFO_CONSTRAIN_SIZE,
           .constrain_size.gui       = pw->gui,
           .constrain_size.width     = *width,
           .constrain_size.height    = *height,
           .constrain_size.direction = g_ResizeDirection,
    };
    pw_get_info(&Info);
    *width  = Info.constrain_size.width;
    *height = Info.constrain_size.height;
}

bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height)
{
    CplugWindow* pw = userGUI;
    PW_ASSERT(width > 0);
    PW_ASSERT(height > 0);

#ifdef PW_DX11
    PW_DX11_RELEASE(pw->pRenderTarget)
    PW_DX11_RELEASE(pw->pRenderTargetView)
    PW_DX11_RELEASE(pw->pDepthStencil)
    PW_DX11_RELEASE(pw->pDepthStencilView)

    pw->SwapChainDesc.BufferDesc.Width  = width;
    pw->SwapChainDesc.BufferDesc.Height = height;

    if (pw->pSwapchain)
    {
        HRESULT hr = pw->pSwapchain->lpVtbl->ResizeBuffers(
            pw->pSwapchain,
            pw->SwapChainDesc.BufferCount,
            pw->SwapChainDesc.BufferDesc.Width,
            pw->SwapChainDesc.BufferDesc.Height,
            pw->SwapChainDesc.BufferDesc.Format,
            0);
        PW_ASSERT(SUCCEEDED(hr));

        hr = pw_dx11_create_render_target(pw);
        PW_ASSERT(SUCCEEDED(hr));
        PW_ASSERT(pw->pRenderTarget);
        PW_ASSERT(pw->pRenderTargetView);
        PW_ASSERT(pw->pDepthStencil);
        PW_ASSERT(pw->pDepthStencilView);
    }
#endif

    pw_event(&(PWEvent){
        .type          = PW_EVENT_RESIZE,
        .gui           = pw->gui,
        .resize.width  = width,
        .resize.height = height,
    });
    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
    const UINT uFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE;
    return SetWindowPos(pw->hwnd, HWND_TOP, 0, 0, width, height, uFlags);
}

DWORD PWChooseFileThread(_In_ LPVOID lpParameter)
{
    CplugWindow* pw = lpParameter;

    HRESULT               hr        = 0;
    FILEOPENDIALOGOPTIONS options   = 0;
    IFileDialog*          pfd       = NULL;
    IShellItem*           psiFolder = NULL;
    const IID* const      clsid     = pw->ChooseFile.IsSave ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog;
    const IID* const      iid       = pw->ChooseFile.IsSave ? &IID_IFileSaveDialog : &IID_IFileOpenDialog;

    // https://learn.microsoft.com/en-us/windows/win32/shell/common-file-dialog

    hr = CoInitializeEx(NULL, 0);
    PW_ASSERT(hr == S_OK);
    if (hr)
        goto error;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC, iid, (void**)&pfd);
    PW_ASSERT(hr == S_OK);
    if (hr)
        goto error;

    if (pw->ChooseFile.pFolder)
    {
        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateitemfromparsingname
        hr = SHCreateItemFromParsingName(pw->ChooseFile.pFolder, NULL, &IID_IShellItem, (void**)&psiFolder);
        PW_ASSERT(hr == S_OK);
        if (hr)
            goto error;

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setfolder
        pfd->lpVtbl->SetFolder(pfd, psiFolder);
    }

    // Set the options on the dialog.
    // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-getoptions
    // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setoptions
    // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_fileopendialogoptions

    // Apprently there's no way to choose both files & folders using the newer IFileDialog API
    // https://stackoverflow.com/questions/8269696/how-to-use-ifiledialog-with-fos-pickfolder-while-still-displaying-file-names-in
    // Apprently you can use the older API SHBrowseForFolder, but that looks shabby and with worse UX
    // ie. the old API is not great for commercial products
    hr = pfd->lpVtbl->GetOptions(pfd, &options);
    PW_ASSERT(hr == S_OK);
    if (hr)
        goto error;

    if (pw->ChooseFile.Mutliselect)
        options |= FOS_ALLOWMULTISELECT;
    if (pw->ChooseFile.IsFolder)
        options |= FOS_PICKFOLDERS;
    hr = pfd->lpVtbl->SetOptions(pfd, options);
    PW_ASSERT(hr == S_OK);
    if (hr)
        goto error;

    if (pw->ChooseFile.NumTypes)
    {
        // Set default extension. We will use the first extension in the array.
        // Converts string from a format like this L"*.jpg;*.jpeg" to this L"jpg"
        WCHAR   ext[64] = {0};
        LPCWSTR wc      = pw->ChooseFile.pFileTypes->pszSpec;
        LPCWSTR wc_end;
        while (*wc == '*' || *wc == '.')
            wc++;
        wc_end = wc;
        while (*wc_end != 0 && *wc_end != ';')
            wc_end++;
        _snwprintf(ext, ARRAYSIZE(ext), L"%.*ls", (wc_end - wc), wc);

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setfiletypes
        hr = pfd->lpVtbl->SetFileTypes(pfd, pw->ChooseFile.NumTypes, pw->ChooseFile.pFileTypes);
        PW_ASSERT(hr == S_OK);
        if (hr)
            goto error;

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setfiletypeindex
        hr = pfd->lpVtbl->SetFileTypeIndex(pfd, 1);
        PW_ASSERT(hr == S_OK);
        if (hr)
            goto error;

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-setdefaultextension
        hr = pfd->lpVtbl->SetDefaultExtension(pfd, ext);
        PW_ASSERT(hr == S_OK);
        if (hr)
            goto error;
    }

    if (pw->ChooseFile.pName)
    {
        pfd->lpVtbl->SetFileName(pfd, pw->ChooseFile.pName);
        PW_ASSERT(hr == S_OK);
        if (hr)
            goto error;
    }

    // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-imodalwindow-show
    hr = pfd->lpVtbl->Show(pfd, NULL);
    if (hr) // hr = non zero if user cancelled
        goto error;

    if (pw->ChooseFile.Mutliselect)
    {
        OutputDebugStringW(L"multiple\n");
        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileopendialog-getresults
        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellitemarray
        IFileOpenDialog* pod      = (IFileOpenDialog*)pfd;
        IShellItemArray* penum    = NULL;
        DWORD            NumItems = 0;
        DWORD            i        = 0;

        hr = pod->lpVtbl->GetResults(pod, &penum);
        PW_ASSERT(hr == S_OK);
        if (penum)
            penum->lpVtbl->GetCount(penum, &NumItems);

        if (NumItems)
        {
            SIZE_T AllocSize          = sizeof(char*) * NumItems;
            pw->ChooseFile.pFilePaths = PW_MALLOC(AllocSize);
            ZeroMemory(pw->ChooseFile.pFilePaths, AllocSize);
        }

        for (; i < NumItems; i++)
        {
            IShellItem* psi = NULL;

            hr = penum->lpVtbl->GetItemAt(penum, i, &psi);
            PW_ASSERT(hr == S_OK);
            if (psi)
            {
                PWSTR pszFilePath = NULL;

                hr = psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &pszFilePath);
                PW_ASSERT(hr == S_OK);
                if (pszFilePath)
                {
                    pw->ChooseFile.pFilePaths[i] = PWMakeUTF8String(pszFilePath);
                    pw->ChooseFile.NumPaths++;
                }

                psi->lpVtbl->Release(psi);
            }
        }

        if (penum)
            penum->lpVtbl->Release(penum);
    }
    else
    {
        OutputDebugStringW(L"single\n");
        IShellItem* psiResult   = NULL;
        PWSTR       pszFilePath = NULL;

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifiledialog-getresult
        hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
        PW_ASSERT(hr == S_OK);

        // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-getdisplayname
        if (psiResult)
            hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &pszFilePath);
        PW_ASSERT(hr == S_OK);

        if (pszFilePath)
        {
            pw->ChooseFile.NumPaths      = 1;
            pw->ChooseFile.pFilePaths    = PW_MALLOC(sizeof(char*));
            pw->ChooseFile.pFilePaths[0] = PWMakeUTF8String(pszFilePath);
            CoTaskMemFree(pszFilePath);
        }
        if (psiResult)
            psiResult->lpVtbl->Release(psiResult);
    }

error:
    if (psiFolder)
        psiFolder->lpVtbl->Release(psiFolder);
    if (pfd)
        pfd->lpVtbl->Release(pfd);

    CoUninitialize();

    // Handle callback on main thread
    PostMessageW(pw->hwnd, WM_COMMAND, PW_WM_COMMAND_ChooseFile, 0);

    return hr;
}

bool pw_choose_file(const PWChooseFileArgs* args)
{
    CplugWindow* pw  = args->pw;
    int          num = 0;
    PW_ASSERT(pw->ChooseFile.hThread == NULL); // Is thread running?

    // Test valid combinations of arguments
    PW_ASSERT(args->pw);
    PW_ASSERT(args->callback);
    PW_ASSERT((args->is_folder ? (args->is_save == false) : true));
    PW_ASSERT((args->is_folder ? (args->num_extensions == 0) : true));
    PW_ASSERT(args->multiselect ? (args->is_save == false) : true);
    PW_ASSERT((args->num_extensions ? (args->extension_names != NULL) : true));
    PW_ASSERT((args->num_extensions ? (args->extension_types != NULL) : true));

    ZeroMemory(&pw->ChooseFile, sizeof(pw->ChooseFile));

    pw->ChooseFile.callback_data = args->callback_data;
    pw->ChooseFile.callback      = args->callback;

    pw->ChooseFile.IsSave      = args->is_save;
    pw->ChooseFile.IsFolder    = args->is_folder;
    pw->ChooseFile.Mutliselect = args->multiselect;

    // The goal in this function is to serialise the users data passed in "args" into a format that will be used by a
    // IFileDialog on another thread.
    // https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifiledialog
    // The purpose of opening the file dialog on another thread is to avoid blocking on the main thread.

    if (args->num_extensions)
    {
        pw->ChooseFile.NumTypes = args->num_extensions;

        size_t size               = sizeof(*pw->ChooseFile.pFileTypes) * args->num_extensions;
        pw->ChooseFile.pFileTypes = (COMDLG_FILTERSPEC*)PW_MALLOC(size);
        ZeroMemory(pw->ChooseFile.pFileTypes, size);

        for (int i = 0; i < args->num_extensions; i++)
        {
            const char* ext8  = args->extension_types[i];
            const char* name8 = args->extension_names[i];

            COMDLG_FILTERSPEC* pFilterSpec = &pw->ChooseFile.pFileTypes[i];

            // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
            num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name8, -1, NULL, 0);
            PW_ASSERT(num != 0);
            if (num == 0)
                goto error;

            LPWSTR name16        = PW_MALLOC((sizeof(WCHAR) * (num + 1)));
            pFilterSpec->pszName = name16;
            num                  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name8, -1, name16, num);
            name16[num]          = 0;

            num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ext8, -1, NULL, 0);
            PW_ASSERT(num != 0);
            if (num == 0)
                goto error;

            LPWSTR ext16         = PW_MALLOC((sizeof(WCHAR) * (num + 3)));
            pFilterSpec->pszSpec = ext16;
            ext16[0]             = L'*';
            ext16[1]             = L'.';
            num                  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ext8, -1, ext16 + 2, num);
            ext16[num + 2]       = 0;
        }
    }

    if (args->folder)
    {
        num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->folder, -1, NULL, 0);
        PW_ASSERT(num >= 3); // Prevents unix directories starting with "/". Windows directories look like this "C:\"
        if (num < 3)
            goto error;

        pw->ChooseFile.pFolder = PW_MALLOC(((num + 1) * sizeof(WCHAR)));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->folder, -1, pw->ChooseFile.pFolder, num);
        pw->ChooseFile.pFolder[num] = 0;
    }

    if (args->filename)
    {
        num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->filename, -1, NULL, 0);
        PW_ASSERT(num != 0);
        if (num == 0)
            goto error;

        pw->ChooseFile.pName = PW_MALLOC(((num + 1) * sizeof(WCHAR)));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->filename, -1, pw->ChooseFile.pName, num);
        pw->ChooseFile.pName[num] = 0;
    }

    if (args->title)
    {
        num = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->title, -1, NULL, 0);
        PW_ASSERT(num != 0);
        if (num == 0)
            goto error;

        pw->ChooseFile.pTitle = PW_MALLOC(((num + 1) * sizeof(WCHAR)));
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, args->title, -1, pw->ChooseFile.pTitle, num);
        pw->ChooseFile.pTitle[num] = 0;
    }

    pw->ChooseFile.hThread = CreateThread(0, 0, PWChooseFileThread, pw, 0, NULL);
    return true;

error:
    PWFreeChooseFile(pw);
    return false;
}