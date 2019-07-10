#include "main.h"
#include <imgui.h>
#include <cmath>
#include <string.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>
#include <cstdint>

#include "jsmn.h"
#include "folder_tree.h"
#include "custom_fields.h"
#include "common.h"
#include "json.h"
#include "temporary_storage.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "task_list.h"
#include "account.h"
#include "task_view.h"
#include "platform.h"
#include "base32.h"
#include "tracing.h"
#include "users.h"
#include "workflows.h"
#include "header.h"
#include "ui.h"
#include "inbox.h"

const Request_Id NO_REQUEST = -1;
const Request_Id FOLDER_TREE_CHILDREN_REQUEST = -2; // TODO BIG HAQ
const Request_Id NOTIFICATION_MARK_AS_READ_REQUEST = -3;
const Request_Id LOAD_USERS_REQUEST = -4;
const Request_Id LOAD_CUSTOM_FIELDS_REQUEST = -5;

Request_Id me_request = NO_REQUEST;
Request_Id folder_header_request = NO_REQUEST;
Request_Id folder_contents_request = NO_REQUEST;
Request_Id folders_request = NO_REQUEST;
Request_Id task_request = NO_REQUEST;
Request_Id task_comments_request = NO_REQUEST;
Request_Id inbox_request = NO_REQUEST;
Request_Id account_request = NO_REQUEST;
Request_Id workflows_request = NO_REQUEST;
Request_Id modify_task_request = NO_REQUEST;
Request_Id suggested_folders_request = NO_REQUEST;
Request_Id suggested_contacts_request = NO_REQUEST;
Request_Id starred_folders_request = NO_REQUEST;
Request_Id spaces_request = NO_REQUEST;
Request_Id spaces_folders_request = NO_REQUEST;

const Folder_Id ROOT_FOLDER = -1;

bool custom_statuses_were_loaded = false;

static bool draw_memory_debug = false;
static bool draw_side_menu = true;

static bool task_view_open_requested = false;

static float folder_tree_column_width = 300.0f;

static Memory_Image logo{};

View current_view = View_Task_List;

Task_Id selected_folder_task_id = 0;

Task current_task{};

static char* task_json_content = NULL;
static char* account_json_content = NULL;
static char* folder_tasks_json_content = NULL;
static char* workflows_json_content = NULL;
static char* folder_header_json_content = NULL;
static char* suggested_folders_json_content = NULL; // TODO looks like a lot of waste
static char* suggested_users_json_content = NULL; // TODO also there
static char* starred_folders_json_content = NULL;
static char* spaces_json_content = NULL;
static char* spaces_folders_json_content = NULL;
static char* inbox_json_content = NULL;

ImFont* font_regular;
ImFont* font_28px;
ImFont* font_19px;
ImFont* font_19px_bold;
ImFont* font_bold;
ImFont* font_italic;
ImFont* font_bold_italic;

u32 tick = 0;

u32 started_showing_main_ui_at = 0;

u32 started_loading_folder_contents_at = 0;
u32 finished_loading_folder_contents_at = 0;
u32 finished_loading_folder_header_at = 0;

u32 started_loading_task_at = 0;
u32 finished_loading_task_at = 0;
u32 finished_loading_task_comments_at = 0;

u32 finished_loading_me_at = 0;
u32 finished_loading_statuses_at = 0;
u32 finished_loading_account_at = 0;

u32 task_view_opened_at = 0;

static Request_Id request_id_counter = 0;

static double frame_times[60];
static u32 last_frame_vtx_count = 0;

PRINTLIKE(3, 4) void api_request(Http_Method method, Request_Id& request_id, const char* format, ...) {
    va_list args;
    va_start(args, format);

    String url = tprintf(format, args);

    va_end(args);

    request_id = request_id_counter++;

    platform_api_request(request_id, url, method);
}

PRINTLIKE(2, 3) void image_request(Request_Id& request_id, const char* format, ...) {
    va_list args;
    va_start(args, format);

    String url = tprintf(format, args);

    va_end(args);

    request_id = request_id_counter++;

    platform_load_remote_image(request_id, url);
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

void request_folder_children_for_folder_tree(Folder_Id folder_id) {
    u8 output_folder_and_account_id[16];

    fill_id16('A', account.id, 'G', folder_id, output_folder_and_account_id);

    String url = tprintf("folders/%.16s/folders?descendants=false&fields=['color']", output_folder_and_account_id);

    platform_api_request(FOLDER_TREE_CHILDREN_REQUEST, url, Http_Get, (void*) (intptr_t) folder_id);
}

static String build_folder_request_url(Array<Folder_Id> folders) {
    String url = tprintf("folders/");

    for (Folder_Id* it = folders.data; it != folders.data + folders.length; it++) {
        Folder_Id folder_id = *it;

        u8 output_folder_and_account_id[16];

        fill_id16('A', account.id, 'G', folder_id, output_folder_and_account_id);

        if (it == folders.data) {
            url = tprintf("%.*s%.16s", url.length, url.start, output_folder_and_account_id);
        } else {
            url = tprintf("%.*s,%.16s", url.length, url.start, output_folder_and_account_id);
        }
    }

    return url;
}

void request_multiple_folders_for_spaces(Array<Folder_Id> folders) {
    assert(folders.length > 0);

    String url = build_folder_request_url(folders);

    api_request(Http_Get, spaces_folders_request, "%s", url.start);
}

void request_multiple_folders(Array<Folder_Id> folders) {
    assert(folders.length > 0);

    String url = build_folder_request_url(folders);

    url = tprintf("%.*s%s", url.length, url.start, "?fields=['color']");

    api_request(Http_Get, folders_request, "%s", url.start);
}

void request_multiple_users(Array<User_Id> users) {
    assert(users.length > 0);

    String url = tprintf("contacts/");

    for (User_Id * it = users.data; it != users.data + users.length; it++) {
        User_Id user_id = *it;

        if (!is_user_requested(user_id)) {
            mark_user_as_requested(user_id);

            u8 output_user_id[8];

            fill_id8('U', user_id, output_user_id);

            if (it == users.data) {
                url = tprintf("%.*s%.8s", url.length, url.start, output_user_id);
            } else {
                url = tprintf("%.*s,%.8s", url.length, url.start, output_user_id);
            }
        }
    }

    platform_api_request(LOAD_USERS_REQUEST, url, Http_Get, NULL);
}

static void request_multiple_custom_fields(Array<Custom_Field_Id> custom_fields) {
    assert(custom_fields.length > 0);

    String url = tprintf("customfields/");

    for (Custom_Field_Id * it = custom_fields.data; it != custom_fields.data + custom_fields.length; it++) {
        Custom_Field_Id custom_field_id = *it;

        u32 hash = hash_id(custom_field_id);

        if (!is_custom_field_requested(custom_field_id, hash)) {
            mark_custom_field_as_requested(custom_field_id, hash);

            u8 output_custom_field_and_account_id[16];

            fill_id16('A', account.id, 'M', custom_field_id, output_custom_field_and_account_id);

            if (it == custom_fields.data) {
                url = tprintf("%.*s%.16s", url.length, url.start, output_custom_field_and_account_id);
            } else {
                url = tprintf("%.*s,%.16s", url.length, url.start, output_custom_field_and_account_id);
            }
        }
    }

    platform_api_request(LOAD_CUSTOM_FIELDS_REQUEST, url, Http_Get, NULL);
}

static void request_last_selected_folder_if_present() {
    char* last_selected_folder = platform_local_storage_get("last_selected_folder");

    bool has_requested_a_folder = false;

    if (last_selected_folder) {
        Folder_Id folder_id;

        if (string_to_int(&folder_id, last_selected_folder, 10) == STR2INT_SUCCESS) {
            select_and_request_folder_by_id(folder_id);

            has_requested_a_folder = true;
        }
    }

    if (!has_requested_a_folder) {
        select_and_request_folder_by_id(ROOT_FOLDER);
    }
}

static void request_account_data() {
    request_folder_children_for_folder_tree(ROOT_FOLDER);
    request_last_selected_folder_if_present();
}

extern "C"
EXPORT
void api_request_success(Request_Id request_id, char* content, u32 content_length, void* data) {
//    printf("Got request %lu with content at %p\n", request_id, (void*) content_json);
    Json_With_Tokens json_with_tokens;
    json_with_tokens.json = content;
    json_with_tokens.tokens = parse_json_into_tokens(content, content_length, json_with_tokens.num_tokens);

    if (request_id == FOLDER_TREE_CHILDREN_REQUEST) {
        // TODO @Leak content is leaked
        process_folder_tree_children_request((Folder_Id) (intptr_t) data, content, json_with_tokens.tokens, json_with_tokens.num_tokens);
    } else if (request_id == NOTIFICATION_MARK_AS_READ_REQUEST) {
        // TODO @Leak content is leaked
        process_json_data_segment(content, json_with_tokens.tokens, json_with_tokens.num_tokens, process_inbox_data);
    } else if (request_id == LOAD_USERS_REQUEST) {
        // TODO @Leak content is leaked
        process_json_data_segment(content, json_with_tokens.tokens, json_with_tokens.num_tokens, process_users_data);
    } else if (request_id == LOAD_CUSTOM_FIELDS_REQUEST) {
        // TODO @Leak content is leaked
        process_json_data_segment(content, json_with_tokens.tokens, json_with_tokens.num_tokens, process_custom_fields_data);
    } else if (request_id == me_request) {
        me_request = NO_REQUEST;
        process_json_data_segment(content, json_with_tokens.tokens, json_with_tokens.num_tokens, process_users_data);
        finished_loading_me_at = tick;
    } else if (request_id == starred_folders_request) {
        starred_folders_request = NO_REQUEST;

        process_json_content(starred_folders_json_content, process_starred_folders_data, json_with_tokens);
    } else if (request_id == spaces_request) {
        spaces_request = NO_REQUEST;

        process_json_content(spaces_json_content, process_spaces_data, json_with_tokens);
    } else if (request_id == spaces_folders_request) {
        spaces_folders_request = NO_REQUEST;

        process_json_content(spaces_folders_json_content, process_spaces_folders_data, json_with_tokens);
    } else if (request_id == folders_request) {
        folders_request = NO_REQUEST;

        // TODO @Leak content is leaked
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
    } else if (request_id == task_comments_request) {
        task_comments_request = NO_REQUEST;

        process_json_data_segment(json_with_tokens.json, json_with_tokens.tokens, json_with_tokens.num_tokens, process_task_comments_data);
        finished_loading_task_comments_at = tick;

        FREE(json_with_tokens.json);
    } else if (request_id == account_request) {
        account_request = NO_REQUEST;

        process_json_content(account_json_content, process_account_data, json_with_tokens);

        printf("Received account, account.id %d\n", account.id);

        platform_local_storage_set("account_id", tprintf("%i", account.id));

        request_account_data();

        finished_loading_account_at = tick;
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
    } else if (request_id == inbox_request) {
        inbox_request = NO_REQUEST;
        process_json_content(inbox_json_content, process_inbox_data, json_with_tokens);
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

        return;
    }

    Space* space_or_null = find_space_by_avatar_request_id(request_id);

    if (space_or_null) {
        Memory_Image avatar{};
        avatar.width = width;
        avatar.height = height;

        load_image_into_gpu_memory(avatar, pixel_data);

        set_space_avatar_image(space_or_null, avatar);
    }

    free(pixel_data);
}

extern "C"
EXPORT
void disk_image_load_success(Image_Load_Callback callback, u8* pixel_data, u32 width, u32 height) {
    Memory_Image image;

    image.width = width;
    image.height = height;

    load_image_into_gpu_memory(image, pixel_data);

    free(pixel_data);

    callback(image);
}

void select_and_request_folder_by_id(Folder_Id id) {
    u8 output_account_and_folder_id[16];
    u32 id_length = (int) ARRAY_SIZE(output_account_and_folder_id);
    fill_id16('A', account.id, 'G', id, output_account_and_folder_id);

    set_current_folder_id(id);
    current_view = View_Task_List;

    platform_local_storage_set("last_selected_folder", tprintf("%i", id));

    api_request(Http_Get, folder_contents_request, "folders/%.*s/tasks%s", id_length, output_account_and_folder_id,
                "?fields=['customFields','superTaskIds','parentIds','responsibleIds']&subTasks=true");

    if (id >= 0) {
        api_request(Http_Get, folder_header_request, "folders/%.*s%s", id_length, output_account_and_folder_id, "?fields=['customColumnIds']");
    } else {
        folder_header_request = NO_REQUEST;
        process_current_folder_as_logical();
    }

    started_loading_folder_contents_at = tick;
}

void request_task_by_task_id(Task_Id task_id) {
    u8 output_account_and_task_id[16];

    fill_id16('A', account.id, 'T', task_id, output_account_and_task_id);

    selected_folder_task_id = task_id;

    api_request(Http_Get, task_request, "tasks/%.16s?fields=['inheritedCustomColumnIds']", output_account_and_task_id);
    api_request(Http_Get, task_comments_request, "tasks/%.16s/comments", output_account_and_task_id);

    started_loading_task_at = tick;

    task_view_open_requested = true;
}

void modify_task_e16(Task_Id task_id, const u8 entity_prefix, s32 entity_id, const char* command, bool array = true) {
    u8 output_account_and_task_id[16];
    u8 output_entity_id[16];

    fill_id16('A', account.id, 'T', task_id, output_account_and_task_id);
    fill_id16('A', account.id, entity_prefix, entity_id, output_entity_id);

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

    fill_id16('A', account.id, 'T', task_id, output_account_and_task_id);
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

void mark_notification_as_read(Inbox_Notification_Id notification_id) {
    u8 output_account_and_notification_id[16];
    fill_id16('A', account.id, 'N', notification_id, output_account_and_notification_id);

    String url = tprintf("internal/notifications/%.16s?unread=false", output_account_and_notification_id);

    platform_api_request(NOTIFICATION_MARK_AS_READ_REQUEST, url, Http_Put);
}

void ImGui::FadeInOverlay(float alpha, u32 color) {
    if (alpha >= 0.99f) {
        return;
    }

    float rounding = ImGui::GetStyle().FrameRounding;

    ImVec2 top_left = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    u32 overlay_color = (color & 0x00FFFFFF) | ((255 - (u32) (alpha * 255.0f)) << 24);

    ImGui::GetForegroundDrawList()->AddRectFilled(top_left, top_left + size, overlay_color, rounding);
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

    ImGui::GetForegroundDrawList()->AddText(top_left, IM_COL32_BLACK, text_start, text_end);
}

#if DEBUG_MEMORY
static void draw_memory_debug_contents() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 16, 16 });
    ImGui::BeginChildFrame(ImGui::GetID("memory_debug"), ImVec2(-1, -1));

    ImGuiIO& io = ImGui::GetIO();

    ImGui::Text("%f %f", io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Text("%f %f", io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

    if (ImGui::ListBoxHeader("Memory allocations", ImVec2(-1, -1))) {
        draw_memory_records();

        ImGui::ListBoxFooter();
    }

    ImGui::EndChildFrame();
    ImGui::PopStyleVar();
}
#endif

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

        bool task_is_loading = task_request != NO_REQUEST;

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

static void draw_loading_screen() {
    ImVec2 screen_center = ImGui::GetIO().DisplaySize / 2.0f;
    ImVec2 image_size{ (float) logo.width, (float) logo.height };

    float spinner_side = image_size.y / 5.0f;

    ImVec2 image_top_left = screen_center - (image_size / 2.0f);
    ImVec2 spinner_top_left = image_top_left + ImVec2(image_size.x + 20.0f, image_size.y / 2.0f - spinner_side / 2.0f);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddImage((void*) (uintptr_t) logo.texture_id, image_top_left, image_top_left + image_size);
    draw_loading_spinner(draw_list, spinner_top_left, spinner_side, 6, color_link);
}

static void draw_ui() {
    bool loading_me = me_request != NO_REQUEST;

    if (loading_me) {
        draw_loading_screen();

        return;
    } else if (started_showing_main_ui_at == 0) {
        started_showing_main_ui_at = tick;
    }

    if (tick - started_showing_main_ui_at < 24) {
        float alpha = lerp(started_showing_main_ui_at, tick, 1.0f, 24);

        ImGui::FadeInOverlay(alpha, color_background_dark);
    }

    bool draw_side_menu_this_frame = draw_side_menu;

    draw_header(draw_side_menu_this_frame, draw_side_menu, folder_tree_column_width);
    draw_average_frame_time();

    if (draw_side_menu_this_frame) {
        draw_folder_tree(folder_tree_column_width * platform_get_pixel_ratio());
        ImGui::SameLine();
    }

    switch (current_view) {
        case View_Task_List: {
            draw_task_list();
            break;
        }

        case View_Inbox: {
            draw_inbox();
            break;
        }

#if DEBUG_MEMORY
        case View_Memory: {
            draw_memory_debug_contents();
            break;
        }
#endif
    }

    draw_task_view_popup_if_necessary();

    // May want to control how often this happens

    Temporary_List<User_Id> user_request_queue = get_and_clear_user_request_queue();

    if (user_request_queue.length > 0) {
        request_multiple_users(list_to_array(&user_request_queue));
    }

    Temporary_List<Custom_Field_Id> custom_field_request_queue = get_and_clear_custom_field_request_queue();

    if (custom_field_request_queue.length > 0) {
        request_multiple_custom_fields(list_to_array(&custom_field_request_queue));
    }
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
    platform_render_frame();

    last_frame_vtx_count = (u32) ImGui::GetDrawData()->TotalVtxCount;
    frame_times[tick % (ARRAY_SIZE(frame_times))] = platform_get_delta_time_ms(frame_start_time); // Before assumed swapBuffers
}

void load_persisted_settings() {
    char* account_id_string = platform_local_storage_get("account_id");

    if (account_id_string) {
        printf("Account %s found in settings\n", account_id_string);

        if (string_to_int(&account.id, account_id_string, 10) == STR2INT_SUCCESS) {
            if (account.id != NO_ACCOUNT) {
                request_account_data();
            }
        }
    } else {
        api_request(Http_Get, account_request, "account");

        printf("Account id not found in settings\n");
    }
}

static const char* default_font = "resources/OpenSans-Regular.ttf";

static ImFont* load_font(const char* path, float size) {
    float pixel_ratio = platform_get_pixel_ratio();

    ImGuiIO &io = ImGui::GetIO();

    ImFontConfig font_config{};
    font_config.OversampleH = 3;
    font_config.OversampleV = 1;

    if (strcmp(path, default_font) == 0) {
        static size_t file_size = 0;
        static void* sans_regular = ImFileLoadToMemory("resources/OpenSans-Regular.ttf", "r", &file_size);

        return io.Fonts->AddFontFromMemoryTTF(sans_regular, file_size, (size) * pixel_ratio, &font_config, io.Fonts->GetGlyphRangesCyrillic());
    } else {
        return io.Fonts->AddFontFromFileTTF(path, (size) * pixel_ratio, &font_config, io.Fonts->GetGlyphRangesCyrillic());
    }
}

static void setup_ui() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle* style = &ImGui::GetStyle();
    ImGui::StyleColorsLight(style);

    style->Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(color_background_dark);
    style->FrameRounding = 4.0f;
    style->WindowPadding = { 0, 0 };
    style->FramePadding = { 0, 0 };
    style->ItemSpacing = ImVec2(style->ItemSpacing.x, 0);
    style->PopupBorderSize = 0;
    style->ScaleAllSizes(platform_get_pixel_ratio());

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsResizeFromEdges = true;

    const float default_font_size = 16.0f;

    // font_regular should be loaded first, so it becomes a default font
    font_regular = load_font(default_font, default_font_size);

    font_28px = load_font(default_font, 28.0f);
    font_19px = load_font(default_font, 19.0f);
    font_19px_bold = load_font("resources/OpenSans-Bold.ttf", 19.0f);
    font_bold = load_font("resources/OpenSans-Bold.ttf", default_font_size);
    font_italic = load_font("resources/OpenSans-Italic.ttf", default_font_size);
    font_bold_italic = load_font("resources/OpenSans-BoldItalic.ttf", default_font_size);

    u8* pixels = NULL;
    s32 width = 0;
    s32 height = 0;

    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->TexID = (void*) (intptr_t) platform_make_texture(width, height, pixels);
    io.Fonts->ClearTexData();
}

static void* imgui_malloc_wrapper(size_t size, void* user_data) {
    return MALLOC(size);
}

static void imgui_free_wrapper(void* ptr, void* user_data) {
    if (ptr) {
        FREE(ptr);
    }
}

EXPORT
bool init() {
    platform_early_init();

    init_user_storage();
    init_custom_field_storage();
    init_folder_tree();

    api_request(Http_Get, me_request, "contacts?me=true");
    api_request(Http_Get, inbox_request, "internal/notifications?notificationTypes=['Assign','Mention']");
    api_request(Http_Get, workflows_request, "workflows");
    api_request(Http_Get, spaces_request, "internal/spaces?type=User");
    api_request(Http_Get, starred_folders_request, "folders?starred&fields=['color']");
    api_request(Http_Get, suggested_folders_request, "folders?suggestedParents&fields=['color']");
    api_request(Http_Get, suggested_contacts_request, "internal/contacts?suggestType=Responsibles");

    load_persisted_settings();

    ImGui::CreateContext();

    ImGui::SetAllocatorFunctions(imgui_malloc_wrapper, imgui_free_wrapper);

    bool result = platform_init();

    setup_ui();

    printf("Platform init: %s\n", result ? "true" : "false");

    load_task_view_resources();

    load_png_from_disk_async("resources/wrike_logo.png", [](Memory_Image image) {
        logo = image;

        set_header_logo(logo);
    });

    return result;
}

EXPORT
int main() {
    if (!init()) {
        return -1;
    }

    platform_loop();
}