/* Released into the public domain by Tré Dudman - 2025
 * For licensing and more info see https://github.com/Tremus/CPLUG */

#import <Cocoa/Cocoa.h>
#include <cplug.h>
#include <cplug_extensions/window.h>

#ifdef PW_METAL
#import <MetalKit/MetalKit.h>
@interface CplugWindow : MTKView <NSWindowDelegate, NSDraggingDestination, NSDraggingSource>
#else
@interface CplugWindow : NSView <NSWindowDelegate, NSDraggingDestination, NSDraggingSource>
#endif // PW_METAL
{
@public
    void* gui;
    void* plugin;

    // The state below used for hacky resize corner detection.
    // Setting a NSWindowDelegate would be ideal, but not always possible, because many hosts set their own, and
    // replacing theirs with ours can create problems.
    NSRect   resizeStartFrame;
    uint32_t pwResizeFlags; // Not to be confused with NSWindow.resizeFlags!
    bool     checkResizeFlag;

    // A quirk of macOS vs Windows mouse exit events:

    // On macOS, if you're dragging something within your window and your mouse leaves the window, your NSView is sent
    // [mouseExited], and will continue to be sent [mouseDragged] which we translate to MOUSE_MOVE. If while dragging,
    // you reenter the window, you will not be sent a [mouseEntered] event, nor will you be sent a second [mouseExited]
    // event if your mouse leaves for a second time during the same drag. If during a drag your mouse exits and reenters
    // the window then drops inside the window, you will be sent [mouseUp] then [mouseEntered].
    // The docs within NSTrackingArea.h roughly explain this.

    // On Windows, if you're dragging something within your window and your mouse leaves the window, you will not
    // receive WM_MOUSELEAVE until your drag stops. When your drag is released outside of the window, first you receive
    // WM_LBUTTONUP, then WM_MOUSELEAVE. If your drag leaves the window and reenters before dropping, you will only
    // receive WM_LBUTTONUP and WM_MOUSEMOVE events, without any LEAVE event (ENTER doesn't exist on Windows).

    // Consistent event propagation is important in cross platform development, which means one of these two platforms
    // must become the leaky abstraction...
    // I consider the behaviour of Windows to be make more sense. MOUSE_EXIT should be sent when the mouse is outside
    // of the window and not currently interacting with anything. When interactions have stopped (ie. there is no mouse
    // button held down), EXIT should be sent only if the mouse is outside the window.
    // These booleans below are unfortunately necessary for the speghetti code to make this library work like Windows.
    // I have been unsuccesful in using NSTrackingAreaOptions to recreate Windows behaviour...
    bool isDragging;
    bool isMouseOver;

    UInt32          numDraggedFiles;
    char**          draggedFiles;
    NSTrackingArea* trackingArea;
#ifndef PW_METAL
    CFRunLoopTimerRef timerRef;
#endif
}
@end

#ifndef PW_METAL
void pw_timer_cb(CFRunLoopTimerRef timer, void* info)
{
    CplugWindow* pw = (CplugWindow*)info;
    pw_tick(pw->gui);
}
#endif

enum
{
    PW_FLAG_RESIZE_UNKNOWN = 0,
    PW_FLAG_RESIZE_LEFT    = 1 << 0,
    PW_FLAG_RESIZE_RIGHT   = 1 << 1,
    PW_FLAG_RESIZE_TOP     = 1 << 2,
    PW_FLAG_RESIZE_BOTTOM  = 1 << 3,
};

#ifdef NDEBUG
#define pwAssertValidResizeFlags(...)
#else
void pwAssertValidResizeFlags(uint32_t flags)
{
    PW_ASSERT(flags <= 10);
    PW_ASSERT(flags != 3);
    PW_ASSERT(flags != 7);
}
#endif

enum PWResizeDirection pwTranslateResizeFlags(uint32_t flags)
{
    pwAssertValidResizeFlags(flags);
    if (flags > 7)
        flags--;
    if (flags > 3)
        flags--;
    PW_ASSERT(flags >= PW_RESIZE_UNKNOWN && flags <= PW_RESIZE_BOTTOMRIGHT);
    return flags;
}

uint64_t pwTranslateModifierFlags(NSEvent* event)
{
    uint32_t mods = 0;

    NSEventModifierFlags nsflags = [event modifierFlags];

    if (nsflags & NSEventModifierFlagShift)
        mods |= PW_MOD_KEY_SHIFT;
    if (nsflags & NSEventModifierFlagControl)
        mods |= PW_MOD_KEY_CTRL;
    if (nsflags & NSEventModifierFlagOption)
        mods |= PW_MOD_KEY_OPTION;
    if (nsflags & NSEventModifierFlagCommand)
        mods |= PW_MOD_KEY_CMD;

    // NOTE: a problem with using 'buttonNumber' API is that it will return 0 within mouseEntered/Exit/Moved events.
    // 0 means the left button is pressed. This is likely bad design on Apples part which they rightly fixed in 10.6
    // with NSEvent.pressedMouseButtons, however in typical Apple fashion they don't document any of this.
    // https://developer.apple.com/documentation/appkit/nsevent/1527828-buttonnumber?language=objc
    // NSInteger btnNum = [event buttonNumber];
    // https://developer.apple.com/documentation/appkit/nsevent/1527943-pressedmousebuttons?language=objc
    NSUInteger pressedBtns = NSEvent.pressedMouseButtons;
    if (pressedBtns & 1)
        mods |= PW_MOD_LEFT_BUTTON;
    if (pressedBtns & 2)
        mods |= PW_MOD_RIGHT_BUTTON;
    if (pressedBtns & 4)
        mods |= PW_MOD_MIDDLE_BUTTON;

    return mods;
}

PWEvent pwTranslateMouseEvent(CplugWindow* pw, NSEvent* event)
{
    NSPoint point = [event locationInWindow];

    PWEvent e = {
        .gui                            = pw->gui,
        .mouse.x                        = point.x,
        .mouse.y                        = pw.frame.size.height - point.y,
        .mouse.modifiers                = pwTranslateModifierFlags(event),
        .mouse.time_ms                  = (uint32_t)([event timestamp] * 1000),
        .mouse.double_click_interval_ms = (uint32_t)([NSEvent doubleClickInterval] * 1000),
    };

    return e;
}

@implementation CplugWindow

- (void)dealloc
{
    if (trackingArea)
        [trackingArea release];
    [super dealloc];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)viewDidMoveToWindow
{
    NSWindow* window = self.window;

    if (window && gui == NULL)
    {
        NSWindowStyleMask windowStyle = window.styleMask;

        windowStyle |= NSWindowStyleMaskResizable;
        [window setStyleMask:windowStyle];

#ifdef CPLUG_BUILD_AUV2
        // The Audio Unit API has no specification about how a host should ask a plugin about its desired width/height
        // This means we have to resort to 'NSWindowDelegate' trickery to listen for resize changes in the hosts window,
        // and override its behaviour. We do this trickery in the 'windowWillResize' method

        // This trickery however comes into conflict with how other DAWs manage their windows. Steinberg's Cubase and
        // VST3PluginTestHost, Reaper, Logic Pro (Intel & Rosetta), and Ableton 12 (10 doesn't) all register delegates.
        // Setting a delegate here in a VST3 build in Cubase prevents the GUI from showing...
        // Setting a delegate here in Logic running in Rosetta mode will crash the DAW on close.
        if (window.delegate == NULL)
            [window setDelegate:self];
#endif

        // Due to the aforementioned delegate problem, here we set up observers as a fallback.
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

        [center addObserver:self
                   selector:@selector(parentWindowDidResize)
                       name:NSWindowDidResizeNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(parentWindowStartResize)
                       name:NSWindowWillStartLiveResizeNotification
                     object:nil];
        [center addObserver:self
                   selector:@selector(parentWindowEndResize)
                       name:NSWindowDidEndLiveResizeNotification
                     object:nil];

        [center addObserver:self
                   selector:@selector(parentWindowLostKeyboardFocus)
                       name:NSWindowDidResignKeyNotification
                     object:nil];

        [window makeFirstResponder:self];

        gui = pw_create_gui(plugin, self);
        PW_ASSERT(gui);

#ifndef PW_METAL
        CFRunLoopTimerContext context = {};
        context.info                  = self;
        double interval               = 0.016; // 16ms

        timerRef =
            CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + interval, interval, 0, 0, pw_timer_cb, &context);
        PW_ASSERT(timerRef != NULL);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(), timerRef, kCFRunLoopCommonModes);
#endif

        [self setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    }

    [super viewDidMoveToWindow];
}

- (void)removeFromSuperview
{
    // Do your deinit here, not in cplug_destroyGUI, which never gets called in Audio Units!
    // Be prepared for [removeFromSuperview] to be called by a host before cplug_destroyGUI()
#ifndef PW_METAL
    if (timerRef)
    {
        CFRunLoopTimerInvalidate(timerRef);
        CFRelease(timerRef);
        timerRef = NULL;
    }
#endif
    if (gui)
    {
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

        [center removeObserver:self name:NSWindowDidResizeNotification object:nil];
        [center removeObserver:self name:NSWindowWillStartLiveResizeNotification object:nil];
        [center removeObserver:self name:NSWindowDidEndLiveResizeNotification object:nil];
        [center removeObserver:self name:NSWindowDidResignKeyNotification object:nil];

        void* ptr = gui;
        gui       = NULL;
        pw_destroy_gui(ptr);
    }

    [super removeFromSuperview];
}

#ifdef PW_METAL
- (void)drawRect:(NSRect)rect
{
    pw_tick(gui);
}
#endif

- (void)setFrameSize:(NSSize)newSize
{
    // handle host resize
    PW_ASSERT(newSize.width > 0);
    PW_ASSERT(newSize.height > 0);
    PW_ASSERT(plugin != NULL);

    if (gui)
    {
        const PWEvent event = {
            .gui    = gui,
            .type   = PW_EVENT_RESIZE,
            .resize = {
                .width  = newSize.width,
                .height = newSize.height,
            }};
        pw_event(&event);

        checkResizeFlag = false;
    }
    [super setFrameSize:newSize];
}

#ifdef CPLUG_BUILD_AUV2
- (NSSize)windowWillResize:(NSWindow*)sender toSize:(NSSize)frameSize
{
    // Current size
    NSSize windowSize = [sender frame].size;
    NSSize viewSize   = [self frame].size;

    NSSize diff = {windowSize.width - viewSize.width, windowSize.height - viewSize.height};

    NSSize   nextViewSize = {frameSize.width - diff.width, frameSize.height - diff.height};
    uint32_t width        = nextViewSize.width;
    uint32_t height       = nextViewSize.height;
    PW_ASSERT(width > 0);
    PW_ASSERT(height > 0);
    cplug_checkSize(self, &width, &height);
    nextViewSize.width  = width + diff.width;
    nextViewSize.height = height + diff.height;

    return nextViewSize;
}
#endif // CPLUG_BUILD_AUV2

- (void)parentWindowDidResize
{
    if (checkResizeFlag == false)
    {
        uint32_t currentWidth, currentHeight, nextWidth, nextHeight;
        currentWidth = nextWidth = self.frame.size.width;
        currentHeight = nextHeight = self.frame.size.height;

        cplug_checkSize(self, &nextWidth, &nextHeight);

        if (currentWidth != nextWidth || currentHeight != nextHeight)
            cplug_setSize(self, nextWidth, nextHeight);
    }
}

- (void)parentWindowStartResize
{
    NSRect rect            = self.window.frame;
    self->resizeStartFrame = rect;
    self->pwResizeFlags    = PW_FLAG_RESIZE_UNKNOWN;
}

- (void)parentWindowEndResize
{
    self->pwResizeFlags = PW_FLAG_RESIZE_UNKNOWN;
}

- (void)parentWindowLostKeyboardFocus
{
    PWEvent event = {
        .gui  = gui,
        .type = PW_EVENT_KEY_FOCUS_LOST,
    };
    pw_event(&event);
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];

    if (trackingArea != nil)
    {
        [self removeTrackingArea:trackingArea];
        [trackingArea release];
    }

    NSTrackingAreaOptions options =
        (NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect);
    trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds] options:options owner:self userInfo:nil];
    [self addTrackingArea:trackingArea];
}

- (void)mouseEntered:(NSEvent*)event;
{
    if (!isMouseOver)
    {
        PWEvent e = pwTranslateMouseEvent(self, event);
        e.type    = PW_EVENT_MOUSE_ENTER;
        pw_event(&e);
    }
}
- (void)mouseExited:(NSEvent*)event;
{
    if (!isDragging)
    {
        isMouseOver = false;

        PWEvent e = pwTranslateMouseEvent(self, event);
        e.type    = PW_EVENT_MOUSE_EXIT;
        pw_event(&e);
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_MOVE;
    pw_event(&e);
}
- (void)mouseDragged:(NSEvent*)event
{
    isDragging = true;
    PWEvent e  = pwTranslateMouseEvent(self, event);
    e.type     = PW_EVENT_MOUSE_MOVE;
    pw_event(&e);
}
- (void)rightMouseDragged:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_MOVE;
    pw_event(&e);
}
- (void)otherMouseDragged:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_MOVE;
    pw_event(&e);
}

- (void)mouseDown:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_LEFT_DOWN;
    pw_event(&e);
}
- (void)rightMouseDown:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_RIGHT_DOWN;
    pw_event(&e);
}
- (void)otherMouseDown:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);
    e.type    = PW_EVENT_MOUSE_MIDDLE_DOWN;
    pw_event(&e);
}
- (void)mouseUp:(NSEvent*)event
{
    PWEvent e          = pwTranslateMouseEvent(self, event);
    e.type             = PW_EVENT_MOUSE_LEFT_UP;
    e.mouse.modifiers &= ~PW_MOD_LEFT_BUTTON;
    pw_event(&e);

    BOOL isLeftButtonDown = e.mouse.modifiers & PW_MOD_LEFT_BUTTON;
    if (isDragging && !isLeftButtonDown)
    {
        isDragging = false;

        CGSize size = [self bounds].size;
        isMouseOver = e.mouse.x >= 0 && e.mouse.y >= 0 && e.mouse.x < size.width && e.mouse.y < size.height;
        if (!isMouseOver)
        {
            e.type = PW_EVENT_MOUSE_EXIT;
            pw_event(&e);
        }
    }
}
- (void)rightMouseUp:(NSEvent*)event
{
    PWEvent e          = pwTranslateMouseEvent(self, event);
    e.type             = PW_EVENT_MOUSE_RIGHT_UP;
    e.mouse.modifiers &= ~PW_MOD_RIGHT_BUTTON;
    pw_event(&e);
}
- (void)otherMouseUp:(NSEvent*)event
{
    PWEvent e          = pwTranslateMouseEvent(self, event);
    e.type             = PW_EVENT_MOUSE_MIDDLE_UP;
    e.mouse.modifiers &= ~PW_MOD_MIDDLE_BUTTON;
    pw_event(&e);
}

- (void)scrollWheel:(NSEvent*)event
{
    PWEvent e = pwTranslateMouseEvent(self, event);

    if (event.isDirectionInvertedFromDevice)
        e.mouse.modifiers |= PW_MOD_INVERTED_SCROLL;

    if (event.hasPreciseScrollingDeltas)
    {
        e.type    = PW_EVENT_MOUSE_SCROLL_TOUCHPAD;
        e.mouse.x = [event scrollingDeltaX];
        e.mouse.y = [event scrollingDeltaY];
    }
    else
    {
        e.type = PW_EVENT_MOUSE_SCROLL_WHEEL;

        CGEventRef cgevent = [event CGEvent];
        PW_ASSERT(cgevent != NULL);

        e.mouse.x = CGEventGetIntegerValueField(cgevent, kCGScrollWheelEventDeltaAxis2) * 120;
        e.mouse.y = CGEventGetIntegerValueField(cgevent, kCGScrollWheelEventDeltaAxis1) * 120;
    }
    pw_event(&e);
}

- (void)keyDown:(NSEvent*)event
{
    PWEvent e = {.gui = gui};
    _Static_assert(offsetof(PWEvent, key.modifiers) == offsetof(PWEvent, text.modifiers), "");
    e.type            = PW_EVENT_KEY_DOWN;
    e.key.modifiers   = pwTranslateModifierFlags(event);
    e.key.virtual_key = [event keyCode];

    bool consumed = pw_event(&e);

    if (pw_check_keyboard_focus(self))
    {
        e.type = PW_EVENT_TEXT;

        NSString* nsstring = [event characters];

        if ([nsstring length] == 1)
        {
            unichar firstchar = [nsstring characterAtIndex:0];
            // check macos reserved key 0xF700..0xF8FF
            if (firstchar >= NSUpArrowFunctionKey && firstchar <= 0xF8FF)
                return;
        }

        const char* str = [nsstring UTF8String];
        PW_ASSERT(str != NULL);

        strncpy((char*)&e.text.codepoint, str, sizeof(e.text.codepoint));

        if (e.text.modifiers & PW_MOD_KEY_CMD) // not a text event
            return;
        // ASCII DEL (backspace). Not renderable text. Should be handled by virtual key code
        if (e.text.codepoint == 0x7f)
            return;

        if (e.text.codepoint)
            consumed = pw_event(&e) || consumed;
    }

    if (!consumed)
        [super keyDown:event];
}

- (void)keyUp:(NSEvent*)event
{
    PWEvent e = {
        .gui  = gui,
        .type = PW_EVENT_KEY_UP,
    };
    e.key.virtual_key = [event keyCode];
    e.key.modifiers   = pwTranslateModifierFlags(event);

    bool consumed = pw_event((&e));
    if (!consumed)
        [super keyUp:event];
}

// DRAGGING

- (BOOL)wantsPeriodicDraggingUpdates
{
    return YES;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    PW_ASSERT(numDraggedFiles == 0);
    PW_ASSERT(draggedFiles == NULL);
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    NSArray*      files      = [pasteboard propertyListForType:NSFilenamesPboardType];

    if (files)
    {
        NSUInteger numFiles = [files count];
        if (numFiles)
        {
            const size_t arrSize   = sizeof(char*) * numFiles;
            const size_t stride    = 1024;
            const size_t allocSize = arrSize + numFiles * stride;

            char** pathArr = PW_MALLOC(allocSize);
            char*  path    = (char*)(pathArr + numFiles);
            memset(pathArr, 0, allocSize);

            for (NSUInteger i = 0; i < numFiles; i++)
            {
                NSString* obj = [files objectAtIndex:i];
                if (!obj)
                    break;

                const char* str = [obj UTF8String];
                strncpy(path, str, stride - 1);

                pathArr[i] = path;
                numDraggedFiles++;
                path += stride;
            }

            draggedFiles = pathArr;
        }
    }

    if (draggedFiles)
    {
        NSPoint point = [sender draggingLocation];

        const PWEvent event = {
            .gui  = gui,
            .type = PW_EVENT_FILE_ENTER,
            .file =
                {
                    .x         = point.x,
                    .y         = [self frame].size.height - point.y,
                    .num_paths = numDraggedFiles,
                    .paths     = (const char* const*)draggedFiles,
                },
        };

        bool interested = pw_event(&event);
        return interested ? NSDragOperationGeneric : NSDragOperationNone;
    }

    return NSDragOperationNone;
}

// https://developer.apple.com/documentation/appkit/nsdraggingdestination/1415998-draggingupdated
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender
{
    if (!numDraggedFiles)
        return NSDragOperationNone;

    PW_ASSERT(draggedFiles);

    NSPoint       point = [sender draggingLocation];
    const PWEvent event = {
        .gui  = gui,
        .type = PW_EVENT_FILE_MOVE,
        .file =
            {
                .x         = point.x,
                .y         = [self frame].size.height - point.y,
                .num_paths = numDraggedFiles,
                .paths     = (const char* const*)draggedFiles,
            },
    };

    bool interested = pw_event(&event);
    return interested ? NSDragOperationGeneric : NSDragOperationNone;
}

- (void)draggingExited:(nullable id<NSDraggingInfo>)sender
{
    PW_ASSERT(numDraggedFiles);
    PW_ASSERT(draggedFiles);

    pw_event(&(PWEvent){.gui = gui, .type = PW_EVENT_FILE_EXIT});

    if (draggedFiles)
    {
        PW_FREE(draggedFiles);
        numDraggedFiles = 0;
        draggedFiles    = NULL;
    }
}

// https://developer.apple.com/documentation/appkit/nsdraggingdestination/1415970-performdragoperation
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    PW_ASSERT(numDraggedFiles);
    PW_ASSERT(draggedFiles);

    NSPoint point = [sender draggingLocation];

    const PWEvent event = {
        .gui  = gui,
        .type = PW_EVENT_FILE_DROP,
        .file =
            {
                .x         = point.x,
                .y         = [self frame].size.height - point.y,
                .num_paths = numDraggedFiles,
                .paths     = (const char* const*)draggedFiles,
            },
    };

    bool ok = pw_event(&event);
    if (draggedFiles)
    {
        PW_FREE(draggedFiles);
        numDraggedFiles = 0;
        draggedFiles    = NULL;
    }
    return ok;
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context
{
    return NSDragOperationCopy;
}

@end // CplugWindow

void pw_set_clipboard_text(void* gui, const char* text)
{
    NSArray* types = [NSArray arrayWithObjects:NSStringPboardType, nil];

    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard declareTypes:types owner:nil];
    [pasteboard setString:[NSString stringWithUTF8String:text] forType:NSStringPboardType];
}

bool pw_get_clipboard_text(void* gui, char** ptext, size_t* len)
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString*     nsstring   = [pasteboard stringForType:NSPasteboardTypeString];
    if (nsstring == NULL)
        return false;

    const char* utf8 = [nsstring UTF8String];

    *len   = strlen(utf8);
    *ptext = PW_MALLOC(*len + 1);
    memcpy(*ptext, utf8, *len);
    (*ptext)[*len] = 0;
    return true;
}

void pw_free_clipboard_text(char* ptr)
{
    PW_ASSERT(ptr != NULL);
    PW_FREE(ptr);
}

void pw_set_mouse_cursor(void* gui, enum PWCursorType type)
{
    NSCursor* cursor = NULL;

    switch (type)
    {
    case PW_CURSOR_ARROW:
        cursor = NSCursor.arrowCursor;
        break;
    case PW_CURSOR_IBEAM:
        cursor = NSCursor.IBeamCursor;
        break;
    case PW_CURSOR_NO:
        cursor = NSCursor.operationNotAllowedCursor;
        break;
    case PW_CURSOR_CROSS:
        cursor = NSCursor.crosshairCursor;
        break;

    case PW_CURSOR_ARROW_DRAG:
        cursor = NSCursor.dragCopyCursor;
        break;
    case PW_CURSOR_HAND_POINT:
        cursor = NSCursor.pointingHandCursor;
        break;
    case PW_CURSOR_HAND_DRAGGABLE:
        cursor = NSCursor.openHandCursor;
        break;
    case PW_CURSOR_HAND_DRAGGING:
        cursor = NSCursor.closedHandCursor;
        break;

    // https://stackoverflow.com/a/46635398
    // https://stackoverflow.com/a/27294770
    // Appears to be available from at least 10.10 (Yosemite)
    case PW_CURSOR_RESIZE_WE:
        cursor = [NSCursor performSelector:@selector(_windowResizeEastWestCursor)];
        break;
    case PW_CURSOR_RESIZE_NS:
        cursor = [NSCursor performSelector:@selector(_windowResizeNorthSouthCursor)];
        break;
    case PW_CURSOR_RESIZE_NESW:
        cursor = [NSCursor performSelector:@selector(_windowResizeNorthEastSouthWestCursor)];
        break;
    case PW_CURSOR_RESIZE_NWSE:
        cursor = [NSCursor performSelector:@selector(_windowResizeNorthWestSouthEastCursor)];
        break;
    default:
        cursor = [NSCursor arrowCursor];
        break;
    }
    [cursor set];
}

void pw_get_keyboard_focus(void* _pw)
{
    CplugWindow* pw = (CplugWindow*)_pw;
    if (pw.window && pw.window.keyWindow == false)
        [pw.window makeKeyWindow];
}

bool pw_check_keyboard_focus(const void* _pw)
{
    CplugWindow* pw       = (CplugWindow*)_pw;
    bool         hasFocus = false;
    // https://developer.apple.com/documentation/appkit/nswindow/iskeywindow?language=objc
    if (pw.window)
        hasFocus = pw.window.keyWindow;
    return hasFocus;
}

void pw_release_keyboard_focus(void* _pw)
{
    CplugWindow* pw = (CplugWindow*)_pw;
    if (pw.window && pw.window.keyWindow)
        [pw.window resignKeyWindow];
}

void pw_beep() { NSBeep(); }

void* cplug_createGUI(void* userPlugin)
{
    _Static_assert(offsetof(struct PWGetInfo, init_size) > offsetof(struct PWGetInfo, type), "");
    struct PWGetInfo info;
    info.type             = PW_INFO_INIT_SIZE;
    info.init_size.plugin = userPlugin;
    info.init_size.width  = 0;
    info.init_size.height = 0;
    pw_get_info(&info);
    // Did you forget to initialise the size of your GUI?
    PW_ASSERT(info.init_size.width > 0);
    PW_ASSERT(info.init_size.height > 0);

    NSRect frame;
    frame.origin.x    = 0;
    frame.origin.y    = 0;
    frame.size.width  = info.init_size.width;
    frame.size.height = info.init_size.height;

    CplugWindow* pw = [[CplugWindow alloc] initWithFrame:frame];

    pw->gui              = NULL;
    pw->plugin           = userPlugin;
    pw->resizeStartFrame = (NSRect){{0, 0}, {0, 0}};
    pw->pwResizeFlags    = 0;
    pw->checkResizeFlag  = false;
    pw->isDragging       = false;
    pw->isMouseOver      = false;
    pw->numDraggedFiles  = 0;
    pw->draggedFiles     = NULL;
    pw->trackingArea     = NULL;

    // https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/DragandDrop/DragandDrop.html
    [pw registerForDraggedTypes:[NSArray arrayWithObjects:NSFilenamesPboardType, nil]];

    pw.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    pw.layer.opaque              = YES;

#ifdef PW_METAL
    pw.device                  = MTLCreateSystemDefaultDevice();
    pw.colorPixelFormat        = MTLPixelFormatBGRA8Unorm;
    pw.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    setenv("MTL_HUD_ENABLED", "1", 1);
#else
    pw->timerRef = NULL;
#endif

    return (void*)pw;
}

// NOTE: VST3 & CLAP only. When building with AUv2, do deinit in removeFromSuperview.
void cplug_destroyGUI(void* userGUI)
{
    CplugWindow* pw = (CplugWindow*)userGUI;
    if (pw.superview)
        [pw removeFromSuperview];
    [pw release];
}

void cplug_setParent(void* userGUI, void* view)
{
    CplugWindow* pw = (CplugWindow*)userGUI;
    if (pw.superview)
        [pw removeFromSuperview];
    if (view)
        [(NSView*)view addSubview:pw];
}

void cplug_setVisible(void* userGUI, bool visible)
{
    CplugWindow* pw = (CplugWindow*)userGUI;
    [pw setHidden:(visible ? NO : YES)];
}

void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    CplugWindow* pw = (CplugWindow*)userGUI;
    *width          = (uint32_t)pw.frame.size.width;
    *height         = (uint32_t)pw.frame.size.height;
}

void cplug_checkSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    CplugWindow* pw = (CplugWindow*)userGUI;

    if (pw.window.inLiveResize)
    {
        NSRect rect = pw.window.frame;
        if (rect.origin.x != pw->resizeStartFrame.origin.x)
            pw->pwResizeFlags |= PW_FLAG_RESIZE_LEFT;
        // You may blindly assume X:0,Y:0 to be the top right of your screen, but Apple is here to keep you on your toes
        // One of the bizarre quirks of Cocoa is they consider the origin point of X:0,Y:0 to the bottom left.
        if (rect.origin.y == pw->resizeStartFrame.origin.y && rect.size.height != pw->resizeStartFrame.size.height)
            pw->pwResizeFlags |= PW_FLAG_RESIZE_TOP;
        if (rect.origin.x == pw->resizeStartFrame.origin.x && rect.size.width != pw->resizeStartFrame.size.width)
            pw->pwResizeFlags |= PW_FLAG_RESIZE_RIGHT;
        if (rect.origin.y != pw->resizeStartFrame.origin.y)
            pw->pwResizeFlags |= PW_FLAG_RESIZE_BOTTOM;
    }

    pwAssertValidResizeFlags(pw->pwResizeFlags);
    pw->checkResizeFlag = true;

    PWGetInfo info = {
        .type                     = PW_INFO_CONSTRAIN_SIZE,
        .constrain_size.gui       = pw->gui,
        .constrain_size.width     = *width,
        .constrain_size.height    = *height,
        .constrain_size.direction = PW_RESIZE_UNKNOWN,
    };
    if (pw.window.inLiveResize)
    {
        info.constrain_size.direction = pwTranslateResizeFlags(pw->pwResizeFlags);
    }
    pw_get_info(&info);
    *width  = info.constrain_size.width;
    *height = info.constrain_size.height;
}

bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height)
{
    CplugWindow* pw = (CplugWindow*)userGUI;

    NSSize size;
    size.width  = width;
    size.height = height;
    [pw setFrameSize:size];
    return true;
}

void cplug_setScaleFactor(void* userGUI, float scale)
{
    // ignore. handle in 'viewDidChangeBackingProperties'
}

void pw_get_screen_size(uint32_t* width, uint32_t* height)
{
    NSRect rect = [[NSScreen mainScreen] frame];
    *width      = rect.size.width;
    *height     = rect.size.height;
}

float pw_get_dpi(void* _pw)
{
    PW_ASSERT(_pw);
    CplugWindow* pw = (CplugWindow*)_pw;
    PW_ASSERT(pw.window);
    return [pw.window screen].backingScaleFactor;
}

// It's the same ptr!
void* pw_get_native_window(void* _pw) { return _pw; }

#ifdef PW_METAL
void* pw_get_metal_device(void* _pw)
{
    CplugWindow*  pw     = (CplugWindow*)_pw;
    id<MTLDevice> device = [pw device];
    PW_ASSERT(device);
    return device;
}

void* pw_get_metal_drawable(void* _pw)
{
    CplugWindow*        pw       = (CplugWindow*)_pw;
    id<CAMetalDrawable> drawable = [pw currentDrawable];
    PW_ASSERT(drawable);
    return drawable;
}

void* pw_get_metal_depth_stencil_texture(void* _pw)
{
    CplugWindow*   pw           = (CplugWindow*)_pw;
    id<MTLTexture> depthStencil = [pw depthStencilTexture];
    PW_ASSERT(depthStencil);
    return depthStencil;
}
void* pw_get_metal_msaa_tex(void* _pw)
{
    CplugWindow*   pw   = (CplugWindow*)_pw;
    id<MTLTexture> msaa = [pw multisampleColorTexture];
    PW_ASSERT(msaa);
    return msaa;
}
#endif

void pw_drag_files(void* _pw, const char* const* paths, uint32_t num_paths)
{
    // https://developer.apple.com/documentation/appkit/nsview/begindraggingsession(with:event:source:)?language=objc
    // https://developer.apple.com/documentation/appkit/nsdraggingitem?language=objc
    // https://developer.apple.com/documentation/appkit/nsdraggingitem/setdraggingframe(_:contents:)?language=objc

    CplugWindow* pw = _pw;

    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    NSEvent* event = [[pw window] currentEvent];
    // Remember to read the instructions for pw_drag_files!
    PW_ASSERT(event.type == NSEventTypeLeftMouseDown);

    NSMutableArray<NSDraggingItem*>* items = [[NSMutableArray alloc] init];
    for (uint32_t i = 0; i < num_paths; i++)
    {
        NSString*       path = [NSString stringWithUTF8String:paths[i]];
        NSURL*          url  = [NSURL fileURLWithPath:path];
        NSImage*        img  = [[NSWorkspace sharedWorkspace] iconForFile:path];
        NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];

        NSRect frame;
        frame.origin = [event locationInWindow];
        frame.size   = img.size;

        frame.origin.x -= frame.size.width * 0.5f;
        frame.origin.y -= frame.size.height * 0.5f;

        [item setDraggingFrame:frame contents:img];
        [items addObject:item];
    }

    [pw beginDraggingSessionWithItems:items event:event source:pw];
    [pool release];
}

@interface PWSavePanelDelegate : NSObject <NSComboBoxDelegate>
{
@public
    NSSavePanel* savePanel;
    NSComboBox*  comboBox;
    const char** extensions;
    uint32_t     numExtensions;
}
@end

@implementation PWSavePanelDelegate

- (PWSavePanelDelegate*)init
{
    savePanel     = NULL;
    comboBox      = NULL;
    extensions    = NULL;
    numExtensions = 0;

    return [super init];
}

- (void)comboBoxSelectionDidChange:(NSNotification*)notification
{
    if (savePanel && comboBox && extensions && numExtensions)
    {
        int idx = [comboBox indexOfSelectedItem];
        if (idx < numExtensions)
        {
            const char* ext = extensions[idx];

            NSMutableArray<NSString*>* ext_arr = [[NSMutableArray<NSString*> alloc] init];
            NSString*                  ext_str = [NSString stringWithUTF8String:ext];
            [ext_arr addObject:ext_str];
            [savePanel setAllowedFileTypes:ext_arr];

            [ext_arr release];
            [ext_str release];
        }
    }
}

@end

// Thank you Andrew Belt!
// https://github.com/AndrewBelt/osdialog/blob/master/osdialog_mac.m
bool pw_choose_file(const PWChooseFileArgs* args)
{
    // One may assume that file picker modal would block whatever thread you run it on until the modal is closed.
    // This behaviour is true of Windows, but due to undocumented Apple magic this is incorrect on macOS.
    // Opening the modal will noticably lock the main thread for 200ms or more before opening the modal.
    // To avoid this, you may try opening the modal on another thread, however this will crash your program.
    // It appears to be an undocumented requirement that you must open the modal on the main thread.
    // By contrast Windows doesn't have this locking problem, since you can spin up a seperate thread to run it on.
    // From what is known, macOS 10.15+ run the modal in a seperate process.
    // https://developer.apple.com/documentation/appkit/nssavepanel?language=objc
    // All the stack memory from before the modal starts running seems to persist until after the modal is closed.
    // Apple is using memory/event queue tricks here which leads me to fear this code will break in a macOS update.

    // Test valid combinations of arguments
    PW_ASSERT(args->pw);
    PW_ASSERT(args->callback);
    PW_ASSERT((args->is_folder ? (args->is_save == false) : true));
    PW_ASSERT((args->is_folder ? (args->num_extensions == 0) : true));
    PW_ASSERT(args->multiselect ? (args->is_save == false) : true);
    PW_ASSERT((args->num_extensions ? (args->extension_names != NULL) : true));
    PW_ASSERT((args->num_extensions ? (args->extension_types != NULL) : true));

    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    NSWindow* keyWindow = [[NSApplication sharedApplication] keyWindow];

    NSSavePanel*         panel    = NULL;
    PWSavePanelDelegate* delegate = NULL;

    if (args->is_save)
    {
        panel = [NSSavePanel savePanel];
        [panel setCanSelectHiddenExtension:YES];
        [panel setCanCreateDirectories:YES];
    }
    else
    {
        NSOpenPanel* opanel = [NSOpenPanel openPanel];

        [opanel setCanCreateDirectories:YES];
        [opanel setCanChooseDirectories:args->is_folder];
        [opanel setCanChooseFiles:!args->is_folder];
        [opanel setAllowsMultipleSelection:args->multiselect];

        panel = opanel;
    }
    [panel setLevel:CGShieldingWindowLevel()];

    if (args->folder)
    {
        NSString* dir_str = [NSString stringWithUTF8String:args->folder];
        NSURL*    dir_url = [NSURL fileURLWithPath:dir_str];
        [panel setDirectoryURL:dir_url]; // owned by panel
    }

    if (args->filename)
    {
        NSString* name_str = [NSString stringWithUTF8String:args->filename];
        [panel setNameFieldStringValue:name_str];
    }

    if (args->is_save)
    {
        // https://developer.apple.com/forums/thread/672292
        // https://qiita.com/hanamiju/items/d10524e8650ae171fa2e
        // https://stackoverflow.com/questions/71269976/how-to-create-a-custom-nsview-for-nssavepanel-in-cocoa-macos-objective-c

        double       width         = 300;
        NSView*      accessoryView = [[NSView alloc] initWithFrame:CGRectMake(0, 0, width, 40)];
        NSComboBox*  combo         = [[NSComboBox alloc] initWithFrame:CGRectMake(width - 80, 10, 80, 20)];
        NSTextField* label         = [[NSTextField alloc] init];

        delegate = [[PWSavePanelDelegate alloc] init];

        delegate->savePanel     = panel;
        delegate->comboBox      = combo;
        delegate->extensions    = args->extension_types;
        delegate->numExtensions = args->num_extensions;

        [combo setDelegate:delegate];
        combo.editable = NO;

        for (int i = 0; i < args->num_extensions; i++)
        {
            const char* name = args->extension_names[i];
            [combo addItemWithObjectValue:[NSString stringWithUTF8String:name]];
        }
        [combo selectItemAtIndex:0];

        [label setStringValue:@"File type"];
        label.bordered  = NO;
        label.editable  = NO;
        label.textColor = NSColor.secondaryLabelColor;
        label.font      = [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
        label.alignment = NSTextAlignmentRight;

        combo.translatesAutoresizingMaskIntoConstraints = NO;
        label.translatesAutoresizingMaskIntoConstraints = NO;

        [accessoryView addSubview:combo];
        [accessoryView addSubview:label];
        [panel setAccessoryView:accessoryView];

        [NSLayoutConstraint activateConstraints:@[
            [label.bottomAnchor constraintEqualToAnchor:accessoryView.bottomAnchor constant:-12],
            [label.widthAnchor constraintEqualToConstant:64.0],
            [label.leadingAnchor constraintEqualToAnchor:accessoryView.leadingAnchor constant:0.0],

            [combo.topAnchor constraintEqualToAnchor:accessoryView.topAnchor constant:8.0],
            [combo.leadingAnchor constraintEqualToAnchor:label.trailingAnchor constant:8.0],
            [combo.bottomAnchor constraintEqualToAnchor:accessoryView.bottomAnchor constant:-8.0],
            [combo.trailingAnchor constraintEqualToAnchor:accessoryView.trailingAnchor constant:-20.0],
        ]];
    }
    else if (args->num_extensions) // !args->is_save. NSOpenPanel
    {
        NSMutableArray<NSString*>* array = [[NSMutableArray<NSString*> alloc] init];

        for (int i = 0; i < args->num_extensions; i++)
        {
            const char* str = args->extension_types[i];
            NSString*   obj = [NSString stringWithUTF8String:str];
            [array addObject:obj];
        }
        [panel setAllowedFileTypes:array];
    }

    NSModalResponse response = [panel runModal];

    char**   paths    = NULL;
    uint32_t numPaths = 0;

    if (response == NSModalResponseOK)
    {
        if (args->multiselect)
        {
            NSOpenPanel* opanel = (NSOpenPanel*)panel;

            NSArray<NSURL*>* urls = [opanel URLs];
            numPaths              = [urls count];

            const size_t arrSize   = (sizeof(char*) * numPaths);
            const size_t stride    = 1024;
            const size_t allocSize = arrSize + stride * numPaths;

            paths = PW_MALLOC(allocSize);
            memset(paths, 0, allocSize);
            char* path = (char*)(paths + numPaths);

            for (NSInteger i = 0; i < numPaths; i++)
            {
                NSURL* url_obj = [urls objectAtIndex:i];
                if (!url_obj)
                    break;
                NSString* str_obj = [url_obj path];
                if (!str_obj)
                    break;

                const char* str = [str_obj UTF8String];
                strncpy(path, str, stride - 1);
                paths[i] = path;
                numPaths++;
                path += stride;
            }
        }
        else
        {
            NSURL*    url_obj = [panel URL];
            NSString* str_obj = [url_obj path];

            const char* str = [str_obj UTF8String];
            size_t      len = strlen(str);

            numPaths = 1;
            paths    = PW_MALLOC((sizeof(char**) + len + 1));
            paths[0] = (char*)(paths + 1);
            memcpy(paths[0], str, len);
            paths[0][len] = 0;
        }
    }

    [keyWindow makeKeyAndOrderFront:nil];

    [pool release];

    if (args->callback)
        args->callback(args->callback_data, (const char* const*)paths, numPaths);

    if (paths)
    {
        PW_FREE(paths);
    }

    return true;
}

// AUv2 only
#ifdef CPLUG_BUILD_AUV2
#include <AudioToolbox/AUCocoaUIView.h>
#include <AudioToolbox/AudioUnit.h>

@interface CPLUG_AUV2_VIEW_CLASS : NSObject <AUCocoaUIBase>
- (NSView*)uiViewForAudioUnit:(AudioUnit)audioUnit withSize:(NSSize)preferredSize;
- (unsigned)interfaceVersion;
@end

@implementation CPLUG_AUV2_VIEW_CLASS

- (NSView*)uiViewForAudioUnit:(AudioUnit)inUnit withSize:(NSSize)size
{
    cplug_log("uiViewForAudioUnit => %p %f %f", inUnit, size.width, size.height);
    void*  userPlugin = NULL;
    UInt32 dataSize   = 8;

    AudioUnitGetProperty(inUnit, kAudioUnitProperty_UserPlugin, kAudioUnitScope_Global, 0, &userPlugin, &dataSize);
    CPLUG_LOG_ASSERT_RETURN(userPlugin != NULL, NULL);

    return (NSView*)cplug_createGUI(userPlugin);
}

- (unsigned)interfaceVersion
{
    return 0;
}

@end
#endif // CPLUG_BUILD_AUV2
