#include <jsmn.h>
#include <cstdio>
#include <imgui.h>
#include "id_hash_map.h"
#include "json.h"
#include "temporary_storage.h"
#include "accounts.h"
#include "main.h"
#include "lazy_array.h"
#include "platform.h"
#include "users.h"
#include "workflows.h"
#include "task_view.h"
#include "funimgui.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

enum Task_List_Sort_Field {
    Task_List_Sort_Field_None,
    Task_List_Sort_Field_Title,
    Task_List_Sort_Field_Status,
    Task_List_Sort_Field_Assignee,
    Task_List_Sort_Field_Custom_Field
};

struct Folder_Task {
    Task_Id id;
    Custom_Status_Id custom_status_id;
    u32 custom_status_id_hash;

    String title;
    Relative_Pointer<Custom_Field_Value> custom_field_values;
    u32 num_custom_field_values;

    Relative_Pointer<Task_Id> parent_task_ids;
    u32 num_parent_task_ids;

    Relative_Pointer<User_Id> assignees;
    u32 num_assignees;
};

struct Folder_Header {
    String name;
    Custom_Field_Id* custom_columns;
    u32 num_custom_columns;
};

struct Sorted_Folder_Task {
    Task_Id id;
    u32 id_hash;

    Folder_Task* source_task;
    Custom_Status* cached_status;
    Relative_Pointer<Sorted_Folder_Task*> sub_tasks;
    u32 num_sub_tasks;

    bool is_expanded;
};

struct Flattened_Folder_Task {
    Sorted_Folder_Task* sorted_task;
    u32 nesting_level;

    bool has_visible_subtasks;
};

struct Table_Paint_Context {
    ImDrawList* draw_list;
    Custom_Field** column_to_custom_field;
    u32 total_columns;
    float row_height;
    float scale;
    float text_padding_y;
};

static const u32 custom_columns_start_index = 3;

static Folder_Header current_folder{};

static List<Folder_Task> folder_tasks{};
static Sorted_Folder_Task* sorted_folder_tasks = NULL;
static List<Flattened_Folder_Task> flattened_sorted_folder_task_tree{};
static Lazy_Array<Sorted_Folder_Task*, 32> top_level_tasks{};

static Id_Hash_Map<Task_Id, Sorted_Folder_Task*> id_to_sorted_folder_task{};

static Lazy_Array<Custom_Field_Value, 16> custom_field_values{};
static Lazy_Array<Task_Id, 16> parent_task_ids{};
static Lazy_Array<User_Id, 16> assignee_ids{};
static Lazy_Array<Sorted_Folder_Task*, 64> sub_tasks{};

typedef char Sort_Direction;
static const Sort_Direction Sort_Direction_Normal = 1;
static const Sort_Direction Sort_Direction_Reverse = -1;

static Task_List_Sort_Field sort_field = Task_List_Sort_Field_None;
static Custom_Field_Id sort_custom_field_id{};
static Custom_Field* sort_custom_field;
static Sort_Direction sort_direction = Sort_Direction_Normal;
static bool has_been_sorted_after_loading = false;
static bool show_only_active_tasks = true;
static bool queue_flattened_tree_rebuild = false;

static const u32 active_text_color = argb_to_agbr(0xff4488ff);
static const u32 table_text_color = 0xff191919;

static inline int compare_tasks_custom_fields(Folder_Task* a, Folder_Task* b, Custom_Field_Type custom_field_type) {
    String* a_value = NULL;
    String* b_value = NULL;

    // TODO we could cache that to sort big lists faster
    for (u32 index = 0; index < a->num_custom_field_values; index++) {
        if (a->custom_field_values[index].field_id == sort_custom_field_id) {
            a_value = &a->custom_field_values[index].value;
            break;
        }
    }

    if (!a_value) {
        return 1;
    }

    for (u32 index = 0; index < b->num_custom_field_values; index++) {
        if (b->custom_field_values[index].field_id == sort_custom_field_id) {
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

static inline int compare_folder_tasks_based_on_current_sort(Sorted_Folder_Task* as, Sorted_Folder_Task* bs) {
    Folder_Task* a = as->source_task;
    Folder_Task* b = bs->source_task;

    // TODO instead of switching in each call we could just move those into separate functions
    switch (sort_field) {
        case Task_List_Sort_Field_Title: {
            return strncmp(a->title.start, b->title.start, MIN(a->title.length, b->title.length)) * sort_direction;
        }

        case Task_List_Sort_Field_Assignee: {
            if (!a->num_assignees) {
                return 1;
            }

            if (!b->num_assignees) {
                return -1;
            }

            // TODO all of this could be cached into a Sorted_Task including full name
            User_Id a_first_assignee = a->assignees[0];
            User_Id b_first_assignee = b->assignees[0];

            User* a_assignee = find_user_by_id(a_first_assignee);

            if (!a_assignee) {
                return 1;
            }

            User* b_assignee = find_user_by_id(b_first_assignee);

            if (!b_assignee) {
                return -1;
            }

            temporary_storage_mark();

            String a_name = full_user_name_to_temporary_string(a_assignee);
            String b_name = full_user_name_to_temporary_string(b_assignee);

            s32 comparison_result = strncmp(a_name.start, b_name.start, MIN(a_name.length, b_name.length)) * sort_direction;

            temporary_storage_reset();

            return comparison_result;
        }

        case Task_List_Sort_Field_Status: {
            Custom_Status* a_status = as->cached_status;
            Custom_Status* b_status = bs->cached_status;

            // TODO do status comparison based on status type?

            s32 natural_index_comparison_result = a_status->natural_index - b_status->natural_index;

            if (!natural_index_comparison_result) {
                return (a_status->id - b_status->id) * sort_direction;
            }

            return natural_index_comparison_result * sort_direction;
        }

        case Task_List_Sort_Field_Custom_Field: {
            return compare_tasks_custom_fields(a, b, sort_custom_field->type) * sort_direction;
        }

        case Task_List_Sort_Field_None: {
            assert(!"Invalid sort field");
        }
    }

    return 0;
}

static int compare_folder_tasks_based_on_current_sort_and_their_ids(const void* ap, const void* bp) {
    Sorted_Folder_Task* as = *(Sorted_Folder_Task**) ap;
    Sorted_Folder_Task* bs = *(Sorted_Folder_Task**) bp;

    int result = compare_folder_tasks_based_on_current_sort(as, bs);

    if (result == 0) {
        return (int) (as->source_task->id - bs->source_task->id);
    }

    return result;
}

static void sort_tasks_hierarchically(Sorted_Folder_Task** tasks, u32 length) {
    qsort(tasks, length, sizeof(Sorted_Folder_Task*), compare_folder_tasks_based_on_current_sort_and_their_ids);

    for (u32 task_index = 0; task_index < length; task_index++) {
        for (u32 sub_task_index = 0; sub_task_index < tasks[task_index]->num_sub_tasks; sub_task_index++) {
            sort_tasks_hierarchically(&tasks[task_index]->sub_tasks[0], tasks[task_index]->num_sub_tasks);
        }
    }
}

static bool rebuild_flattened_folder_tree_hierarchically(Sorted_Folder_Task* task, bool is_parent_expanded, u32 level) {
    if (show_only_active_tasks && task->cached_status->group != Status_Group_Active) {
        return false;
    }

    if (is_parent_expanded) {
        Flattened_Folder_Task* flattened_task = &flattened_sorted_folder_task_tree[flattened_sorted_folder_task_tree.length++];
        flattened_task->sorted_task = task;
        flattened_task->nesting_level = level;
        flattened_task->has_visible_subtasks = false;

        for (u32 sub_task_index = 0; sub_task_index < task->num_sub_tasks; sub_task_index++) {
            flattened_task->has_visible_subtasks |= rebuild_flattened_folder_tree_hierarchically(task->sub_tasks[sub_task_index], task->is_expanded, level + 1);
        }
    }

    return true;
}

static void rebuild_flattened_folder_tree() {
    flattened_sorted_folder_task_tree.length = 0;

    for (u32 task_index = 0; task_index < top_level_tasks.length; task_index++) {
        rebuild_flattened_folder_tree_hierarchically(top_level_tasks[task_index], true, 0);
    }
}

static void update_cached_data_for_sorted_tasks() {
    // TODO We actually only need to do that once when tasks/workflows combination changes, not for every sort
    for (u32 index = 0; index < folder_tasks.length; index++) {
        Sorted_Folder_Task* sorted_folder_task = &sorted_folder_tasks[index];
        Folder_Task* source = sorted_folder_task->source_task;

        sorted_folder_task->cached_status = find_custom_status_by_id(source->custom_status_id, source->custom_status_id_hash);
    }
}

static void sort_by_field(Task_List_Sort_Field sort_by) {
    assert(sort_by != Task_List_Sort_Field_Custom_Field);

    if (sort_field == sort_by) {
        sort_direction *= -1;
    } else {
        sort_direction = Sort_Direction_Normal;
    }

    update_cached_data_for_sorted_tasks();

    sort_field = sort_by;

    u64 start = platform_get_app_time_precise();
    sort_tasks_hierarchically(top_level_tasks.data, top_level_tasks.length);
    rebuild_flattened_folder_tree();
    printf("Sorting %i elements by %i took %fms\n", folder_tasks.length, sort_by, platform_get_delta_time_ms(start));
}

static void sort_by_custom_field(Custom_Field_Id field_id) {
    if (sort_field == Task_List_Sort_Field_Custom_Field && field_id == sort_custom_field_id) {
        sort_direction *= -1;
    } else {
        sort_direction = Sort_Direction_Normal;
    }

    update_cached_data_for_sorted_tasks();

    sort_field = Task_List_Sort_Field_Custom_Field;
    sort_custom_field_id = field_id;
    sort_custom_field = find_custom_field_by_id(field_id, hash_id(field_id)); // TODO hash cache?

    u64 start = platform_get_app_time_precise();
    sort_tasks_hierarchically(top_level_tasks.data, top_level_tasks.length);
    rebuild_flattened_folder_tree();
    printf("Sorting %i elements by %i took %fms\n", folder_tasks.length, field_id, platform_get_delta_time_ms(start));
}

Custom_Field** map_columns_to_custom_fields() {
    Custom_Field** column_to_custom_field = (Custom_Field**) talloc(sizeof(Custom_Field*) * current_folder.num_custom_columns);

    for (u32 column = 0; column < current_folder.num_custom_columns; column++) {
        Custom_Field_Id custom_field_id = current_folder.custom_columns[column];

        // TODO hash cache!
        Custom_Field* custom_field = find_custom_field_by_id(custom_field_id, hash_id(custom_field_id));

        column_to_custom_field[column] = custom_field;
    }

    return column_to_custom_field;
}

Custom_Field_Value* try_find_custom_field_value_in_task(Folder_Task* task, Custom_Field* field) {
    if (!field) return NULL;

    for (u32 index = 0; index < task->num_custom_field_values; index++) {
        Custom_Field_Value* value = &task->custom_field_values[index];

        if (value->field_id == field->id) {
            return value;
        }
    }

    return NULL;
}

void draw_assignees_cell_contents(ImDrawList* draw_list, Folder_Task* task, ImVec2 text_position) {
    for (u32 assignee_index = 0; assignee_index < task->num_assignees; assignee_index++) {
        User_Id user_id = task->assignees[assignee_index];
        User* user = find_user_by_id(user_id);

        if (!user) {
            continue;
        }

        bool is_not_last = assignee_index < task->num_assignees - 1;
        const char* name_pattern = "%.*s %.*s";

        if (is_not_last) {
            name_pattern = "%.*s %.*s, ";
        }

        char* start, *end;

        tprintf(name_pattern, &start, &end,
                user->first_name.length, user->first_name.start,
                user->last_name.length, user->last_name.start);

        float text_width = ImGui::CalcTextSize(start, end).x;

        draw_list->AddText(text_position, table_text_color, start, end);

        text_position.x += text_width;
    }
}

bool draw_open_task_button(Table_Paint_Context& context, ImVec2 cell_top_left, float column_width) {
    ImVec2 button_size(30.0f * context.scale, context.row_height);
    ImVec2 top_left = cell_top_left + ImVec2(column_width, 0) - ImVec2(button_size.x, 0);
    ImVec2 bottom_right = top_left + button_size;

    ImRect bounds(top_left, bottom_right);

    ImGuiID id = ImGui::GetID("task_open_button");

    bool is_clipped = !ImGui::ItemAdd(bounds, id);

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

    if (is_clipped) {
        return pressed;
    }

    ImVec2 icon_size{ button_size.x / 3.5f, context.row_height / 4.0f };
    ImVec2 icon_top_left = top_left + button_size / 2.0f - icon_size / 2.0f;
    ImVec2 icon_bottom_right = icon_top_left + icon_size;
    ImVec2 icon_bottom_left = icon_top_left + ImVec2(0.0f, icon_size.y);
    ImVec2 icon_secondary_offset = ImVec2(-2.0f, 1.5f) * context.scale;

    u32 color = hovered ? active_text_color : table_text_color;

    context.draw_list->AddRectFilled(top_left, bottom_right, IM_COL32_WHITE);
    context.draw_list->AddLine(icon_top_left + icon_secondary_offset, icon_bottom_left + icon_secondary_offset, color, 1.5f);
    context.draw_list->AddLine(icon_bottom_left + icon_secondary_offset, icon_bottom_right + icon_secondary_offset, color, 1.5f);
    context.draw_list->AddRect(icon_top_left, icon_bottom_right, color, 0, ImDrawCornerFlags_All, 1.5f);

    return pressed;
}

bool draw_expand_arrow_button(Table_Paint_Context& context, bool is_expanded, ImVec2 cell_top_left, float nesting_level_padding) {
    const static u32 expand_arrow_color = 0xff848484;
    const static u32 expand_arrow_hovered = argb_to_agbr(0xff73a6ff);

    ImVec2 arrow_point = cell_top_left + ImVec2(context.scale * 20.0f + nesting_level_padding, context.row_height / 2.0f);
    float arrow_half_height = ImGui::GetFontSize() / 4.0f;
    float arrow_width = arrow_half_height;

    ImGuiID id = ImGui::GetID("task_expand_arrow");

    const ImRect bounds({ arrow_point.x - arrow_width * 3.5f, cell_top_left.y },
                        { arrow_point.x + arrow_width * 2.5f, cell_top_left.y + context.row_height });

    bool is_clipped = !ImGui::ItemAdd(bounds, id);

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

    if (is_clipped) {
        return pressed;
    }

    u32 color = hovered ? expand_arrow_hovered : expand_arrow_color;

    if (!is_expanded) {
        ImVec2 arrow_top_left = arrow_point - ImVec2(arrow_width,  arrow_half_height);
        ImVec2 arrow_bottom_right = arrow_point - ImVec2(arrow_width, -arrow_half_height);

        context.draw_list->AddLine(arrow_point, arrow_top_left, color);
        context.draw_list->AddLine(arrow_point, arrow_bottom_right, color);
    } else {
        ImVec2 arrow_right = arrow_point + ImVec2(arrow_width / 2.0f, 0.0f);
        ImVec2 arrow_bottom_point = arrow_right - ImVec2(arrow_width, -arrow_half_height);

        context.draw_list->AddLine(arrow_right - ImVec2(arrow_width * 2.0f, 0.0f), arrow_bottom_point, color);
        context.draw_list->AddLine(arrow_right, arrow_bottom_point, color);
    }

    return pressed;
}

void draw_table_cell_for_task(Table_Paint_Context& context, u32 column, float column_width, Flattened_Folder_Task* flattened_task, ImVec2 cell_top_left) {
    ImVec2 padding(context.scale * 8.0f, context.text_padding_y);
    Sorted_Folder_Task* sorted_task = flattened_task->sorted_task;
    Folder_Task* task = sorted_task->source_task;

    switch (column) {
        case 0: {
            float nesting_level_padding = flattened_task->nesting_level * 20.0f * context.scale;

            if (flattened_task->has_visible_subtasks) {
                if (draw_expand_arrow_button(context, sorted_task->is_expanded, cell_top_left, nesting_level_padding)) {
                    sorted_task->is_expanded = !sorted_task->is_expanded;
                    /* TODO We don't need to rebuild the whole tree there
                     * TODO     this is just inserting/removing subtask tree after the current task and could be done
                     * TODO     with a simple subtree traversal to determine subtree size, then a subsequent list insert
                     * TODO     or a simple memcpy in case of a removal
                     * TODO Also queuing there is not really necessary, since the only part of the flattened array
                     * TODO     which changes lies after the current element but the current solution feels cleaner,
                     * TODO     although it delays the update for one frame
                     */
                    queue_flattened_tree_rebuild = true;
                }
            }

            ImVec2 title_padding(context.scale * 40.0f + nesting_level_padding, context.text_padding_y);

            char* start = task->title.start, * end = task->title.start + task->title.length;

            context.draw_list->AddText(cell_top_left + title_padding, table_text_color, start, end);

            if (ImGui::IsMouseHoveringRect(cell_top_left, cell_top_left + ImVec2(column_width, context.row_height))) {
                if (draw_open_task_button(context, cell_top_left, column_width)) {
                    request_task_by_task_id(task->id);
                }
            }

            break;
        }

        case 1: {
            Custom_Status* status = sorted_task->cached_status;

            if (status) {
                char* start = status->name.start, * end = status->name.start + status->name.length;

                context.draw_list->AddText(cell_top_left + padding, status->color, start, end);
            }

            break;
        }

        case 2: {
            ImVec2 text_position = cell_top_left + padding;

            draw_assignees_cell_contents(context.draw_list, task, text_position);

            break;
        }

        default: {
            if (column > custom_columns_start_index) {
                Custom_Field* custom_field = context.column_to_custom_field[column - custom_columns_start_index];
                Custom_Field_Value* field_value = try_find_custom_field_value_in_task(task, custom_field);

                if (field_value) {
                    char* start = field_value->value.start, * end = field_value->value.start + field_value->value.length;

                    context.draw_list->AddText(cell_top_left + padding, table_text_color, start, end);
                }
            }
        }
    }
}

float get_column_width(Table_Paint_Context& context, u32 column) {
    float column_width = 50.0f;

    if (column == 0) {
        column_width = 500.0f;
    } else if (column == 1) {
        column_width = 150.0f;
    } else if (column == 2) {
        column_width = 200.0f;
    }

    return column_width * context.scale;
}

String get_column_title(Table_Paint_Context& context, u32 column) {
    String column_title{};

    switch (column) {
        case 0: column_title.start = (char*) "Title"; break;
        case 1: column_title.start = (char*) "Status"; break;
        case 2: column_title.start = (char*) "Assignees"; break;

        default: {
            Custom_Field* custom_field = context.column_to_custom_field[column - custom_columns_start_index];
            column_title = custom_field->title;
        }
    }

    if (!column_title.length) {
        column_title.length = (u32) strlen(column_title.start);
    }

    return column_title;
}

Task_List_Sort_Field get_column_sort_field(u32 column) {
    switch (column) {
        case 0: return Task_List_Sort_Field_Title;
        case 1: return Task_List_Sort_Field_Status;
        case 2: return Task_List_Sort_Field_Assignee;

        default: {
            return Task_List_Sort_Field_Custom_Field;
        }
    }
}

void draw_folder_header(Table_Paint_Context& context, float content_width) {
    ImVec2 top_left = ImGui::GetCursorScreenPos();

    float toolbar_height = 32.0f * context.scale;
    float folder_header_height = 56.0f * context.scale;

    ImGui::Dummy({ 0, folder_header_height + toolbar_height});

    ImGui::PushFont(font_header);

    ImVec2 folder_header_padding = ImVec2(32.0f, folder_header_height / 2.0f - ImGui::GetFontSize() / 2.0f);

    context.draw_list->AddText(top_left + folder_header_padding, table_text_color,
                       current_folder.name.start, current_folder.name.start + current_folder.name.length);

    ImGui::PopFont();

    const u32 toolbar_background = 0xfff7f7f7;

    ImVec2 toolbar_top_left = top_left + ImVec2(0, folder_header_height);
    ImVec2 toolbar_bottom_right = toolbar_top_left + ImVec2(content_width, toolbar_height);

    context.draw_list->AddRectFilled(toolbar_top_left, toolbar_bottom_right, toolbar_background);
}

void draw_table_header(Table_Paint_Context& context, ImVec2 window_top_left) {
    float column_left_x = 0.0f;
    ImDrawList* draw_list = context.draw_list;

    for (u32 column = 0; column < context.total_columns; column++) {
        float column_width = get_column_width(context, column);
        String column_title = get_column_title(context, column);

        char* start, * end;

        start = column_title.start;
        end = column_title.start + column_title.length;

        ImVec2 column_top_left_absolute = window_top_left + ImVec2(column_left_x, 0) + ImVec2(0, ImGui::GetScrollY());

        ImVec2 size { column_width, context.row_height };

        ImGui::PushID(column);

        ImGuiID id = ImGui::GetID("header_sort_button");

        ImGui::PopID();

        const ImRect bounds(column_top_left_absolute, column_top_left_absolute + size);
        bool is_clipped = !ImGui::ItemAdd(bounds, id);

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);

        bool sorting_by_this_column;

        {
            Task_List_Sort_Field column_sort_field = get_column_sort_field(column);

            if (sort_field == Task_List_Sort_Field_Custom_Field) {
                Custom_Field* column_custom_field = context.column_to_custom_field[column - custom_columns_start_index];

                if (pressed) {
                    sort_by_custom_field(column_custom_field->id);
                }

                sorting_by_this_column = sort_custom_field_id == column_custom_field->id;
            } else {
                if (pressed) {
                    sort_by_field(column_sort_field);
                }

                sorting_by_this_column = column_sort_field == sort_field;
            }
        }

        if (is_clipped) {
            column_left_x += column_width;

            continue;
        }

        u32 text_color = hovered ? active_text_color : table_text_color;

        const u32 grid_color = 0xffebebeb;

        draw_list->AddRectFilled(column_top_left_absolute, column_top_left_absolute + size, IM_COL32_WHITE);
        draw_list->AddText(column_top_left_absolute + ImVec2(8.0f * context.scale, context.text_padding_y), text_color, start, end);
        draw_list->AddLine(column_top_left_absolute, column_top_left_absolute + ImVec2(0, context.row_height), grid_color, 1.25f);

        if (sorting_by_this_column) {
            // TODO draw sort direction arrow
        }

        column_left_x += column_width;
    }
}

void draw_task_list() {
    ImGuiID task_list = ImGui::GetID("task_list");
    ImGui::BeginChildFrame(task_list, ImVec2(-1, -1));

    const bool is_folder_data_loading = folder_contents_request != NO_REQUEST || folder_header_request != NO_REQUEST;
    const bool are_users_loading = contacts_request != NO_REQUEST;

    if (!is_folder_data_loading && custom_statuses_were_loaded && !are_users_loading) {
        if (!has_been_sorted_after_loading) {
            sort_by_field(Task_List_Sort_Field_Title);
            has_been_sorted_after_loading = true;
        }

        if (queue_flattened_tree_rebuild) {
            queue_flattened_tree_rebuild = false;
            rebuild_flattened_folder_tree();
        }

        const u32 grid_color = 0xffebebeb;
        const float scale = platform_get_pixel_ratio();
        const float row_height = 28.0f * scale;

        Table_Paint_Context paint_context;
        paint_context.scale = scale;
        paint_context.draw_list = ImGui::GetWindowDrawList();
        paint_context.row_height = row_height;
        paint_context.text_padding_y = row_height / 2.0f - ImGui::GetFontSize() / 2.0f;
        paint_context.column_to_custom_field = map_columns_to_custom_fields();
        paint_context.total_columns = current_folder.num_custom_columns + custom_columns_start_index;

        draw_folder_header(paint_context, ImGui::GetWindowWidth());

        ImGui::BeginChild("table_content", ImVec2(-1, -1), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        paint_context.draw_list = draw_list;

        const float content_width = ImGui::GetWindowWidth();
        const float content_height = ImGui::GetWindowHeight();

        const ImVec2 window_top_left = ImGui::GetCursorScreenPos();
        const ImVec2 window_bottom_right_no_scroll = ImGui::GetWindowPos() + ImVec2(content_width, content_height);

        float column_left_x = 0.0f;

        u32 top_row = MAX(0, (u32) floorf(ImGui::GetScrollY() / row_height));
        u32 bottom_row = MIN(flattened_sorted_folder_task_tree.length, (u32) ceilf((ImGui::GetScrollY() + content_height) / row_height));

        for (u32 column = 0; column < paint_context.total_columns; column++) {
            float column_width = get_column_width(paint_context, column);

            for (u32 row = top_row; row < bottom_row; row++) {
                Flattened_Folder_Task* flattened_task = &flattened_sorted_folder_task_tree[row];

                float row_top_y = row_height * (row + 1);

                ImVec2 top_left(column_left_x, row_top_y);
                top_left += window_top_left;

                ImGui::PushID(flattened_task);

                draw_table_cell_for_task(paint_context, column, column_width, flattened_task, top_left);

                ImGui::PopID();
            }

            ImVec2 column_top_left_absolute = window_top_left + ImVec2(column_left_x, 0) + ImVec2(0, ImGui::GetScrollY());
            ImVec2 column_bottom_left_absolute = window_top_left + ImVec2(column_left_x, content_height) + ImVec2(0, ImGui::GetScrollY());

            draw_list->AddRectFilled(column_top_left_absolute + ImVec2(column_width, 0), window_bottom_right_no_scroll, IM_COL32_WHITE);
            draw_list->AddLine(column_top_left_absolute, column_bottom_left_absolute, grid_color, 1.25f);

            column_left_x += column_width;
        }

        for (u32 row = 0; row < flattened_sorted_folder_task_tree.length; row++) {
            float row_line_y = row_height * (row + 1);

            draw_list->AddLine(window_top_left + ImVec2(0, row_line_y), window_top_left + ImVec2(column_left_x, row_line_y), grid_color, 1.25f);
        }

        draw_table_header(paint_context, window_top_left);

        // Scrollbar
        ImGui::Dummy(ImVec2(column_left_x, flattened_sorted_folder_task_tree.length * row_height));
        ImGui::EndChild();

        u32
                loading_end_time = MAX(finished_loading_folder_contents_at, finished_loading_statuses_at);
                loading_end_time = MAX(finished_loading_users_at, loading_end_time);
                loading_end_time = MAX(finished_loading_statuses_at, loading_end_time);

        float alpha = lerp(loading_end_time, tick, 1.0f, 8);

        ImGui::FadeInOverlay(alpha);
    } else {
        u32
                loading_start_time = MIN(started_loading_folder_contents_at, started_loading_statuses_at);
                loading_start_time = MIN(started_loading_users_at, loading_start_time);
                loading_start_time = MIN(started_loading_statuses_at, loading_start_time);

        ImGui::LoadingIndicator(loading_start_time);
    }

    ImGui::EndChildFrame();
}

static void process_folder_contents_data_object(char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    Folder_Task* folder_task = &folder_tasks[folder_tasks.length];
    folder_task->num_parent_task_ids = 0;
    folder_task->num_custom_field_values = 0;
    folder_task->num_assignees = 0;

    Sorted_Folder_Task* sorted_folder_task = &sorted_folder_tasks[folder_tasks.length];
    sorted_folder_task->num_sub_tasks = 0;
    sorted_folder_task->source_task = folder_task;
    sorted_folder_task->is_expanded = false;

    folder_tasks.length++;

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, folder_task->title);
        } else if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, folder_task->id);
        } else if (json_string_equals(json, property_token, "customStatusId")) {
            json_token_to_right_part_of_id16(json, next_token, folder_task->custom_status_id);

            folder_task->custom_status_id_hash = hash_id(folder_task->custom_status_id);
        } else if (json_string_equals(json, property_token, "responsibleIds")) {
            assert(next_token->type == JSMN_ARRAY);

            token++;

            if (next_token->size > 0) {
                folder_task->assignees = lazy_array_reserve_n_values_relative_pointer(assignee_ids, next_token->size);
            }

            for (u32 field_index = 0; field_index < next_token->size; field_index++, token++) {
                json_token_to_id8(json, token, folder_task->assignees[folder_task->num_assignees++]);
            }

            token--;
        } else if (json_string_equals(json, property_token, "superTaskIds")) {
            assert(next_token->type == JSMN_ARRAY);

            token++;

            if (next_token->size > 0) {
                folder_task->parent_task_ids = lazy_array_reserve_n_values_relative_pointer(parent_task_ids, next_token->size);
            }

            for (u32 field_index = 0; field_index < next_token->size; field_index++, token++) {
                json_token_to_right_part_of_id16(json, token, folder_task->parent_task_ids[folder_task->num_parent_task_ids++]);
            }

            token--;
        } else if (json_string_equals(json, property_token, "customFields")) {
            assert(next_token->type == JSMN_ARRAY);

            token++;

            if (next_token->size > 0) {
                folder_task->custom_field_values = lazy_array_reserve_n_values_relative_pointer(custom_field_values, next_token->size);
            }

            for (u32 field_index = 0; field_index < next_token->size; field_index++) {
                Custom_Field_Value* value = &folder_task->custom_field_values[folder_task->num_custom_field_values++];

                // TODO a dependency on task_view is not really good, should we move the code somewhere else?
                process_task_custom_field_value(value, json, token);
            }

            token--;
        } else {
            eat_json(token);
            token--;
        }
    }

    sorted_folder_task->id = folder_task->id;
    sorted_folder_task->id_hash = hash_id(folder_task->id);

    id_hash_map_put(&id_to_sorted_folder_task, sorted_folder_task, folder_task->id, sorted_folder_task->id_hash);
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

        if (json_string_equals(json, property_token, "title")) {
            json_token_to_string(json, next_token, current_folder.name);
        } else if (json_string_equals(json, property_token, "customColumnIds")) {
            current_folder.custom_columns = (Custom_Field_Id*) REALLOC(current_folder.custom_columns, sizeof(Custom_Field_Id) * next_token->size);
            current_folder.num_custom_columns = 0;

            for (u32 array_index = 0; array_index < next_token->size; array_index++) {
                jsmntok_t* id_token = ++token;

                assert(id_token->type == JSMN_STRING);

                json_token_to_right_part_of_id16(json, id_token, current_folder.custom_columns[current_folder.num_custom_columns++]);
            }
        } else {
            eat_json(token);
            token--;
        }
    }
}

static void associate_parent_tasks_with_sub_tasks() {
    // Step 1: count sub tasks for each parent, while also deciding which parents should go into the root
    for (u32 task_index = 0; task_index < folder_tasks.length; task_index++) {
        Sorted_Folder_Task* folder_task = &sorted_folder_tasks[task_index];
        Folder_Task* source_task = folder_task->source_task;

        bool found_at_least_one_parent = false;

        if (source_task->num_parent_task_ids) {
            for (u32 id_index = 0; id_index < source_task->num_parent_task_ids; id_index++) {
                Task_Id parent_id = source_task->parent_task_ids[id_index];

                Sorted_Folder_Task* parent_or_null = id_hash_map_get(&id_to_sorted_folder_task, parent_id, hash_id(parent_id));

                if (parent_or_null) {
                    found_at_least_one_parent = true;

                    parent_or_null->num_sub_tasks++;
                }
            }
        }

        if (!found_at_least_one_parent) {
            Sorted_Folder_Task** pointer_to_task = lazy_array_reserve_n_values(top_level_tasks, 1);
            *pointer_to_task = folder_task;
        }
    }

    // Step 2: allocate space for sub tasks
    for (u32 task_index = 0; task_index < folder_tasks.length; task_index++) {
        Sorted_Folder_Task* folder_task = &sorted_folder_tasks[task_index];

        if (folder_task->num_sub_tasks) {
            folder_task->sub_tasks = lazy_array_reserve_n_values_relative_pointer(sub_tasks, folder_task->num_sub_tasks);

            folder_task->num_sub_tasks = 0;
        }
    }

    // Step 3: fill sub tasks
    for (u32 task_index = 0; task_index < folder_tasks.length; task_index++) {
        Sorted_Folder_Task* folder_task = &sorted_folder_tasks[task_index];
        Folder_Task* source_task = folder_task->source_task;

        if (source_task->num_parent_task_ids) {
            for (u32 id_index = 0; id_index < source_task->num_parent_task_ids; id_index++) {
                Task_Id parent_id = source_task->parent_task_ids[id_index];

                Sorted_Folder_Task* parent_or_null = id_hash_map_get(&id_to_sorted_folder_task, parent_id, hash_id(parent_id));

                if (parent_or_null) {
                    parent_or_null->sub_tasks[parent_or_null->num_sub_tasks++] = folder_task;
                }
            }
        }
    }
}

void process_folder_contents_data(char* json, u32 data_size, jsmntok_t*& token) {
    if (id_to_sorted_folder_task.table) {
        id_hash_map_destroy(&id_to_sorted_folder_task);
    }

    id_hash_map_init(&id_to_sorted_folder_task);

    if (folder_tasks.length < data_size) {
        folder_tasks.data = (Folder_Task*) REALLOC(folder_tasks.data, sizeof(Folder_Task) * data_size);
        sorted_folder_tasks = (Sorted_Folder_Task*) REALLOC(sorted_folder_tasks, sizeof(Sorted_Folder_Task) * data_size);
        flattened_sorted_folder_task_tree.data = (Flattened_Folder_Task*) REALLOC(flattened_sorted_folder_task_tree.data, sizeof(Flattened_Folder_Task) * data_size);
    }

    folder_tasks.length = 0;

    lazy_array_soft_reset(custom_field_values);
    lazy_array_soft_reset(parent_task_ids);
    lazy_array_soft_reset(top_level_tasks);
    lazy_array_soft_reset(sub_tasks);

    for (u32 array_index = 0; array_index < data_size; array_index++) {
        process_folder_contents_data_object(json, token);
    }

    associate_parent_tasks_with_sub_tasks();

    has_been_sorted_after_loading = false;
}

void process_current_folder_as_logical() {
    current_folder.num_custom_columns = 0;
}