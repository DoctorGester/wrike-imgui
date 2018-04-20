#include "main.h"
#include "draw.h"
#include "funimgui.h"
#include <imgui.h>
#include "framemonitor.h"
#include <emscripten/emscripten.h>
#include <cmath>
#include <chrono>
#include <imgui_internal.h>
#include <jsmn.h>

#include "download.h"
#include "jsmn.h"
#include "folder_tree.h"
#include "common.h"
#include "json.h"
#include "temporary_storage.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "hash_map.h"
#include "task_list.h"
#include "accounts.h"
#include "task_view.h"

#define PRINTLIKE(string_index, first_to_check) __attribute__((__format__ (__printf__, string_index, first_to_check)))

static Draw GDraw;
static FrameMonitor* gFrameMonitor = nullptr;

typedef signed long Request_Id;

const Request_Id NO_REQUEST = -1;

static Request_Id request_id_counter = 0;

Request_Id folder_tree_request = NO_REQUEST;
Request_Id folder_header_request = NO_REQUEST;
Request_Id folder_contents_request = NO_REQUEST;
Request_Id task_request = NO_REQUEST;
Request_Id contacts_request = NO_REQUEST;
Request_Id accounts_request = NO_REQUEST;
Request_Id workflows_request = NO_REQUEST;

bool had_last_selected_folder_so_doesnt_need_to_load_the_root_folder = false;

static bool draw_side_menu = true;

// TODO use temporary storage there
static char temporary_request_buffer[512];

static Memory_Image logo;

Id16 selected_folder_task_id;

static Folder_Tree_Node* selected_node = NULL; // TODO should be a Task_Id

Task current_task{};

User* users = NULL;
u32 users_count = 0;

static char* task_json_content = NULL;
static char* users_json_content = NULL;
static char* accounts_json_content = NULL;
static char* folder_tasks_json_content = NULL;
static char* workflows_json_content = NULL;
static char* folder_header_json_content = NULL;

u32 tick = 0;

u32 started_loading_folder_contents_at = 0;
u32 finished_loading_folder_contents_at = 0;
u32 finished_loading_folder_header_at = 0;

u32 started_loading_task_at = 0;
u32 finished_loading_task_at = 0;

u32 started_loading_statuses_at = 0;
u32 finished_loading_statuses_at = 0;

PRINTLIKE(2, 3) static void api_request(Request_Id& request_id, const char* format, ...) {
    va_list args;
    va_start(args, format);

    vsprintf(temporary_request_buffer, format, args);

    va_end(args);

    EM_ASM({ api_get(Pointer_stringify($0), $1) }, &temporary_request_buffer[0], request_id_counter);

    request_id = request_id_counter++;
}

static void process_users_data_object(char* json, jsmntok_t*&token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    User* user = &users[users_count++];

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_id(json, next_token, user->id);
        } else if (json_string_equals(json, property_token, "firstName")) {
            json_token_to_string(json, next_token, user->firstName);
        } else if (json_string_equals(json, property_token, "lastName")) {
            json_token_to_string(json, next_token, user->lastName);
        } else {
            eat_json(token);
            token--;
        }
    }
}

static void process_json_content(char*& json_reference, Data_Process_Callback callback, char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (json_reference) {
        free(json_reference);
    }

    json_reference = json;

    process_json_data_segment(json, tokens, num_tokens, callback);
}

static void process_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (users_count < data_size) {
        users = (User*) realloc(users, sizeof(User) * data_size);
    }

    users_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_users_data_object(json, token);
    }
}

static void request_workflows_for_each_account() {
    // TODO this will have issues with multiple accounts because it sets global variables
    for (u32 index = 0; index < accounts_count; index++) {
        Account& account = accounts[index];
        api_request(workflows_request, "accounts/%.*s/workflows", ID_8_LENGTH, account.id.id);
    }

    started_loading_statuses_at = tick;
}

extern "C"
EMSCRIPTEN_KEEPALIVE
void api_request_success(Request_Id request_id, char* content_json) {
    printf("Got request %lu with content at %p\n", request_id, (void*) content_json);

    u32 parsed_tokens;
    jsmntok_t* json_tokens = parse_json_into_tokens(content_json, parsed_tokens);

    // TODO simplify, beautify! 3 last arguments are the same, can be a struct
    if (request_id == folder_tree_request) {
        folder_tree_request = NO_REQUEST;
        process_folder_tree_request(content_json, json_tokens, parsed_tokens);
    } else if (request_id == folder_contents_request) {
        folder_contents_request = NO_REQUEST;

        process_json_content(folder_tasks_json_content, process_folder_contents_data, content_json, json_tokens, parsed_tokens);
        finished_loading_folder_contents_at = tick;
    } else if (request_id == folder_header_request) {
        folder_header_request = NO_REQUEST;

        process_json_content(folder_header_json_content, process_folder_header_data, content_json, json_tokens, parsed_tokens);
        finished_loading_folder_header_at = tick;
    } else if (request_id == task_request) {
        task_request = NO_REQUEST;

        process_json_content(task_json_content, process_task_data, content_json, json_tokens, parsed_tokens);
        finished_loading_task_at = tick;
    } else if (request_id == contacts_request) {
        contacts_request = NO_REQUEST;
        process_json_content(users_json_content, process_users_data, content_json, json_tokens, parsed_tokens);
    } else if (request_id == accounts_request) {
        accounts_request = NO_REQUEST;
        process_json_content(accounts_json_content, process_accounts_data, content_json, json_tokens, parsed_tokens);

        request_workflows_for_each_account();
    } else if (request_id == workflows_request) {
        workflows_request = NO_REQUEST;
        process_json_content(workflows_json_content, process_workflows_data, content_json, json_tokens, parsed_tokens);
    }
}

int levenstein_cache[1024];

int levenshtein(const char* a, const char* b, u32 a_length, u32 b_length) {
    assert(a_length <= ARRAY_SIZE(levenstein_cache));

    int index = 0;
    int bIndex = 0;
    int distance;
    int bDistance;
    int result;
    char code;

    /* Shortcut optimizations / degenerate cases. */
    if (a == b) {
        return 0;
    }

    if (a_length == 0) {
        return b_length;
    }

    if (b_length == 0) {
        return a_length;
    }

    /* initialize the vector. */
    while (index < a_length) {
        levenstein_cache[index] = index + 1;
        index++;
    }

    /* Loop. */
    while (bIndex < b_length) {
        code = b[bIndex];
        result = distance = bIndex++;
        index = -1;

        while (++index < a_length) {
            bDistance = code == a[index] ? distance : distance + 1;
            distance = levenstein_cache[index];

            levenstein_cache[index] = result = distance > result
                                    ? bDistance > result
                                      ? result + 1
                                      : bDistance
                                    : bDistance > distance
                                      ? distance + 1
                                      : bDistance;
        }
    }

    return result;
}

void request_folder_contents(String &folder_id) {
    EM_ASM({ local_storage_set(Pointer_stringify($0), Pointer_stringify($1, $2)) },
           "last_selected_folder",
           folder_id.start,
           folder_id.length
    );

    api_request(folder_contents_request, "folders/%.*s/tasks%s", (int) folder_id.length, folder_id.start, "?fields=['customFields']");
    api_request(folder_header_request, "folders/%.*s%s", (int) folder_id.length, folder_id.start, "?fields=['customColumnIds']");

    started_loading_folder_contents_at = tick;
}

void select_folder_node_and_request_contents_if_necessary(Folder_Tree_Node* folder_node) {
    selected_node = folder_node;

    request_folder_contents(selected_node->id);
}

void request_task(Id16& task_id) {
    selected_folder_task_id = task_id;

    EM_ASM({ local_storage_set(Pointer_stringify($0), Pointer_stringify($1, $2)) },
           "last_selected_task",
           task_id.id,
           ID_16_LENGTH
    );

    api_request(task_request, "tasks/%.*s", ID_16_LENGTH, task_id.id);
    started_loading_task_at = tick;
}

void draw_folder_tree_node(Folder_Tree_Node* tree_node) {
    for (int i = 0; i < tree_node->num_children; i++) {
        Folder_Tree_Node* child_node = tree_node->children[i];
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow;

        bool leaf_node = child_node->num_children == 0;

        if (leaf_node) {
            node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        String& id = child_node->id;

        ImGui::PushID(id.start, id.start + id.length);
        bool node_open = ImGui::TreeNodeEx((char*) NULL, node_flags, "%.*s", child_node->name.length, child_node->name.start);
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

char search_buffer[128];


void ImGui::LoadingIndicator(u32 started_showing_at) {
    float scale = (float) emscripten_get_device_pixel_ratio();
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
    return size / 2.0f * emscripten_get_device_pixel_ratio();
}

void ImGui::Image(Memory_Image& image) {
    ImGui::Image((void*)(intptr_t) image.texture_id, get_scaled_image_size(image));
}

struct Sorted_Node {
    Folder_Tree_Node* source_node;
    s32 distance;
};

static Sorted_Node* sorted_nodes = NULL;

int compare_nodes(const Sorted_Node& a, const Sorted_Node& b) {
    //s32 a_result = levenshtein(a->name.start, search_buffer, a->name.length, buffer_length);
    //s32 b_result = levenshtein(b->name.start, search_buffer, b->name.length, buffer_length);

    //return a_result - b_result;
    return a.distance - b.distance;
}

int compare_nodes2(const void* ap, const void* bp) {
    Sorted_Node* a = (Sorted_Node*) ap;
    Sorted_Node* b = (Sorted_Node*) bp;

    return a->distance - b->distance;
}

void search_folder_tree() {
    if (!all_nodes) {
        return;
    }

    if (!sorted_nodes) {
        sorted_nodes = (Sorted_Node*) malloc(sizeof(Sorted_Node) * total_nodes);

        for (u32 index = 0; index < total_nodes; index++) {
            Sorted_Node& sorted_node = sorted_nodes[index];
            sorted_node.source_node = &all_nodes[index];
        }
    }

    u32 buffer_length = strlen(search_buffer);

    if (buffer_length == 0) {
        return;
    }

    auto start2 = std::chrono::steady_clock::now();

    for (u32 index = 0; index < total_nodes; index++) {
        Sorted_Node& sorted_node = sorted_nodes[index];
        String& name = sorted_node.source_node->name;
        sorted_node.distance = levenshtein(name.start, search_buffer, name.length, buffer_length);
    }

    //std::sort(sorted_nodes, sorted_nodes + total_nodes, compare_nodes);
    qsort(sorted_nodes, total_nodes, sizeof(Sorted_Node), compare_nodes2);

    long long int time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start2).count();

    printf("Took %llu to sort %lu elements\n", time, total_nodes);
    //qsort(sorted_nodes, total_nodes, sizeof(Sorted_Node), compare_nodes2);
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
        search_folder_tree();
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
    if (buffer_length > 0 && sorted_nodes) {
        for (u32 i = 0; i < 30; i++) {
            Sorted_Node* node = &sorted_nodes[i];

            char* name = string_to_temporary_null_terminated_string(node->source_node->name);

            String& id = node->source_node->id;
            ImGui::PushID(id.start, id.start + id.length);
            if (ImGui::Selectable(name)) {
                select_folder_node_and_request_contents_if_necessary(node->source_node);
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
    float scale = (float) emscripten_get_device_pixel_ratio();

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

extern "C"
EMSCRIPTEN_KEEPALIVE
char* handle_clipboard_copy() {
    char* temp = (char*) talloc(current_task.permalink.length + 1);
    sprintf(temp, "%.*s", current_task.permalink.length, current_task.permalink.start);

    return temp;
}

extern "C"
EMSCRIPTEN_KEEPALIVE
void loop()
{
    gFrameMonitor->startFrame();

    clear_temporary_storage();

    tick++;

    static bool bShowTestWindow = true;
    FunImGui::BeginFrame();

    ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse;

    bool open = true;

    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowPos(ImVec2());
    ImGui::Begin("Wrike", &open, flags);

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

    gFrameMonitor->drawAverage();

    ImGui::Columns(draw_side_menu_this_frame ? 3 : 2);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetColumnWidth(0, 300.0f);
    }

    if (draw_side_menu_this_frame) {
        draw_folder_tree();
        ImGui::NextColumn();
    }

    draw_task_list();

    ImGui::NextColumn();

    bool task_is_loading = task_request != NO_REQUEST;

    if (/*!selected_folder_task_id.length || */task_is_loading) {
        ImGui::ListBoxHeader("##task_content", ImVec2(-1, -1));

        if (task_is_loading) {
            ImGui::LoadingIndicator(started_loading_task_at);
        }

        ImGui::ListBoxFooter();
    } else {
        draw_task_contents();
    }

    ImGui::Columns(1);
    ImGui::End();

    //bool o = true;
    //ImGui::ShowTestWindow(&o);

    Draw::clear();
    ImGui::Render();

    gFrameMonitor->endFrame();
}

EMSCRIPTEN_KEEPALIVE
bool init()
{
    init_temporary_storage();

    api_request(accounts_request, "accounts?fields=['customFields']");
    api_request(folder_tree_request, "folders?fields=['starred']");
    api_request(contacts_request, "contacts");

    folder_tree_init();

    memset(search_buffer, 0, sizeof(search_buffer));

    char* last_selected_folder
            = (char*) EM_ASM_INT({ return local_storage_get_string(Pointer_stringify($0)) }, "last_selected_folder");

    char* last_selected_task
            = (char*) EM_ASM_INT({ return local_storage_get_string(Pointer_stringify($0)) }, "last_selected_task");

    if (last_selected_folder) {
        String folder_id;
        folder_id.start = last_selected_folder;
        folder_id.length = strlen(last_selected_folder);

        request_folder_contents(folder_id);

        had_last_selected_folder_so_doesnt_need_to_load_the_root_folder = true;
    }

    if (last_selected_task) {
        Id16 task_id;
        memcpy(task_id.id, last_selected_task, ID_16_LENGTH);

        request_task(task_id);
    }

    bool result = GDraw.init();
    initializeEmFunGImGui();
    FunImGui::init();
    gFrameMonitor = new FrameMonitor;

    load_png_from_disk("resources/wrike_logo.png", logo);

    return result;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
int main() {
    init();
    emscripten_set_main_loop(loop, 0, 1);
}
#endif
