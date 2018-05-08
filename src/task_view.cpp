#include "task_view.h"
#include "common.h"
#include "main.h"
#include "funimgui.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "platform.h"
#include "users.h"
#include "workflows.h"

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

static void add_rect_filled(ImDrawList* draw_list, const ImVec2& a, const ImVec2& b, u32 color) {
    float scale = platform_get_pixel_ratio();

    draw_list->AddRectFilled(a * scale, b * scale, color);
}

static void add_rect(ImDrawList* draw_list, const ImVec2& a, const ImVec2& b, u32 color, float thickness = 1) {
    float scale = platform_get_pixel_ratio();

    draw_list->AddRect(a * scale, b * scale, color, 0.0f, ImDrawCornerFlags_All, thickness);
}

static void add_text(ImDrawList* draw_list, const ImVec2& pos, u32 color, const char* text_begin, const char* text_end) {
    float scale = platform_get_pixel_ratio();

    draw_list->AddText(pos * scale, color, text_begin, text_end);
}

static void add_line(ImDrawList* draw_list, const ImVec2& a, const ImVec2& b, u32 color) {
    float scale = platform_get_pixel_ratio();

    draw_list->AddLine(a * scale, b * scale, color);
}

static void add_image(ImDrawList* draw_list, u32 texture_id, const ImVec2& a, const ImVec2& b) {
    float scale = platform_get_pixel_ratio();

    draw_list->AddImage((void*)(intptr_t) texture_id, a * scale, b * scale);
}

static void draw_status_selector(Custom_Status* status, float alpha) {
    float scale = platform_get_pixel_ratio();

    u32 color = change_color_luminance(status->color, 0.42f);
    u32 bg_color = change_color_luminance(status->color, 0.94f);
    u32 border_color = change_color_luminance(status->color, 0.89f);

    ImGui::PushFont(font_bold);

    const char* text_begin = status->name.start;
    const char* text_end = text_begin + status->name.length;
    ImVec2 text_size = ImGui::CalcTextSize(text_begin, text_end, false) / scale;
    ImVec2 checkbox_size = ImVec2(18, 18);

    const float left_line_width = 10;
    const float padding = 21;

    float checkbox_offset_x = left_line_width + padding;
    float text_offset_x = checkbox_offset_x + checkbox_size.x + 8.0f;

    ImVec2 start = ImGui::GetCursorScreenPos() / scale;
    ImVec2 size = ImVec2(text_size.x + text_offset_x + padding * 2.0f - left_line_width, 50.0f);
    ImVec2 checkbox_start = start + ImVec2(checkbox_offset_x, size.y / 2 - checkbox_size.y / 2);
    ImVec2 text_start = start + ImVec2(text_offset_x, size.y / 2 - text_size.y / 2);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("status_selector", size * scale);

    add_rect_filled(draw_list, start, start + size, bg_color);
    add_rect_filled(draw_list, start, start + ImVec2(left_line_width, size.y), color);
    add_line(draw_list, start + ImVec2(left_line_width, 0), start + ImVec2(size.x, 0), border_color);
    add_line(draw_list, start + ImVec2(left_line_width, size.y), start + size, border_color);
    add_line(draw_list, start + ImVec2(size.x, 0), start + size, border_color);

    if (status->group == Status_Group_Completed) {
        add_image(draw_list, checkmark.texture_id, checkbox_start, checkbox_start + checkbox_size);
    } else {
        add_rect_filled(draw_list, checkbox_start, checkbox_start + checkbox_size, IM_COL32_WHITE);
    }

    add_rect(draw_list, checkbox_start, checkbox_start + checkbox_size, color, 2.0f);
    add_text(draw_list, text_start, IM_COL32_BLACK, text_begin, text_end);

//    ImVec2 arrow_position = text_start + ImVec2(text_size.x + 4, 0);
//    ImGui::PushStyleColor(ImGuiCol_Text, color);
//    ImGui::RenderArrow(arrow_position * scale, ImGuiKey_DownArrow, 0.5f);
//    ImGui::PopStyleColor();

    ImGui::PopFont();
}

static void draw_parent_folders() {
    static const ImVec4 font_color_v4 = ImGui::ColorConvertU32ToFloat4(argb_to_agbr(0xff555555));
    static const u32 border_color = argb_to_agbr(0xffe0e0e0);

    for (u32 parent_index = 0; parent_index < current_task.parents.length; parent_index++) {
        bool is_last = parent_index == (current_task.parents.length - 1);

        Folder_Id folder_id = current_task.parents[parent_index];
        Folder_Tree_Node* folder_tree_node = find_folder_tree_node_by_id(folder_id);

        if (folder_tree_node) {
            ImGui::TextColored(font_color_v4, "%.*s", folder_tree_node->name.length, folder_tree_node->name.start);

            ImVec2 offset = ImVec2(4.0f, 2.0f) * platform_get_pixel_ratio();

            ImGui::GetWindowDrawList()->AddRect(
                    ImGui::GetItemRectMin() - offset,
                    ImGui::GetItemRectMax() + offset,
                    border_color,
                    2.0f
            );

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
        ImGui::Text("#%lu by %.*s %.1s",
                    current_task.id,
                    author->first_name.length, author->first_name.start,
                    author->last_name);
        ImGui::PopStyleColor();
    }
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
    float wrap_width = ImGui::GetColumnWidth(-1) - 30.0f; // Accommodate for scroll bar

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

    draw_parent_folders();

    ImGui::Separator();

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

    ImGui::BeginChild("task_description_and_comments", ImVec2(-1, -1));
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

    printf("Parsed description with a total length of %lu\n", total_text_length);
}