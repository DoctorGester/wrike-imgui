#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <imgui_user.h>
#include "platform.h"
#include "main.h"

#define glBindVertexArray(x) glBindVertexArrayOES(x)
#define glGenVertexArrays(x, y) glGenVertexArraysOES(x, y)
#define GL_VERTEX_ARRAY_BINDING GL_VERTEX_ARRAY_BINDING_OES
#include "opengl.cpp"

static const char* vertex_shader_source =
        "uniform mat4 ProjMtx;                                  \n"
        "attribute vec2 Position;                               \n"
        "attribute vec2 UV;                                     \n"
        "attribute vec4 Color;                                  \n"
        "varying vec2 Frag_UV;                                  \n"
        "varying vec4 Frag_Color;                               \n"
        "void main()                                            \n"
        "{                                                      \n"
        "   Frag_UV = UV;                                       \n"
        "   Frag_Color = Color;                                 \n"
        "   gl_Position = ProjMtx * vec4(Position.xy, 0, 1);    \n"
        "}                                                      \n";

static const char* fragment_shader_source =
        "uniform sampler2D Texture;                                     \n"
        "varying mediump vec2 Frag_UV;                                  \n"
        "varying mediump vec4 Frag_Color;                               \n"
        "void main()                                                    \n"
        "{                                                              \n"
        "   gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV);    \n"
        "}                                                              \n";


static float frame_pixel_ratio = 1.0f;

static Gl_Data gl_data{};

static void begin_frame() {
    ImGuiIO &io = ImGui::GetIO();

    int width = 0;
    int height = 0;

    emscripten_get_canvas_element_size("canvas", &width, &height);

    frame_pixel_ratio = (float) emscripten_get_device_pixel_ratio();

    io.DisplaySize = ImVec2((float) width, (float) height);
    io.DisplayFramebufferScale = ImVec2(frame_pixel_ratio, frame_pixel_ratio);

    static u64 last = 0;

    io.DeltaTime = platform_get_delta_time_ms(last) / 1000.0f;
    last = platform_get_app_time_precise();

    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void do_one_frame() {
    begin_frame();
    loop();
}

static int emscripten_mouse_callback(int eventType, const EmscriptenMouseEvent* mouseEvent, void* /*userData*/) {
    float scale = platform_get_pixel_ratio();
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float) mouseEvent->canvasX * scale, (float) mouseEvent->canvasY * scale);
    io.MouseDown[0] = mouseEvent->buttons & 1;
    io.MouseDown[1] = mouseEvent->buttons & 2;
    io.MouseDown[2] = mouseEvent->buttons & 4;

    if (eventType == EMSCRIPTEN_EVENT_MOUSEUP) {
        // This allows us to respond to user clicks with stuff like opening urls, which otherwise get popup blocked
        do_one_frame();
    }

    return true;
}

static int emscripten_touch_callback(int eventType, const EmscriptenTouchEvent *touchEvent, void *userData) {
    float scale = platform_get_pixel_ratio();
    ImGuiIO& io = ImGui::GetIO();

    if (touchEvent->numTouches > 0 || eventType == EMSCRIPTEN_EVENT_TOUCHEND) {
        const EmscriptenTouchPoint& touch = touchEvent->touches[0];
        io.MousePos = ImVec2((float)touch.canvasX * scale, (float)touch.canvasY * scale);
        io.MouseDown[0] = true;
    } else {
        io.MouseDown[0] = false;
        io.MouseDown[1] = false;
        io.MouseDown[2] = false;
    }

    return true;
}

static int emscripten_wheel_callback(int /*eventType*/, const EmscriptenWheelEvent* wheelEvent, void* /*userData*/) {
    printf("Scroll %f\n", wheelEvent->deltaY);

    ImGuiIO& io = ImGui::GetIO();

    if(wheelEvent->deltaY > 0)
        io.MouseWheel = -1.f/5.f;
    else if(wheelEvent->deltaY < 0)
        io.MouseWheel = 1.f/5.f;

    io.MouseWheel = (float) -wheelEvent->deltaY / 40.0f;
    io.MouseWheelH = (float) -wheelEvent->deltaX / 40.0f;

    return true;
}

static int emscripten_keyboard_callback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* /*userData*/) {
    bool handled = true;
    ImGuiIO& io = ImGui::GetIO();
    //let copy paste fall through to browser
    if(
            (keyEvent->ctrlKey || keyEvent->metaKey) &&
            (
                    0 == strcmp("KeyC", keyEvent->code) ||
                    0 == strcmp("KeyV", keyEvent->code) ||
                    0 == strcmp("KeyX", keyEvent->code)
            )
            )
    {
        handled = true;
    }

    //todo, detect a single unicode codepoint instead of a single character

    if(
            strlen(keyEvent->key) <= 2 &&
            eventType == EMSCRIPTEN_EVENT_KEYDOWN
            )
    {
        io.AddInputCharactersUTF8(keyEvent->key);
    }
#if DEBUGPRINT_KEYBOARD
    printf(
            "%d key: %s, code: %s, ctrl: %d, shift: %d, alt: %d, meta: %d, repeat: %d, which: %i\n",
            eventType,
            keyEvent->key,
            keyEvent->code,
            keyEvent->ctrlKey,
            keyEvent->shiftKey,
            keyEvent->altKey,
            keyEvent->metaKey,
            keyEvent->repeat,
            keyEvent->which
    );
#endif // DEBUGPRINT_KEYBOARD
    if(keyEvent->repeat)
    {
        return true;
    }
    io.KeyCtrl = keyEvent->ctrlKey;
    io.KeyShift = keyEvent->shiftKey;
    io.KeyAlt = keyEvent->altKey;
    io.KeySuper = keyEvent->metaKey;
    //io.KeySuper = false;

#if DEBUGPRINT_KEYBOARD
    printf("ctrl: %d, shift: %d, alt: %d, meta: %d\n",
        keyEvent->ctrlKey,
        keyEvent->shiftKey,
        keyEvent->altKey,
        keyEvent->metaKey
    );
#endif // DEBUGPRINT_KEYBOARD


    switch(eventType)
    {
        case EMSCRIPTEN_EVENT_KEYDOWN:
        {
            io.KeysDown[keyEvent->which] = 1;
        }
            break;
        case EMSCRIPTEN_EVENT_KEYUP:
        {
            io.KeysDown[keyEvent->which] = 0;
        }
            break;
        case EMSCRIPTEN_EVENT_KEYPRESS:
        {
            printf("%s was pressed\n", keyEvent->key);
        }
            break;
    }
    return handled;
}

static void register_input_callbacks() {
    emscripten_set_mousemove_callback(nullptr, nullptr, false, &emscripten_mouse_callback);
    emscripten_set_mousedown_callback(nullptr, nullptr, false, &emscripten_mouse_callback);
    emscripten_set_mouseup_callback(nullptr, nullptr, false, &emscripten_mouse_callback);
    emscripten_set_wheel_callback("canvas", nullptr, false, &emscripten_wheel_callback);
    emscripten_set_touchstart_callback(nullptr, nullptr, false, &emscripten_touch_callback);
    emscripten_set_touchmove_callback(nullptr, nullptr, false, &emscripten_touch_callback);
    emscripten_set_touchend_callback(nullptr, nullptr, false, &emscripten_touch_callback);
    emscripten_set_touchcancel_callback(nullptr, nullptr, false, &emscripten_touch_callback);

    //emscripten_set_keypress_callback(nullptr, nullptr, false, &FunImGui::keyboardCallback);
    emscripten_set_keydown_callback(nullptr, nullptr, false, &emscripten_keyboard_callback);
    emscripten_set_keyup_callback(nullptr, nullptr, false, &emscripten_keyboard_callback);
}

static bool create_webgl_context() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.antialias = false;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_create_context(nullptr, &attrs);
    if(context > 0)
    {
        EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(context);
        if(EMSCRIPTEN_RESULT_SUCCESS == result)
        {
            EmscriptenFullscreenStrategy strategy;
            strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_DEFAULT;
            strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
            strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
            result = emscripten_enter_soft_fullscreen("canvas", &strategy);
            return EMSCRIPTEN_RESULT_SUCCESS == result;
        }
    }

    return false;
}

static const char* get_clipboard_text(void*) {
    return "";
}

static void set_clipboard_text(void*, const char* text) {
}

static void setup_io() {
    ImGuiIO &io = ImGui::GetIO();

    io.KeyMap[ImGuiKey_Tab] = 9;
    io.KeyMap[ImGuiKey_LeftArrow] = 37;
    io.KeyMap[ImGuiKey_RightArrow] = 39;
    io.KeyMap[ImGuiKey_UpArrow] = 38;
    io.KeyMap[ImGuiKey_DownArrow] = 40;
    io.KeyMap[ImGuiKey_PageUp] = 33;
    io.KeyMap[ImGuiKey_PageDown] = 34;
    io.KeyMap[ImGuiKey_Home] = 36;
    io.KeyMap[ImGuiKey_End] = 35;
    io.KeyMap[ImGuiKey_Delete] = 46;
    io.KeyMap[ImGuiKey_Backspace] = 8;
    io.KeyMap[ImGuiKey_Space] = 32;
    io.KeyMap[ImGuiKey_Enter] = 13;
    io.KeyMap[ImGuiKey_Escape] = 27;
    io.KeyMap[ImGuiKey_A] = 65;
    io.KeyMap[ImGuiKey_C] = 67;
    io.KeyMap[ImGuiKey_V] = 86;
    io.KeyMap[ImGuiKey_X] = 88;
    io.KeyMap[ImGuiKey_Y] = 89;
    io.KeyMap[ImGuiKey_Z] = 90;

    io.SetClipboardTextFn = set_clipboard_text;
    io.GetClipboardTextFn = get_clipboard_text;
    io.ClipboardUserData = NULL;
}

void platform_early_init(){}

bool platform_init() {
//    TODO This code breaks the whole app, why?
//    if (!create_webgl_context()) {
//        EM_ASM("document.body.innerText = 'Your browser does not support WebGL.';");
//        return false;
//    }

    create_webgl_context();

    setup_io();

    frame_pixel_ratio = (float) emscripten_get_device_pixel_ratio();

    opengl_program_init(gl_data, vertex_shader_source, fragment_shader_source);

    register_input_callbacks();

    return true;
}

void platform_loop() {
    emscripten_set_main_loop(do_one_frame, 0, 1);
}

void platform_render_frame() {
    opengl_render_frame(ImGui::GetDrawData(), gl_data);
}

void platform_load_remote_image(Request_Id request_id, String full_url) {
    EM_ASM({ load_image(Pointer_stringify($0, $1), $2) }, full_url.start, full_url.length, request_id);
}

void platform_api_request(Request_Id request_id, String url, Http_Method method, void* data) {
    const s8* method_as_string;
    switch (method) {
        case Http_Put: {
            method_as_string = "PUT";
            break;
        }

        case Http_Get: {
            method_as_string = "GET";
            break;
        }

        default: {
            assert(!"Unknown request method");
        }
    }

    EM_ASM({ api_get(Pointer_stringify($0, $1), $2, Pointer_stringify($3), $4) }, url.start, url.length, request_id, method_as_string, data);
}

void platform_local_storage_set(const char* key, String value) {
    EM_ASM({ local_storage_set(Pointer_stringify($0), Pointer_stringify($1, $2)) },
           key,
           value.start,
           value.length
    );
}

char* platform_local_storage_get(const char* key) {
    return (char*) EM_ASM_INT({ return local_storage_get_string(Pointer_stringify($0)) }, key);
}

void platform_open_url(String &permalink) {
    EM_ASM({ window.open(Pointer_stringify($0, $1), '_blank'); },
           permalink.start, permalink.length);
}

void platform_load_png_async(Array<u8> in, Image_Load_Callback callback) {
    EM_ASM({ decode_png($0, $1, $2); }, in.data, in.length, callback);
}

u32 platform_make_texture(u32 width, u32 height, u8 *pixels) {
    return opengl_make_texture(width, height, pixels);
}

float platform_get_pixel_ratio() {
    return frame_pixel_ratio;
}

u64 platform_get_app_time_precise() {
    double time = emscripten_get_now();
    u64 output;

    memcpy(&output, &time, sizeof(double));

    return output;
}

float platform_get_delta_time_ms(u64 delta_to) {
    double now = emscripten_get_now();
    double double_delta_to;

    memcpy(&double_delta_to, &delta_to, sizeof(u64));

    return (float) (now - double_delta_to);
}