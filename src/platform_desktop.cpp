#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <curl/multi.h>
#include "common.h"
#include "platform.h"
#include "funimgui.h"
#include "main.h"

struct Running_Request {
    u32 status_code_or_zero;
    Request_Id request_id;
    char* debug_url = NULL;
    char* data_read = NULL;
    u32 data_length = 0;
    u64 started_at = 0;
};

static SDL_Window* application_window = NULL;
static SDL_GLContext gl_context;

static CURLM* curl_multi = NULL;
static Running_Request** running_requests = NULL;
static u32 num_running_requests = 0;
static SDL_mutex* requests_process_mutex = NULL;
static SDL_cond* new_requests_signal = NULL;


static Uint64 application_time = 0;
static bool mouse_pressed[3] = { false, false, false };

static char* private_token = NULL;

static const char* vertex_shader_source =
        "#version 150\n"
        "uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "	Frag_UV = UV;\n"
        "	Frag_Color = Color;\n"
        "	gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

static const char* fragment_shader_source =
        "#version 150\n"
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color;\n"
        "void main()\n"
        "{\n"
        "	Out_Color = Frag_Color * texture( Texture, Frag_UV.st);\n"
        "}\n";

static char* file_to_string(const char* file) {
    FILE* file_handle = fopen(file, "r");

    if (!file_handle) {
        return NULL;
    }

    fseek(file_handle, 0, SEEK_END);
    s32 size = ftell(file_handle);

    if (size <= 0) {
        fclose(file_handle);
        return NULL;
    }

    rewind(file_handle);

    char* buffer = (char*) MALLOC((size + 1) * sizeof(char));
    fread(buffer, size, 1, file_handle);
    buffer[size] = '\0';

    return buffer;
}

static void create_open_gl_context() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    int enable_vertical_sync_success = SDL_GL_SetSwapInterval(1);

    if (!enable_vertical_sync_success) {
        printf("VSync could not be enabled\n");
    }

    gl_context = SDL_GL_CreateContext(application_window);

    printf("Using OpenGL version %s\n", glGetString(GL_VERSION));
}

static bool process_sdl_events(SDL_Event* event) {
    ImGuiIO &io = ImGui::GetIO();
    switch (event->type) {
        case SDL_MOUSEWHEEL: {
            if (event->wheel.x > 0) io.MouseWheelH += 1;
            if (event->wheel.x < 0) io.MouseWheelH -= 1;
            if (event->wheel.y > 0) io.MouseWheel += 1;
            if (event->wheel.y < 0) io.MouseWheel -= 1;
            return true;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (event->button.button == SDL_BUTTON_LEFT) mouse_pressed[0] = true;
            if (event->button.button == SDL_BUTTON_RIGHT) mouse_pressed[1] = true;
            if (event->button.button == SDL_BUTTON_MIDDLE) mouse_pressed[2] = true;
            return true;
        }
        case SDL_TEXTINPUT: {
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int key = event->key.keysym.scancode;
            IM_ASSERT(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown));
            io.KeysDown[key] = (event->type == SDL_KEYDOWN);
            io.KeyShift = ((SDL_GetModState() & KMOD_SHIFT) != 0);
            io.KeyCtrl = ((SDL_GetModState() & KMOD_CTRL) != 0);
            io.KeyAlt = ((SDL_GetModState() & KMOD_ALT) != 0);
            io.KeySuper = ((SDL_GetModState() & KMOD_GUI) != 0);
            return true;
        }
    }
    return false;
}

static bool poll_events_and_check_exit_event() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        process_sdl_events(&event);

        if (event.type == SDL_QUIT) {
            return true;
        }
    }

    return false;
}

static void set_clipboard_text(void*, const char* text) {
    SDL_SetClipboardText(text);
}

static const char* get_clipboard_text(void*) {
    return SDL_GetClipboardText();
}

static void setup_io() {
    ImGuiIO& io = ImGui::GetIO();

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
    io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
    io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
    io.KeyMap[ImGuiKey_Insert] = SDL_SCANCODE_INSERT;
    io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
    io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
    io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
    io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
    io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
    io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
    io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
    io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
    io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

    io.SetClipboardTextFn = set_clipboard_text;
    io.GetClipboardTextFn = get_clipboard_text;
    io.ClipboardUserData = NULL;
}

static void poll_curl_messages() {
    int messages_left = 0;

    CURLMsg* message = NULL;

    while ((message = curl_multi_info_read(curl_multi, &messages_left))) {
        if (message->msg != CURLMSG_DONE) {
            continue;
        }

        CURL* curl = message->easy_handle;
        CURLcode result_code = message->data.result;

        if (result_code != CURLE_OK) {
            printf("Got CURL code %d\n", result_code);
            continue;
        }

        u32 http_status_code = 0;

        Running_Request* request = NULL;

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status_code);
        curl_easy_getinfo(curl, CURLINFO_PRIVATE, &request);

        assert(request);
        assert(http_status_code);

        float time = (float) (((double) SDL_GetPerformanceCounter() - request->started_at) / SDL_GetPerformanceFrequency());

        printf("GET %s completed with %lu, time: %fs\n", request->debug_url, http_status_code, time);

        double total, name, conn, app, pre, start;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &name);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &conn);
        curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &app);
        curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &pre);
        curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &start);

        printf("CURL TIME: total %f\n", total);
        printf("CURL TIME: name %f\n", name);
        printf("CURL TIME: conn %f\n", conn);
        printf("CURL TIME: app %f\n", app);
        printf("CURL TIME: pre %f\n", pre);
        printf("CURL TIME: start %f\n", start);

        request->status_code_or_zero = http_status_code;

        curl_multi_remove_handle(curl_multi, curl);
        curl_easy_cleanup(curl);
    }
}

static int curl_multi_thread_spin(void*) {
    curl_multi = curl_multi_init();

    while (true) {
        int running_transfers = 0;

        SDL_LockMutex(requests_process_mutex);

        curl_multi_perform(curl_multi, &running_transfers);

        if (num_running_requests) {
            poll_curl_messages();
        } else {
            if (SDL_CondWait(new_requests_signal, requests_process_mutex) == -1) {
                // Fatal error, do we need to unlock there too?
                break;
            }
        }

        SDL_UnlockMutex(requests_process_mutex);
    }

    return 0;
}

static void process_completed_api_requests() {
    SDL_LockMutex(requests_process_mutex);

    for (s32 index = 0; index < num_running_requests; index++) {
        Running_Request* request = running_requests[index];

        if (request->status_code_or_zero) {
            if (request->status_code_or_zero == 200) {
                u64 start_process_request = SDL_GetPerformanceCounter();

                api_request_success(request->request_id, request->data_read, request->data_length);

                u64 delta = SDL_GetPerformanceCounter() - start_process_request;

                printf("Request processed in %fms\n", delta * 1000.0 / SDL_GetPerformanceFrequency());
            } else {
                printf("%.*s\n", request->data_length, request->data_read);
            }

            // data_read is managed by receiver
            FREE(request->debug_url);
            FREE(request);

            if (num_running_requests > 1) {
                running_requests[index] = running_requests[num_running_requests - 1];
            }

            num_running_requests--;
            index--;
        }
    }

    SDL_UnlockMutex(requests_process_mutex);
}

bool platform_init() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS);

    u32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    application_window = SDL_CreateWindow("Wrike", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, window_flags);

    create_open_gl_context();

    setup_io();

    new_requests_signal = SDL_CreateCond();
    requests_process_mutex = SDL_CreateMutex();

    SDL_CreateThread(curl_multi_thread_spin, "CURLMultiThread", (void*) NULL);

    // TODO bad API, we shouldn't be making external calls in platform impl, move those out
    FunImGui::initGraphics(vertex_shader_source, fragment_shader_source);

    private_token = file_to_string("private.key");

    return true;
}

void platform_loop() {
    // Even though we have vsync enabled by default the app will burn
    // cpu cycles if it's not on screen
    static u32 frame_start_time = 0;

    while (true) {
        frame_start_time = SDL_GetTicks();

        if (poll_events_and_check_exit_event()) {
            break;
        }

        process_completed_api_requests();

        loop();

        // TODO SDL_GetTicks() is not accurate
        // TODO SDL_Delay() is not accurate
        // TODO limited to 60fps, bad
        u32 delta = SDL_GetTicks() - frame_start_time;
        if (delta < 16) {
            SDL_Delay(16 - delta);
        }
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(application_window);
    SDL_Quit();

    exit(0);
}

void platform_begin_frame() {
    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    int w, h;
    int display_w, display_h;
    SDL_GetWindowSize(application_window, &w, &h);
    SDL_GL_GetDrawableSize(application_window, &display_w, &display_h);
    io.DisplaySize = ImVec2((float)display_w, (float)display_h);
    io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

    // Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    io.DeltaTime = application_time > 0 ? (float)((double)(current_time - application_time) / frequency) : (1.0f / 60.0f);
    application_time = current_time;

    // Setup mouse inputs (we already got mouse wheel, keyboard keys & characters from our event handler)
    int mx, my;
    Uint32 mouse_buttons = SDL_GetMouseState(&mx, &my);
    float scale = platform_get_pixel_ratio();
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    io.MouseDown[0] = mouse_pressed[0] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
    io.MouseDown[1] = mouse_pressed[1] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    io.MouseDown[2] = mouse_pressed[2] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    mouse_pressed[0] = mouse_pressed[1] = mouse_pressed[2] = false;

    if ((SDL_GetWindowFlags(application_window) & SDL_WINDOW_INPUT_FOCUS) != 0)
    io.MousePos = ImVec2((float)mx * scale, (float)my * scale);

    SDL_GL_MakeCurrent(application_window, gl_context);

    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void platform_end_frame() {
    SDL_GL_SwapWindow(application_window);
}

static size_t handle_curl_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Running_Request* request = (Running_Request*) userdata;

    assert(request);

    u32 received_data_length = size * nmemb;

    // TODO very inefficient
    request->data_read = (char*) REALLOC(request->data_read, request->data_length + received_data_length);
    memcpy(request->data_read + request->data_length, ptr, received_data_length);
    request->data_length += received_data_length;

    return received_data_length;
}

void platform_api_get(Request_Id& request_id, char* url) {
    printf("Requested api get for %lu/%s\n", request_id, url);

    const char* url_prefix = "https://www.wrike.com/api/v3/";
    const u32 buffer_length = strlen(url_prefix) + strlen(url) + 1;
    char* buffer = (char*) talloc(buffer_length);
    snprintf(buffer, buffer_length, "%s%s", url_prefix, url);

    u32 new_request_index = num_running_requests++;
    running_requests = (Running_Request**) REALLOC(running_requests, num_running_requests * sizeof(Running_Request*));

    // TODO cleanup those
    curl_slist* header_chunk = NULL;
    header_chunk = curl_slist_append(header_chunk, "Accept: application/json");
    header_chunk = curl_slist_append(header_chunk, private_token);

    // TODO optimize
    Running_Request* new_request = (Running_Request*) CALLOC(1, sizeof(Running_Request));
    new_request->status_code_or_zero = 0;
    new_request->request_id = request_id;
    new_request->debug_url = (char*) MALLOC(buffer_length);
    new_request->started_at = SDL_GetPerformanceCounter();
    memcpy(new_request->debug_url, buffer, buffer_length);

    CURL* curl_easy = curl_easy_init();
    curl_easy_setopt(curl_easy, CURLOPT_URL, buffer);
    curl_easy_setopt(curl_easy, CURLOPT_HTTPHEADER, header_chunk);
    curl_easy_setopt(curl_easy, CURLOPT_PRIVATE, new_request);
    curl_easy_setopt(curl_easy, CURLOPT_WRITEDATA, new_request);
    curl_easy_setopt(curl_easy, CURLOPT_WRITEFUNCTION, &handle_curl_write);
    curl_easy_setopt(curl_easy, CURLOPT_BUFFERSIZE, CURL_MAX_READ_SIZE);


    SDL_LockMutex(requests_process_mutex);

    curl_multi_add_handle(curl_multi, curl_easy);

    running_requests[new_request_index] = new_request;

    SDL_CondSignal(new_requests_signal);
    SDL_UnlockMutex(requests_process_mutex);
}

// TODO super duper temporary coderino
void platform_local_storage_set(const char* key, String &value) {
    FILE* file_handle = fopen(key, "w");

    if (!file_handle) {
        printf("Error opening file!\n");
        return;
    }

    fprintf(file_handle, "%.*s", value.length, value.start);
    fclose(file_handle);
}

char* platform_local_storage_get(const char* key) {
    return file_to_string(key);
}

void platform_open_in_wrike(String &permalink) {
    // TODO
}

float platform_get_pixel_ratio() {
    int framebuffer_w = 0;
    int window_w = 0;

    SDL_GetWindowSize(application_window, &window_w, NULL);
    SDL_GL_GetDrawableSize(application_window, &framebuffer_w, NULL);

    // TODO performance overhead, cache?
    return (float) framebuffer_w / window_w;
}

float platform_get_app_time_ms() {
    static u64 start_time = SDL_GetPerformanceCounter();
    static u64 frequency = SDL_GetPerformanceFrequency();

    return (float) ((SDL_GetPerformanceCounter() - start_time) * 1000.0 / frequency);
}