#include "main.h"
#include "renderer.h"
#include <imgui.h>
#include <cmath>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>
#include <cstdint>

#include "jsmn.h"
#include "folder_tree.h"
#include "common.h"
#include "json.h"
#include "temporary_storage.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "task_list.h"
#include "accounts.h"
#include "task_view.h"
#include "platform.h"
#include "base32.h"
#include "tracing.h"
#include "users.h"
#include "workflows.h"
#include "header.h"
#include "ui.h"

const Request_Id NO_REQUEST = -1;
const Request_Id FOLDER_TREE_CHILDREN_REQUEST = -2; // TODO BIG HAQ

Request_Id folder_header_request = NO_REQUEST;
Request_Id folder_contents_request = NO_REQUEST;
Request_Id folders_request = NO_REQUEST;
Request_Id task_request = NO_REQUEST;
Request_Id contacts_request = NO_REQUEST;
Request_Id accounts_request = NO_REQUEST;
Request_Id workflows_request = NO_REQUEST;
Request_Id modify_task_request = NO_REQUEST;
Request_Id suggested_folders_request = NO_REQUEST;
Request_Id suggested_contacts_request = NO_REQUEST;
Request_Id starred_folders_request = NO_REQUEST;

const Account_Id NO_ACCOUNT = -1;
const Folder_Id ROOT_FOLDER = -1;

bool had_last_selected_folder_so_doesnt_need_to_load_the_root_folder = false;
bool custom_statuses_were_loaded = false;

static bool draw_memory_debug = false;
static bool draw_side_menu = true;

static bool task_view_open_requested = false;

static float folder_tree_column_width = 300.0f;

static Account_Id selected_account_id = NO_ACCOUNT;

static Memory_Image logo{};

Task_Id selected_folder_task_id = 0;

Task current_task{};

static char* task_json_content = NULL;
static char* users_json_content = NULL;
static char* accounts_json_content = NULL;
static char* folder_tasks_json_content = NULL;
static char* workflows_json_content = NULL;
static char* folder_header_json_content = NULL;
static char* suggested_folders_json_content = NULL; // TODO looks like a lot of waste
static char* suggested_users_json_content = NULL; // TODO also there
static char* starred_folders_json_content = NULL;

u32 tick = 0;

u32 started_loading_folder_contents_at = 0;
u32 finished_loading_folder_contents_at = 0;
u32 finished_loading_folder_header_at = 0;

u32 started_loading_task_at = 0;
u32 finished_loading_task_at = 0;

u32 started_loading_statuses_at = 0;
u32 finished_loading_statuses_at = 0;

u32 started_loading_users_at = 0;
u32 finished_loading_users_at = 0;

u32 task_view_opened_at = 0;

static Request_Id request_id_counter = 0;

static double frame_times[60];
static u32 last_frame_vtx_count = 0;

PRINTLIKE(3, 4) void api_request(Http_Method method, Request_Id& request_id, const char* format, ...) {
    // TODO use temporary storage there
    static char temporary_request_buffer[512];

    va_list args;
    va_start(args, format);

    vsprintf(temporary_request_buffer, format, args);

    va_end(args);

    request_id = request_id_counter++;

    platform_api_request(request_id, temporary_request_buffer, method);
}

PRINTLIKE(2, 3) void image_request(Request_Id& request_id, const char* format, ...) {
    // TODO use temporary storage there
    static char temporary_request_buffer[512];

    va_list args;
    va_start(args, format);

    vsprintf(temporary_request_buffer, format, args);

    va_end(args);

    request_id = request_id_counter++;

    platform_load_remote_image(request_id, temporary_request_buffer);
}

struct Json_With_Tokens {
    char* json;
    jsmntok_t* tokens;
    u32 num_tokens;
};

static void process_json_content(char*& json_reference, Data_Process_Callback callback, Json_With_Tokens json_with_tokens) {
    if (json_reference) {
        FREE(json_reference);
    }

    json_reference = json_with_tokens.json;

    process_json_data_segment(json_with_tokens.json, json_with_tokens.tokens, json_with_tokens.num_tokens, callback);
}

static void select_account() {
    assert(accounts_count);

    selected_account_id = accounts[0].id;

    String account_id_string = tprintf("%i", accounts[0].id);

    platform_local_storage_set("selected_account", account_id_string);
}

void request_folder_children_for_folder_tree(Folder_Id folder_id) {
    u8 output_folder_and_account_id[16];

    fill_id16('A', selected_account_id, 'G', folder_id, output_folder_and_account_id);

    String url = tprintf("folders/%.16s/folders?descendants=false&fields=['color']", output_folder_and_account_id);

    platform_api_request(FOLDER_TREE_CHILDREN_REQUEST, url.start, Http_Get, (void*) (intptr_t) folder_id);
}

void request_multiple_folders(List<Folder_Id> folders) {
    assert(folders.length > 0);

    String url = tprintf("folders/");

    for (Folder_Id* it = folders.data; it != folders.data + folders.length; it++) {
        Folder_Id folder_id = *it;

        u8 output_folder_and_account_id[16];

        fill_id16('A', selected_account_id, 'G', folder_id, output_folder_and_account_id);

        if (it == folders.data) {
            url = tprintf("%.*s%.16s", url.length, url.start, output_folder_and_account_id);
        } else {
            url = tprintf("%.*s,%.16s", url.length, url.start, output_folder_and_account_id);
        }
    }

    url = tprintf("%.*s%s", url.length, url.start, "?fields=['color']");

    api_request(Http_Get, folders_request, "%s", url.start);
}

static void request_workflow_for_account(Account_Id account_id) {
    u8 output_account_id[8];

    fill_id8('A', account_id, output_account_id);

    api_request(Http_Get, workflows_request, "accounts/%.*s/workflows", (u32) ARRAY_SIZE(output_account_id), output_account_id);

    started_loading_statuses_at = tick;
}

static void request_suggestions_for_account(Account_Id account_id) {
    u8 output_account_id[8];

    fill_id8('A', account_id, output_account_id);

    api_request(Http_Get, starred_folders_request, "accounts/%.*s/folders?starred&fields=['color']", (u32) ARRAY_SIZE(output_account_id), output_account_id);
    api_request(Http_Get, suggested_folders_request, "accounts/%.*s/folders?suggestedParents&fields=['color']", (u32) ARRAY_SIZE(output_account_id), output_account_id);
    api_request(Http_Get, suggested_contacts_request, "internal/accounts/%.*s/contacts?suggestType=Responsibles", (u32) ARRAY_SIZE(output_account_id), output_account_id);
}

extern "C"
EXPORT
void api_request_success(Request_Id request_id, char* content, u32 content_length, void* data) {
//    printf("Got request %lu with content at %p\n", request_id, (void*) content_json);
    Json_With_Tokens json_with_tokens;
    json_with_tokens.json = content;
    json_with_tokens.tokens = parse_json_into_tokens(content, content_length, json_with_tokens.num_tokens);

    if (request_id == FOLDER_TREE_CHILDREN_REQUEST) {
        process_folder_tree_request((Folder_Id) (intptr_t) data, content, json_with_tokens.tokens, json_with_tokens.num_tokens);
    } else if (request_id == starred_folders_request) {
        starred_folders_request = NO_REQUEST;

        process_json_content(starred_folders_json_content, process_starred_folders_data, json_with_tokens);
    } else if (request_id == folders_request) {
        folders_request = NO_REQUEST;

        process_json_data_segment(content, json_with_tokens.tokens, json_with_tokens.num_tokens, process_multiple_folders_data);
    } else if (request_id == folder_contents_request) {
        folder_contents_request = NO_REQUEST;

        process_json_content(folder_tasks_json_content, process_folder_contents_data, json_with_tokens);
        finished_loading_folder_contents_at = tick;
    } else if (request_id == folder_header_request) {
        folder_header_request = NO_REQUEST;

        process_json_content(folder_header_json_content, process_folder_header_data, json_with_tokens);
        finished_loading_folder_header_at = tick;
    } else if (request_id == task_request) {
        task_request = NO_REQUEST;

        process_json_content(task_json_content, process_task_data, json_with_tokens);
        finished_loading_task_at = tick;
    } else if (request_id == contacts_request) {
        contacts_request = NO_REQUEST;
        process_json_content(users_json_content, process_users_data, json_with_tokens);
        finished_loading_users_at = tick;
    } else if (request_id == accounts_request) {
        accounts_request = NO_REQUEST;
        process_json_content(accounts_json_content, process_accounts_data, json_with_tokens);

        if (selected_account_id == NO_ACCOUNT) {
            select_account();
            request_suggestions_for_account(selected_account_id);
            request_workflow_for_account(selected_account_id);

            folder_tree_init(ROOT_FOLDER);
            request_folder_children_for_folder_tree(ROOT_FOLDER);
        }
    } else if (request_id == workflows_request) {
        workflows_request = NO_REQUEST;
        process_json_content(workflows_json_content, process_workflows_data, json_with_tokens);

        custom_statuses_were_loaded = true;
        finished_loading_statuses_at = tick;
    } else if (request_id == suggested_folders_request) {
        suggested_folders_request = NO_REQUEST;
        process_json_content(suggested_folders_json_content, process_suggested_folders_data, json_with_tokens);
    } else if (request_id == suggested_contacts_request) {
        suggested_contacts_request = NO_REQUEST;
        process_json_content(suggested_users_json_content, process_suggested_users_data, json_with_tokens);
    } else if (request_id == modify_task_request) {
        modify_task_request = NO_REQUEST;

        process_json_content(task_json_content, process_task_data, json_with_tokens);
    }

    FREE(json_with_tokens.tokens);
}

extern "C"
EXPORT
void image_load_success(Request_Id request_id, u8* pixel_data, u32 width, u32 height) {
    User* user_or_null = find_user_by_avatar_request_id(request_id);

    if (user_or_null) {
        Memory_Image& avatar = user_or_null->avatar;
        avatar.width = width;
        avatar.height = height;

        load_image_into_gpu_memory(avatar, pixel_data);

        user_or_null->avatar_loaded_at = tick;

        free(pixel_data);
    }
}

void request_folder_contents(String &folder_id) {
    platform_local_storage_set("last_selected_folder", folder_id);

    // TODO dirty temporary code, we need to request by regular id instead of passing a string
    u8* token_start = (u8*) folder_id.start;
    u8 result[UNBASE32_LEN(16)];
    base32_decode(token_start, 16, result);

    s32 id = uchars_to_s32(result + 6);

    api_request(Http_Get, folder_contents_request, "folders/%.*s/tasks%s", (int) folder_id.length, folder_id.start, "?fields=['customFields','superTaskIds','responsibleIds']&subTasks=true");

    if (id >= 0) {
        api_request(Http_Get, folder_header_request, "folders/%.*s%s", (int) folder_id.length, folder_id.start, "?fields=['customColumnIds']");
    } else {
        folder_header_request = NO_REQUEST;
        process_current_folder_as_logical();
    }

    started_loading_folder_contents_at = tick;
}

void select_folder_node_and_request_contents_if_necessary(Folder_Id id) {
    u8* account_and_folder_id = (u8*) talloc(sizeof(u8) * 16);

    fill_id16('A', selected_account_id, 'G', id, account_and_folder_id);

    String id_as_string;
    id_as_string.start = (char*) account_and_folder_id;
    id_as_string.length = 16;

    request_folder_contents(id_as_string);
}

static void request_task_by_full_id(String &full_id) {
    // TODO I'm not sure if that's all a good solution
    // TODO might make sense to store a regular id and only build API id on request

    u8* string_start = (u8*) full_id.start;
    u8 result[UNBASE32_LEN(16)];

    // TODO wasteful to decode all bytes but only use some
    base32_decode(string_start, 16, result);

    selected_folder_task_id = uchars_to_s32(result + 6);

    platform_local_storage_set("last_selected_task", full_id);

    api_request(Http_Get, task_request, "tasks/%.*s?fields=['inheritedCustomColumnIds']", full_id.length, full_id.start);
    started_loading_task_at = tick;
}

void request_task_by_task_id(Task_Id task_id) {
    u8* account_and_task_id = (u8*) talloc(sizeof(u8) * 16);

    fill_id16('A', selected_account_id, 'T', task_id, account_and_task_id);

    String id_as_string;
    id_as_string.start = (char*) account_and_task_id;
    id_as_string.length = 16;

    request_task_by_full_id(id_as_string);

    task_view_open_requested = true;
}

void modify_task_e16(Task_Id task_id, const u8 entity_prefix, s32 entity_id, const char* command, bool array = true) {
    u8 output_account_and_task_id[16];
    u8 output_entity_id[16];

    fill_id16('A', selected_account_id, 'T', task_id, output_account_and_task_id);
    fill_id16('A', selected_account_id, entity_prefix, entity_id, output_entity_id);

    const char* pattern = array ? "tasks/%.*s?%s=[\"%.*s\"]" : "tasks/%.*s?%s=%.*s";

    api_request(Http_Put, modify_task_request, pattern,
                (u32) ARRAY_SIZE(output_account_and_task_id), output_account_and_task_id,
                command,
                (u32) ARRAY_SIZE(output_entity_id), output_entity_id
    );
}

void modify_task_e8(Task_Id task_id, const u8 entity_prefix, s32 entity_id, const char* command, bool array = true) {
    u8 output_account_and_task_id[16];
    u8 output_entity_id[8];

    fill_id16('A', selected_account_id, 'T', task_id, output_account_and_task_id);
    fill_id8(entity_prefix, entity_id, output_entity_id);

    const char* pattern = array ? "tasks/%.*s?%s=[\"%.*s\"]" : "tasks/%.*s?%s=%.*s";

    api_request(Http_Put, modify_task_request, pattern,
                (u32) ARRAY_SIZE(output_account_and_task_id), output_account_and_task_id,
                command,
                (u32) ARRAY_SIZE(output_entity_id), output_entity_id
    );
}

void add_parent_folder(Task_Id task_id, Folder_Id folder_id) {
    modify_task_e16(task_id, 'G', folder_id, "addParents");
}

void remove_parent_folder(Task_Id task_id, Folder_Id folder_id) {
    modify_task_e16(task_id, 'G', folder_id, "removeParents");
}

void add_assignee_to_task(Task_Id task_id, User_Id user_id) {
    modify_task_e8(task_id, 'U', user_id, "addResponsibles");
}

void remove_assignee_from_task(Task_Id task_id, User_Id user_id) {
    modify_task_e8(task_id, 'U', user_id, "removeResponsibles");
}

void set_task_status(Task_Id task_id, Custom_Status_Id status_id) {
    modify_task_e16(task_id, 'K', status_id, "customStatus", false);
}

void ImGui::FadeInOverlay(float alpha) {
    if (alpha >= 0.99f) {
        return;
    }

    float rounding = ImGui::GetStyle().FrameRounding;

    ImVec2 top_left = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    u32 overlay_color = IM_COL32(255, 255, 255, 255 - (u32) (alpha * 255.0f));

    ImGui::GetOverlayDrawList()->AddRectFilled(top_left, top_left + size, overlay_color, rounding);
}

static void draw_average_frame_time() {
    double sum = 0;

    for (double* it = frame_times; it != frame_times + ARRAY_SIZE(frame_times); it++) {
        sum += *it;
    }

    char* text_start;
    char* text_end;

    tprintf("Loop time: %.2fms, %i vtx", &text_start, &text_end, sum / (float) ARRAY_SIZE(frame_times), last_frame_vtx_count);

    ImVec2 top_left = ImGui::GetIO().DisplaySize - ImVec2(200.0f, 20.0f) * platform_get_pixel_ratio();

    ImGui::GetOverlayDrawList()->AddText(top_left, IM_COL32_BLACK, text_start, text_end);
}

static void draw_memory_debug_contents() {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::Text("%f %f", io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Text("%f %f", io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

    if (ImGui::ListBoxHeader("Memory allocations", ImVec2(-1, -1))) {
        draw_memory_records();

        ImGui::ListBoxFooter();
    }
}

static void draw_task_view_popup_if_necessary() {
    ImVec2 display_size = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(display_size / 2.0f, ImGuiCond_Appearing, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ display_size.x / 2.0f, display_size.y / 1.25f }, ImGuiCond_Appearing);

    ImGuiID task_view_id = ImGui::GetID("##task_view");

    if (task_view_open_requested) {
        ImGui::OpenPopupEx(task_view_id);

        task_view_open_requested = false;
        task_view_opened_at = tick;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * platform_get_pixel_ratio());

    if (ImGui::BeginPopupEx(task_view_id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings)) {
        const u32 backdrop_color_no_alpha = argb_to_agbr(0x0027415a);

        u32 alpha = (u32) lroundf(lerp(task_view_opened_at, tick, 0xd8, 12));
        u32 backdrop_color = backdrop_color_no_alpha | (alpha << 24);

        ImGui::PushClipRect({}, display_size, false);
        ImGui::GetWindowDrawList()->AddRectFilled({}, display_size, backdrop_color);
        ImGui::PopClipRect();

        bool task_is_loading = task_request != NO_REQUEST || contacts_request != NO_REQUEST;

        if (selected_folder_task_id && !task_is_loading) {
            draw_task_contents();
        } else {
            ImGui::ListBoxHeader("##task_content", ImVec2(-1, -1));

            if (task_is_loading) {
                draw_window_loading_indicator();
            }

            ImGui::ListBoxFooter();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
}

static void draw_ui() {
    // TODO temporary code, desktop only
    static const u32 d_key_in_sdl = 7;

    if (ImGui::IsKeyPressed(d_key_in_sdl) && ImGui::GetIO().KeyCtrl) {
        draw_memory_debug = !draw_memory_debug;
    }

    if (draw_memory_debug) {
        draw_memory_debug_contents();

        return;
    }

    // TODO we don't need to load ALL contacts to show the main view, only the "me" contact, make a separate request for that!
    bool loading_contacts = contacts_request != NO_REQUEST;
    bool loading_workflows = !custom_statuses_were_loaded;

    if (loading_contacts || loading_workflows) {
        ImVec2 screen_center = ImGui::GetIO().DisplaySize / 2.0f;
        ImVec2 image_size{ (float) logo.width, (float) logo.height };

        float spinner_side = image_size.y / 5.0f;

        ImVec2 image_top_left = screen_center - (image_size / 2.0f);
        ImVec2 spinner_top_left = image_top_left + ImVec2(image_size.x + 20.0f, image_size.y / 2.0f - spinner_side / 2.0f);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddImage((void*) (uintptr_t) logo.texture_id, image_top_left, image_top_left + image_size);
        draw_loading_spinner(draw_list, spinner_top_left, spinner_side, 6, color_link);

        return;
    }

    bool draw_side_menu_this_frame = draw_side_menu;

    draw_header(draw_side_menu_this_frame, draw_side_menu, folder_tree_column_width);
    draw_average_frame_time();

    if (draw_side_menu_this_frame) {
        draw_folder_tree(folder_tree_column_width * platform_get_pixel_ratio());
        ImGui::SameLine();
    }

    draw_task_list();
    draw_task_view_popup_if_necessary();
}

extern "C"
EXPORT
char* handle_clipboard_copy() {
    char* temp = (char*) talloc(current_task.permalink.length + 1);
    sprintf(temp, "%.*s", current_task.permalink.length, current_task.permalink.start);

    return temp;
}

extern "C"
EXPORT
void handle_clipboard_paste(char* data, u32 data_length) {
    const char* search_for = "open.htm?id=";

    char* substring_start = string_in_substring(data, search_for, data_length);

    if (substring_start) {
        char* id_start = substring_start + strlen(search_for);
        u32 id_length = data_length - (id_start - data);

        Task_Id id_value = 0;

        for (char* c = id_start; c != id_start + id_length && *c; c++) {
            id_value = id_value * 10 + (*c - '0');
        }

        printf("Task Id %.*s found in buffer, loading\n", id_length, id_start);

        request_task_by_task_id(id_value);
    }
}

extern "C"
EXPORT
void loop() {
    u64 frame_start_time = platform_get_app_time_precise();

    clear_temporary_storage();

    tick++;

    platform_begin_frame();
    ImGui::NewFrame();

    ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse;

    bool open = true;

    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowPos(ImVec2());
    ImGui::Begin("Wrike", &open, flags);

    draw_ui();

    ImGui::End();

    ImGui::Render();
    renderer_draw_lists(ImGui::GetDrawData());

    last_frame_vtx_count = (u32) ImGui::GetDrawData()->TotalVtxCount;
    frame_times[tick % (ARRAY_SIZE(frame_times))] = platform_get_delta_time_ms(frame_start_time); // Before assumed swapBuffers

    platform_end_frame();
}

void load_persisted_settings() {
    char* selected_account = platform_local_storage_get("selected_account");
    char* last_selected_folder = platform_local_storage_get("last_selected_folder");
    char* last_selected_task = platform_local_storage_get("last_selected_task");

    if (selected_account) {
        char* number_end = NULL;

        selected_account_id = (s32) strtol(selected_account, &number_end, 10);

        if (selected_account_id != NO_ACCOUNT) {
            folder_tree_init(ROOT_FOLDER);

            request_workflow_for_account(selected_account_id);
            request_suggestions_for_account(selected_account_id);
            request_folder_children_for_folder_tree(ROOT_FOLDER);
        }
    }

    if (last_selected_folder) {
        String folder_id;
        folder_id.start = last_selected_folder;
        folder_id.length = strlen(last_selected_folder);

        request_folder_contents(folder_id);

        had_last_selected_folder_so_doesnt_need_to_load_the_root_folder = true;
    }

    if (last_selected_task) {
        String task_id;
        task_id.start = last_selected_task;
        task_id.length = strlen(last_selected_task);

        request_task_by_full_id(task_id);
    }
}

static void setup_ui() {
    ImGuiStyle* style = &ImGui::GetStyle();
    ImGui::StyleColorsLight(style);

    style->Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(color_background_dark);
    style->FrameRounding = 4.0f;
    style->WindowPadding = { 0, 0 };
    style->FramePadding = { 0, 0 };
    style->ItemSpacing = ImVec2(style->ItemSpacing.x, 0);
    style->PopupBorderSize = 0;
    style->ScaleAllSizes(platform_get_pixel_ratio());

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

static void* imgui_malloc_wrapper(size_t size, void* user_data) {
    (void) user_data;
    return malloc(size);
}

static void imgui_free_wrapper(void* ptr, void* user_data) {
    (void) user_data;
    free(ptr);
}

EXPORT
bool init() {
    init_temporary_storage();
    create_imgui_context();

    ImGui::SetAllocatorFunctions(imgui_malloc_wrapper, imgui_free_wrapper);

    bool result = platform_init();

    setup_ui();

    api_request(Http_Get, accounts_request, "accounts?fields=['customFields']");
    api_request(Http_Get, contacts_request, "contacts");

    started_loading_users_at = tick;

    load_persisted_settings();

    printf("Platform init: %s\n", result ? "true" : "false");

    load_png_from_disk("resources/wrike_logo.png", logo);
    set_header_logo(logo);

    return result;
}

EXPORT
int main() {
    if (!init()) {
        return -1;
    }

    platform_loop();
}