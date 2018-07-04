#include "task_view.h"
#include "common.h"
#include "main.h"
#include "renderer.h"
#include "rich_text.h"
#include "render_rich_text.h"
#include "platform.h"
#include "users.h"
#include "workflows.h"
#include "accounts.h"
#include "ui.h"

#include <imgui.h>
#include <cstdlib>
#include <html_entities.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <jsmn.h>
#include <cstdint>
#include <cctype>

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

struct Horizontal_Layout {
    ImVec2 top_left;
    ImVec2 cursor;
    float row_height;
    float scale;
};

struct Wrapping_Horizontal_Layout : Horizontal_Layout {
    float wrap_width;
    bool has_drawn_at_least_one_element;
};

static Memory_Image checkmark{};
static List<User*> filtered_users{};

static const float status_picker_row_height = 50.0f;
static const float assignee_avatar_side = 32.0f;

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
        rgb.r = rgb.g = rgb.b = hsl.l;
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

static char* string_contains_substring_ignore_case(String string, char* query_lowercase) {
    static const u32 buffer_length = 512;
    static char string_buffer[buffer_length];

    assert(string.length < buffer_length);

    for (u32 index = 0; index < string.length; index++) {
        string_buffer[index] = (char) tolower(string.start[index]);
    }

    string_buffer[string.length] = 0;

    return string_in_substring(string_buffer, query_lowercase, string.length);
}

static Horizontal_Layout horizontal_layout(ImVec2 top_left, float row_height) {
    Horizontal_Layout layout;
    layout.scale = platform_get_pixel_ratio();
    layout.cursor = top_left;
    layout.top_left = top_left;
    layout.row_height = row_height;

    return layout;
}

static Wrapping_Horizontal_Layout wrapping_horizontal_layout(ImVec2 top_left, float row_height, float wrap_width) {
    Wrapping_Horizontal_Layout layout;
    layout.scale = platform_get_pixel_ratio();
    layout.cursor = top_left;
    layout.top_left = top_left;
    layout.row_height = row_height;
    layout.wrap_width = wrap_width;
    layout.has_drawn_at_least_one_element = false;

    return layout;
}

static bool layout_check_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size) {
    bool would_like_to_wrap = layout.cursor.x + item_size.x > layout.top_left.x + layout.wrap_width;

    return layout.has_drawn_at_least_one_element && would_like_to_wrap;
}

static void layout_wrap(Wrapping_Horizontal_Layout& layout) {
    layout.cursor.x = layout.top_left.x;
    layout.cursor.y += layout.row_height;
}

static bool layout_check_and_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size) {
    if (layout_check_wrap(layout, item_size)) {
        layout_wrap(layout);

        return true;
    }

    return false;
}

static void layout_advance(Horizontal_Layout& layout, float value) {
    layout.cursor.x += value;
}

static ImVec2 layout_center_vertically(Horizontal_Layout& layout, float known_height) {
    return { layout.cursor.x, layout.cursor.y + layout.row_height / 2.0f - known_height / 2.0f };
}

static void update_user_search(char* query) {
    u64 start_time = platform_get_app_time_precise();

    if (!filtered_users.data) {
        filtered_users.data = (User**) MALLOC(sizeof(User*) * users.length);
    }

    filtered_users.length = 0;

    u32 query_length = (u32) strlen(query);
    bool add_all = query_length == 0;

    char* query_lowercase = (char*) talloc(query_length + 1);

    // Query to lowercase
    {
        for (u32 index = 0; index < query_length; index++) {
            query_lowercase[index] = (char) tolower(query[index]);
        }

        query_lowercase[query_length] = 0;
    }

    for (User* it = users.data; it != users.data + users.length; it++) {
        if (add_all) {
            filtered_users[filtered_users.length++] = it;

            continue;
        }

        char* first_name_contains_query = string_contains_substring_ignore_case(it->first_name, query_lowercase);
        char* last_name_contains_query = string_contains_substring_ignore_case(it->last_name, query_lowercase);

        if (first_name_contains_query || last_name_contains_query) {
            filtered_users[filtered_users.length++] = it;
        }
    }

    printf("Took %f ms to filter %i elements out of %i\n", platform_get_delta_time_ms(start_time), filtered_users.length, users.length);
}

static bool draw_status_picker_dropdown_status_selection_button(ImDrawList* draw_list, Custom_Status* status, bool is_current, ImVec2 size, float scale) {
    static const u32 hover_color = 0x11000000;
    static const u32 active_color = hover_color * 2;
    static const u32 current_color = color_link;

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImVec2 bottom_right = top_left + size;

    ImGui::PushID(status);

    bool clicked = ImGui::InvisibleButton("select_status", size);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    ImGui::PopID();

    u32 background_color = 0;
    u32 text_color = is_current ? IM_COL32_WHITE : IM_COL32_BLACK;

    if (is_current) {
        background_color = current_color;
    } else if (active) {
        background_color = active_color;
    } else if (hovered) {
        background_color = hover_color;
    }

    if (background_color) {
        draw_list->AddRectFilled(top_left, bottom_right, background_color);
    }

    float spacing = 6.0f * scale;
    String name = status->name;

    float status_color_display_rounding = 2.0f * scale;
    float status_color_display_side = size.y / 2.0f;

    ImVec2 status_color_display_top_left = top_left + ImVec2(spacing, size.y / 2.0f - status_color_display_side / 2.0f);
    ImVec2 status_color_display_bottom_right = status_color_display_top_left + ImVec2(status_color_display_side, status_color_display_side);

    draw_list->AddRectFilled(status_color_display_top_left,
                             status_color_display_bottom_right,
                             status->color,
                             status_color_display_rounding);

    draw_list->AddRect(status_color_display_top_left,
                       status_color_display_bottom_right,
                       IM_COL32_WHITE,
                       status_color_display_rounding);

    ImVec2 text_size = ImGui::CalcTextSize(name.start, name.start + name.length);
    ImVec2 text_top_left = top_left + ImVec2(spacing + status_color_display_side + spacing, size.y / 2.0f - text_size.y / 2.0f);

    draw_list->AddText(text_top_left, text_color, name.start, name.start + name.length);

    return clicked;
}

static void draw_status_picker_dropdown_contents(Custom_Status* current_task_status) {
    Workflow* workflow = current_task_status->workflow;

    ImGui::Text("%.*s", workflow->name.length, workflow->name.start);

    float scale = platform_get_pixel_ratio();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 button_size{ ImGui::GetWindowContentRegionWidth(), 24.0f * scale };

    for (Custom_Status* status = workflow->statuses.data; status != workflow->statuses.data + workflow->statuses.length; status++) {
        if (!status->is_hidden) {
            if (draw_status_picker_dropdown_status_selection_button(draw_list, status, status == current_task_status, button_size, scale)) {
                current_task.status_id = status->id;

                set_task_status(current_task.id, status->id);

                ImGui::CloseCurrentPopup();
            }
        }
    }
}

static void draw_status_picker_dropdown_if_open(ImVec2 status_picker_position, ImGuiID status_picker_dropdown_id, Custom_Status* status) {
    bool is_status_picker_open = ImGui::IsPopupOpen(status_picker_dropdown_id);

    ImVec2 status_picker_size = ImVec2(300, 300) * platform_get_pixel_ratio();

    ImGui::SetNextWindowPos(status_picker_position);
    ImGui::SetNextWindowSize(status_picker_size);

    if (is_status_picker_open && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        ImGui::ClosePopup(status_picker_dropdown_id);
    }

    if (ImGui::BeginPopupEx(status_picker_dropdown_id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        draw_status_picker_dropdown_contents(status);

        ImGui::EndPopup();
    }
}

static bool draw_status_picker(ImVec2 top_left, u32 status_color, String status_name, bool is_completed, float& out_width) {
    float scale = platform_get_pixel_ratio();

    u32 line_and_checkbox_color = change_color_luminance(status_color, 0.42f);
    u32 bg_color = change_color_luminance(status_color, 0.94f);
    u32 border_color = change_color_luminance(status_color, 0.89f);
    u32 arrow_color = change_color_luminance(status_color, 0.34f);

    ImGui::PushFont(font_bold);

    const char* text_begin = status_name.start;
    const char* text_end = text_begin + status_name.length;
    ImVec2 text_size = ImGui::CalcTextSize(text_begin, text_end, false);
    ImVec2 checkbox_size = ImVec2(18, 18) * scale;

    const float left_line_width = 10 * scale;
    const float padding = 21 * scale;

    float checkbox_offset_x = left_line_width + padding;
    float text_offset_x = checkbox_offset_x + checkbox_size.x + 8.0f * scale;

    ImVec2 start = top_left;
    ImVec2 size = ImVec2(text_size.x + text_offset_x + padding * 2.0f - left_line_width, status_picker_row_height * scale);
    ImVec2 checkbox_start = start + ImVec2(checkbox_offset_x, size.y / 2 - checkbox_size.y / 2);
    ImVec2 text_start = start + ImVec2(text_offset_x, size.y / 2 - text_size.y / 2);
    ImVec2 arrow_position = text_start + ImVec2(text_size.x + 4 * scale, 0) + ImVec2(0, text_size.y / 2.0f);

    out_width = size.x;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    Button_State button_state = button("status_selector", top_left, size);

    if (button_state.clipped) {
        ImGui::PopFont();

        return button_state.pressed;
    }

    draw_list->AddRectFilled(start, start + size, bg_color);
    draw_list->AddRectFilled(start, start + ImVec2(left_line_width, size.y), line_and_checkbox_color);
    draw_list->AddLine(start + ImVec2(left_line_width, 0), start + ImVec2(size.x, 0), border_color);
    draw_list->AddLine(start + ImVec2(left_line_width, size.y), start + size, border_color);
    draw_list->AddLine(start + ImVec2(size.x, 0), start + size, border_color);

    if (is_completed) {
        draw_list->AddImage((void*)(intptr_t) checkmark.texture_id, checkbox_start, checkbox_start + checkbox_size);
    } else {
        draw_list->AddRectFilled(checkbox_start, checkbox_start + checkbox_size, IM_COL32_WHITE);
    }

    draw_list->AddRect(checkbox_start, checkbox_start + checkbox_size, line_and_checkbox_color, 0.0f, ImDrawCornerFlags_All, 2.0f);
    draw_list->AddText(text_start, 0xff111111, text_begin, text_end);
    draw_list->AddTriangleFilled(arrow_position,
                                 arrow_position + ImVec2(5,    0) * scale,
                                 arrow_position + ImVec2(2.5f, 4) * scale,
                                 arrow_color);

    ImGui::PopFont();

    return button_state.pressed;
}

static bool draw_parent_folder_ticker(Wrapping_Horizontal_Layout& layout, Folder_Tree_Node* folder_tree_node, bool ghost_tag, bool can_wrap) {
    static const u32 border_color_default = argb_to_agbr(0xffe0e0e0);
    static const u32 border_color_hover = argb_to_agbr(0xffb3b3b3);

    char* text_begin = folder_tree_node->name.start;
    char* text_end = text_begin + folder_tree_node->name.length;

    ImVec2 offset = ImVec2(4.0f, 2.0f) * layout.scale;
    ImVec2 text_size = ImGui::CalcTextSize(text_begin, text_end);
    ImVec2 size = text_size + offset * 2;

    float rounding = 1.0f * layout.scale;

    if (can_wrap) {
        layout_check_and_wrap(layout, size);
    }

    ImVec2 start = layout.cursor;

    ImGui::PushID(folder_tree_node);
    Button_State button_state = button("folder_ticker", start, size);
    ImGui::PopID();

    layout_advance(layout, size.x + 9.0f * layout.scale);

    if (button_state.clipped) {
        return button_state.pressed;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    Folder_Color* color = folder_tree_node->color;

    u32 background_color = button_state.hovered ? color->background_hover : color->background;
    u32 text_color = color->text;
    u32 border_color = button_state.hovered ? border_color_hover : border_color_default;

    if (ghost_tag) {
        const u32 alpha = button_state.hovered ? 190 : 128;
        const u32 clear_alpha = 0x00FFFFFF;
        const u32 half_alpha_mask = alpha << 24;

        if (background_color) {
            background_color = (background_color & clear_alpha) | half_alpha_mask;
        }

        text_color = (text_color & clear_alpha) | half_alpha_mask;
        border_color = (border_color & clear_alpha) | half_alpha_mask;
    }

    draw_list->AddRectFilled(start, start + size, background_color, rounding);
    draw_list->AddRect(start, start + size, border_color, rounding);
    draw_list->AddText(start + offset, text_color, text_begin, text_end);

    return button_state.pressed;
}

static bool draw_folder_picker_folder_selection_button(ImDrawList* draw_list, String name, Folder_Color* color, ImVec2 size, float spacing) {
    static const u32 hover_color = 0x11000000;
    static const u32 active_color = hover_color * 2;

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImVec2 bottom_right = top_left + size;

    bool clicked = ImGui::InvisibleButton("select_folder", size);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (active) {
        draw_list->AddRectFilled(top_left, bottom_right, active_color);
    } else if (hovered) {
        draw_list->AddRectFilled(top_left, bottom_right, hover_color);
    }

    draw_list->AddRectFilled(top_left, top_left + ImVec2(spacing, size.y), color->background);

    ImVec2 text_size = ImGui::CalcTextSize(name.start, name.start + name.length);
    ImVec2 text_top_left = top_left + ImVec2(spacing + spacing, size.y / 2.0f - text_size.y / 2.0f);

    draw_list->AddText(text_top_left, 0xff000000, name.start, name.start + name.length);

    return clicked;
}

static void draw_folder_picker_contents(bool set_focus) {
    static const u32 search_buffer_size = 512;
    static char search_buffer[search_buffer_size];

    static List<Folder_Tree_Node*> search_result{};

    if (ImGui::InputText("##folder_picker_search", search_buffer, search_buffer_size)) {
        folder_tree_search(search_buffer, &search_result);
    }

    if (set_focus) {
        ImGui::SetScrollY(0);
        ImGui::SetKeyboardFocusHere();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 selection_button_size{ ImGui::GetContentRegionAvailWidth(), 24.0f * platform_get_pixel_ratio() };
    float padding = 4.0f * platform_get_pixel_ratio();

    // TODO code duplication. Same occurs in folder search in folder_tree.cpp.
    // TODO Maybe we could just fill search result with all data in case of an empty query?
    if (search_result.data && strlen(search_buffer)) {
        ImGuiListClipper clipper(search_result.length);

        while (clipper.Step()) {
            for (Folder_Tree_Node** it = search_result.data + clipper.DisplayStart; it != search_result.data + clipper.DisplayEnd; it++) {
                Folder_Tree_Node* node = *it;

                ImGui::PushID(node);

                if (draw_folder_picker_folder_selection_button(draw_list, node->name, node->color, selection_button_size, padding)) {
                    add_item_to_list(current_task.parents, node->id);
                    add_parent_folder(current_task.id, node->id);

                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopID();
            }
        }

        clipper.End();
    } else {
        for (Suggested_Folder* it = suggested_folders.data; it != suggested_folders.data + suggested_folders.length; it++) {
            ImGui::PushID(it);

            if (draw_folder_picker_folder_selection_button(draw_list, it->name, it->color, selection_button_size, padding)) {
                add_item_to_list(current_task.parents, it->id);
                add_parent_folder(current_task.id, it->id);

                ImGui::CloseCurrentPopup();
            }

            ImGui::PopID();
        }

        ImGuiListClipper clipper(total_nodes);

        while (clipper.Step()) {
            for (Folder_Tree_Node* it = all_nodes + clipper.DisplayStart; it != all_nodes + clipper.DisplayEnd; it++) {
                ImGui::PushID(it);

                if (draw_folder_picker_folder_selection_button(draw_list, it->name, it->color, selection_button_size, padding)) {
                    add_item_to_list(current_task.parents, it->id);
                    add_parent_folder(current_task.id, it->id);

                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopID();
            }
        }

        clipper.End();
    }
}

static void draw_folder_picker_button(Wrapping_Horizontal_Layout& layout) {
    static const ImGuiID folder_picker_id = ImGui::GetID("folder_picker");

    bool is_folder_picker_open = ImGui::IsPopupOpen(folder_picker_id);

    ImVec2 folder_picker_position = layout.cursor;
    ImVec2 folder_picker_size = ImVec2(300, 300) * platform_get_pixel_ratio();

    bool should_set_focus = false;

    if (!is_folder_picker_open) {
        float button_side_px = 9.0f * layout.scale;
        ImVec2 button_size{ button_side_px, button_side_px };

        layout_check_and_wrap(layout, button_size);

        ImVec2 top_left = layout.cursor + ImVec2(0, 5.0f * layout.scale);

        Button_State button_state = button("add_folder", top_left, button_size);

        if (button_state.pressed) {
            ImGui::OpenPopupEx(folder_picker_id);

            should_set_focus = true;
        }

        if (!button_state.clipped) {
            const float icon_half_width = button_side_px / 2.0f;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 center = top_left + button_size / 2.0f;

            const u32 color = button_state.hovered ? color_link : 0xffc0c0c2;

            draw_list->AddLine(center - ImVec2(0, icon_half_width), center + ImVec2(0, icon_half_width), color, 4.0f);
            draw_list->AddLine(center - ImVec2(icon_half_width, 0), center + ImVec2(icon_half_width, 0), color, 4.0f);
        }
    }

    ImGui::SetNextWindowPos(folder_picker_position);
    ImGui::SetNextWindowSize(folder_picker_size);

    if (is_folder_picker_open) {
        if (ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
            ImGui::ClosePopup(folder_picker_id);
        }
    }

    if (ImGui::BeginPopupEx(folder_picker_id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        draw_folder_picker_contents(should_set_focus);

        ImGui::EndPopup();
    }
}

static void draw_parent_folders(Wrapping_Horizontal_Layout& layout, List<Folder_Id> folders, bool ghost_tags) {
    for (u32 parent_index = 0; parent_index < folders.length; parent_index++) {
        Folder_Id folder_id = folders[parent_index];
        Folder_Tree_Node* folder_tree_node = find_folder_tree_node_by_id(folder_id);

        if (folder_tree_node) {
            if (draw_parent_folder_ticker(layout, folder_tree_node, ghost_tags, folder_id != layout.has_drawn_at_least_one_element)) {
                select_folder_node_and_request_contents_if_necessary(folder_tree_node);
            }

            layout.has_drawn_at_least_one_element = true;
        }
    }
}

static float draw_parent_folders(ImVec2 top_left, float wrap_width) {
    float row_height = 24.0f * platform_get_pixel_ratio();
    Wrapping_Horizontal_Layout layout = wrapping_horizontal_layout(top_left, row_height, wrap_width);

    draw_parent_folders(layout, current_task.super_parents, true);
    draw_parent_folders(layout, current_task.parents, false);

    draw_folder_picker_button(layout);

    return (layout.cursor.y + layout.row_height) - layout.top_left.y;
}

static bool draw_contact_picker_assignee_selection_button(ImDrawList* draw_list, User* user, ImVec2 size, float spacing) {
    static const u32 hover_color = 0x11000000;
    static const u32 active_color = hover_color * 2;

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImVec2 bottom_right = top_left + size;

    ImGui::PushID(user);

    bool clicked = ImGui::InvisibleButton("select_assignee", size);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (active) {
        draw_list->AddRectFilled(top_left, bottom_right, active_color);
    } else if (hovered) {
        draw_list->AddRectFilled(top_left, bottom_right, hover_color);
    }

    draw_circular_user_avatar(draw_list, user, top_left, size.y);

    s32 buffer_length = snprintf(NULL, 0, "%.*s %.*s", user->first_name.length, user->first_name.start, user->last_name.length, user->last_name.start);

    s8* name_start = (s8*) talloc((u32) buffer_length + 1);
    s8* name_end = name_start + buffer_length;

    snprintf(name_start, buffer_length + 1, "%.*s %.*s", user->first_name.length, user->first_name.start, user->last_name.length, user->last_name.start);

    ImVec2 text_size = ImGui::CalcTextSize(name_start, name_end);
    ImVec2 text_top_left = top_left +
                           ImVec2(size.y, 0.0f) +
                           ImVec2(spacing, 0.0f) +
                           ImVec2(0.0f, size.y / 2.0f - text_size.y / 2.0f);

    draw_list->AddText(text_top_left, 0xff000000, name_start, name_start + strlen(name_start));

    ImGui::PopID();

    return clicked;
}

static bool draw_add_assignee_button(ImVec2 top_left, float button_side_px, float scale) {
    ImVec2 button_size{ button_side_px, button_side_px };
    ImVec2 text_size;

    bool no_assignees = !current_task.assignees.length;
    const char* label_start = "Add assignee";
    const char* label_end = label_start + strlen(label_start);

    ImGui::PushFont(font_large);

    if (no_assignees) {
        text_size = ImGui::CalcTextSize(label_start, label_end);
        button_size.x += text_size.x;
    }

    Button_State button_state = button("add_assignee", top_left, button_size);

    if (!button_state.clipped) {
        const float icon_half_width = button_side_px / 4.0f;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 center = top_left + ImVec2(button_side_px, button_side_px) / 2.0f;

        const u32 color = button_state.hovered ? color_link : color_black_text_on_white;

        draw_list->AddLine(center - ImVec2(0, icon_half_width), center + ImVec2(0, icon_half_width), color, 1.5f);
        draw_list->AddLine(center - ImVec2(icon_half_width, 0), center + ImVec2(icon_half_width, 0), color, 1.5f);

        if (no_assignees) {
            ImVec2 text_top_left = top_left + ImVec2(button_side_px, 0) + ImVec2(0, button_size.y / 2.0f - text_size.y / 2.0f);

            draw_list->AddText(text_top_left, color, label_start, label_end);
        }
    }

    ImGui::PopFont();

    return button_state.pressed;
}

static void draw_add_assignee_button_and_contact_picker(Horizontal_Layout& layout) {
    const float button_side_px = assignee_avatar_side * platform_get_pixel_ratio();
    const ImVec2 contact_picker_size = ImVec2(300.0f, 330.0f) * platform_get_pixel_ratio();

    ImGuiID contact_picker_id = ImGui::GetID("contact_picker");

    bool should_open_contact_picker = false;
    bool is_contact_picker_open = ImGui::IsPopupOpen(contact_picker_id);

    ImVec2 top_left = layout_center_vertically(layout, button_side_px);

    if (!is_contact_picker_open) {
        should_open_contact_picker = draw_add_assignee_button(top_left, button_side_px, layout.scale);
    }

    if (should_open_contact_picker) {
        ImGui::OpenPopupEx(contact_picker_id);
    }

    ImGui::SetNextWindowPos(top_left);
    ImGui::SetNextWindowSize(contact_picker_size);

    if (is_contact_picker_open && ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
        ImGui::ClosePopup(contact_picker_id);
    }

    if (ImGui::BeginPopupEx(contact_picker_id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        static const u32 placeholder_color = 0x80000000;
        static const u32 search_buffer_size = 512;
        static char search_buffer[search_buffer_size];

        if (!filtered_users.data) {
            update_user_search((char*) "");
        }

        ImVec2 placeholder_text_position = ImGui::GetCursorScreenPos() + ImGui::GetStyle().FramePadding;

        if (should_open_contact_picker) {
            ImGui::SetScrollY(0);
            ImGui::SetKeyboardFocusHere();
        }

        if (ImGui::InputText("##contact_search", search_buffer, search_buffer_size)) {
            update_user_search(search_buffer);
        }

        if (strlen(search_buffer) == 0) {
            const char* text = "Add by name or e-mail";

            ImGui::GetWindowDrawList()->AddText(placeholder_text_position, placeholder_color, text, text + strlen(text));
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        float button_width = ImGui::GetContentRegionAvailWidth();
        float spacing = ImGui::GetStyle().FramePadding.x;

        if (strlen(search_buffer) == 0) {
            for (User* it = suggested_users.data; it != suggested_users.data + suggested_users.length; it++) {
                if (draw_contact_picker_assignee_selection_button(draw_list, it, {button_width, button_side_px}, spacing)) {
                    add_item_to_list(current_task.assignees, it->id);
                    add_assignee_to_task(current_task.id, it->id);

                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGuiListClipper clipper(filtered_users.length);

        while (clipper.Step()) {
            for (User** it = filtered_users.data + clipper.DisplayStart; it != filtered_users.data + clipper.DisplayEnd; it++) {
                if (draw_contact_picker_assignee_selection_button(draw_list, *it, { button_width, button_side_px }, spacing)) {
                    add_item_to_list(current_task.assignees, (*it)->id);
                    add_assignee_to_task(current_task.id, (*it)->id);

                    ImGui::CloseCurrentPopup();
                }
            }
        }

        clipper.End();

        ImGui::EndPopup();
    }
}

struct Assignee {
    User* user;
    String name;
    float name_width;
};

static bool draw_unassign_button(ImVec2 top_left, float button_side) {
    ImVec2 size { button_side, button_side };

    Button_State button_state = button("unassign_user", top_left, size);

    if (button_state.clipped) return button_state.pressed;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 center = top_left + size / 2.0f;

    draw_list->AddCircleFilled(center, button_side / 2.0f, 0xff000000, 16);

    draw_list->AddLine(center - size / 4.0f,
                       center + size / 4.0f,
                       0xffffffff);

    draw_list->AddLine(center + ImVec2(button_side, -button_side) / 4.0f,
                       center + ImVec2(-button_side, button_side) / 4.0f,
                       0xffffffff);

    return button_state.pressed;
}

static bool draw_assignee(ImVec2 top_left, Assignee* assignee, float avatar_side_px) {
    ImGui::PushID(assignee->user);

    button("assign_user", top_left, { avatar_side_px, avatar_side_px });
    bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);

    ImGui::SetItemAllowOverlap();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_circular_user_avatar(draw_list, assignee->user, top_left, avatar_side_px);

    bool is_unassign_clicked = false;

    if (is_hovered) {
        const float unassign_button_side = 12.0f * platform_get_pixel_ratio();

        ImVec2 unassign_top_left = top_left +
                                   ImVec2(avatar_side_px, 0.0f) -
                                   ImVec2(unassign_button_side * 0.8f, 0.0f);

        is_unassign_clicked = draw_unassign_button(unassign_top_left, unassign_button_side);
    }

    ImGui::PopID();

    return is_unassign_clicked;
}

static bool draw_assignee_with_name(ImVec2 top_left, Assignee* assignee, float avatar_side_px, float margin_left, float& out_width) {
    float total_width = avatar_side_px + margin_left + assignee->name_width;

    ImGui::PushID(assignee->user);

    button("assignee", top_left, { total_width, avatar_side_px });
    bool is_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);

    ImGui::SetItemAllowOverlap();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 bottom_right = top_left + ImVec2(total_width, avatar_side_px);
    ImVec2 text_top_left = top_left + ImVec2(
            avatar_side_px + margin_left,
            avatar_side_px / 2.0f - ImGui::GetFontSize() / 2.0f
    );

    if (is_hovered) {
        float capsule_radius = avatar_side_px / 2.0f;
        ImVec2 capsule_left_center = top_left + ImVec2(capsule_radius, capsule_radius);
        ImVec2 capsule_right_center = bottom_right - ImVec2(capsule_radius, capsule_radius);

        /*
         * PI/2  ->  PI/2
         *   --------
         *  (lc    rc)
         *   --------
         * 3PI/2 <- -PI/2 because of the nature of PathArcTo
         */

        draw_list->PathClear();
        draw_list->PathArcTo(capsule_left_center,  capsule_radius, IM_PI * 3.0f / 2.0f, IM_PI / 2.0f, 64);
        draw_list->PathArcTo(capsule_right_center, capsule_radius, IM_PI / 2.0f,       -IM_PI / 2.0f, 64);
        draw_list->PathFillConvex(0x11000000);
    }

    draw_circular_user_avatar(draw_list, assignee->user, top_left, avatar_side_px);

    draw_list->AddText(text_top_left, color_black_text_on_white, assignee->name.start, assignee->name.start + assignee->name.length);

    bool is_unassign_clicked = false;

    if (is_hovered) {
        const float unassign_button_side = 12.0f * platform_get_pixel_ratio();

        ImVec2 unassign_top_left = top_left +
                                   ImVec2(total_width, 0.0f) -
                                   ImVec2(unassign_button_side * 0.8f, 0.0f);

        is_unassign_clicked = draw_unassign_button(unassign_top_left, unassign_button_side);
    }

    ImGui::PopID();

    out_width = total_width;

    return is_unassign_clicked;
}

static bool draw_more_assignees_button(ImVec2 top_left, u32 how_many, float side_px) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const u32 color = 0xfff5f5f5;

    char* text_start = NULL;
    char* text_end = NULL;

    tprintf("+%i", &text_start, &text_end, how_many);

    ImGui::PushFont(font_large);
    ImVec2 text_size = ImGui::CalcTextSize(text_start, text_end);
    ImVec2 button_size{ side_px, side_px };

    Button_State state = button("show_more_assignees", top_left, button_size);

    if (state.clipped) {
        ImGui::PopFont();

        return state.pressed;
    }

    draw_list->AddCircleFilled(top_left + ImVec2(side_px, side_px) / 2.0f, side_px / 2.0f, color);
    draw_list->AddText(top_left + button_size / 2.0f - text_size / 2.0f, 0xe6000000, text_start, text_end);

    if (state.hovered) {
        draw_list->AddCircle(top_left + ImVec2(side_px, side_px) / 2.0f, side_px / 2.0f, color_link);
    }

    ImGui::PopFont();

    return state.pressed;
}

static void draw_assignees(Horizontal_Layout& layout, float wrap_pos) {
    static User_Id assignee_to_remove_next_frame = 0;

    const float avatar_side_px = assignee_avatar_side * platform_get_pixel_ratio();
    const int assignees_to_consider_for_name_plus_avatar_display = 2;

    /*
     * Render at minimum 2 avatar + name as possible, the rest are +X
     * If 2 avatars with names do not fit, render how many avatars fit,
     * the rest are +X
     */

    if (assignee_to_remove_next_frame) {
        remove_item_from_list_keep_order(current_task.assignees, assignee_to_remove_next_frame);
        assignee_to_remove_next_frame = 0;

        // Bail out early so behavior matched the case when there were no assignees in the first place
        if (!current_task.assignees.length) {
            return;
        }
    }

    List<Assignee> assignees;
    assignees.data = (Assignee*) talloc(sizeof(Assignee) * current_task.assignees.length);
    assignees.length = 0;

    float name_plus_avatar_total_width = 0.0f;

    u32 name_plus_avatar_assignees = 0;

    float name_margin_left = 8.0f * layout.scale;
    float element_margin_left = 12.0f * layout.scale;
    float add_assignee_button_width = avatar_side_px + name_margin_left;

    name_plus_avatar_total_width += add_assignee_button_width;

    for (u32 index = 0; index < current_task.assignees.length; index++) {
        User_Id user_id = current_task.assignees[index];
        User* user = find_user_by_id(user_id, hash_id(user_id)); // TODO hash cache

        if (!user) {
            continue;
        }

        Assignee new_assignee;
        new_assignee.user = user;
        new_assignee.name.length = user->first_name.length + 1 + 1 + 1; // Space, one letter, dot
        new_assignee.name.start = (char*) talloc(new_assignee.name.length);

        sprintf(new_assignee.name.start, "%.*s %.1s.", user->first_name.length, user->first_name.start, user->last_name.start);

        new_assignee.name_width = ImGui::CalcTextSize(new_assignee.name.start, new_assignee.name.start + new_assignee.name.length, false).x + name_margin_left;

        assignees.data[assignees.length++] = new_assignee;

        if (index < assignees_to_consider_for_name_plus_avatar_display) {
            name_plus_avatar_total_width += avatar_side_px + name_margin_left + new_assignee.name_width + element_margin_left;
            name_plus_avatar_assignees++;
        }
    }

    u32 remaining_after_name_plus_avatar_display = assignees.length - name_plus_avatar_assignees;

    // Then add a +X avatar-sized circle where X is remaining
    if (remaining_after_name_plus_avatar_display) {
        name_plus_avatar_total_width += avatar_side_px + name_margin_left;
    }

    float window_start_x = layout.cursor.x;

    if (window_start_x + name_plus_avatar_total_width > wrap_pos) {
        // If we don't fit then render everyone who fits as avatars
        float available_space = MAX(0, wrap_pos - window_start_x - add_assignee_button_width);

        u32 can_fit_avatars = MAX(1, (u32) floorf(available_space / (avatar_side_px + element_margin_left)));
        u32 fits_avatars = MIN(can_fit_avatars, assignees.length);

        if (available_space < 0) {
            fits_avatars = 1;
        }

        for (u32 assignee_index = 0; assignee_index < fits_avatars; assignee_index++) {
            layout_advance(layout, element_margin_left);

            bool is_last = assignee_index == (fits_avatars - 1);
            Assignee* assignee = &assignees[assignee_index];
            ImVec2 assignee_element_top_left = layout_center_vertically(layout, avatar_side_px);

            if (!is_last) {
                if (draw_assignee(assignee_element_top_left, assignee, avatar_side_px)) {
                    assignee_to_remove_next_frame = assignee->user->id;
                    remove_assignee_from_task(current_task.id, assignee->user->id);
                }
            } else {
                u32 remaining_after_avatars = assignees.length - fits_avatars;

                if (!remaining_after_avatars) {
                    if (draw_assignee(assignee_element_top_left, assignee, avatar_side_px)) {
                        assignee_to_remove_next_frame = assignee->user->id;
                        remove_assignee_from_task(current_task.id, assignee->user->id);
                    }
                } else {
                    draw_more_assignees_button(assignee_element_top_left, remaining_after_avatars + 1, avatar_side_px);
                }
            }

            layout_advance(layout, avatar_side_px);
        }
    } else {
        for (u32 assignee_index = 0; assignee_index < name_plus_avatar_assignees; assignee_index++) {
            layout_advance(layout, element_margin_left);

            Assignee* assignee = &assignees[assignee_index];

            float assignee_with_name_width = 0.0f;
            ImVec2 assignee_element_top_left = layout_center_vertically(layout, avatar_side_px);

            if (draw_assignee_with_name(assignee_element_top_left, assignee, avatar_side_px, name_margin_left, assignee_with_name_width)) {
                assignee_to_remove_next_frame = assignee->user->id;
                remove_assignee_from_task(current_task.id, assignee->user->id);
            }

            layout_advance(layout, assignee_with_name_width);
        }

        if (remaining_after_name_plus_avatar_display) {
            layout_advance(layout, element_margin_left);

            ImVec2 more_button_top_left = layout_center_vertically(layout, avatar_side_px);

            draw_more_assignees_button(more_button_top_left, remaining_after_name_plus_avatar_display, avatar_side_px);

            layout_advance(layout, avatar_side_px);
        }
    }
}

static float draw_authors_and_task_id_and_return_new_wrap_width(Horizontal_Layout& layout, float wrap_width) {
    User_Id author_id = current_task.authors[0]; // TODO currently only using the first
    User* author = find_user_by_id(author_id);

    if (!author) {
        return wrap_width;
    }

    static const u32 color_id_and_author = argb_to_agbr(0xff8c8c8c);

    char* start = NULL, *end = NULL;

    tprintf("#%i by %.*s %.1s", &start, &end,
            current_task.id,
            author->first_name.length, author->first_name.start,
            author->last_name.start);

    ImVec2 text_size = ImGui::CalcTextSize(start, end, false);

    float right_margin = 8.0f * layout.scale;
    float right_border_x_absolute = layout.top_left.x + wrap_width;
    float left_border_x = right_border_x_absolute - text_size.x - right_margin;
    float centered_y = layout_center_vertically(layout, text_size.y).y;

    ImVec2 text_top_left{ left_border_x, centered_y };

    ImGui::GetWindowDrawList()->AddText(text_top_left, color_id_and_author, start, end);

    return wrap_width - text_size.x - right_margin;
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

static float draw_custom_fields(ImVec2 top_left, float wrap_width) {
    static const u32 key_value_text_color = argb_to_agbr(0xff111111);
    static const u32 placeholder_text_color = argb_to_agbr(0xffaeaeae);
    static const u32 border_color = argb_to_agbr(0xffeeeeee);
    static const u32 background_color = argb_to_agbr(0xfffafafa);

    ImGui::PushFont(font_bold);

    float previous_line_end_x = top_left.x;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Save to restore them later
    // We need to keep the sizes instead of raw pointers because buffer resizes might occur
    s32 vtx_buffer_old_size = draw_list->VtxBuffer.Size;
    s32 idx_buffer_old_size = draw_list->IdxBuffer.Size;
    u32 vtx_background_idx = draw_list->_VtxCurrentIdx;

    // Reserve buffer space to fill later when we know the total size
    draw_list->PrimReserve(6, 4);
    draw_list->PrimRect({0,0}, {0,0}, 0);

    float scale = platform_get_pixel_ratio();
    const ImVec2 padding = ImVec2(27.0f, 5.0f) * scale;
    const ImVec2 text_margin_right = ImVec2(9.0f, 0.0f) * scale;

    float row_height = ImGui::GetFontSize() + padding.y * 2.0f;

    Wrapping_Horizontal_Layout layout = wrapping_horizontal_layout(top_left, row_height, wrap_width);

    for (u32 field_index = 0; field_index < current_task.inherited_custom_fields.length; field_index++) {
        Custom_Field_Id custom_field_id = current_task.inherited_custom_fields[field_index];
        Custom_Field* custom_field = find_custom_field_by_id(custom_field_id);

        if (!custom_field) {
            continue;
        }

        Custom_Field_Value* possible_value = find_custom_field_value_by_custom_field_id(custom_field_id);

        const char* key_text_begin = custom_field->title.start;
        const char* key_text_end = key_text_begin + custom_field->title.length;

        ImVec2 key_text_size = ImGui::CalcTextSize(key_text_begin, key_text_end, false);
        ImVec2 value_size = ImVec2(100.0f * layout.scale, key_text_size.y + padding.y * 2);

        String value_text;

        if (possible_value) {
            value_text = possible_value->value;
        } else {
            const char* placeholder_text = get_custom_field_placeholder_text_by_custom_field_type(custom_field->type);

            value_text.start = (char*) placeholder_text;
            value_text.length = (u32) strlen(placeholder_text);
        }

        ImVec2 box_size = key_text_size + padding * 2 + text_margin_right + ImVec2(value_size.x, 0.0f);

        if (layout_check_wrap(layout, box_size)) {
            ImVec2 pre_wrap_cursor = layout.cursor;

            ImVec2 row_line_start = ImVec2(layout.top_left.x, pre_wrap_cursor.y + box_size.y);
            ImVec2 row_line_end = pre_wrap_cursor + ImVec2(0, box_size.y);
            ImVec2 previous_line_part_start = ImVec2(previous_line_end_x, pre_wrap_cursor.y);

            draw_list->AddLine(row_line_start, row_line_end, border_color);
            draw_list->AddLine(previous_line_part_start, pre_wrap_cursor, border_color);

            previous_line_end_x = row_line_end.x;

            layout_wrap(layout);
        }

        ImVec2 cursor_screen = layout.cursor;

        layout.has_drawn_at_least_one_element = true;

        ImVec2 separator_top = cursor_screen + padding + text_margin_right + ImVec2(key_text_size.x, 0);
        ImVec2 separator_bot = cursor_screen + padding + text_margin_right + key_text_size;
        ImVec2 value_text_start = separator_top + text_margin_right;

        const char* value_text_begin = value_text.start;
        const char* value_text_end = value_text_begin + value_text.length;

        const u32 value_color = possible_value ? key_value_text_color : placeholder_text_color;

        draw_list->AddText(cursor_screen + padding, key_value_text_color, key_text_begin, key_text_end);
        draw_list->AddLine(separator_top, separator_bot, border_color);
        draw_list->AddLine(cursor_screen + ImVec2(box_size.x, 0), cursor_screen + box_size, border_color);
        draw_list->AddText(font_regular, font_regular->FontSize, value_text_start, value_color, value_text_begin, value_text_end);

        layout_advance(layout, box_size.x);
    }

    float bottom_y = layout.cursor.y;

    if (layout.has_drawn_at_least_one_element) {
        bottom_y += layout.row_height;
    }

    // Fill background
    {
        ImVec2 bottom_right_corner = ImVec2(
                layout.top_left.x + ImGui::GetWindowContentRegionWidth(),
                bottom_y
        );

        // Here we keep raw pointers instead of sizes because a resize won't occur without PrimReserve
        ImDrawVert* current_vtx_write = draw_list->_VtxWritePtr;
        ImDrawIdx* current_idx_write = draw_list->_IdxWritePtr;
        u32 current_vtx_current_idx = draw_list->_VtxCurrentIdx;

        draw_list->_VtxWritePtr = draw_list->VtxBuffer.Data + vtx_buffer_old_size;
        draw_list->_IdxWritePtr = draw_list->IdxBuffer.Data + idx_buffer_old_size;
        draw_list->_VtxCurrentIdx = vtx_background_idx;

        draw_list->PrimRect(layout.top_left, bottom_right_corner, background_color);

        draw_list->_VtxWritePtr = current_vtx_write;
        draw_list->_IdxWritePtr = current_idx_write;
        draw_list->_VtxCurrentIdx = current_vtx_current_idx;
    }

    ImGui::PopFont();

    return bottom_y - layout.top_left.y;
}

static float draw_task_description(ImVec2 top_left, float wrap_width) {
    Rich_Text_String* text_start = current_task.description;
    Rich_Text_String* text_end = text_start + current_task.description_strings - 1;

    ImVec2 cursor = top_left;

    float scale = platform_get_pixel_ratio();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    for (Rich_Text_String* paragraph_end = text_start, *paragraph_start = paragraph_end; paragraph_end <= text_end; paragraph_end++) {
        bool is_newline = paragraph_end->text.length == 0;
        bool should_flush_text = is_newline || paragraph_end == text_end;

        char* string_start = paragraph_start->text.start;
        char* string_end = paragraph_end->text.start + paragraph_end->text.length;

        if (is_newline && (string_end - string_start) == 0) {
            cursor.x = top_left.x;
            cursor.y += 24.0f * scale;

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

            float indent = paragraph_start->style.list_depth * 20.0f;

            if (indent) {
                cursor.x += indent * scale;
            }

            float text_height = add_rich_text(
                    draw_list,
                    cursor,
                    paragraph_start,
                    paragraph_end,
                    wrap_width,
                    1.0f
            );

            cursor.x = top_left.x;
            cursor.y += text_height;

            paragraph_start = paragraph_end + 1;
        }
    }

    return cursor.y - top_left.y;
}

static void draw_default_status_picker(ImVec2 top_left, float& out_width) {
    static const u32 active_status_color = argb_to_agbr(0xFF2196F3);
    static const char* active_status_name = "Active";

    String name{};
    name.start = (char*) active_status_name;
    name.length = (u32) strlen(active_status_name);

    draw_status_picker(top_left, active_status_color, name, false, out_width);
}

static void draw_task_header(float wrap_width) {
    float scale = platform_get_pixel_ratio();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImVec2 padding = ImVec2(24, 12) * scale;

    float padded_x = top_left.x + padding.x;
    float cursor_row_y = top_left.y + padding.y;

    {
        ImGui::PushFont(font_header);

        String title = current_task.title;

        ImVec2 title_top_left = ImVec2(padded_x, cursor_row_y);
        ImVec2 title_size = ImGui::CalcTextSize(title.start, title.start + title.length, false, wrap_width);

        draw_list->AddText(NULL, 0, title_top_left, color_black_text_on_white, title.start, title.start + title.length, wrap_width);

        ImGui::PopFont();

        cursor_row_y += title_size.y;
    }

    cursor_row_y += 8.0f * scale;

    {
        ImVec2 parent_folders_top_left = ImVec2(padded_x, cursor_row_y);

        float parent_folders_height = draw_parent_folders(parent_folders_top_left, wrap_width);

        cursor_row_y += parent_folders_height;
    }

    cursor_row_y += padding.y;

    ImGui::Dummy(ImVec2(0, cursor_row_y - top_left.y));
}

static void draw_status_and_assignees_row(ImVec2 top_left, float wrap_width) {
    float row_height = status_picker_row_height * platform_get_pixel_ratio();

    Horizontal_Layout layout = horizontal_layout(top_left, row_height);

    Custom_Status* status = find_custom_status_by_id(current_task.status_id);
    float picker_width = 0.0f;

    if (status) {
        bool is_completed = status->group == Status_Group_Completed;

        static const ImGuiID status_picker_dropdown_id = ImGui::GetID("status_picker");

        if (draw_status_picker(layout.cursor, status->color, status->name, is_completed, picker_width)) {
            ImGui::OpenPopupEx(status_picker_dropdown_id);
        }

        draw_status_picker_dropdown_if_open(layout.cursor + ImVec2(0, row_height), status_picker_dropdown_id, status);
    } else {
        draw_default_status_picker(layout.cursor, picker_width);
    }

    layout_advance(layout, picker_width);

    wrap_width = draw_authors_and_task_id_and_return_new_wrap_width(layout, wrap_width);

    draw_assignees(layout, layout.top_left.x + wrap_width);
    draw_add_assignee_button_and_contact_picker(layout);
}

void draw_task_contents() {
    // TODO temporary code, resource loading should be moved out somewhere
    if (!checkmark.width) {
        load_png_from_disk("resources/checkmark_task_complete.png", checkmark);
    }

    float header_wrap_width = ImGui::GetContentRegionAvailWidth() - 50.0f * platform_get_pixel_ratio(); // Accommodate for scroll bar

    ImGuiID task_content_id = ImGui::GetID("task_content");
    ImGui::BeginChildFrame(task_content_id, ImVec2(-1, -1), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    draw_task_header(header_wrap_width);

    ImGui::BeginChild("task_contents", ImVec2(-1, -1));

    float content_wrap_width = ImGui::GetContentRegionAvailWidth();

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float scale = platform_get_pixel_ratio();
    float cursor_y = top_left.y;
    float full_width = ImGui::GetContentRegionAvailWidth();
    float leftmost_x = top_left.x;

    ImVec2 status_picker_top_left = ImVec2(leftmost_x, cursor_y);

    draw_status_and_assignees_row(status_picker_top_left, content_wrap_width);

    ImVec2 pre_status_separator_left = ImVec2(leftmost_x, cursor_y);
    ImVec2 pre_status_separator_right = pre_status_separator_left + ImVec2(full_width, 0);

    ImVec2 post_status_separator_left = ImVec2(leftmost_x, cursor_y) + ImVec2(0, status_picker_row_height) * scale;
    ImVec2 post_status_separator_right = post_status_separator_left + ImVec2(full_width, 0);

    cursor_y += status_picker_row_height * scale;

    if (current_task.inherited_custom_fields.length) {
        ImVec2 custom_fields_top_left(leftmost_x, cursor_y);

        float custom_fields_total_height = draw_custom_fields(custom_fields_top_left, content_wrap_width);

        cursor_y += custom_fields_total_height;
    }

    ImVec2 post_custom_fields_left = ImVec2(leftmost_x, cursor_y);
    ImVec2 post_custom_fields_right = post_custom_fields_left + ImVec2(full_width, 0);

    const u32 separator_color = 0xffd6d6d6;

    draw_list->AddLine(pre_status_separator_left, pre_status_separator_right, separator_color);
    draw_list->AddLine(post_status_separator_left, post_status_separator_right, separator_color);
    draw_list->AddLine(post_custom_fields_left, post_custom_fields_right, separator_color);

    cursor_y += 48.0f * scale;

    if (current_task.description_strings > 0) {
        float text_padding_x = 32.0f * scale;

        ImVec2 description_top_left(top_left.x + text_padding_x, cursor_y);

        cursor_y += draw_task_description(description_top_left, content_wrap_width - text_padding_x * 2.0f);
        cursor_y += 8.0f * scale;
    }

    bool should_draw_shadow = ImGui::GetScrollY() > 0.01f;

    if (should_draw_shadow) {
        ImVec2 shadow_top_left = top_left + ImVec2(0, ImGui::GetScrollY());
        ImVec2 shadow_bottom_right = shadow_top_left + ImVec2(full_width, 4.0f * scale);

        const u32 shadow_start = 0x33000000;
        const u32 shadow_end = 0x00000000;

        draw_list->AddRectFilledMultiColor(shadow_top_left, shadow_bottom_right, shadow_start, shadow_start, shadow_end, shadow_end);
    }

    ImGui::Dummy({ 0, cursor_y - top_left.y });

    ImGui::EndChild();

    float alpha = lerp(MAX(finished_loading_task_at, finished_loading_users_at), tick, 1.0f, 8);
    ImGui::FadeInOverlay(alpha);

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
        } else if (IS_PROPERTY("superParentIds")) {
            token_array_to_id_list(json, token, current_task.super_parents, json_token_to_right_part_of_id16);
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
    //  never adds characters, only removes them, but this is 'cleaner'
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