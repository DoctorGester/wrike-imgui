#include <jsmn.h>
#include <cstdio>
#include <emscripten.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "hash_map.h"
#include "json.h"
#include "temporary_storage.h"
#include "accounts.h"
#include "main.h"

enum Status_Group {
    Status_Group_Invalid,
    Status_Group_Active,
    Status_Group_Completed,
    Status_Group_Deferred,
    Status_Group_Cancelled
};

enum Task_List_Sort_Field {
    Task_List_Sort_Field_None,
    Task_List_Sort_Field_Title,
    Task_List_Sort_Field_Status,
    Task_List_Sort_Field_Assignee,
    Task_List_Sort_Field_Custom_Field
};

struct Custom_Field_Value {
    Id16 field_id;
    String value;
};

struct Folder_Task {
    Id16 id;
    Id16 custom_status_id;
    u32 custom_status_id_hash;

    String title;
    Custom_Field_Value* custom_field_values;
    u32 num_custom_field_values;
};

struct Folder_Header {
    Id16* custom_columns;
    u32 num_custom_columns;
};

struct Custom_Status {
    Id16 id;
    String name;
    Status_Group group;
    u32 id_hash;
    u32 color;
    u32 natural_index;
};

struct Sorted_Folder_Task {
    Folder_Task* source_task;
    Custom_Status* cached_status;
};

static const u32 custom_columns_start_index = 2;

static Folder_Header current_folder{};

static Folder_Task* folder_tasks = NULL;
static Sorted_Folder_Task* sorted_folder_tasks = NULL;
static u32 folder_task_count = 0;

typedef char Sort_Direction;
static const Sort_Direction Sort_Direction_Normal = 1;
static const Sort_Direction Sort_Direction_Reverse = -1;

static Task_List_Sort_Field sort_field = Task_List_Sort_Field_None;
static Id16 sort_custom_field_id{};
static Custom_Field* sort_custom_field;
static Sort_Direction sort_direction = Sort_Direction_Normal;
static bool has_been_sorted_after_loading = false;
static bool show_only_active_tasks = true;

static Custom_Field_Value* custom_field_values = NULL;
static u32 custom_field_values_count = 0;
static u32 custom_field_values_watermark = 0;

static Custom_Status* custom_statuses = NULL;
static u32 custom_statuses_count = 0;
static Hash_Map<Custom_Status*> id_to_custom_status = { 0 };

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

static Custom_Field_Value* reserve_n_custom_field_values(u32 n) {
    if (custom_field_values_count + n > custom_field_values_watermark) {
        if (custom_field_values_watermark == 0) {
            custom_field_values_watermark = 16;
        } else {
            custom_field_values_watermark *= 2;
        }

        custom_field_values = (Custom_Field_Value*) realloc(custom_field_values, sizeof(Custom_Field_Value) * custom_field_values_watermark);
    }

    Custom_Field_Value* result = &custom_field_values[custom_field_values_count];

    custom_field_values_count += n;

    return result;
}

static inline int compare_tasks_custom_fields(Folder_Task* a, Folder_Task* b, Custom_Field_Type custom_field_type) {
    String* a_value = NULL;
    String* b_value = NULL;

    // TODO we could cache that to sort big lists faster
    for (u32 index = 0; index < a->num_custom_field_values; index++) {
        if (are_ids_equal(&a->custom_field_values[index].field_id, &sort_custom_field_id)) {
            a_value = &a->custom_field_values[index].value;
            break;
        }
    }

    if (!a_value) {
        return 1;
    }

    for (u32 index = 0; index < b->num_custom_field_values; index++) {
        if (are_ids_equal(&b->custom_field_values[index].field_id, &sort_custom_field_id)) {
            b_value = &b->custom_field_values[index].value;
            break;
        }
    }

    if (!b_value) {
        return -1;
    }

    switch (custom_field_type) {
        case Custom_Field_Type_Numeric: {
            return string_atoi(a_value) - string_atoi(b_value);
        }

        case Custom_Field_Type_DropDown:
        case Custom_Field_Type_Text: {
            return strncmp(a_value->start, b_value->start, MIN(a_value->length, b_value->length));
        }

        default: {
            return 0;
        }
    }
}

static Custom_Status* find_task_custom_status(Folder_Task* task) {
    // TODO ugly!
    String id_as_string;
    id_as_string.start = task->custom_status_id.id;
    id_as_string.length = ID_16_LENGTH;

    return hash_map_get(&id_to_custom_status, id_as_string, task->custom_status_id_hash);
}

static inline int compare_folder_tasks_based_on_current_sort(Sorted_Folder_Task* as, Sorted_Folder_Task* bs) {
    Folder_Task* a = as->source_task;
    Folder_Task* b = bs->source_task;

    // TODO instead of switching in each call we could just move those into separate functions
    switch (sort_field) {
        case Task_List_Sort_Field_Title: {
            return strncmp(a->title.start, b->title.start, MIN(a->title.length, b->title.length)) * sort_direction;
        }

        case Task_List_Sort_Field_Status: {
            Custom_Status* a_status = as->cached_status;
            Custom_Status* b_status = bs->cached_status;

            // TODO do status comparison based on status type?

            return (a_status->natural_index - b_status->natural_index) * sort_direction;
        }

        case Task_List_Sort_Field_Custom_Field: {
            return compare_tasks_custom_fields(a, b, sort_custom_field->type) * sort_direction;
        }
    }

    return 0;
}

static int compare_folder_tasks_based_on_current_sort_and_their_ids(const void* ap, const void* bp) {
    Sorted_Folder_Task* as = (Sorted_Folder_Task*) ap;
    Sorted_Folder_Task* bs = (Sorted_Folder_Task*) bp;

    int result = compare_folder_tasks_based_on_current_sort(as, bs);

    if (result == 0) {
        /* TODO EXTREMELY slow. This takes up the most time when sorting big datasets
         * could try to convert id to a number and do a regular int comparison
         */

        return memcmp(as->source_task->id.id, bs->source_task->id.id, ID_16_LENGTH);
    }

    return result;
}

static void sort_by_field(Task_List_Sort_Field sort_by) {
    assert(sort_by != Task_List_Sort_Field_Custom_Field);

    if (sort_field == sort_by) {
        sort_direction *= -1;
    } else {
        sort_direction = Sort_Direction_Normal;
    }

    // TODO We actually only need to do that once when tasks/workflows combination changes, not for every sort
    for (u32 index = 0; index < folder_task_count; index++) {
        Sorted_Folder_Task* sorted_folder_task = &sorted_folder_tasks[index];
        sorted_folder_task->cached_status = find_task_custom_status(sorted_folder_task->source_task);
    }

    sort_field = sort_by;

    double start = emscripten_get_now();
    qsort(sorted_folder_tasks, folder_task_count, sizeof(Sorted_Folder_Task), compare_folder_tasks_based_on_current_sort_and_their_ids);
    printf("Sorting %lu elements by %i took %fms\n", folder_task_count, sort_by, emscripten_get_now() - start);
}

static void sort_by_custom_field(Id16& field_id) {
    if (sort_field == Task_List_Sort_Field_Custom_Field && are_ids_equal(&field_id, &sort_custom_field_id)) {
        sort_direction *= -1;
    } else {
        sort_direction = Sort_Direction_Normal;
    }

    for (u32 index = 0; index < folder_task_count; index++) {
        Sorted_Folder_Task* sorted_folder_task = &sorted_folder_tasks[index];
        sorted_folder_task->cached_status = find_task_custom_status(sorted_folder_task->source_task);
    }

    // TODO ugly! We need to find a way to use ID as hash map key
    String id_as_string;
    id_as_string.start = field_id.id;
    id_as_string.length = ID_16_LENGTH;

    sort_field = Task_List_Sort_Field_Custom_Field;
    sort_custom_field_id = field_id;
    sort_custom_field = hash_map_get(&id_to_custom_field, id_as_string, hash_string(id_as_string));

    double start = emscripten_get_now();
    qsort(sorted_folder_tasks, folder_task_count, sizeof(Sorted_Folder_Task), compare_folder_tasks_based_on_current_sort_and_their_ids);
    printf("Sorting %lu elements by %.16s took %fms\n", folder_task_count, field_id.id, emscripten_get_now() - start);
}

void draw_task_column(Sorted_Folder_Task* sorted_task, u32 column, Custom_Field* custom_field_or_null, float alpha) {
    Folder_Task* task = sorted_task->source_task;

    if (column == 0) {
        ImGui::PushID(task->id.id, task->id.id + ID_16_LENGTH);

        // TODO slow, could compare hashes if that becomes an issue
        bool is_selected = are_ids_equal(&task->id, &selected_folder_task_id);
        bool clicked = ImGui::Selectable("##dummy_task", is_selected, ImGuiSelectableFlags_SpanAllColumns);

        ImGui::PopID();

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0, 0, 0, alpha), "%.*s", task->title.length, task->title.start);

        if (clicked) {
            request_task(task->id);
        }
    } else if (column == 1 && id_to_custom_status.size > 0) {
        if (sorted_task->cached_status) {
            ImVec4 color = ImGui::ColorConvertU32ToFloat4(sorted_task->cached_status->color);
            color.w = alpha;

            ImGui::TextColored(color, "[%.*s]", sorted_task->cached_status->name.length, sorted_task->cached_status->name.start);
        } else {
            ImGui::Text("...");
        }
    } else if (custom_field_or_null) {
        // TODO seems slow, any better way?
        bool found = false;

        for (u32 j = 0; j < task->num_custom_field_values; j++) {
            Custom_Field_Value* value = &task->custom_field_values[j];

            if (are_ids_equal(&value->field_id, &custom_field_or_null->id)) {
                ImGui::TextColored(ImVec4(0, 0, 0, alpha), "%.*s", value->value.length, value->value.start);
                found = true;
                break;
            }
        }

        if (!found) {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        }
    }
}

void draw_task_list_header(float view_width, float custom_column_width, Custom_Field** column_to_custom_field, u32 total_columns) {
    ImGui::BeginColumns("table_view_header_columns", total_columns);

    ImGui::SetColumnWidth(0, view_width * 0.4f);
    ImGui::SetColumnWidth(1, view_width * 0.2f);

    for (u32 column = 0; column < current_folder.num_custom_columns; column++) {
        ImGui::SetColumnWidth(custom_columns_start_index + column, custom_column_width);
    }

    for (u32 column = 0; column < total_columns; column++) {
        String column_title;

        Custom_Field* custom_field = column >= custom_columns_start_index ?
                                     column_to_custom_field[column - custom_columns_start_index] :
                                     NULL;

        if (column == 0) {
            column_title.start = (char*) "Name";
            column_title.length = strlen(column_title.start);
        } else if (column == 1) {
            column_title.start = (char*) "Status";
            column_title.length = strlen(column_title.start);
        } else {
            column_title = custom_field->title;
        }

        char* column_title_null_terminated = string_to_temporary_null_terminated_string(column_title);

        Id16& custom_field_id = current_folder.custom_columns[column - custom_columns_start_index];

        // TODO ugly/copypaste
        if (ImGui::Button(column_title_null_terminated)) {
            switch (column) {
                case 0: {
                    sort_by_field(Task_List_Sort_Field_Title);
                    break;
                }

                case 1: {
                    sort_by_field(Task_List_Sort_Field_Status);
                    break;
                }

                default: {
                    sort_by_custom_field(custom_field_id);
                }
            }
        }

        bool sorting_by_this_column = false;

        switch (column) {
            case 0: {
                sorting_by_this_column = sort_field == Task_List_Sort_Field_Title;
                break;
            }

            case 1: {
                sorting_by_this_column = sort_field == Task_List_Sort_Field_Status;
                break;
            }

            default: {
                sorting_by_this_column = sort_field == Task_List_Sort_Field_Custom_Field && are_ids_equal(&sort_custom_field_id, &custom_field_id);
            }
        }

        if (sorting_by_this_column) {
            ImGui::SameLine();
            ImGui::RenderArrow(ImGui::GetCursorScreenPos(), sort_direction == Sort_Direction_Reverse ? ImGuiDir_Up : ImGuiDir_Down);
            ImGui::NewLine();
        }

        if (column + 1 < total_columns) {
            ImGui::NextColumn();
        }
    }

    ImGui::EndColumns();
}

void draw_task_list() {
    ImGuiID task_list = ImGui::GetID("task_list");
    ImGui::BeginChildFrame(task_list, ImVec2(-1, -1), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    //ImGui::ListBoxHeader("##tasks", ImVec2(-1, -1));

    const bool custom_statuses_loaded = custom_statuses_count > 0;
    const bool is_folder_data_loading = folder_contents_request != NO_REQUEST || folder_header_request != NO_REQUEST;

    if (!is_folder_data_loading && custom_statuses_loaded) {
        if (!has_been_sorted_after_loading) {
            sort_by_field(Task_List_Sort_Field_Title);
            has_been_sorted_after_loading = true;
        }

        ImGui::Checkbox("Show only active tasks", &show_only_active_tasks);
        ImGui::Separator();

        u32 loading_end_time = MAX(finished_loading_folder_header_at, MAX(finished_loading_folder_contents_at, finished_loading_statuses_at));

        float alpha = lerp(loading_end_time, tick, 1.0f, 8);

        // TODO we could put those into a clipper as well
#if 0
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
#endif

        float view_width = ImGui::GetContentRegionAvailWidth();
        u32 total_columns = current_folder.num_custom_columns + custom_columns_start_index;
        float custom_column_width = (view_width * 0.4f);

        if (current_folder.num_custom_columns) {
            custom_column_width /= current_folder.num_custom_columns;
        }

        Custom_Field** column_to_custom_field = (Custom_Field**) talloc(sizeof(Custom_Field*) * current_folder.num_custom_columns);

        for (u32 column = 0; column < current_folder.num_custom_columns; column++) {
            Id16* custom_field_id = &current_folder.custom_columns[column];

            // TODO ugly! We need to find a way to use ID as hash map key
            String id_as_string;
            id_as_string.start = custom_field_id->id;
            id_as_string.length = ID_16_LENGTH;

            // TODO hash cache!
            Custom_Field* custom_field = hash_map_get(&id_to_custom_field, id_as_string, hash_string(id_as_string));

            column_to_custom_field[column] = custom_field;
        }

        draw_task_list_header(view_width, custom_column_width, column_to_custom_field, total_columns);

        ImGui::Separator();
        ImGui::BeginChild("task_description_and_comments", ImVec2(-1, -1));
        ImGui::BeginColumns("table_view_columns", total_columns);

        ImGui::SetColumnWidth(0, view_width * 0.4f);
        ImGui::SetColumnWidth(1, view_width * 0.2f);

        for (u32 column = 0; column < current_folder.num_custom_columns; column++) {
            ImGui::SetColumnWidth(custom_columns_start_index + column, custom_column_width);
        }

        for (u32 column = 0; column < total_columns; column++) {
            Custom_Field* custom_field = column >= custom_columns_start_index ?
                                         column_to_custom_field[column - custom_columns_start_index] :
                                         NULL;

            //ImGuiListClipper clipper(folder_task_count, ImGui::GetTextLineHeightWithSpacing());

            //while (clipper.Step()) {
            for (int index = 0; index < folder_task_count; index++) {
                Sorted_Folder_Task* task = &sorted_folder_tasks[index];

                if (show_only_active_tasks && task->cached_status->group != Status_Group_Active) {
                    continue;
                }

                draw_task_column(task, column, custom_field, alpha);
            }
            //}

            //clipper.End();

            if (column + 1 < total_columns) {
                ImGui::NextColumn();
            }
        }

        ImGui::EndColumns();
        ImGui::EndChild();
    } else {
        ImGui::LoadingIndicator(MIN(started_loading_folder_contents_at, started_loading_statuses_at));
    }

    ImGui::EndChildFrame();
}

static void process_folder_task_custom_field(Custom_Field_Value* custom_field_value, char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_id(json, next_token, custom_field_value->field_id);
        } else if (json_string_equals(json, property_token, "value")) {
            json_token_to_string(json, next_token, custom_field_value->value);
        } else {
            eat_json(token);
            token--;
        }
    }
}

static void process_folder_contents_data_object(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Folder_Task* folder_task = &folder_tasks[folder_task_count];
    folder_task->num_custom_field_values = 0;

    Sorted_Folder_Task* sorted_folder_task = &sorted_folder_tasks[folder_task_count];
    sorted_folder_task->source_task = folder_task;

    folder_task_count++;

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

            folder_task->custom_status_id_hash = hash_id(folder_task->custom_status_id);
        } else if (json_string_equals(json, property_token, "customFields")) {
            assert(next_token->type == JSMN_ARRAY);

            token++;

            if (next_token->size > 0) {
                folder_task->custom_field_values = reserve_n_custom_field_values(next_token->size);
            }

            for (u32 field_index = 0; field_index < next_token->size; field_index++) {
                Custom_Field_Value* value = &folder_task->custom_field_values[folder_task->num_custom_field_values++];

                process_folder_task_custom_field(value, json, token);
            }

            token--;
        } else {
            eat_json(token);
            token--;
        }
    }
}

static void process_custom_status(char* json, jsmntok_t*& token, u32 natural_index) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Custom_Status* custom_status = &custom_statuses[custom_statuses_count++];
    custom_status->natural_index = natural_index;

    // TODO unused
    bool is_standard = false;

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

            custom_status->group = status_group_name_to_status_group(group_name);
        } else {
            eat_json(token);
            token--;
        }
    }

    if (!custom_status->color) {
        custom_status->color = argb_to_agbr(status_group_to_color(custom_status->group));
    }

    custom_status->id_hash = hash_id(custom_status->id);

//    printf("Got status %.*s with hash %lu\n", custom_status->name.length, custom_status->name.start, custom_status->id_hash);

    hash_map_put(&id_to_custom_status, custom_status, custom_status->id_hash);
}

void process_workflows_data(char* json, u32 data_size, jsmntok_t*&token) {
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
                    process_custom_status(json, token, status_index);
                }

                token--;
            } else {
                eat_json(token);
                token--;
            }
        }
    }
}

void process_folder_header_data(char* json, u32 data_size, jsmntok_t*& token) {
    // We only request singular folders now
    assert(data_size == 1);
    assert(token->type == JSMN_OBJECT);

    jsmntok_t* object_token = token++;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "customColumnIds")) {
            current_folder.custom_columns = (Id16*) realloc(current_folder.custom_columns, sizeof(Id16) * next_token->size);
            current_folder.num_custom_columns = 0;

            for (u32 array_index = 0; array_index < next_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                json_token_to_id(json, id_token, current_folder.custom_columns[current_folder.num_custom_columns++]);
            }
        } else {
            eat_json(token);
            token--;
        }
    }
}

void process_folder_contents_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (folder_task_count < data_size) {
        folder_tasks = (Folder_Task*) realloc(folder_tasks, sizeof(Folder_Task) * data_size);
        sorted_folder_tasks = (Sorted_Folder_Task*) realloc(sorted_folder_tasks, sizeof(Sorted_Folder_Task) * data_size);
    }

    folder_task_count = 0;
    custom_field_values_count = 0;

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_contents_data_object(json, token);
    }

    has_been_sorted_after_loading = false;
}