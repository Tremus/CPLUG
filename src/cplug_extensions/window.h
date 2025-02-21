/* Released into the public domain by Tré Dudman - 2025
 * For licensing and more info see https://github.com/Tremus/CPLUG */
/*
CPLUG Window extention
This library will implement all cplug_guiXXXXX functions and provide a simpler API

*****************
* Library goals *
*****************
- Normalise Window events & lifetimes between Windows & macOS
- Solve problems unique to plugins that embed windows in DAWs.
  eg. Some DAWs use evil trickery to steal keystrokes from your window. This library will steal them back!
  eg. Plugin specifications don't have ways to inform the plugin how the window is being resized(eg. bottom right
      corner, left edge). This library uses tricks to detect the resize corner.
  eg. No global state that leads to bugs when multiple instances of your program are active.
- Be feature rich
- Be lightweight
- Have comprehensible & well documented code in case users need to jump in and change things

&&&&&&&&&&&&&&&&&
& Main Features &
&&&&&&&&&&&&&&&&&
- Resize / constrain size
- Mouse & keyboard events
- Change cursor types
- Get/release keybaord focus
- Get/set clipboard text
- File drag & drop (import)
- File save dialogue box

Create these functions in your program to get strated. See their forward declarations below for their usage:
- pw_get_info
- pw_create_gui
- pw_destroy_gui
- pw_tick
- pw_event

?????????????????????
? Optional Features ?
?????????????????????

This library uses a few libc functions which may bug you. If they do, the following can be overriden
#define PW_ASSERT, PW_MALLOC, PW_FREE to override

Define the macro PW_DX11 to automatically set up a DX11 device & swapchain etc.
Also gain additional getter functions for:
- ID3D11Device*           pDevice              pw_get_dx11_device()
- ID3D11DeviceContext*    pDeviceContext       pw_get_dx11_device_context()
- ID3D11RenderTargetView* pRenderTargetView    pw_get_dx11_render_target_view()
- ID3D11DepthStencilView* pDepthStencilView    pw_get_dx11_depth_stencil_view()

Define the macro PW_METAL to use MTKView instead of NSView.
Also gain additional getter functions for:
- id<MTLDevice>       device                   pw_get_metal_device()
- id<CAMetalDrawable> currentDrawable          pw_get_metal_drawable()
- id<MTLTexture>      depthStencilTexture      pw_get_metal_depth_stencil_texture()

$$$$$$$$$$$$$$$$$$$
$ Example program $
$$$$$$$$$$$$$$$$$$$
*/
#if 0

#include <cplug.h>
#include <cplug_extensions/window.h>

struct GUI;

typedef struct Plugin
{
    CplugHostContext* cplug_ctx;
    struct GUI*       gui;

    int width, height;
} Plugin;

typedef struct GUI
{
    Plugin* plugin;
    void*   pw;
} GUI;

void pw_get_info(PWGetInfo* info)
{
    if (info->type == PW_INFO_INIT_SIZE)
    {
        Plugin* plugin         = info->init_size.plugin;
        info->init_size.width  = plugin->width;
        info->init_size.height = plugin->height;
    }
    else if (info->type == PW_INFO_CONSTRAIN_SIZE)
    {
        if (info->constrain_size.width > 1000)
            info->constrain_size.width = 1000;
        if (info->constrain_size.height > 1000)
            info->constrain_size.height = 1000;
    }
}

void* pw_create_gui(void* _plugin, void* pw)
{
    Plugin* plugin = _plugin;
    GUI*    gui    = calloc(1, sizeof(*gui));

    plugin->gui = gui;
    gui->plugin = plugin;
    gui->pw     = pw;

    // ... init

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    // ... deinit

    gui->plugin->gui = NULL;
    free(gui);
}

void pw_tick(void* _gui)
{
    GUI* gui = _gui;
    // ... draw frame
}

bool pw_event(const PWEvent* event)
{
    if (event->type == PW_EVENT_RESIZE)
    {
        GUI*    gui    = event->gui;
        Plugin* plugin = gui->plugin;
        plugin->width  = event->resize.width;
        plugin->height = event->resize.height;
    }
    else if (event->type == PW_EVENT_MOUSE_LEFT_DOWN)
    {
        pw_beep();
    }
    return false;
}
#endif
/*
================
= Dependencies =
================
- cplug
- [Windows] dxguid (if using DX11)
- [macOS] a compiler supporting Objective-C
- [macOS] -framework Quartz
- [macOS] -framework Cocoa
- [macOS] -framework Metal    (if using Metal)
- [macOS] -framework MetalKit (if using Metal)

>>>>>>>>>>>
> Roadmap >
>>>>>>>>>>>
- Support other dialogue boxes, eg. alerts and colour pickers
- Support optional OpenGL or nah?

%%%%%%%%%%%%%%%%%%%%%
% Special thanks to %
%%%%%%%%%%%%%%%%%%%%%
- GLFW contributors for most of the platform code
- REUK for help with text input on Windows
- github/@floooh (Andre Weissflog) sokol libraries for some graphics code and API design of sokol_app.h
- Andrew Belts osdialog lib https://github.com/AndrewBelt/osdialog
*/

#ifndef CPLUG_PW_H
#define CPLUG_PW_H

#ifndef PW_ASSERT

#ifdef NDEBUG
#define PW_ASSERT(...)
#else // !NDEBUG

#ifdef _WIN32
#define PW_ASSERT(cond) (cond) ? (void)0 : __debugbreak()
#else
#define PW_ASSERT(cond) (cond) ? (void)0 : __builtin_debugtrap()
#endif // _WIN32

#endif // NDEBUG

#endif // !PW_ASSERT

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef PW_MALLOC
#define PW_MALLOC(sz) malloc(sz)
#endif
#ifndef PW_FREE
#define PW_FREE(ptr) free(ptr)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
enum PWCursorType : int; // AppleClang freaks out if you forward declare an enum in C++, even within extern "C"
#else
enum PWCursorType;
#endif // __cplusplus

struct PWGetInfo;
struct PWEvent;
struct PWChooseFileArgs;

// ESSENTIAL API

// Define these in your program
void  pw_get_info(struct PWGetInfo* info);
void* pw_create_gui(void* plugin, void* pw);
void  pw_destroy_gui(void* gui);
void  pw_tick(void* gui); // Timer callback
// By default you should return false, unless you know you need to return true for certain events.
// See struct PWEvent below for instructions.
bool pw_event(const struct PWEvent* event);

// GRAPHICS API

#if defined(PW_METAL) && defined(__APPLE__)

void* pw_get_metal_device(void* pw);
void* pw_get_metal_drawable(void* pw);
void* pw_get_metal_depth_stencil_texture(void* pw);

#endif // PW_METAL

#if defined(PW_DX11) && defined(_WIN32)

void* pw_get_dx11_device(void* pw);
void* pw_get_dx11_device_context(void* pw);
void* pw_get_dx11_render_target_view(void* pw);
void* pw_get_dx11_depth_stencil_view(void* pw);

#endif

// BONUS UTILITIES API

// Returns HWND or NSView
void* pw_get_native_window(void* pw);
void  pw_get_keyboard_focus(void* pw);
bool  pw_check_keyboard_focus(const void* pw);
void  pw_release_keyboard_focus(void* pw);
void  pw_get_screen_size(uint32_t* width, uint32_t* height);
float pw_get_dpi(void* pw);

void pw_set_clipboard_text(void* pw, const char* text);
// Get a pointer to a \0 terminated C string.
bool pw_get_clipboard_text(void* pw, char** out, size_t* len); // allocates memory
void pw_free_clipboard_text(char* ptr);                        // free here

void pw_set_mouse_cursor(void* pw, enum PWCursorType type);
void pw_beep(); // System default beep sound. Often used by alert popups to annoy users

// Drag files from your window into other windows
// On macOS, this may ONLY be called on 'PW_EVENT_MOUSE_LEFT_DOWN' events
void pw_drag_files(void* pw, const char* const* paths, uint32_t num_paths);

// Open & Save file dialogue box.
// Must be called from the main thread.
// Your supplied callback is called asynchronously from the main thread
bool pw_choose_file(const struct PWChooseFileArgs*);

// callback_data: Pointer passed in PWChooseFileArgs
// paths: Array of paths chosen selected by the user.
//        If saving a file, the extension will be appended automatically by the OS.
//        If the returned file path already exists, the user has already accepted a prompt to overwrite it
//        Set to NULL if user cancels.
// num_paths: Number of items in array. Set to 0 if user cancels
typedef void (*pw_choose_file_callback)(void* callback_data, const char* const* paths, uint32_t num_paths);

typedef struct PWChooseFileArgs
{
    void* pw;

    void*                   callback_data;
    pw_choose_file_callback callback;

    bool is_save;     // false if open, true if save
    bool is_folder;   // false if pick files, true if pick folders
    bool multiselect; // false if single, true if multiple

    uint32_t     num_extensions;  // number of filters and display names must match!
    const char** extension_names; // eg. char* names[] = {"Text Document (.txt)", "Microsoft Word Document (.doc)"};
    const char** extension_types; // eg. char* filters[] = {"txt", "doc"};

    const char* title;    // Window title
    const char* folder;   // Init folder
    const char* filename; // Default filename

} PWChooseFileArgs;

// https://developer.apple.com/documentation/appkit/nscursor?language=objc
enum PWCursorType
#ifdef __cplusplus
    : int
#endif
{
    PW_CURSOR_ARROW, // Default cursor
    PW_CURSOR_IBEAM, // 'I' used for hovering over text
    PW_CURSOR_NO,    // Circle with diagonal strike through
    PW_CURSOR_CROSS, // Precision select/crosshair

    PW_CURSOR_ARROW_DRAG, // Default cursor with copy box
    PW_CURSOR_HAND_POINT,
    PW_CURSOR_HAND_DRAGGABLE,
    PW_CURSOR_HAND_DRAGGING,

    PW_CURSOR_RESIZE_WE,
    PW_CURSOR_RESIZE_NS,
    PW_CURSOR_RESIZE_NESW,
    PW_CURSOR_RESIZE_NWSE,
};

// clang-format off
// NOTE: Virtual keys values are a mirror of that found in the OS headers.
// Values between Windows and macOS are different
#if defined(_WIN32)
// https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
enum PWVirtualKey
{
    PW_KEY_BACKSPACE = 0x08,

    PW_KEY_PAGE_UP   = 0x21,
    PW_KEY_PAGE_DOWN,
    PW_KEY_END,
    PW_KEY_HOME,
    PW_KEY_ARROW_LEFT,
    PW_KEY_ARROW_UP,
    PW_KEY_ARROW_RIGHT,
    PW_KEY_ARROW_DOWN,

    PW_KEY_INSERT = 0x2D,
    PW_KEY_DELETE = 0x2E, // DEL, not to be confused with backspace

    PW_KEY_0 = 0x30,
    PW_KEY_1,
    PW_KEY_2,
    PW_KEY_3,
    PW_KEY_4,
    PW_KEY_5,
    PW_KEY_6,
    PW_KEY_7,
    PW_KEY_8,
    PW_KEY_9,

    PW_KEY_A = 0x41,
    PW_KEY_B,
    PW_KEY_C,
    PW_KEY_D,
    PW_KEY_E,
    PW_KEY_F,
    PW_KEY_G,
    PW_KEY_H,
    PW_KEY_I,
    PW_KEY_J,
    PW_KEY_K,
    PW_KEY_L,
    PW_KEY_M,
    PW_KEY_N,
    PW_KEY_O,
    PW_KEY_P,
    PW_KEY_Q,
    PW_KEY_R,
    PW_KEY_S,
    PW_KEY_T,
    PW_KEY_U,
    PW_KEY_V,
    PW_KEY_W,
    PW_KEY_X,
    PW_KEY_Y,
    PW_KEY_Z,

    PW_KEY_F1 = 0x70,
    PW_KEY_F2,
    PW_KEY_F3,
    PW_KEY_F4,
    PW_KEY_F5,
    PW_KEY_F6,
    PW_KEY_F7,
    PW_KEY_F8,
    PW_KEY_F9,
    PW_KEY_F10,
    PW_KEY_F11,
    PW_KEY_F12,
    PW_KEY_F13,
    PW_KEY_F14,
    PW_KEY_F15,
    PW_KEY_F16,
    PW_KEY_F17,
    PW_KEY_F18,
    PW_KEY_F19,
    PW_KEY_F20,
    PW_KEY_F21,
    PW_KEY_F22,
    PW_KEY_F23,
    PW_KEY_F24,

    PW_KEY_SHIFT_LEFT = 0xA0,
    PW_KEY_SHIFT_RIGHT,
    PW_KEY_CTRL_LEFT,
    PW_KEY_CTRL_RIGHT,
    PW_KEY_ALT_LEFT,
    PW_KEY_ALT_RIGHT,

    PW_KEY_MEDIA_NEXT = 0xB0,
    PW_KEY_MEDIA_PREV,
    PW_KEY_MEDIA_STOP,
    PW_KEY_MEDIA_PLAY_PAUSE,

    PW_KEY_OEM_1 = 0xBA, // ;:
    PW_KEY_PLUS,         // +=
    PW_KEY_COMMA,        // ,<
    PW_KEY_MINUS,        // -_
    PW_KEY_PERIOD,       // .>
    PW_KEY_OEM_2,        // /?
    PW_KEY_OEM_3,        // `~

    PW_KEY_OEM_4,        // [{
    PW_KEY_OEM_5,        // \|
    PW_KEY_OEM_6,        // ]}
    PW_KEY_OEM_7,        // '"

    PW_KEY_PLAY = 0xFA,
    PW_KEY_ZOOM = 0xFB,
};
#elif defined(__APPLE__)
// Virtual key codes were found here:
// /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/Carbon.framework/Versions/A/Frameworks/HIToolbox.framework/Versions/A/Headers/Events.h
// Include the Carbon framework and use your IDE to search for 'kVK_ANSI_A'
enum PWVirtualKey
{
    // Layout dependent
    PW_KEY_A                    = 0x00,
    PW_KEY_S                    = 0x01,
    PW_KEY_D                    = 0x02,
    PW_KEY_F                    = 0x03,
    PW_KEY_H                    = 0x04,
    PW_KEY_G                    = 0x05,
    PW_KEY_Z                    = 0x06,
    PW_KEY_X                    = 0x07,
    PW_KEY_C                    = 0x08,
    PW_KEY_V                    = 0x09,
    PW_KEY_B                    = 0x0B,
    PW_KEY_Q                    = 0x0C,
    PW_KEY_W                    = 0x0D,
    PW_KEY_E                    = 0x0E,
    PW_KEY_R                    = 0x0F,
    PW_KEY_Y                    = 0x10,
    PW_KEY_T                    = 0x11,
    PW_KEY_1                    = 0x12,
    PW_KEY_2                    = 0x13,
    PW_KEY_3                    = 0x14,
    PW_KEY_4                    = 0x15,
    PW_KEY_6                    = 0x16,
    PW_KEY_5                    = 0x17,
    PW_KEY_PLUS                 = 0x18, // =+
    PW_KEY_9                    = 0x19,
    PW_KEY_7                    = 0x1A,
    PW_KEY_MINUS                = 0x1B, // _-
    PW_KEY_8                    = 0x1C,
    PW_KEY_0                    = 0x1D,
    PW_KEY_OEM_6                = 0x1E, // ]}
    PW_KEY_O                    = 0x1F,
    PW_KEY_U                    = 0x20,
    PW_KEY_OEM_4                = 0x21, // [{
    PW_KEY_I                    = 0x22,
    PW_KEY_P                    = 0x23,
    PW_KEY_L                    = 0x25,
    PW_KEY_J                    = 0x26,
    PW_KEY_OEM_7                = 0x27, // '"
    PW_KEY_K                    = 0x28,
    PW_KEY_OEM_1                = 0x29, // ;:
    PW_KEY_OEM_5                = 0x2A, // \|
    PW_KEY_COMMA                = 0x2B, // ,<
    PW_KEY_OEM_2                = 0x2C,
    PW_KEY_N                    = 0x2D,
    PW_KEY_M                    = 0x2E,
    PW_KEY_PERIOD               = 0x2F, // >.
    PW_KEY_OEM_3                = 0x32,
    PW_KEY_KeypadDecimal        = 0x41,
    PW_KEY_KeypadMultiply       = 0x43,
    PW_KEY_KeypadPlus           = 0x45,
    PW_KEY_KeypadClear          = 0x47,
    PW_KEY_KeypadDivide         = 0x4B,
    PW_KEY_KeypadEnter          = 0x4C,
    PW_KEY_KeypadMinus          = 0x4E,
    PW_KEY_KeypadEquals         = 0x51,
    PW_KEY_Keypad0              = 0x52,
    PW_KEY_Keypad1              = 0x53,
    PW_KEY_Keypad2              = 0x54,
    PW_KEY_Keypad3              = 0x55,
    PW_KEY_Keypad4              = 0x56,
    PW_KEY_Keypad5              = 0x57,
    PW_KEY_Keypad6              = 0x58,
    PW_KEY_Keypad7              = 0x59,
    PW_KEY_Keypad8              = 0x5B,
    PW_KEY_Keypad9              = 0x5C,

    // Layout independent
    PW_KEY_Return                    = 0x24,
    PW_KEY_Tab                       = 0x30,
    PW_KEY_Space                     = 0x31,
    PW_KEY_BACKSPACE                 = 0x33,
    PW_KEY_Escape                    = 0x35,
    PW_KEY_Command                   = 0x37,
    PW_KEY_Shift                     = 0x38,
    PW_KEY_CapsLock                  = 0x39,
    PW_KEY_Option                    = 0x3A,
    PW_KEY_Control                   = 0x3B,
    PW_KEY_RightCommand              = 0x36,
    PW_KEY_RightShift                = 0x3C,
    PW_KEY_RightOption               = 0x3D,
    PW_KEY_RightControl              = 0x3E,
    // PW_KEY_Function                  = 0x3F,
    PW_KEY_F17                       = 0x40,
    // PW_KEY_VolumeUp                  = 0x48,
    // PW_KEY_VolumeDown                = 0x49,
    // PW_KEY_Mute                      = 0x4A,
    PW_KEY_F18                       = 0x4F,
    PW_KEY_F19                       = 0x50,
    PW_KEY_F20                       = 0x5A,
    PW_KEY_F5                        = 0x60,
    PW_KEY_F6                        = 0x61,
    PW_KEY_F7                        = 0x62,
    PW_KEY_F3                        = 0x63,
    PW_KEY_F8                        = 0x64,
    PW_KEY_F9                        = 0x65,
    PW_KEY_F11                       = 0x67,
    PW_KEY_F13                       = 0x69,
    PW_KEY_F16                       = 0x6A,
    PW_KEY_F14                       = 0x6B,
    PW_KEY_F10                       = 0x6D,
    PW_KEY_F12                       = 0x6F,
    PW_KEY_F15                       = 0x71,
    PW_KEY_INSERT                    = 0x72, // 'Help' key on macos
    PW_KEY_HOME                      = 0x73,
    PW_KEY_PAGE_UP                   = 0x74,
    PW_KEY_DELETE                    = 0x75,
    PW_KEY_F4                        = 0x76,
    PW_KEY_END                       = 0x77,
    PW_KEY_F2                        = 0x78,
    PW_KEY_PAGE_DOWN                 = 0x79,
    PW_KEY_F1                        = 0x7A,
    PW_KEY_ARROW_LEFT                = 0x7B,
    PW_KEY_ARROW_RIGHT               = 0x7C,
    PW_KEY_ARROW_DOWN                = 0x7D,
    PW_KEY_ARROW_UP                  = 0x7E
};
#endif
// clang-format on

enum
{
    PW_MOD_LEFT_BUTTON   = 1 << 0,
    PW_MOD_RIGHT_BUTTON  = 1 << 1,
    PW_MOD_MIDDLE_BUTTON = 1 << 2,
    PW_MOD_KEY_CTRL      = 1 << 3,
    PW_MOD_KEY_ALT       = 1 << 4,
    PW_MOD_KEY_SHIFT     = 1 << 5,
    PW_MOD_KEY_CMD       = 1 << 6,
    PW_MOD_KEY_OPTION    = 1 << 7,
    // Flag set when touch events are inverted on Apple devices
    // See: [NSEvent isDirectionInvertedFromDevice]
    PW_MOD_INVERTED_SCROLL = 1 << 8,

#ifdef _WIN32
    PW_MOD_PLATFORM_KEY_CTRL = PW_MOD_KEY_CTRL,
    PW_MOD_PLATFORM_KEY_ALT  = PW_MOD_KEY_ALT,
#elif defined(__APPLE__)
    PW_MOD_PLATFORM_KEY_CTRL = PW_MOD_KEY_CMD,
    PW_MOD_PLATFORM_KEY_ALT  = PW_MOD_KEY_OPTION,
#endif
};

enum PWEventType
{
    PW_EVENT_RESIZE,
    PW_EVENT_DPI_CHANGED,

    PW_EVENT_MOUSE_EXIT,
    PW_EVENT_MOUSE_ENTER,
    PW_EVENT_MOUSE_MOVE,
    PW_EVENT_MOUSE_SCROLL_WHEEL,
    PW_EVENT_MOUSE_SCROLL_TOUCHPAD, // NOTE: touchpad not yet implemented on Windows
    PW_EVENT_MOUSE_LEFT_DOWN,
    PW_EVENT_MOUSE_RIGHT_DOWN,
    PW_EVENT_MOUSE_MIDDLE_DOWN,
    PW_EVENT_MOUSE_LEFT_UP,
    PW_EVENT_MOUSE_RIGHT_UP,
    PW_EVENT_MOUSE_MIDDLE_UP,

    PW_EVENT_KEY_DOWN,
    PW_EVENT_KEY_UP,
    PW_EVENT_TEXT,
    PW_EVENT_KEY_FOCUS_LOST,

    PW_EVENT_FILE_ENTER,
    PW_EVENT_FILE_MOVE,
    PW_EVENT_FILE_DROP,
    PW_EVENT_FILE_EXIT,
};

typedef struct PWEvent
{
    enum PWEventType type;
    void*            gui;

    union
    {
        // PW_EVENT_RESIZE
        struct
        {
            uint32_t width, height;
        } resize;

        // PW_EVENT_DPI_CHANGED
        float dpi;

        // PW_EVENT_MOUSE_EXIT - N/A

        // PW_EVENT_MOUSE_ENTER
        // PW_EVENT_MOUSE_MOVE
        // PW_EVENT_MOUSE_SCROLL_TOUCHPAD
        // PW_EVENT_MOUSE_SCROLL_WHEEL
        // PW_EVENT_MOUSE_LEFT_DOWN
        // PW_EVENT_MOUSE_LEFT_UP
        // PW_EVENT_MOUSE_RIGHT_DOWN
        // PW_EVENT_MOUSE_RIGHT_UP
        // PW_EVENT_MOUSE_MIDDLE_DOWN
        // PW_EVENT_MOUSE_MIDDLE_UP
        struct
        {
            float    x;
            float    y;
            uint32_t modifiers; // Flags. See PW_MOD_XXX
            uint32_t time_ms;
            uint32_t double_click_interval_ms;
        } mouse;

        // PW_EVENT_KEY_DOWN
        // PW_EVENT_KEY_UP
        // Return true if event was consumed. Returning false will propogate the message to the parent window
        struct
        {
            enum PWVirtualKey virtual_key;
            uint32_t          modifiers; // Flags. See PW_MOD_XXX
        } key;
        // PW_EVENT_TEXT
        // Return true if event was consumed. Returning false will propogate the message to the parent window
        struct
        {
            int      codepoint; // utf8 / utf32
            uint32_t modifiers; // Flags. See PW_MOD_XXX
        } text;

        // PW_EVENT_FILE_ENTER
        // Return true if anything in your window may be interested in file.
        // PW_EVENT_FILE_MOVE
        // Return true if area beneath mouse position is interested in file.
        // MOVE gets called a lot, so make sure your implementation is fast.
        // PW_EVENT_FILE_DROP
        // Return true if action completed.
        struct
        {
            float              x, y; // mouse position
            const char* const* paths;
            uint32_t           num_paths;
        } file;
        // PW_EVENT_FILE_EXIT - N/A
    };
} PWEvent;

enum PWInfoType
{
    PW_INFO_INIT_SIZE,
    PW_INFO_CONSTRAIN_SIZE,
};

typedef enum PWResizeDirection
{
    PW_RESIZE_UNKNOWN,
    PW_RESIZE_LEFT,
    PW_RESIZE_RIGHT,
    PW_RESIZE_TOP,
    PW_RESIZE_TOPLEFT,
    PW_RESIZE_TOPRIGHT,
    PW_RESIZE_BOTTOM,
    PW_RESIZE_BOTTOMLEFT,
    PW_RESIZE_BOTTOMRIGHT,
} PWResizeDirection;

typedef struct PWGetInfo
{
    enum PWInfoType type;
    int             padding;
    union
    {
        // PW_INFO_INIT_SIZE
        // Called before your GUI is created
        struct
        {
            void*    plugin;
            uint32_t width, height;
        } init_size;
        // PW_INFO_CONSTRAIN_SIZE
        // Called before your GUI is resized
        // width & height will contain the proposed size. Overwrite these properties with your own values
        struct
        {
            void*    gui;
            uint32_t width, height;

            PWResizeDirection direction;
        } constrain_size;
    };
} PWGetInfo;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // !CPLUG_PW_H