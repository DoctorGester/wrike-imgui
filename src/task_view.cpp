#include "task_view.h"
#include "common.h"
#include "main.h"
#include "funimgui.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "platform.h"
#include "users.h"
#include "workflows.h"
#include "accounts.h"

#include <imgui.h>
#include <cstdlib>
#include <html_entities.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>
#include <cstdint>

struct RGB {
    float r;
    float g;
    float b;
};

struct HSL {
    int h;
    float s;
    float l;
};

static Memory_Image checkmark{};

static HSL rgb_to_hsl(RGB rgb) {
    HSL hsl;

    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;

    float min = MIN(MIN(r, g), b);
    float max = MAX(MAX(r, g), b);
    float delta = max - min;

    hsl.l = (max + min) / 2;

    if (delta == 0) {
        hsl.h = 0;
        hsl.s = 0.0f;
    } else {
        hsl.s = (hsl.l <= 0.5) ? (delta / (max + min)) : (delta / (2 - max - min));

        float hue;

        if (r == max) {
            hue = ((g - b) / 6) / delta;
        } else if (g == max) {
            hue = (1.0f / 3) + ((b - r) / 6) / delta;
        } else {
            hue = (2.0f / 3) + ((r - g) / 6) / delta;
        }

        if (hue < 0)
            hue += 1;
        if (hue > 1)
            hue -= 1;

        hsl.h = (int) (hue * 360);
    }

    return hsl;
}

static float hue_to_rgb(float v1, float v2, float vH) {
    if (vH < 0)
        vH += 1;

    if (vH > 1)
        vH -= 1;

    if ((6 * vH) < 1)
        return (v1 + (v2 - v1) * 6 * vH);

    if ((2 * vH) < 1)
        return v2;

    if ((3 * vH) < 2)
        return (v1 + (v2 - v1) * ((2.0f / 3) - vH) * 6);

    return v1;
}

struct RGB hsl_to_rgb(struct HSL hsl) {
    RGB rgb;

    if (hsl.s == 0) {
        rgb.r = rgb.g = rgb.b = (u8) (hsl.l * 255);
    } else {
        float v1, v2;
        float hue = (float) hsl.h / 360;

        v2 = (hsl.l < 0.5) ? (hsl.l * (1 + hsl.s)) : ((hsl.l + hsl.s) - (hsl.l * hsl.s));
        v1 = 2 * hsl.l - v2;

        rgb.r = hue_to_rgb(v1, v2, hue + (1.0f / 3));
        rgb.g = hue_to_rgb(v1, v2, hue);
        rgb.b = hue_to_rgb(v1, v2, hue - (1.0f / 3));
    }

    return rgb;
}

static u32 change_color_luminance(u32 in_color, float luminance) {
    ImVec4 color = ImGui::ColorConvertU32ToFloat4(in_color);
    HSL hsl = rgb_to_hsl({ color.x, color.y, color.z });
    hsl.l = luminance;
    RGB rgb_out = hsl_to_rgb(hsl);

    return ImGui::ColorConvertFloat4ToU32({ rgb_out.r, rgb_out.g, rgb_out.b, 1.0f });
}

static void draw_status_selector(Custom_Status* status, float alpha) {
    float scale = platform_get_pixel_ratio();

    u32 color = change_color_luminance(status->color, 0.42f);
    u32 bg_color = change_color_luminance(status->color, 0.94f);
    u32 border_color = change_color_luminance(status->color, 0.89f);

    ImGui::PushFont(font_bold);

    const char* text_begin = status->name.start;
    const char* text_end = text_begin + status->name.length;
    ImVec2 text_size = ImGui::CalcTextSize(text_begin, text_end, false);
    ImVec2 checkbox_size = ImVec2(18, 18) * scale;

    const float left_line_width = 10 * scale;
    const float padding = 21 * scale;

    float checkbox_offset_x = left_line_width + padding;
    float text_offset_x = checkbox_offset_x + checkbox_size.x + 8.0f * scale;

    ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 size = ImVec2(text_size.x + text_offset_x + padding * 2.0f - left_line_width, 50.0f * scale);
    ImVec2 checkbox_start = start + ImVec2(checkbox_offset_x, size.y / 2 - checkbox_size.y / 2);
    ImVec2 text_start = start + ImVec2(text_offset_x, size.y / 2 - text_size.y / 2);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("status_selector", size);

    draw_list->AddRectFilled(start, start + size, bg_color);
    draw_list->AddRectFilled(start, start + ImVec2(left_line_width, size.y), color);
    draw_list->AddLine(start + ImVec2(left_line_width, 0), start + ImVec2(size.x, 0), border_color);
    draw_list->AddLine(start + ImVec2(left_line_width, size.y), start + size, border_color);
    draw_list->AddLine(start + ImVec2(size.x, 0), start + size, border_color);

    if (status->group == Status_Group_Completed) {
        draw_list->AddImage((void*)(intptr_t) checkmark.texture_id, checkbox_start, checkbox_start + checkbox_size);
    } else {
        draw_list->AddRectFilled(checkbox_start, checkbox_start + checkbox_size, IM_COL32_WHITE);
    }

    draw_list->AddRect(checkbox_start, checkbox_start + checkbox_size, color, 0.0f, ImDrawCornerFlags_All, 2.0f);
    draw_list->AddText(text_start, IM_COL32_BLACK, text_begin, text_end);

//    ImVec2 arrow_position = text_start + ImVec2(text_size.x + 4, 0);
//    ImGui::PushStyleColor(ImGuiCol_Text, color);
//    ImGui::RenderArrow(arrow_position * scale, ImGuiKey_DownArrow, 0.5f);
//    ImGui::PopStyleColor();

    ImGui::PopFont();
}

static void draw_parent_folder(Folder_Tree_Node* folder, bool can_wrap, float wrap_pos) {
    static const u32 border_color = argb_to_agbr(0xffe0e0e0);
    static const u32 text_color = argb_to_agbr(0xff555555);

    const char* text_begin = folder->name.start;
    const char* text_end = text_begin + folder->name.length;

    ImVec2 offset = ImVec2(4.0f, 2.0f) * platform_get_pixel_ratio();
    ImVec2 text_size = ImGui::CalcTextSize(text_begin, text_end);
    ImVec2 size = text_size + offset * 2;

    float window_start_x = ImGui::GetCursorPosX();

    if (window_start_x + size.x > wrap_pos && can_wrap) {
        ImGui::NewLine();
    }

    ImVec2 start = ImGui::GetCursorScreenPos();

    // Make ID unique when actually using it
    ImGui::InvisibleButton("folder_ticker", size);

    ImGui::GetWindowDrawList()->AddRect(
            start,
            start + size,
            border_color,
            2.0f
    );

    ImGui::GetWindowDrawList()->AddText(start + offset, text_color, text_begin, text_end);
}

static void draw_parent_folders(float wrap_pos) {
    for (u32 parent_index = 0; parent_index < current_task.parents.length; parent_index++) {
        bool is_last = parent_index == (current_task.parents.length - 1);

        Folder_Id folder_id = current_task.parents[parent_index];
        Folder_Tree_Node* folder_tree_node = find_folder_tree_node_by_id(folder_id);

        if (folder_tree_node) {
            draw_parent_folder(folder_tree_node, parent_index > 0, wrap_pos);

            if (!is_last) {
                ImGui::SameLine();
            }
        }
    }
}

static void draw_assignees() {
    // TODO
    /*
     * Render at minimum 2 avatar + name as possible, the rest are +X
     * If 2 avatars with names do not fit, render how many avatars fit,
     * the rest are +X
     */

    for (u32 index = 0; index < current_task.assignees.length; index++) {
        bool is_last = index == (current_task.assignees.length - 1);

        User_Id user_id = current_task.assignees[index];
        User* user = find_user_by_id(user_id, hash_id(user_id)); // TODO hash cache

        if (user) {
            const char* pattern_last = "%.*s %.1s.";
            const char* pattern_preceding = "%.*s %.1s.,";

            ImGui::Text(is_last ? pattern_last : pattern_preceding,
                        user->first_name.length, user->first_name.start, user->last_name.start
            );

            if (!is_last) {
                ImGui::SameLine();
            }
        }
    }
}

static void draw_authors_and_task_id() {
    User_Id author_id = current_task.authors[0]; // TODO currently only using the first
    User* author = find_user_by_id(author_id);

    if (author) {
        ImGui::SameLine();

        static const u32 color_id_and_author = argb_to_agbr(0xff8c8c8c);
        ImGui::PushStyleColor(ImGuiCol_Text, color_id_and_author);
        ImGui::Text("#%i by %.*s %.1s",
                    current_task.id,
                    author->first_name.length, author->first_name.start,
                    author->last_name.start);
        ImGui::PopStyleColor();
    }
}

// TODO a naive and slow approach, could cache
static Custom_Field_Value* find_custom_field_value_by_custom_field_id(Custom_Field_Id id) {
    for (u32 value_index = 0; value_index < current_task.custom_field_values.length; value_index++) {
        Custom_Field_Value* value = &current_task.custom_field_values.data[value_index];

        if (value->field_id == id) {
            return value;
        }
    }

    return NULL;
}

static const char* get_custom_field_placeholder_text_by_custom_field_type(Custom_Field_Type type) {
    switch (type) {
        default: return "Enter data";
        case Custom_Field_Type_DropDown: return "Select";
        case Custom_Field_Type_Contacts: return "Add User";
        case Custom_Field_Type_Date: return "mm/dd/yy";
    }
}

static bool draw_custom_fields(float wrap_pos) {
    static const u32 key_value_text_color = argb_to_agbr(0xff111111);
    static const u32 placeholder_text_color = argb_to_agbr(0xffaeaeae);
    static const u32 border_color = argb_to_agbr(0xffeeeeee);
    static const u32 background_color = argb_to_agbr(0xfffafafa);

    bool drew_at_least_one_custom_field = false;

    ImGui::PushFont(font_bold);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2());

    ImVec2 top_left_corner = ImGui::GetCursorScreenPos();
    float previous_line_end_x = top_left_corner.x;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Save to restore them later
    // We need to keep the sizes instead of raw pointers because buffer resizes might occur
    s32 vtx_buffer_old_size = draw_list->VtxBuffer.Size;
    s32 idx_buffer_old_size = draw_list->IdxBuffer.Size;
    u32 vtx_background_idx = draw_list->_VtxCurrentIdx;

    // Reserve buffer space to fill later when we know the total size
    draw_list->PrimReserve(6, 4);

    for (u32 field_index = 0; field_index < current_task.inherited_custom_fields.length; field_index++) {
        Custom_Field_Id custom_field_id = current_task.inherited_custom_fields[field_index];
        Custom_Field* custom_field = find_custom_field_by_id(custom_field_id);

        if (!custom_field) {
            continue;
        }

        drew_at_least_one_custom_field = true;

        Custom_Field_Value* possible_value = find_custom_field_value_by_custom_field_id(custom_field_id);

        const char* key_text_begin = custom_field->title.start;
        const char* key_text_end = key_text_begin + custom_field->title.length;

        ImVec2 cursor = ImGui::GetCursorPos();
        ImVec2 key_text_size = ImGui::CalcTextSize(key_text_begin, key_text_end, false);

        const float scale = platform_get_pixel_ratio();
        const ImVec2 padding = ImVec2(27.0f, 5.0f) * scale;
        const ImVec2 text_margin_right = ImVec2(9.0f, 0.0f) * scale;

        ImVec2 value_size = ImVec2(100.0f * scale, key_text_size.y + padding.y * 2);

        String value_text;

        if (possible_value) {
            value_text = possible_value->value;
        } else {
            const char* placeholder_text = get_custom_field_placeholder_text_by_custom_field_type(custom_field->type);

            value_text.start = (char*) placeholder_text;
            value_text.length = (u32) strlen(placeholder_text);
        }

        ImVec2 box_size = key_text_size + padding * 2 + text_margin_right + ImVec2(value_size.x, 0.0f);

        if (cursor.x + box_size.x > wrap_pos && field_index > 0) {
            ImVec2 cursor_screen = ImGui::GetCursorScreenPos();
            ImVec2 row_line_start = ImVec2(top_left_corner.x, cursor_screen.y + box_size.y);
            ImVec2 row_line_end = cursor_screen + ImVec2(0, box_size.y);

            draw_list->AddLine(row_line_start, row_line_end, border_color);

            ImVec2 previous_line_part_start = ImVec2(previous_line_end_x, cursor_screen.y);

            draw_list->AddLine(previous_line_part_start, cursor_screen, border_color);

            previous_line_end_x = row_line_end.x;

            ImGui::NewLine();
        }

        ImVec2 screen_start = ImGui::GetCursorScreenPos();
        ImVec2 separator_top = screen_start + padding + text_margin_right + ImVec2(key_text_size.x, 0);
        ImVec2 separator_bot = screen_start + padding + text_margin_right + key_text_size;
        ImVec2 value_text_start = separator_top + text_margin_right;

        const char* value_text_begin = value_text.start;
        const char* value_text_end = value_text_begin + value_text.length;

        const u32 value_color = possible_value ? key_value_text_color : placeholder_text_color;

        draw_list->AddText(screen_start + padding, key_value_text_color, key_text_begin, key_text_end);
        draw_list->AddLine(separator_top, separator_bot, border_color);
        draw_list->AddLine(screen_start + ImVec2(box_size.x, 0), screen_start + box_size, border_color);
        draw_list->AddText(font_regular, font_regular->FontSize, value_text_start, value_color, value_text_begin, value_text_end);

        ImGui::Dummy(box_size);
        ImGui::SameLine();
    }

    if (drew_at_least_one_custom_field) {
        ImGui::NewLine();
    }

    // Fill background
    {
        ImVec2 bottom_right_corner = ImVec2(
                ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionWidth(),
                ImGui::GetCursorScreenPos().y
        );

        // Here we keep raw pointers instead of sizes because a resize won't occur without PrimReserve
        ImDrawVert* current_vtx_write = draw_list->_VtxWritePtr;
        ImDrawIdx* current_idx_write = draw_list->_IdxWritePtr;
        u32 current_vtx_current_idx = draw_list->_VtxCurrentIdx;

        draw_list->_VtxWritePtr = draw_list->VtxBuffer.Data + vtx_buffer_old_size;
        draw_list->_IdxWritePtr = draw_list->IdxBuffer.Data + idx_buffer_old_size;
        draw_list->_VtxCurrentIdx = vtx_background_idx;

        draw_list->PrimRect(top_left_corner, bottom_right_corner, background_color);

        draw_list->_VtxWritePtr = current_vtx_write;
        draw_list->_IdxWritePtr = current_idx_write;
        draw_list->_VtxCurrentIdx = current_vtx_current_idx;
    }

    ImGui::PopStyleVar();
    ImGui::PopFont();

    return drew_at_least_one_custom_field;
}

static void draw_task_description(float wrap_width, float alpha) {
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

void draw_task_contents() {
    // TODO temporary code, resource loading should be moved out somewhere
    if (!checkmark.width) {
        load_png_from_disk("resources/checkmark_task_complete.png", checkmark);
    }

    // TODO modifying alpha of everything is cumbersome, we could use a semi-transparent overlay
    float wrap_width = ImGui::GetColumnWidth(-1) - 50.0f * platform_get_pixel_ratio(); // Accommodate for scroll bar

    ImGuiID task_content_id = ImGui::GetID("task_content");
    ImGui::BeginChildFrame(task_content_id, ImVec2(-1, -1), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    float alpha = lerp(MAX(finished_loading_task_at, finished_loading_users_at), tick, 1.0f, 8);

    ImVec4 title_color = ImVec4(0, 0, 0, alpha);

    float wrap_pos = ImGui::GetCursorPosX() + wrap_width;
    ImGui::PushTextWrapPos(wrap_pos);
    ImGui::PushFont(font_header);
    ImGui::TextColored(title_color, "%.*s", current_task.title.length, current_task.title.start);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    draw_parent_folders(wrap_pos);

    ImGui::Separator();

    ImGui::BeginChild("task_contents", ImVec2(-1, -1));

    Custom_Status* status = find_custom_status_by_id(current_task.status_id);

    if (status) {
        draw_status_selector(status, alpha);
    } else {
        ImGui::Text("...");
    }

    if (current_task.assignees.length) {
        ImGui::SameLine();
    }

    draw_assignees();

    if (current_task.authors.length) {
        draw_authors_and_task_id();
    }

    ImGui::Separator();

    if (ImGui::Button("Open in Wrike")) {
        platform_open_in_wrike(current_task.permalink);
    }

    ImGui::Separator();

    if (current_task.inherited_custom_fields.length) {
        if (draw_custom_fields(wrap_pos)) {
            ImGui::Separator();
        }
    }

    if (current_task.description_strings > 0) {
        draw_task_description(wrap_width, alpha);
    }

    ImGui::EndChild();

    ImGui::EndChildFrame();
}

typedef void (*Id_Processor)(char* json, jsmntok_t* token, s32& id);

template <typename T>
static void token_array_to_id_list(char* json,
                                   jsmntok_t*& token,
                                   List<T>& id_list,
                                   Id_Processor id_processor) {
    assert(token->type == JSMN_ARRAY);

    if (id_list.length < token->size) {
        id_list.data = (T*) REALLOC(id_list.data, sizeof(T) * token->size);
    }

    id_list.length = 0;

    for (u32 array_index = 0, length = (u32) token->size; array_index < length; array_index++) {
        jsmntok_t* id_token = ++token;

        assert(id_token->type == JSMN_STRING);

        id_processor(json, id_token, id_list[id_list.length++]);
    }
}

void process_task_custom_field_value(Custom_Field_Value* custom_field_value, char* json, jsmntok_t*& token) {
    jsmntok_t* object_token = token++;

    assert(object_token->type == JSMN_OBJECT);

    for (u32 propety_index = 0; propety_index < object_token->size; propety_index++, token++) {
        jsmntok_t* property_token = token++;

        assert(property_token->type == JSMN_STRING);

        jsmntok_t* next_token = token;

        if (json_string_equals(json, property_token, "id")) {
            json_token_to_right_part_of_id16(json, next_token, custom_field_value->field_id);
        } else if (json_string_equals(json, property_token, "value")) {
            json_token_to_string(json, next_token, custom_field_value->value);
        } else {
            eat_json(token);
            token--;
        }
    }
}

void process_task_data(char* json, u32 data_size, jsmntok_t*& token) {
    // The task stopped existing?
    if (data_size == 0) {
        current_task = {};

        return;
    }

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

        if (IS_PROPERTY("id")) {
            json_token_to_right_part_of_id16(json, next_token, current_task.id);
        } else if (IS_PROPERTY("title")) {
            TOKEN_TO_STRING(current_task.title);
        } else if (IS_PROPERTY("description")) {
            TOKEN_TO_STRING(description);
        } else if (IS_PROPERTY("permalink")) {
            TOKEN_TO_STRING(current_task.permalink);
        } else if (IS_PROPERTY("customStatusId")) {
            json_token_to_right_part_of_id16(json, next_token, current_task.status_id);
        } else if (IS_PROPERTY("responsibleIds")) {
            token_array_to_id_list(json, token, current_task.assignees, json_token_to_id8);
        } else if (IS_PROPERTY("authorIds")) {
            token_array_to_id_list(json, token, current_task.authors, json_token_to_id8);
        } else if (IS_PROPERTY("parentIds")) {
            token_array_to_id_list(json, token, current_task.parents, json_token_to_right_part_of_id16);
        } else if (IS_PROPERTY("inheritedCustomColumnIds")) {
            token_array_to_id_list(json, token, current_task.inherited_custom_fields, json_token_to_right_part_of_id16);
        } else if (IS_PROPERTY("customFields")) {
            assert(next_token->type == JSMN_ARRAY);

            token++;

            if (current_task.custom_field_values.length < next_token->size) {
                current_task.custom_field_values.data = (Custom_Field_Value*) REALLOC(
                        current_task.custom_field_values.data,
                        sizeof(Custom_Field_Value) * next_token->size
                );
            }

            current_task.custom_field_values.length = 0;

            for (u32 field_index = 0; field_index < next_token->size; field_index++) {
                Custom_Field_Value* value = &current_task.custom_field_values[current_task.custom_field_values.length++];

                process_task_custom_field_value(value, json, token);
            }

            token--;
        } else {
            eat_json(token);
            token--;
        }
    }

#undef IS_PROPERTY
#undef TOKEN_TO_STRING

    if (current_task.description) {
        FREE(current_task.description);
    }

    current_task.description_strings = 0;

    // We copy to strip comments from description
    // In fact we could just do that in the original string, since stripping comments
    //  never adds characters, but removes them, but this is 'cleaner'
    String temporary_description_string;

    {
        char* temporary_description = (char*) talloc(description.length);

        temporary_description_string.start = temporary_description;
        temporary_description_string.length = description.length;

        memcpy(temporary_description, description.start, description.length);
    }

    destructively_strip_html_comments(temporary_description_string);

    parse_rich_text(temporary_description_string, current_task.description, current_task.description_strings);

    u32 total_text_length = 0;

    for (u32 index = 0; index < current_task.description_strings; index++) {
        total_text_length += current_task.description[index].text.length;
    }

    String& description_text = current_task.description_text;
    description_text.start = (char*) REALLOC(description_text.start, total_text_length);
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

    printf("Parsed description with a total length of %i\n", total_text_length);
}