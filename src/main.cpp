#include "main.h"
#include "funimgui.h"
#include <imgui.h>
#include "framemonitor.h"
#include <cmath>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>

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

static FrameMonitor* frame_monitor = nullptr;

const Request_Id NO_REQUEST = -1;

Request_Id folder_tree_request = NO_REQUEST;
Request_Id folder_header_request = NO_REQUEST;
Request_Id folder_contents_request = NO_REQUEST;
Request_Id task_request = NO_REQUEST;
Request_Id contacts_request = NO_REQUEST;
Request_Id accounts_request = NO_REQUEST;
Request_Id workflows_request = NO_REQUEST;
Request_Id modify_task_request = NO_REQUEST;
Request_Id suggested_folders_request = NO_REQUEST;
Request_Id suggested_contacts_request = NO_REQUEST;

bool had_last_selected_folder_so_doesnt_need_to_load_the_root_folder = false;
bool custom_statuses_were_loaded = false;

static bool draw_memory_debug = false;
static bool draw_side_menu = false;

static Memory_Image logo;

static Account_Id selected_account_id;

Task_Id selected_folder_task_id = 0;

static Folder_Tree_Node* selected_node = NULL; // TODO should be a Task_Id

Task current_task{};

static char* task_json_content = NULL;
static char* users_json_content = NULL;
static char* accounts_json_content = NULL;
static char* folder_tasks_json_content = NULL;
static char* workflows_json_content = NULL;
static char* folder_header_json_content = NULL;
static char* suggested_folders_json_content = NULL; // TODO looks like a lot of waste
static char* suggested_users_json_content = NULL; // TODO also there

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

static Request_Id request_id_counter = 0;

List<Folder_Tree_Node*> folder_tree_search_result{};

char search_buffer[128];

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

    api_request(Http_Get, suggested_folders_request, "accounts/%.*s/folders?suggestedParents&fields=['color']", (u32) ARRAY_SIZE(output_account_id), output_account_id);
    api_request(Http_Get, suggested_contacts_request, "internal/accounts/%.*s/contacts?suggestType=Responsibles", (u32) ARRAY_SIZE(output_account_id), output_account_id);
}

extern "C"
EXPORT
void api_request_success(Request_Id request_id, char* content, u32 content_length) {
//    printf("Got request %lu with content at %p\n", request_id, (void*) content_json);
    Json_With_Tokens json_with_tokens;
    json_with_tokens.json = content;
    json_with_tokens.tokens = parse_json_into_tokens(content, content_length, json_with_tokens.num_tokens);

    if (request_id == folder_tree_request) {
        folder_tree_request = NO_REQUEST;
        process_folder_tree_request(content, json_with_tokens.tokens, json_with_tokens.num_tokens);
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

        select_account();
        request_suggestions_for_account(selected_account_id);
        request_workflow_for_account(selected_account_id);
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

void select_folder_node_and_request_contents_if_necessary(Folder_Tree_Node* folder_node) {
    selected_node = folder_node;

    u8* account_and_folder_id = (u8*) talloc(sizeof(u8) * 16);

    fill_id16('A', selected_account_id, 'G', folder_node->id, account_and_folder_id);

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
}

void modify_task_e16(Task_Id task_id, const u8 entity_prefix, s32 entity_id, const char* command) {
    u8 output_account_and_task_id[16];
    u8 output_entity_id[16];

    fill_id16('A', selected_account_id, 'T', task_id, output_account_and_task_id);
    fill_id16('A', selected_account_id, entity_prefix, entity_id, output_entity_id);

    api_request(Http_Put, modify_task_request, "tasks/%.*s?%s=[\"%.*s\"]",
                (u32) ARRAY_SIZE(output_account_and_task_id), output_account_and_task_id,
                command,
                (u32) ARRAY_SIZE(output_entity_id), output_entity_id
    );
}

void modify_task_e8(Task_Id task_id, const u8 entity_prefix, s32 entity_id, const char* command) {
    u8 output_account_and_task_id[16];
    u8 output_entity_id[8];

    fill_id16('A', selected_account_id, 'T', task_id, output_account_and_task_id);
    fill_id8(entity_prefix, entity_id, output_entity_id);

    api_request(Http_Put, modify_task_request, "tasks/%.*s?%s=[\"%.*s\"]",
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

void draw_folder_tree_node(Folder_Tree_Node* tree_node) {
    for (int i = 0; i < tree_node->num_children; i++) {
        Folder_Tree_Node* child_node = tree_node->children[i];
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow;

        bool leaf_node = child_node->num_children == 0;

        if (leaf_node) {
            node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (!child_node->name.start) {
            continue;
        }

        ImGui::PushID(child_node->id);
        bool node_open = ImGui::TreeNodeEx(child_node, node_flags, "%.*s", child_node->name.length, child_node->name.start);
        ImGui::PopID();

        if (ImGui::IsItemClicked()) {
            select_folder_node_and_request_contents_if_necessary(child_node);
        }

        if (node_open && !leaf_node) {
            draw_folder_tree_node(child_node);

            ImGui::TreePop();
        }
    }
}

void ImGui::LoadingIndicator(u32 started_showing_at) {
    float scale = platform_get_pixel_ratio();
    ImVec2 cursor = ImGui::GetCursorScreenPos() + (ImVec2(12, 12) * scale);
    const float speed_scale = 10.0f;
    float cos = cosf(tick / speed_scale);
    float sin = sinf(tick / speed_scale);
    float size = scale * 10.0f;

    u32 alpha = (u32) roundf(lerp(started_showing_at, tick, 255, 14));

    ImGui::GetWindowDrawList()->AddQuadFilled(
            cursor + ImRotate(ImVec2(-size, -size), cos, sin),
            cursor + ImRotate(ImVec2(+size, -size), cos, sin),
            cursor + ImRotate(ImVec2(+size, +size), cos, sin),
            cursor + ImRotate(ImVec2(-size, +size), cos, sin),
            IM_COL32(0, 255, 200, alpha)
    );
}

ImVec2 get_scaled_image_size(Memory_Image& image) {
    // Assume original image is retina-sized
    ImVec2 size{ (float) image.width, (float) image.height };
    return size / 2.0f * platform_get_pixel_ratio();
}

void ImGui::Image(Memory_Image& image) {
    ImGui::Image((void*)(intptr_t) image.texture_id, get_scaled_image_size(image));
}

void ImGui::FadeInOverlay(float alpha) {
    float rounding = ImGui::GetStyle().FrameRounding;

    ImVec2 top_left = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    u32 overlay_color = IM_COL32(255, 255, 255, 255 - (u32) (alpha * 255.0f));

    ImGui::GetOverlayDrawList()->AddRectFilled(top_left, top_left + size, overlay_color, rounding);
}

void draw_folder_tree_search_input() {
    static const u32 bottom_border_active_color = argb_to_agbr(0xFF4488ff);
    static const u32 bottom_border_hover_color = argb_to_agbr(0x99ffffff);
    static const u32 non_active_color = 0x80ffffff;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, color_background_dark);
    ImGui::PushStyleColor(ImGuiCol_Text, 0xFFFFFFFF);

    ImVec2 placeholder_text_position = ImGui::GetCursorPos() + ImGui::GetStyle().FramePadding;

    ImGui::PushItemWidth(-1);

    if (ImGui::InputText("##tree_search", search_buffer, ARRAY_SIZE(search_buffer))) {
        u64 search_start = platform_get_app_time_precise();

        folder_tree_search(search_buffer, &folder_tree_search_result);

        printf("Took %f to search %i elements by %s\n", platform_get_delta_time_ms(search_start), total_nodes, search_buffer);
    }

    ImGui::PopItemWidth();

    ImVec2 input_rect_min = ImGui::GetItemRectMin();
    ImVec2 input_rect_max = ImGui::GetItemRectMax();

    ImVec2 post_input = ImGui::GetCursorPos();

    bool is_active = ImGui::IsItemActive();
    bool is_hovered = ImGui::IsItemHovered();

    if (strlen(search_buffer) == 0) {
        ImGui::SetCursorPos(placeholder_text_position);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(non_active_color), "Filter");
    }

    ImVec2& frame_padding = ImGui::GetStyle().FramePadding;

    u32 bottom_border_color = non_active_color;

    if (is_active) {
        bottom_border_color = bottom_border_active_color;
    } else if (is_hovered) {
        bottom_border_color = bottom_border_hover_color;
    }

    ImGui::GetWindowDrawList()->AddLine(
            ImVec2(input_rect_min.x, input_rect_max.y) + frame_padding,
            input_rect_max + frame_padding,
            bottom_border_color,
            1.0f
    );

    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(post_input);
}

void draw_folder_tree() {
    draw_folder_tree_search_input();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, color_background_dark);
    ImGui::PushStyleColor(ImGuiCol_Text, 0xFFFFFFFF);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, color_background_dark);
    ImGui::ListBoxHeader("##folder_tree", ImVec2(-1, -1));

    if (folder_tree_request != NO_REQUEST) {
        ImGui::LoadingIndicator(0);
    }

    u32 buffer_length = strlen(search_buffer);
    if (buffer_length > 0 && folder_tree_search_result.data) {
        for (Folder_Tree_Node** node_pointer = folder_tree_search_result.data; node_pointer != folder_tree_search_result.data + folder_tree_search_result.length; node_pointer++) {
            Folder_Tree_Node* node = *node_pointer;
            char* name = string_to_temporary_null_terminated_string(node->name);

            ImGui::PushID(node->id);
            if (ImGui::Selectable(name)) {
                select_folder_node_and_request_contents_if_necessary(node);
            }
            ImGui::PopID();
        }
    } else {
        if (total_starred > 0) {
            for (u32 node_index = 0; node_index < total_starred; node_index++) {
                Folder_Tree_Node* starred_node = starred_nodes[node_index];

                char* name = string_to_temporary_null_terminated_string(starred_node->name);

                if (ImGui::Selectable(name)) {
                    select_folder_node_and_request_contents_if_necessary(starred_node);
                }
            }

            ImGui::Separator();
        }

        if (root_node) {
            draw_folder_tree_node(root_node);
        }
    }

    ImGui::ListBoxFooter();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();
}

void draw_side_menu_toggle_button(const ImVec2& size) {
    float scale = platform_get_pixel_ratio();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 left_side = ImGui::GetCursorScreenPos();
    const ImVec2 button_size = size * scale;
    const float bar_height = button_size.y / 5.0f;
    const u32 bar_color = argb_to_agbr(0xffa9b3bd);
    const u32 bar_color_hover = argb_to_agbr(0xffd4d9de);
    const float middle_height = button_size.y / 2.0f;
    const float rounding = 2.0f * scale; // TODO this doesn't properly round on 1920x1080

    ImVec2 right_side = left_side + ImVec2(button_size.x, bar_height);

    if (ImGui::InvisibleButton("side_menu_toggle", button_size)) {
        draw_side_menu = !draw_side_menu;
    }

    const u32 color = ImGui::IsItemHovered() ? bar_color_hover : bar_color;

    draw_list->AddRectFilled(left_side,
                             right_side,
                             color, rounding);

    draw_list->AddRectFilled(left_side + ImVec2(0.0f, middle_height - bar_height / 2.0f),
                             right_side + ImVec2(0.0f, middle_height - bar_height / 2.0f),
                             color, rounding);

    draw_list->AddRectFilled(left_side + ImVec2(0.0f, button_size.y - bar_height),
                             right_side + ImVec2(0.0f, button_size.y - bar_height),
                             color, rounding);
}

void draw_ui() {
    // TODO temporary code, desktop only
    static const u32 d_key_in_sdl = 7;

    if (ImGui::IsKeyPressed(d_key_in_sdl) && ImGui::GetIO().KeyCtrl) {
        draw_memory_debug = !draw_memory_debug;
    }

    if (draw_memory_debug) {
        ImGuiIO& io = ImGui::GetIO();

        ImGui::Text("%f %f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("%f %f", io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);

        if (ImGui::ListBoxHeader("Memory allocations", ImVec2(-1, -1))) {
            draw_memory_records();

            ImGui::ListBoxFooter();
        }

        return;
    }

    bool draw_side_menu_this_frame = draw_side_menu;

    ImVec2 logo_size = get_scaled_image_size(logo);

    if (draw_side_menu_this_frame) {
        ImGui::Image(logo);
    } else {
        ImGui::Dummy(ImVec2(0, logo_size.y));
    }

    ImGui::SameLine();

    const ImVec2 toggle_button_size = ImVec2(20.0f, 18.0f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + logo_size.y / 2.0f - toggle_button_size.y / 2.0f);

    draw_side_menu_toggle_button(toggle_button_size);

    frame_monitor->drawAverage();

    ImGui::Columns(draw_side_menu_this_frame ? 3 : 2);

//    if (ImGui::IsWindowAppearing()) {
//        ImGui::SetColumnWidth(0, 300.0f);
//    }

    if (draw_side_menu_this_frame) {
        draw_folder_tree();
        ImGui::NextColumn();
    }

    draw_task_list();

    ImGui::NextColumn();

    bool task_is_loading = task_request != NO_REQUEST || contacts_request != NO_REQUEST;

    if (selected_folder_task_id && !task_is_loading) {
        draw_task_contents();
    } else {
        ImGui::ListBoxHeader("##task_content", ImVec2(-1, -1));

        if (task_is_loading) {
            ImGui::LoadingIndicator(MIN(started_loading_task_at, started_loading_users_at));
        }

        ImGui::ListBoxFooter();
    }

    ImGui::Columns(1);
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
void loop()
{
    frame_monitor->startFrame();

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
    FunImGui::RenderDrawLists(ImGui::GetDrawData());
    frame_monitor->endFrame(); // Before assumed swapBuffers
    platform_end_frame();
}

void load_persisted_settings() {
    char* last_selected_folder = platform_local_storage_get("last_selected_folder");
    char* last_selected_task = platform_local_storage_get("last_selected_task");

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

static void setup_ui_style() {
    ImGuiStyle* style = &ImGui::GetStyle();
    ImGui::StyleColorsLight(style);

    style->Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(color_background_dark);
    style->FrameRounding = 4.0f;
    style->ScaleAllSizes(platform_get_pixel_ratio());
}

static void* imgui_malloc_wrapper(size_t size, void* user_data) {
    (void) user_data;
    return malloc(size);
}

static void imgui_free_wrapper(void* ptr, void* user_data) {
    (void) user_data;
    // Imgui does that
    if (ptr) {
        free(ptr);
    }
}

EXPORT
bool init()
{
    init_temporary_storage();
    create_imgui_context();

    ImGui::SetAllocatorFunctions(imgui_malloc_wrapper, imgui_free_wrapper);

    bool result = platform_init();

    setup_ui_style();

    api_request(Http_Get, accounts_request, "accounts?fields=['customFields']");
    api_request(Http_Get, folder_tree_request, "folders?fields=['starred','color']");
    api_request(Http_Get, contacts_request, "contacts");

    started_loading_users_at = tick;

    folder_tree_init();

    load_persisted_settings();

    printf("Platform init: %s\n", result ? "true" : "false");

    frame_monitor = new FrameMonitor;

    load_png_from_disk("resources/wrike_logo.png", logo);

    return result;
}

EXPORT
int main() {
    if (!init()) {
        return -1;
    }

    platform_loop();
}