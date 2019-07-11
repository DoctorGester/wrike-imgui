#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <mach/mach_time.h>
#include <sys/socket.h>
#include <netinet/in.h>


#import "common.h"
#import "platform.h"

#include "metal.mm"

@interface App_Delegate : NSObject<NSApplicationDelegate>
@end

@interface Window_Delegate : NSObject<NSWindowDelegate>
@end

@interface View_Delegate : NSObject<MTKViewDelegate>

@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;

@end

@interface App_View : MTKView
{
    NSTrackingArea* tracking_area;
}
@end

static NSWindow* window;
static Window_Delegate* window_delegate;
static App_Delegate* app_delegate;
static App_View* app_view;

static View_Delegate* view_delegate;
static id<MTLDevice> device;
static MTKTextureLoader* texture_loader;

static u64 timer_frequency;
static NSString* private_token;

static CFAbsoluteTime app_time = 0.0;
static NSCursor *mouse_cursors[ImGuiMouseCursor_COUNT] = {0};
static bool is_mouse_cursor = false;

@implementation Window_Delegate
@end

// Undocumented methods for creating cursors.
@interface NSCursor ()
+ (id)_windowResizeNorthWestSouthEastCursor;
+ (id)_windowResizeNorthEastSouthWestCursor;
+ (id)_windowResizeNorthSouthCursor;
+ (id)_windowResizeEastWestCursor;
@end

@implementation View_Delegate

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {

}

- (void)update_mouse_cursor {
    ImGuiIO &io = ImGui::GetIO();

    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) {
        return;
    }

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None) {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        if (!is_mouse_cursor) {
            is_mouse_cursor = true;
            [NSCursor hide];
        }
    } else {
        // Show OS mouse cursor
        [mouse_cursors[mouse_cursors[imgui_cursor] ? imgui_cursor : ImGuiMouseCursor_Arrow] set];
        if (is_mouse_cursor) {
            is_mouse_cursor = false;
            [NSCursor unhide];
        }
    }
}

- (void)start_new_imgui_frame:(nonnull NSView*)view {
    ImGuiIO &io = ImGui::GetIO();
    const float dpi = platform_get_pixel_ratio();
    io.DisplaySize = ImVec2((float) view.bounds.size.width * dpi, (float) view.bounds.size.height * dpi);
    io.DisplayFramebufferScale = ImVec2(dpi, dpi);

    // Setup time step
    if (app_time == 0.0) {
        app_time = CFAbsoluteTimeGetCurrent();
    }

    CFAbsoluteTime current_time = CFAbsoluteTimeGetCurrent();
    io.DeltaTime = current_time - app_time;
    app_time = current_time;

    [self update_mouse_cursor];
}

- (void)drawInMTKView:(nonnull MTKView *)view {
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize.x = view.bounds.size.width;
    io.DisplaySize.y = view.bounds.size.height;

    CGFloat framebufferScale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
    io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
    io.DeltaTime = 1 / float(view.preferredFramesPerSecond ?: 60);

    id<MTLCommandBuffer> cmd_buffer = [self.commandQueue commandBuffer];

    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;

    if (pass != nil) {
        id<MTLRenderCommandEncoder> encoder = [cmd_buffer renderCommandEncoderWithDescriptor:pass];

        [encoder pushDebugGroup:@"wrike-imgui"];

        [self start_new_imgui_frame:view];

        loop();

        metal_render_frame(ImGui::GetDrawData(), pass, cmd_buffer, encoder);

        [encoder popDebugGroup];
        [encoder endEncoding];

        [cmd_buffer presentDrawable:view.currentDrawable];
    }

    [cmd_buffer commit];
}

@end

static bool handle_os_event(NSEvent *event, NSView *view);

@implementation App_View
- (void)updateTrackingAreas{
    if (tracking_area != nil) {
        [self removeTrackingArea:tracking_area];
        tracking_area = nil;
    }
    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
    NSTrackingActiveInKeyWindow |
    NSTrackingEnabledDuringMouseDrag |
    NSTrackingCursorUpdate |
    NSTrackingInVisibleRect |
    NSTrackingAssumeInside;
    tracking_area = [[NSTrackingArea alloc] initWithRect:[self bounds] options:options owner:self userInfo:nil];
    [self addTrackingArea:tracking_area];
    [super updateTrackingAreas];
}

- (void)mouseMoved:(NSEvent*)event {
    handle_os_event(event, self);
}

- (void)mouseDown:(NSEvent *)event {
    handle_os_event(event, self);
}

- (void)mouseUp:(NSEvent *)event {
    handle_os_event(event, self);
}

- (void)mouseDragged:(NSEvent *)event {
    handle_os_event(event, self);
}

- (void)scrollWheel:(NSEvent *)event {
    handle_os_event(event, self);
}
@end

@implementation App_Delegate
- (void)applicationDidFinishLaunching:(NSNotification*)aNotification {
    const int window_width = 1366;
    const int window_height = 768;
    const char* window_title = "Wrike";

    const NSWindowStyleMask style =
            NSWindowStyleMaskTitled |
            NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable |
            NSWindowStyleMaskResizable;
    NSRect window_rect = NSMakeRect(0, 0, window_width, window_height);

    window = [[NSWindow alloc]
            initWithContentRect:window_rect
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];
    window.title = [NSString stringWithUTF8String:window_title];
    window.acceptsMouseMovedEvents = YES;
    window.restorable = YES;
    window_delegate = [Window_Delegate new];
    window.delegate = window_delegate;

    metal_program_init(device);

    view_delegate = [View_Delegate new];
    view_delegate.commandQueue = [device newCommandQueue];

    app_view = [App_View new];
    [app_view updateTrackingAreas];
    app_view.preferredFramesPerSecond = 60;
    app_view.delegate = view_delegate;
    app_view.device = device;
    app_view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    app_view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    app_view.sampleCount = 1;

    window.contentView = app_view;
    [window makeFirstResponder:app_view];

    app_view.layer.magnificationFilter = kCAFilterNearest;

    [window center];
    [window makeKeyAndOrderFront:nil];

    // If we want to receive key events, we either need to be in the responder chain of the key view,
    // or else we can install a local monitor. The consequence of this heavy-handed approach is that
    // we receive events for all controls, not just Dear ImGui widgets. If we had native controls in our
    // window, we'd want to be much more careful than just ingesting the complete event stream, though we
    // do make an effort to be good citizens by passing along events when Dear ImGui doesn't want to capture.
    NSEventMask event_mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged | NSEventTypeScrollWheel;
    [NSEvent addLocalMonitorForEventsMatchingMask:event_mask handler:^NSEvent * _Nullable(NSEvent *event) {
        BOOL wants_to_capture = handle_os_event(event, app_view);

        if (event.type == NSEventTypeKeyDown && wants_to_capture) {
            return nil;
        } else {
            return event;
        }
    }];

    [app_view updateTrackingAreas];

    [self set_up_keyboard_keys];
    [self set_up_mouse_cursors];
}

- (void)set_up_keyboard_keys {
    ImGuiIO &io = ImGui::GetIO();

    // Setup back-end capabilities flags
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;         // We can honor GetMouseCursor() values (optional)

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    const int offset_for_function_keys = 256 - 0xF700;
    io.KeyMap[ImGuiKey_Tab] = '\t';
    io.KeyMap[ImGuiKey_LeftArrow] = NSLeftArrowFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_RightArrow] = NSRightArrowFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_UpArrow] = NSUpArrowFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_DownArrow] = NSDownArrowFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_PageUp] = NSPageUpFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_PageDown] = NSPageDownFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_Home] = NSHomeFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_End] = NSEndFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_Insert] = NSInsertFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_Delete] = NSDeleteFunctionKey + offset_for_function_keys;
    io.KeyMap[ImGuiKey_Backspace] = 127;
    io.KeyMap[ImGuiKey_Space] = 32;
    io.KeyMap[ImGuiKey_Enter] = 13;
    io.KeyMap[ImGuiKey_Escape] = 27;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';
}

- (void)set_up_mouse_cursors {
    is_mouse_cursor = false;
    mouse_cursors[ImGuiMouseCursor_Arrow] = [NSCursor arrowCursor];
    mouse_cursors[ImGuiMouseCursor_TextInput] = [NSCursor IBeamCursor];
    mouse_cursors[ImGuiMouseCursor_ResizeAll] = [NSCursor closedHandCursor];
    mouse_cursors[ImGuiMouseCursor_Hand] = [NSCursor pointingHandCursor];
    mouse_cursors[ImGuiMouseCursor_ResizeNS] = [NSCursor respondsToSelector:@selector(_windowResizeNorthSouthCursor)]
                                                ? [NSCursor _windowResizeNorthSouthCursor]
                                                : [NSCursor resizeUpDownCursor];
    mouse_cursors[ImGuiMouseCursor_ResizeEW] = [NSCursor respondsToSelector:@selector(_windowResizeEastWestCursor)]
                                                ? [NSCursor _windowResizeEastWestCursor]
                                                : [NSCursor resizeLeftRightCursor];
    mouse_cursors[ImGuiMouseCursor_ResizeNESW] = [NSCursor respondsToSelector:@selector(_windowResizeNorthEastSouthWestCursor)]
                                                  ? [NSCursor _windowResizeNorthEastSouthWestCursor]
                                                  : [NSCursor closedHandCursor];
    mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = [NSCursor respondsToSelector:@selector(_windowResizeNorthWestSouthEastCursor)]
                                                  ? [NSCursor _windowResizeNorthWestSouthEastCursor]
                                                  : [NSCursor closedHandCursor];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}
@end

static int map_character_to_key(int c) {
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 'A';
    if (c == 25) // SHIFT+TAB -> TAB
        return 9;
    if (c >= 0 && c < 256)
        return c;
    if (c >= 0xF700 && c < 0xF700 + 256)
        return c - 0xF700 + 256;
    return -1;
}

static void reset_keys() {
    ImGuiIO &io = ImGui::GetIO();
    for (int n = 0; n < IM_ARRAYSIZE(io.KeysDown); n++) {
        io.KeysDown[n] = false;
    }
}

static bool handle_os_event(NSEvent *event, NSView *view) {
    ImGuiIO &io = ImGui::GetIO();

    switch (event.type) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown: {
            int button = (int) [event buttonNumber];
            if (button >= 0 && button < IM_ARRAYSIZE(io.MouseDown)) {
                io.MouseDown[button] = true;
            }
            return io.WantCaptureMouse;
        }

        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp: {
            int button = (int) [event buttonNumber];
            if (button >= 0 && button < IM_ARRAYSIZE(io.MouseDown)) {
                io.MouseDown[button] = false;
            }
            return io.WantCaptureMouse;
        }

        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged: {
            float pixel_ratio = platform_get_pixel_ratio();

            NSPoint mouse_point = event.locationInWindow;
            mouse_point = [view convertPoint:mouse_point fromView:nil];
            mouse_point = NSMakePoint(mouse_point.x, view.bounds.size.height - mouse_point.y);
            io.MousePos = ImVec2(mouse_point.x * pixel_ratio, mouse_point.y * pixel_ratio);

            break;
        }
    }

    if (event.type == NSEventTypeScrollWheel) {
        double wheel_dx = 0.0;
        double wheel_dy = 0.0;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
        if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_6) {
            wheel_dx = [event scrollingDeltaX];
            wheel_dy = [event scrollingDeltaY];
            if ([event hasPreciseScrollingDeltas]) {
                wheel_dx *= 0.1;
                wheel_dy *= 0.1;
            }
        } else
#endif
        {
            wheel_dx = [event deltaX];
            wheel_dy = [event deltaY];
        }

        if (fabs(wheel_dx) > 0.0) {
            io.MouseWheelH += wheel_dx * 0.1f;
        }

        if (fabs(wheel_dy) > 0.0) {
            io.MouseWheel += wheel_dy * 0.1f;
        }

        return io.WantCaptureMouse;
    }

    // FIXME: All the key handling is wrong and broken. Refer to GLFW's cocoa_init.mm and cocoa_window.mm.
    if (event.type == NSEventTypeKeyDown) {
        NSString *str = [event characters];
        int len = (int) [str length];
        for (int i = 0; i < len; i++) {
            int c = [str characterAtIndex:i];
            if (!io.KeyCtrl && !(c >= 0xF700 && c <= 0xFFFF)) {
                io.AddInputCharacter((unsigned int) c);
            }

            // We must reset in case we're pressing a sequence of special keys while keeping the command pressed
            int key = map_character_to_key(c);
            if (key != -1 && key < 256 && !io.KeyCtrl) {
                reset_keys();
            }

            if (key != -1) {
                io.KeysDown[key] = true;
            }
        }

        return io.WantCaptureKeyboard;
    }

    if (event.type == NSEventTypeKeyUp) {
        NSString *str = [event characters];
        int len = (int) [str length];
        for (int i = 0; i < len; i++) {
            int c = [str characterAtIndex:i];
            int key = map_character_to_key(c);
            if (key != -1) {
                io.KeysDown[key] = false;
            }
        }
        return io.WantCaptureKeyboard;
    }

    if (event.type == NSEventTypeFlagsChanged) {
        unsigned int flags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;

        bool old_key_ctrl = io.KeyCtrl;
        bool old_key_shift = io.KeyShift;
        bool old_key_alt = io.KeyAlt;
        bool old_key_super = io.KeySuper;
        io.KeyCtrl = flags & NSEventModifierFlagControl;
        io.KeyShift = flags & NSEventModifierFlagShift;
        io.KeyAlt = flags & NSEventModifierFlagOption;
        io.KeySuper = flags & NSEventModifierFlagCommand;

        // We must reset them as we will not receive any keyUp event if they where pressed with a modifier
        if ((old_key_shift && !io.KeyShift) || (old_key_ctrl && !io.KeyCtrl) || (old_key_alt && !io.KeyAlt) || (old_key_super && !io.KeySuper)) {
            reset_keys();
        }

        return io.WantCaptureKeyboard;
    }

    return false;
}

static void initialize_timer() {
    mach_timebase_info_data_t mach_base_info;
    mach_timebase_info(&mach_base_info);

    timer_frequency = mach_base_info.denom;
    timer_frequency *= 1000000000;
    timer_frequency /= mach_base_info.numer;
}

static void log_http_error(NSData* data, NSError* error) {
    NSString *message = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];

    NSLog(@"%li %@", error.code, message);

    [message release];
}

bool platform_init() {
    return true;
}

void platform_early_init(){
    initialize_timer();

    device = MTLCreateSystemDefaultDevice();
    texture_loader = [[MTKTextureLoader alloc] initWithDevice: device];

    NSString* key_path = [NSString stringWithUTF8String:platform_resolve_resource_path("private.key")];

    private_token = [NSString stringWithContentsOfFile:key_path encoding:NSUTF8StringEncoding error:nil];
}

void platform_loop(){
    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    app_delegate = [App_Delegate new];
    NSApp.delegate = app_delegate;
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
}

void platform_render_frame(){}

float platform_get_pixel_ratio() {
    return (float) (window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor);
}

u64 platform_get_app_time_precise(){
    return mach_absolute_time();
}

float platform_get_delta_time_ms(u64 delta_to){
    u64 now = platform_get_app_time_precise();

    return (float) ((now - delta_to) * 1000.0 / timer_frequency);
}

static NSString* to_ns_string(String& from) {
    return [[[NSString alloc] autorelease] initWithBytes:from.start length:from.length encoding:NSUTF8StringEncoding];
}

void platform_open_url(String& permalink){
    BOOL success = [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString: to_ns_string(permalink)]];

    printf("Open url: %s\n", success ? "ok" : "error");
}

u64 platform_make_texture(u32 width, u32 height, u8* pixels) {
    MTLTextureDescriptor *texture_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                  width:width
                                                                                                 height:height
                                                                                              mipmapped:NO];
    texture_descriptor.usage = MTLTextureUsageShaderRead;
    texture_descriptor.storageMode = MTLStorageModeManaged;

    id<MTLTexture> texture = [device newTextureWithDescriptor:texture_descriptor];
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:pixels bytesPerRow:width * 4];

    return (uintptr_t) (__bridge void *) texture;
}

void platform_api_request(Request_Id request_id, String path, Http_Method method, void* extra_data){
    printf("Requested api get for %i/%.*s\n", request_id, path.length, path.start);

    String full_url = tprintf("%s%.*s", "https://www.wrike.com/api/v4/", path.length, path.start);

    NSString* data_url = to_ns_string(full_url);

    NSMutableURLRequest *request = [[NSMutableURLRequest new] autorelease];

    switch (method) {
        case Http_Get: {
            [request setHTTPMethod:@"GET"];
            break;
        }

        case Http_Put: {
            [request setHTTPMethod:@"PUT"];
            break;
        }

        default: {
            assert(!"Unknown http method");
        }
    }

    [request setURL:[NSURL URLWithString:data_url]];
    [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
    [request setValue:private_token forHTTPHeaderField:@"Authorization"];

    [[[NSURLSession sharedSession] dataTaskWithRequest:request completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        if (error) {
            log_http_error(data, error);
            return;
        }

        [data retain];

        dispatch_async(dispatch_get_main_queue(), ^{
            // TODO that's fairly slow, but we can't move it up because malloc_and_log is not thread-safe, sigh
            u32 length = (u32) data.length;
            char* copy = (char*) MALLOC(length);

            memcpy(copy, data.bytes, length);

            [data release];

            api_request_success(request_id, copy, length, extra_data);
        });
    }] resume];
}

void platform_load_remote_image(Request_Id request_id, String full_url){
    NSURL* url = [NSURL URLWithString:to_ns_string(full_url)];

    [[[NSURLSession sharedSession] dataTaskWithURL:url completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        if (error) {
            log_http_error(data, error);
            return;
        }

        id<MTLTexture> new_texture = [texture_loader newTextureWithData:data options:@{MTKTextureLoaderOptionSRGB: @NO} error:nil];

        if (new_texture) {
            [new_texture retain];

            dispatch_async(dispatch_get_main_queue(), ^{
                Memory_Image image;
                image.texture_id = (uintptr_t) (__bridge void*) new_texture;
                image.width = new_texture.width;
                image.height = new_texture.height;

                if (!try_accept_loaded_image(request_id, image)) {
                    [new_texture release];
                }
            });
        }
    }] resume];
}

void platform_load_png_async(Array<u8> in, Image_Load_Callback callback){
    NSData* image_data = [NSData dataWithBytes:in.data length:in.length];

    [texture_loader newTextureWithData:image_data options:@{MTKTextureLoaderOptionSRGB: @NO} completionHandler:^(id <MTLTexture> texture, NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [texture retain];

            Memory_Image image;
            image.texture_id = (uintptr_t) (__bridge void*) texture;
            image.width = texture.width;
            image.height = texture.height;

            callback(image);
        });
    }];
}

void platform_local_storage_set(const char* key, String value){
    [[NSUserDefaults standardUserDefaults] setObject:to_ns_string(value) forKey:[NSString stringWithUTF8String:key]];
}

char* platform_local_storage_get(const char* key){
    NSString* value = [[NSUserDefaults standardUserDefaults] stringForKey:[NSString stringWithUTF8String:key]];

    if (!value) {
        return NULL;
    }

    return (char*) [value cStringUsingEncoding:NSUTF8StringEncoding];
}

char* platform_resolve_resource_path(const char* path) {
    NSString* resource_path = [[NSBundle mainBundle] resourcePath];
    NSData* bytes = [resource_path dataUsingEncoding:NSUTF8StringEncoding];

    String result = tprintf("%.*s/%s", (int) bytes.length, (char*) [bytes bytes], path);

    printf("Resource path resolved to %s\n", result.start);

    return result.start;
}