#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cstdint>
#include "common.h"
#include "users.h"
#include "platform.h"
#include "ui.h"

// TODO could this be constexpr if we got rid of the whole platform_get_scale() thing?
static void fill_antialiased_textured_circle(ImDrawList* draw_list, ImVec2 centre, float radius, u32 color, u32 num_segments) {
    const u32 num_points = num_segments + 1;
    const u32 vertex_count = (num_points * 2);
    const u32 index_count = (num_points - 2) * 3 + num_points * 6;

    draw_list->PrimReserve(index_count, vertex_count);

    const float aa_size = 1.0f;

    const u32 transparent_color = color & ~IM_COL32_A_MASK;

    u32 vtx_inner_idx = draw_list->_VtxCurrentIdx;
    u32 vtx_outer_idx = draw_list->_VtxCurrentIdx + 1;

    for (int i = 2; i < num_points; i++) {
        draw_list->_IdxWritePtr[0] = (ImDrawIdx) (vtx_inner_idx);
        draw_list->_IdxWritePtr[1] = (ImDrawIdx) (vtx_inner_idx + ((i - 1) * 2));
        draw_list->_IdxWritePtr[2] = (ImDrawIdx) (vtx_inner_idx + (i * 2));
        draw_list->_IdxWritePtr += 3;
    }

    for (int i = 0; i < num_points; i++) {
        float angle = ((float) i / (float) num_points) * (2.0f * IM_PI);

        ImVec2 xy = ImVec2(centre.x + cosf(angle) * radius, centre.y + sinf(angle) * radius);
        ImVec2 uv = ImVec2(cosf(angle), sinf(angle)) / 2.0f + ImVec2(0.5f, 0.5f);

        float normal_angle = ((i + 0.5f) / (float) num_points) * (2.0f * IM_PI);

        ImVec2 dm(cosf(normal_angle), sinf(normal_angle));
        float dmr2 = dm.x * dm.x + dm.y * dm.y;

        if (dmr2 > 0.000001f) {
            float scale = 1.0f / dmr2;
            if (scale > 100.0f) scale = 100.0f;
            dm *= scale;
        }

        dm *= aa_size * 0.5f;

        draw_list->_VtxWritePtr->pos = xy - dm;
        draw_list->_VtxWritePtr->uv = uv;
        draw_list->_VtxWritePtr->col = color;
        draw_list->_VtxWritePtr++;

        draw_list->_VtxWritePtr->pos = xy + dm;
        draw_list->_VtxWritePtr->uv = uv;
        draw_list->_VtxWritePtr->col = transparent_color;
        draw_list->_VtxWritePtr++;
    }

    for (int i0 = num_points - 1, i1 = 0; i1 < num_points; i0 = i1++) {
        draw_list->_IdxWritePtr[0] = (ImDrawIdx) (vtx_inner_idx + (i1 << 1));
        draw_list->_IdxWritePtr[1] = (ImDrawIdx) (vtx_inner_idx + (i0 << 1));
        draw_list->_IdxWritePtr[2] = (ImDrawIdx) (vtx_outer_idx + (i0 << 1));
        draw_list->_IdxWritePtr[3] = (ImDrawIdx) (vtx_outer_idx + (i0 << 1));
        draw_list->_IdxWritePtr[4] = (ImDrawIdx) (vtx_outer_idx + (i1 << 1));
        draw_list->_IdxWritePtr[5] = (ImDrawIdx) (vtx_inner_idx + (i1 << 1));
        draw_list->_IdxWritePtr += 6;
    }

    draw_list->_VtxCurrentIdx += vertex_count;
}

void draw_loading_spinner(ImDrawList* draw_list, ImVec2 top_left, float radius, int thickness, const u32 color) {
    ImGuiContext& g = *GImGui;

    draw_list->PathClear();

    float num_segments = 360;
    float start = roundf(abs(ImSin(g.Time * 1.8f) * (num_segments - 60)));

    const float a_min = IM_PI * 2.0f * start / num_segments + g.Time * 8;
    const float a_max = IM_PI * 2.0f * (num_segments - 36) / num_segments + g.Time * 8;

    const ImVec2 centre = ImVec2(top_left.x + radius, top_left.y + radius);

    draw_list->PathArcTo(centre, radius, a_min, a_max, 30);
    draw_list->PathStroke(color, false, thickness);
}

void draw_circular_user_avatar(ImDrawList* draw_list, User* user, ImVec2 top_left, float avatar_side_px) {
    if (check_and_request_user_avatar_if_necessary(user)) {
        float half_avatar_side = avatar_side_px / 2.0f;
        ImTextureID avatar_texture_id = (ImTextureID)(intptr_t) user->avatar.texture_id;

        u32 avatar_color = 0x00FFFFFF;
        u32 alpha = (u32) roundf(lerp(user->avatar_loaded_at, tick, 255, 14));
        u32 avatar_color_with_alpha = avatar_color | (alpha << 24);

        draw_list->PushTextureID(avatar_texture_id);
        fill_antialiased_textured_circle(draw_list, top_left + ImVec2(half_avatar_side, half_avatar_side), half_avatar_side, avatar_color_with_alpha, 32);
        draw_list->PopTextureID();
    } else {
        static const u32 spinner_color = color_link;

        draw_loading_spinner(draw_list, top_left, avatar_side_px / 2.0f, 6, spinner_color);
    }
}

void draw_loading_indicator(ImVec2 center, u32 started_showing_at, ImVec2 offset) {
    float scale = platform_get_pixel_ratio();
    const float speed_scale = 10.0f;
    float cos = cosf(tick / speed_scale);
    float sin = sinf(tick / speed_scale);
    float size = scale * 10.0f;

    u32 alpha = (u32) roundf(lerp(started_showing_at, tick, 255, 14));

    center += (offset * scale);

    ImGui::GetWindowDrawList()->AddQuadFilled(
            center + ImRotate(ImVec2(-size, -size), cos, sin),
            center + ImRotate(ImVec2(+size, -size), cos, sin),
            center + ImRotate(ImVec2(+size, +size), cos, sin),
            center + ImRotate(ImVec2(-size, +size), cos, sin),
            IM_COL32(0, 255, 200, alpha)
    );
}

void draw_window_loading_indicator() {
    float spinner_side = 12.0f * platform_get_pixel_ratio();

    ImVec2 top_left = ImGui::GetWindowPos() + ImGui::GetWindowSize() / 2.0f - ImVec2(spinner_side, spinner_side);

    draw_loading_spinner(ImGui::GetWindowDrawList(), top_left, spinner_side, 6, color_link);
}

bool draw_expand_arrow_button(ImDrawList* draw_list, ImVec2 arrow_point, float height, bool is_expanded) {
    const static u32 expand_arrow_color = 0xff848484;
    const static u32 expand_arrow_hovered = argb_to_agbr(0xff73a6ff);

    float arrow_half_height = ImGui::GetFontSize() / 4.0f;
    float arrow_width = arrow_half_height;

    ImVec2 button_top_left{ arrow_point.x - arrow_width * 3.5f, arrow_point.y - height / 2.0f };
    ImVec2 button_size{ arrow_width * 7.0f, height };

    Button_State button_state = button("expand_arrow", button_top_left, button_size);

    if (button_state.clipped) {
        return button_state.pressed;
    }

    u32 color = button_state.hovered ? expand_arrow_hovered : expand_arrow_color;

    if (!is_expanded) {
        ImVec2 arrow_top_left = arrow_point - ImVec2(arrow_width,  arrow_half_height);
        ImVec2 arrow_bottom_right = arrow_point - ImVec2(arrow_width, -arrow_half_height);

        draw_list->AddLine(arrow_point, arrow_top_left, color);
        draw_list->AddLine(arrow_point, arrow_bottom_right, color);
    } else {
        ImVec2 arrow_right = arrow_point + ImVec2(arrow_width / 2.0f, 0.0f);
        ImVec2 arrow_bottom_point = arrow_right - ImVec2(arrow_width, -arrow_half_height);

        draw_list->AddLine(arrow_right - ImVec2(arrow_width * 2.0f, 0.0f), arrow_bottom_point, color);
        draw_list->AddLine(arrow_right, arrow_bottom_point, color);
    }

    return button_state.pressed;
}

void draw_scroll_shadow(ImDrawList* draw_list, ImVec2 top_left, float content_width, float scale) {
    ImVec2 shadow_top_left = top_left + ImVec2(0, ImGui::GetScrollY());
    ImVec2 shadow_bottom_right = shadow_top_left + ImVec2(content_width, 4.0f * scale);

    const u32 shadow_start = 0x33000000;
    const u32 shadow_end = 0x00000000;

    draw_list->AddRectFilledMultiColor(shadow_top_left, shadow_bottom_right, shadow_start, shadow_start, shadow_end, shadow_end);
}

static void button(ImGuiID id, ImVec2 top_left, ImVec2 size, Button_State& state) {
    ImRect bounds(top_left, top_left + size);

    state.clipped = !ImGui::ItemAdd(bounds, id);
    state.pressed = ImGui::ButtonBehavior(bounds, id, &state.hovered, &state.held);
}

Button_State button(const void* pointer_id, ImVec2 top_left, ImVec2 size) {
    ImGuiID id = ImGui::GetID(pointer_id);

    Button_State state;
    button(id, top_left, size, state);

    return state;
}

Button_State button(const char* string_id, ImVec2 top_left, ImVec2 size) {
    ImGuiID id = ImGui::GetID(string_id);

    Button_State state;
    button(id, top_left, size, state);

    return state;
}

Vertical_Layout vertical_layout(ImVec2 top_left) {
    Vertical_Layout layout;
    layout.scale = platform_get_pixel_ratio();
    layout.cursor = top_left;
    layout.top_left = top_left;
    layout.maximum_width = 0.0f;

    return layout;
}

Horizontal_Layout horizontal_layout(ImVec2 top_left, float row_height) {
    Horizontal_Layout layout;
    layout.scale = platform_get_pixel_ratio();
    layout.cursor = top_left;
    layout.top_left = top_left;
    layout.row_height = row_height;

    return layout;
}

Wrapping_Horizontal_Layout wrapping_horizontal_layout(ImVec2 top_left, float row_height, float wrap_width) {
    Wrapping_Horizontal_Layout layout;
    layout.scale = platform_get_pixel_ratio();
    layout.cursor = top_left;
    layout.top_left = top_left;
    layout.row_height = row_height;
    layout.wrap_width = wrap_width;
    layout.has_drawn_at_least_one_element = false;

    return layout;
}

bool layout_check_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size) {
    bool would_like_to_wrap = layout.cursor.x + item_size.x > layout.top_left.x + layout.wrap_width;

    return layout.has_drawn_at_least_one_element && would_like_to_wrap;
}

void layout_wrap(Wrapping_Horizontal_Layout& layout) {
    layout.cursor.x = layout.top_left.x;
    layout.cursor.y += layout.row_height;
}

bool layout_check_and_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size) {
    if (layout_check_wrap(layout, item_size)) {
        layout_wrap(layout);

        return true;
    }

    return false;
}

void layout_advance(Horizontal_Layout& layout, float value) {
    layout.cursor.x += value;
}

ImVec2 layout_center_vertically(Horizontal_Layout& layout, float known_height) {
    return { layout.cursor.x, layout.cursor.y + layout.row_height / 2.0f - known_height / 2.0f };
}

void layout_advance(Vertical_Layout& layout, float value) {
    layout.cursor.y += value;
}

void layout_push_max_width(Vertical_Layout& layout, float width) {
    layout.maximum_width = MAX(layout.maximum_width, width);
}

void layout_push_item_size(Vertical_Layout& layout) {
    ImGui::ItemSize({ layout.maximum_width, layout.cursor.y - layout.top_left.y });
}