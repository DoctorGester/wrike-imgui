#include "main.h"
#include "draw.h"
#include "funimgui.h"
#include <imgui.h>
#include "framemonitor.h"
#include <emscripten/emscripten.h>
#include <cmath>
#include <chrono>
#include <imgui_internal.h>
#include <html_entities.h>
#include <jsmn.h>

#include "download.h"
#include "jsmn.h"
#include "folder_tree.h"
#include "common.h"
#include "json.h"
#include "temporary_storage.h"
#include "rich_text.h"
#include "ui.h"
#include "hash_map.h"

#define PRINTLIKE(string_index, first_to_check) __attribute__((__format__ (__printf__, string_index, first_to_check)))

typedef signed long Request_Id;

static Draw GDraw;
static FrameMonitor* gFrameMonitor = nullptr;
static Download* gDownload = nullptr;

static const Request_Id NO_REQUEST = -1;
static Request_Id request_id_counter = 0;

static Request_Id folder_tree_request = NO_REQUEST;
static Request_Id folder_contents_request = NO_REQUEST;
static Request_Id task_request = NO_REQUEST;
static Request_Id contacts_request = NO_REQUEST;
static Request_Id accounts_request = NO_REQUEST;
static Request_Id workflows_request = NO_REQUEST;

static bool draw_side_menu = true;

bool are_ids_equal(Id16* a, Id16* b) {
    return memcmp(a->id, b->id, ID_16_LENGTH) == 0;
}

bool are_ids_equal(Id8* a, Id8* b) {
    return memcmp(a->id, b->id, ID_8_LENGTH) == 0;
}

// TODO use temporary storage there
static char temporary_request_buffer[512];

enum Status_Group {
    Status_Group_Invalid,
    Status_Group_Active,
    Status_Group_Completed,
    Status_Group_Deferred,
    Status_Group_Cancelled
};

struct Folder_Task {
    Id16 id;
    Id16 custom_status_id;
    String title;
};

struct User {
    Id8 id;
    String firstName;
    String lastName;
};

// Do we need this struct?
struct Task {
    String title;
    String permalink;
    Rich_Text_String* description = NULL;
    u32 description_strings = 0;
    String description_text{};

    Id8* assignees = NULL;
    u32 num_assignees = 0;
};

struct Account {
    Id8 id;
};

struct Custom_Status {
    Id16 id;
    String name;
    u32 id_hash;
    u32 color;
};

static Memory_Image logo;

static Id16 selected_folder_task_id;
static Folder_Tree_Node* selected_node = NULL; // TODO should be a Task_Id

static Task current_task{};

static char* folder_tasks_json_content = NULL;
static Folder_Task* folder_tasks = NULL;
static u32 folder_task_count = 0;

static char* task_json_content = NULL;

static char* users_json_content = NULL;
static User* users = NULL;
static u32 users_count = 0;

static char* accounts_json_content = NULL;
static Account* accounts = NULL;
static u32 accounts_count = 0;

static char* workflows_json_content = NULL;
static Custom_Status* custom_statuses = NULL;
static u32 custom_statuses_count = 0;
static Hash_Map<Custom_Status*> id_to_custom_status = { 0 };


static u32 tick = 0;
static u32 started_loading_folder_contents_at = 0;
static u32 finished_loading_folder_contents_at = 0;

static u32 started_loading_task_at = 0;
static u32 finished_loading_task_at = 0;

static u32 finished_loading_statuses_at = 0;

PRINTLIKE(2, 3) static void api_request(Request_Id& request_id, const char* format, ...) {
    va_list args;
    va_start(args, format);

    vsprintf(temporary_request_buffer, format, args);

    va_end(args);

    EM_ASM({ api_get(Pointer_stringify($0), $1) }, &temporary_request_buffer[0], request_id_counter);

    request_id = request_id_counter++;
}

static inline void json_token_to_id(char* json, jsmntok_t* token, Id16& id) {
    memcpy(id.id, json + token->start, ID_16_LENGTH);
}

static inline void json_token_to_id(char* json, jsmntok_t* token, Id8& id) {
    memcpy(id.id, json + token->start, ID_8_LENGTH);
}

static u32 color_name_to_color_argb(String &color_name) {
    char c = *color_name.start;

    switch (color_name.length) {
        // Red
        case 3: return 0xFFE91E63;

        case 4: {
            if (c == 'B'/*lue*/) return 0xFF2196F3; else
            if (c == 'G'/*ray*/) return 0xFF9E9E9E;

            break;
        }

        case 5: {
            if (c == 'B'/*rown*/) return 0xFF795548; else
            if (c == 'G'/*reen*/) return 0xFF8BC34A;

            break;
        }

        case 6: {
            if (c == 'Y'/*ellow*/) return 0xFFFFEB3B; else
            if (c == 'P'/*urple*/) return 0xFF9C27B0; else
            if (c == 'O'/*range*/) return 0xFFFF9800; else
            if (c == 'I'/*ndigo*/) return 0xFF673AB7;

            break;
        }

        case 8: {
            c = *(color_name.start + 4);

            if (c == /*Dark*/'B'/*lue*/) return 0xFF3F51B5; else
            if (c == /*Dark*/'C'/*yan*/) return 0xFF009688;

            break;
        }

        // Turquoise
        case 9: return 0xFF00BCD4;

        // YellowGreen
        case 11: return 0xFFCDDC39;
    }

    printf("Got unknown color: %.*s\n", color_name.length, color_name.start);
    return 0xFF000000;
}

static Status_Group status_group_name_to_status_group(String& name) {
    if (name.length < 2) {
        return Status_Group_Invalid;
    }

    switch (*name.start) {
        case 'A'/*ctive*/: return Status_Group_Active;
        case 'C': {
            switch (*(name.start + 1)) {
                case /*C*/'o'/*mpleted*/: return Status_Group_Completed;
                case /*C*/'a'/*ncelled*/: return Status_Group_Cancelled;
            }

            break;
        }

        case 'D'/*eferred*/: return Status_Group_Deferred;
    }

    return Status_Group_Invalid;
}

static u32 status_group_to_color(Status_Group group) {
    switch (group) {
        case Status_Group_Active: return 0xFF2196F3;
        case Status_Group_Completed: return 0xFF8BC34A;
        case Status_Group_Cancelled:
        case Status_Group_Deferred:
            return 0xFF9E9E9E;

        default: return 0xFF000000;
    }
}

static void process_folder_contents_data_object(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Folder_Task* folder_task = &folder_tasks[folder_task_count++];

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, folder_task->title);
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_id(json, next_token, folder_task->id);
        } else if (json_string_equals(json, property_token, "customStatusId")) {
            json_token_to_id(json, next_token, folder_task->custom_status_id);
        } else {
            eat_json(token);
            token--;
        }
    }
}

static void process_task_data(char* json, u32 data_size, jsmntok_t*& token) {
    // We only request singular tasks now
    assert(data_size == 1);
    assert(token->type == JSMN_OBJECT);

    jsmntok_t* object_token = token++;

    String description{};

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

#define IS_PROPERTY(s) json_string_equals(json, property_token, (s))
#define TOKEN_TO_STRING(s) json_token_to_string(json, next_token, (s))

        if (IS_PROPERTY("title")) {
            TOKEN_TO_STRING(current_task.title);
        } else if (IS_PROPERTY("description")) {
            TOKEN_TO_STRING(description);
        } else if (IS_PROPERTY("permalink")) {
            TOKEN_TO_STRING(current_task.permalink);
        } else if (IS_PROPERTY("responsibleIds")) {
            assert(next_token->type == JSMN_ARRAY);

            if (current_task.num_assignees < next_token->size) {
                current_task.assignees = (Id8*) realloc(current_task.assignees, sizeof(Id8) * next_token->size);
            }

            current_task.num_assignees = 0;

            for (u32 array_index = 0; array_index < next_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                json_token_to_id(task_json_content, id_token, current_task.assignees[current_task.num_assignees++]);
            }
        } else {
            eat_json(token);
            token--;
        }
    }

#undef IS_PROPERTY
#undef TOKEN_TO_STRING

    if (current_task.description) {
        free(current_task.description);
    }

    current_task.description_strings = 0;

    parse_rich_text(description, current_task.description, current_task.description_strings);

    u32 total_text_length = 0;

    for (u32 index = 0; index < current_task.description_strings; index++) {
        total_text_length += current_task.description[index].text.length;
    }

    String& description_text = current_task.description_text;
    description_text.start = (char*) realloc(description_text.start, total_text_length);
    description_text.length = total_text_length;

    char* current_location = description_text.start;

    for (u32 index = 0; index < current_task.description_strings; index++) {
        Rich_Text_String& rich_text_string = current_task.description[index];
        String& text = rich_text_string.text;

        if (rich_text_string.text.length > 0) {
            size_t new_length = decode_html_entities_utf8(current_location, text.start, text.start + text.length);

            text.start = current_location;
            text.length = new_length;

            current_location += new_length;
        } else {
            text.start = current_location;
        }
    }

    printf("Parsed description with a total length of %lu\n", total_text_length);
}

static void process_folder_contents_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (folder_task_count < data_size) {
        folder_tasks = (Folder_Task*) realloc(folder_tasks, sizeof(Folder_Task) * data_size);
    }

    folder_task_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_contents_data_object(json, token);
    }
}

static void process_folder_contents_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (folder_tasks_json_content) {
        free(folder_tasks_json_content);
    }

    folder_tasks_json_content = json;
    finished_loading_folder_contents_at = tick;

    process_json_data_segment(json, tokens, num_tokens, process_folder_contents_data);
}

static void process_task_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (task_json_content) {
        free(task_json_content);
    }

    task_json_content = json;
    finished_loading_task_at = tick;

    process_json_data_segment(json, tokens, num_tokens, process_task_data);
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

static void process_users_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (users_count < data_size) {
        users = (User*) realloc(users, sizeof(User) * data_size);
    }

    users_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_users_data_object(json, token);
    }
}

static void process_contacts_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (users_json_content) {
        free(users_json_content);
    }

    users_json_content = json;

    process_json_data_segment(json, tokens, num_tokens, process_users_data);
}

static void process_accounts_data(char* json, u32 data_size, jsmntok_t*&token) {
    if (accounts_count < data_size) {
        accounts = (Account*) realloc(accounts, sizeof(Account) * data_size);
    }

    accounts_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        Account* account = &accounts[accounts_count++];

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "id")) {
                json_token_to_id(json, next_token, account->id);
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}

static void process_accounts_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (accounts_json_content) {
        free(accounts_json_content);
    }

    accounts_json_content = json;

    process_json_data_segment(json, tokens, num_tokens, process_accounts_data);
}

static void process_custom_status(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Custom_Status* custom_status = &custom_statuses[custom_statuses_count++];

    // TODO unused
    bool is_standard = false;
    Status_Group group = Status_Group_Invalid;

    custom_status->color = 0;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_id(json, next_token, custom_status->id);
        } else if (json_string_equals(json, property_token, "name")) {
            json_token_to_string(json, next_token, custom_status->name);
        } else if (json_string_equals(json, property_token, "standard")) {
            is_standard = *(json + next_token->start) == 't';
        } else if (json_string_equals(json, property_token, "color")) {
            String color_name;
            json_token_to_string(json, next_token, color_name);

            custom_status->color = argb_to_agbr(color_name_to_color_argb(color_name));
        } else if (json_string_equals(json, property_token, "group")) {
            String group_name;
            json_token_to_string(json, next_token, group_name);

            group = status_group_name_to_status_group(group_name);
        } else {
            eat_json(token);
            token--;
        }
    }

    String id_as_string;
    id_as_string.start = custom_status->id.id;
    id_as_string.length = ID_16_LENGTH;

    if (!custom_status->color) {
        custom_status->color = argb_to_agbr(status_group_to_color(group));
    }

    custom_status->id_hash = hash_string(id_as_string);

//    printf("Got status %.*s with hash %lu\n", custom_status->name.length, custom_status->name.start, custom_status->id_hash);

    hash_map_put(&id_to_custom_status, custom_status, custom_status->id_hash);
}

static void process_workflows_data(char* json, u32 data_size, jsmntok_t*&token) {
    jsmntok_t* json_start = token;

    u32 total_statuses = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "customStatuses")) {
                total_statuses += next_token->size;
            }

            eat_json(token);
            token--;
        }
    }

    token = json_start;

    // TODO hacky, we need to clear the map when it's populated too
    if (id_to_custom_status.size == 0) {
        hash_map_init(&id_to_custom_status, total_statuses);
    }

    if (custom_statuses_count < total_statuses) {
        custom_statuses = (Custom_Status*) realloc(custom_statuses, sizeof(Custom_Status) * total_statuses);
    }

    custom_statuses_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        jsmntok_t* object_token = token++;

        assert(object_token->type == JSMN_OBJECT);

        for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
            jsmntok_t* property_token = token++;

            assert(property_token->type == JSMN_STRING);

            jsmntok_t* next_token = token;

            if (json_string_equals(json, property_token, "customStatuses")) {
                assert(next_token->type == JSMN_ARRAY);

                token++;

                for (u32 status_index = 0; status_index < next_token->size; status_index++) {
                    process_custom_status(json, token);
                }

                token--;
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}

static void process_workflows_request(char* json, jsmntok_t* tokens, u32 num_tokens) {
    if (workflows_json_content) {
        free(workflows_json_content);
    }

    workflows_json_content = json;

    process_json_data_segment(json, tokens, num_tokens, process_workflows_data);

    finished_loading_statuses_at = tick;
}

static void request_workflows_for_each_account() {
    for (u32 index = 0; index < accounts_count; index++) {
        Account& account = accounts[index];
        api_request(workflows_request, "accounts/%.*s/workflows", ID_8_LENGTH, account.id.id);
    }
}

extern "C"
EMSCRIPTEN_KEEPALIVE
void api_request_success(Request_Id request_id, char* content_json) {
    printf("Got request %lu with content at %p\n", request_id, (void*) content_json);

    u32 parsed_tokens;
    jsmntok_t* json_tokens = parse_json_into_tokens(content_json, parsed_tokens);

    if (request_id == folder_tree_request) {
        folder_tree_request = NO_REQUEST;
        process_folder_tree_request(content_json, json_tokens, parsed_tokens);
    } else if (request_id == folder_contents_request) {
        folder_contents_request = NO_REQUEST;
        process_folder_contents_request(content_json, json_tokens, parsed_tokens);
    } else if (request_id == task_request) {
        task_request = NO_REQUEST;
        process_task_request(content_json, json_tokens, parsed_tokens);
    } else if (request_id == contacts_request) {
        contacts_request = NO_REQUEST;
        process_contacts_request(content_json, json_tokens, parsed_tokens);
    } else if (request_id == accounts_request) {
        accounts_request = NO_REQUEST;
        process_accounts_request(content_json, json_tokens, parsed_tokens);

        request_workflows_for_each_account();
    } else if (request_id == workflows_request) {
        workflows_request = NO_REQUEST;
        process_workflows_request(content_json, json_tokens, parsed_tokens);
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

void request_folder(String& folder_id) {
    EM_ASM({ local_storage_set(Pointer_stringify($0), Pointer_stringify($1, $2)) },
           "last_selected_folder",
           folder_id.start,
           folder_id.length
    );

    api_request(folder_contents_request, "folders/%.*s/tasks", (int) folder_id.length, folder_id.start);
    started_loading_folder_contents_at = tick;
}

void select_folder_node_and_request_contents_if_necessary(Folder_Tree_Node* folder_node) {
    selected_node = folder_node;

    request_folder(selected_node->id);
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

char* string_to_temporary_null_terminated_string(String& string) {
    char* node_name_null_terminated = (char*) talloc(string.length + 1);
    sprintf(node_name_null_terminated, "%.*s", string.length, string.start);

    return node_name_null_terminated;
}

static float lerp(float time_from, float time_to, float scale_to, float max) {
    float delta = (time_to - time_from);

    if (delta > max) {
        delta = max;
    }

    return ((scale_to / max) * delta);
}

struct Image {

};

namespace ImGui {
    void LoadingIndicator(u32 started_showing_at);
    void Image(Memory_Image& image);
}

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

void draw_task_contents() {
    float wrap_width = ImGui::GetColumnWidth(-1) - 30.0f; // Accommodate for scroll bar

    ImGuiID task_content_id = ImGui::GetID("task_content");
    ImGui::BeginChildFrame(task_content_id, ImVec2(-1, -1), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float alpha = lerp(finished_loading_task_at, tick, 1.0f, 8);

    ImVec4 title_color = ImVec4(0, 0, 0, alpha);

    float wrap_pos = ImGui::GetCursorPosX() + wrap_width;
    ImGui::PushTextWrapPos(wrap_pos);
    ImGui::PushFont(font_header);
    ImGui::TextColored(title_color, "%.*s", current_task.title.length, current_task.title.start);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    for (u32 index = 0; index < current_task.num_assignees; index++) {
        bool is_last = index == (current_task.num_assignees - 1);

        // TODO temporary code, use a hash map!
        for (u32 user_index = 0; user_index < users_count; user_index++) {
            User* user = &users[user_index];

            if (are_ids_equal(&user->id, &current_task.assignees[index])) {
                const char* pattern_last = "%.*s %.*s";
                const char* pattern_preceding = "%.*s %.*s,";

                ImGui::TextColored(title_color, is_last ? pattern_last : pattern_preceding,
                            user->firstName.length, user->firstName.start,
                            user->lastName.length, user->lastName.start
                );

                if (!is_last) {
                    ImGui::SameLine();
                }

                break;
            }
        }
    }

    if (ImGui::Button("Open in Wrike")) {
        EM_ASM({ window.open(Pointer_stringify($0, $1), '_blank'); },
               current_task.permalink.start, current_task.permalink.length);
    }

    ImGui::Separator();

    ImGui::BeginChild("task_description_and_comments", ImVec2(-1, -1));
    if (current_task.description_strings > 0) {
        Rich_Text_String* text_start = current_task.description;
        Rich_Text_String* text_end = text_start + current_task.description_strings - 1;

        for (Rich_Text_String* paragraph_end = text_start, *paragraph_start = paragraph_end; paragraph_end <= text_end; paragraph_end++) {
            bool is_newline = paragraph_end->text.length == 0;
            bool should_flush_text = is_newline || paragraph_end == text_end;

            char* string_start = paragraph_start->text.start;
            char* string_end = paragraph_end->text.start + paragraph_end->text.length;

            if (is_newline && (string_end - string_start) == 0) {
                ImGui::NewLine();
                paragraph_start = paragraph_end + 1;
                continue;
            }

            if (should_flush_text && paragraph_end - paragraph_start) {
#if 0
                const char* check_mark = "\u2713";
                const char* check_box = "\u25A1";

                if (paragraph_start->text.length >= 4) {
                    bool is_check_mark = strncmp(check_mark, paragraph_start->text.start + 1, strlen(check_mark)) == 0;
                    bool is_check_box = strncmp(check_box, paragraph_start->text.start + 1, strlen(check_box)) == 0;

                    if (is_check_mark || is_check_box) {
                        ImGui::Checkbox("", &is_check_mark);
                        ImGui::SameLine();
                    }
                }
#endif

                float indent = paragraph_start->style.list_depth * 15.0f;

                if (indent) {
                    ImGui::Indent(indent);
                    ImGui::Bullet();
                    ImGui::SameLine();
                }

                add_rich_text(
                        ImGui::GetWindowDrawList(),
                        ImGui::GetCursorScreenPos(),
                        paragraph_start,
                        paragraph_end,
                        wrap_width,
                        alpha
                );

                if (indent) {
                    ImGui::Unindent(indent);
                }

                paragraph_start = paragraph_end + 1;
            }
        }
    }

    ImGui::EndChild();

    ImGui::EndChildFrame();
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

void draw_task_in_task_list(Folder_Task* task, float alpha) {
    ImGui::PushID(task->id.id, task->id.id + ID_16_LENGTH);

    // TODO slow, could compare hashes if that becomes an issue
    bool is_selected = are_ids_equal(&task->id, &selected_folder_task_id);
    bool clicked = ImGui::Selectable("##dummy_task", is_selected);

    // TODO Pretty slow to hash like that every time
    if (id_to_custom_status.size > 0) {
        String id_as_string;
        id_as_string.start = task->custom_status_id.id;
        id_as_string.length = ID_16_LENGTH;

        Custom_Status* custom_status = hash_map_get(&id_to_custom_status, id_as_string, hash_string(id_as_string));

        if (custom_status) {
            ImGui::SameLine();
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(custom_status->color);
            color.w = alpha;

            ImGui::TextColored(color, "[%.*s]", custom_status->name.length, custom_status->name.start);
        }
    }

    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0, 0, 0, alpha), "%.*s", task->title.length, task->title.start);

    if (clicked) {
        request_task(task->id);
    }

    ImGui::PopID();
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

    ImGui::ListBoxHeader("##tasks", ImVec2(-1, -1));

    const bool custom_statuses_loaded = custom_statuses_count > 0;

    if (folder_contents_request == NO_REQUEST && custom_statuses_loaded) {
        float alpha = lerp(MAX(finished_loading_folder_contents_at, finished_loading_statuses_at), tick, 1.0f, 8);

        // TODO we could put those into a clipper as well
        if (selected_node) {
            for (u32 i = 0; i < selected_node->num_children; i++) {
                Folder_Tree_Node* child_folder = selected_node->children[i];

                ImGui::PushID(child_folder);

                bool clicked = ImGui::Selectable("##dummy_folder");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0, 0, 0, alpha), string_to_temporary_null_terminated_string(child_folder->name));

                if (clicked) {
                    select_folder_node_and_request_contents_if_necessary(child_folder);
                }

                ImGui::PopID();
            }
        }

        ImGuiListClipper clipper(folder_task_count);

        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; index++) {
                Folder_Task* task = &folder_tasks[index];

                draw_task_in_task_list(task, alpha);
            }
        }
    } else {
        ImGui::LoadingIndicator(started_loading_folder_contents_at);
    }

    ImGui::ListBoxFooter();

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

    api_request(accounts_request, "accounts");
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

        request_folder(folder_id);
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
    gDownload = new Download;

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
