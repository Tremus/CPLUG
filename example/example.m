#include "example.c"

#if CPLUG_WANT_GUI
#import <Cocoa/Cocoa.h>

@interface MyGUIWrapper : NSView
{
@public
    MyGUI             gui;
    CFRunLoopTimerRef timerRef;
}

@end

@implementation MyGUIWrapper

- (void)dealloc
{
    cplug_log("NSView - dealloc");
    // Do your deinit here, not in cplug_destroyGUI
    CFRunLoopTimerInvalidate(timerRef);
    CFRelease(timerRef);

    gui.plugin->gui = NULL;

    if (gui.img)
        free(gui.img);

    [super dealloc];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

- (void)viewDidMoveToWindow
{
    cplug_log("NSView - viewDidMoveToWindow");
    NSWindow* window = [self window];
    // init graphics API here
#if CPLUG_GUI_RESIZABLE
    NSWindowStyleMask windowStyle = [window styleMask];
    windowStyle                   |= NSWindowStyleMaskResizable;
    [window setStyleMask:windowStyle];
#endif // CPLUG_GUI_RESIZABLE
}

- (void)removeFromSuperview
{
    cplug_log("NSView - removeFromSuperview");
    gui.window = NULL;
    // deinit graphics API here
    [super removeFromSuperview];
}

- (void)setFrameSize:(NSSize)newSize
{
    // handle host resize
    cplug_log("NSView - setFrameSize");

    uint32_t width  = [self frame].size.width;
    uint32_t height = [self frame].size.height;

    gui.width  = width;
    gui.height = height;
    gui.img    = (uint32_t*)realloc(gui.img, width * height * sizeof(*gui.img));

    [super setFrameSize:newSize];
}

- (void)viewWillAppear
{
    cplug_log("NSView - viewWillAppear");
    // handle visible = true
}

- (void)viewDidDisappear
{
    cplug_log("NSView - viewDidDisappear");
    // handle visible = false
}

- (void)viewDidChangeBackingProperties:(NSNotification*)pNotification
{
    cplug_log("NSView - viewDidChangeBackingProperties");
    NSWindow* window      = self.window;
    CGFloat   scaleFactor = window.backingScaleFactor;
    // handle scale factor
}

- (void)drawRect:(NSRect)dirtyRect
{
    cplug_log("NSView - drawRect");
    drawGUI(&gui);
    const unsigned char* const _Nullable data = (unsigned char*)gui.img;
    NSDrawBitmap(self.bounds, gui.width, gui.height, 8, 3, 32, 4 * gui.width, NO, NO, NSDeviceRGBColorSpace, &data);
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseDown(&gui, cursor.x, self.bounds.size.height - cursor.y);
}

- (void)mouseUp:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseUp(&gui);
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseMove(&gui, cursor.x, self.bounds.size.height - cursor.y);
    if (gui.mouseDragging)
        [self setNeedsDisplayInRect:self.bounds];
}

@end

void timer_cb(CFRunLoopTimerRef timer, void* info)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)info;
    if (tickGUI(&wrapper->gui))
        [wrapper setNeedsDisplayInRect:wrapper.bounds];
}

void* cplug_createGUI(void* userPlugin)
{
    NSRect frame;
    frame.origin.x    = 0;
    frame.origin.y    = 0;
    frame.size.width  = GUI_DEFAULT_WIDTH;
    frame.size.height = GUI_DEFAULT_HEIGHT;

    MyGUIWrapper* wrapper = [[MyGUIWrapper alloc] initWithFrame:frame];
    CPLUG_LOG_ASSERT_RETURN(wrapper != NULL, NULL);

    wrapper.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    wrapper.layer.opaque              = YES;

    MyGUI* gui = &wrapper->gui;
    memset(gui, 0, sizeof(*gui));

    gui->plugin      = (MyPlugin*)userPlugin;
    gui->plugin->gui = gui;

    gui->window = wrapper;
    gui->width  = GUI_DEFAULT_WIDTH;
    gui->height = GUI_DEFAULT_HEIGHT;
    gui->img    = (uint32_t*)realloc(gui->img, gui->width * gui->height * sizeof(*gui->img));

    CFRunLoopTimerContext context = {};
    context.info                  = wrapper;
    double interval               = 0.01; // 10ms

    wrapper->timerRef =
        CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + interval, interval, 0, 0, timer_cb, &context);
    assert(wrapper->timerRef != NULL);

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), wrapper->timerRef, kCFRunLoopCommonModes);

    return wrapper;
}

// NOTE: VST3 & CLAP only. When building with AUv2, do deinit in dealloc.
void cplug_destroyGUI(void* userGUI) { [(MyGUIWrapper*)userGUI release]; }

void cplug_setParent(void* userGUI, void* view)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;
    if (wrapper.superview != NULL)
        [wrapper removeFromSuperview];
    if (view != NULL)
    {
        memcpy(
            wrapper->gui.plugin->paramValuesMain,
            wrapper->gui.plugin->paramValuesAudio,
            sizeof(wrapper->gui.plugin->paramValuesMain));
        [(NSView*)view addSubview:wrapper];
    }
}

void cplug_setVisible(void* userGUI, bool visible) { [(MyGUIWrapper*)userGUI setHidden:(visible ? NO : YES)]; }

void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;
    *width                = (uint32_t)wrapper.frame.size.width;
    *height               = (uint32_t)wrapper.frame.size.height;
}

bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;

    NSSize size;
    size.width  = width;
    size.height = height;
    [wrapper setFrameSize:size];
    return true;
}

void cplug_setScaleFactor(void* userGUI, float scale)
{
    // ignore. handle in 'viewDidChangeBackingProperties'
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
#endif // CPLUG_WANT_GUI