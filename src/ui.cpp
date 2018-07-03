#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cstdint>
#include "common.h"
#include "users.h"
#include "platform.h"

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
        static const u32 spinner_color = argb_to_agbr(0xff4488ff);

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