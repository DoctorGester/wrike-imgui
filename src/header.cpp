#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cstdint>
#include "platform.h"
#include "renderer.h"
#include "users.h"
#include "ui.h"

static Memory_Image logo{};

void set_header_logo(Memory_Image new_logo) {
    logo = new_logo;
}

static bool draw_header_button(const char* text, ImVec2 top_left, float header_height, ImVec2& out_size) {
    float scale = platform_get_pixel_ratio();
    float padding_x = 16.0f * scale;

    ImVec2 text_size = ImGui::CalcTextSize(text, text + strlen(text));
    ImVec2 button_size = text_size + ImVec2(padding_x * 2, header_height);
    ImVec2 text_top_left = top_left + ImVec2(padding_x, header_height / 2.0f - text_size.y / 2.0f);

    out_size = button_size;

    Button_State button_state = button((void*) text, top_left, button_size);

    if (button_state.clipped) {
        return button_state.pressed;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddText(text_top_left, 0xccffffff, text, text + strlen(text));

    if (button_state.hovered || button_state.held) {
        u32 top_line_color = button_state.held ? IM_COL32_WHITE : 0x99ffffff;

        draw_list->AddRectFilled(top_left, top_left + ImVec2(button_size.x, 4.0f * scale), top_line_color);
    }

    return button_state.pressed;
}

static bool draw_add_new_entity_button(const ImVec2 top_left, const ImVec2& button_size) {
    const u32 button_color = argb_to_agbr(0xff34bb12);

    float scale = platform_get_pixel_ratio();

    Button_State button_state = button("add_new_entity_button", top_left, button_size);

    if (button_state.clipped) {
        return button_state.pressed;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 button_center = top_left + button_size / 2.0f;

    float line_half_length = 6.0f * scale;

    draw_list->AddCircleFilled(button_center, button_size.x / 2.0f, button_color, 48);
    draw_list->AddLine(
            button_center - ImVec2(line_half_length, 0),
            button_center + ImVec2(line_half_length, 0),
            IM_COL32_WHITE,
            4.0f
    );

    draw_list->AddLine(
            button_center - ImVec2(0, line_half_length),
            button_center + ImVec2(0, line_half_length),
            IM_COL32_WHITE,
            4.0f
    );

    return button_state.pressed;
}

static bool draw_side_menu_toggle_button(const ImVec2 top_left, const ImVec2& button_size) {
    float scale = platform_get_pixel_ratio();

    const float bar_height = button_size.y / 5.0f;
    const u32 bar_color = argb_to_agbr(0xffa9b3bd);
    const u32 bar_color_hover = argb_to_agbr(0xffd4d9de);
    const float middle_height = button_size.y / 2.0f;
    const float rounding = 2.0f * scale; // TODO this doesn't properly round on 1920x1080

    ImVec2 right_side = top_left + ImVec2(button_size.x, bar_height);

    Button_State button_state = button("side_menu_toggle_button", top_left, button_size);

    if (button_state.clipped) {
        return button_state.pressed;
    }

    const u32 color = button_state.hovered ? bar_color_hover : bar_color;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(top_left,
                             right_side,
                             color, rounding);

    draw_list->AddRectFilled(top_left + ImVec2(0.0f, middle_height - bar_height / 2.0f),
                             right_side + ImVec2(0.0f, middle_height - bar_height / 2.0f),
                             color, rounding);

    draw_list->AddRectFilled(top_left + ImVec2(0.0f, button_size.y - bar_height),
                             right_side + ImVec2(0.0f, button_size.y - bar_height),
                             color, rounding);

    return button_state.pressed;
}

static void draw_profile_widget(User* user, float header_height) {
    float scale = platform_get_pixel_ratio();
    float total_width = ImGui::GetContentRegionAvailWidth();
    float avatar_side_px = 32.0f * scale;
    float padding_right = 8.0f * scale;

    ImVec2 top_right(total_width, 0);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImGui::PushFont(font_large);

    String name = user->first_name;
    ImVec2 name_size = ImGui::CalcTextSize(name.start, name.start + name.length);
    ImVec2 name_top_left = top_right - ImVec2(padding_right + name_size.x, 0) + ImVec2(0, header_height / 2.0f - name_size.y / 2.0f);
    draw_list->AddText(name_top_left, 0xccffffff, name.start, name.start + name.length);

    ImGui::PopFont();

    ImVec2 avatar_top_left = ImVec2(name_top_left.x, 0) -
                             ImVec2(avatar_side_px, 0) -
                             ImVec2(padding_right, 0) +
                             ImVec2(0, header_height / 2.0f - avatar_side_px / 2.0f);

    draw_circular_user_avatar(draw_list, this_user, avatar_top_left, avatar_side_px);
}

void draw_header(bool draw_side_menu_this_frame, bool& draw_side_menu, float folder_tree_column_width) {
    float scale = platform_get_pixel_ratio();
    float header_height = 56.0f * scale;
    float effective_folder_tree_column_width = (draw_side_menu_this_frame ? folder_tree_column_width : 40.0f) * scale;

    ImVec2 top_left = ImGui::GetCursorScreenPos();
    ImVec2 toggle_button_offset{};

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (draw_side_menu_this_frame) {
        ImVec2 logo_size = ImVec2(71, 28) * scale;
        ImVec2 logo_margin = ImVec2(30.0f * scale, header_height / 2.0f - logo_size.y / 2.0f);
        ImVec2 logo_top_left = top_left + logo_margin;

        draw_list->AddImage((void*) (intptr_t) logo.texture_id, logo_top_left, logo_top_left + logo_size);

        toggle_button_offset = ImVec2(-20.0f, 0.0f) * scale;
    }

    ImVec2 toggle_button_size = ImVec2(16, 13) * scale;
    ImVec2 toggle_button_top_left = ImVec2(effective_folder_tree_column_width - toggle_button_size.x, 0) +
                                    ImVec2(0, header_height / 2.0f - toggle_button_size.y / 2.0f) +
                                    toggle_button_offset;

    if (draw_side_menu_toggle_button(toggle_button_top_left, toggle_button_size)) {
        draw_side_menu = !draw_side_menu;
    }

    ImVec2 new_entity_button_size = ImVec2(28, 28) * scale;
    ImVec2 new_entity_button_top_left = top_left +
                                        ImVec2(20.0f, 0) * scale +
                                        ImVec2(effective_folder_tree_column_width, 0) +
                                        ImVec2(0, header_height / 2.0f - new_entity_button_size.y / 2.0f);

    if (draw_add_new_entity_button(new_entity_button_top_left, new_entity_button_size)) {
        // TODO new entity creation dropdown
    }

    ImVec2 header_menu_cursor = ImVec2(new_entity_button_top_left.x + new_entity_button_size.x + 8.0f * scale, 0);
    ImVec2 out_size{};

    ImGui::PushFont(font_large);

    draw_header_button("Inbox", header_menu_cursor, header_height, out_size); header_menu_cursor.x += out_size.x;
    draw_header_button("My Work", header_menu_cursor, header_height, out_size); header_menu_cursor.x += out_size.x;
    draw_header_button("Dashboards", header_menu_cursor, header_height, out_size); header_menu_cursor.x += out_size.x;
    draw_header_button("Calendars", header_menu_cursor, header_height, out_size); header_menu_cursor.x += out_size.x;
    draw_header_button("Reports", header_menu_cursor, header_height, out_size); header_menu_cursor.x += out_size.x;
    draw_header_button("Stream", header_menu_cursor, header_height, out_size);

    ImGui::PopFont();

    if (this_user) {
        draw_profile_widget(this_user, header_height);
    }

    ImGui::Dummy(ImVec2(0, header_height));
}