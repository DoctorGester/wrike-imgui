#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <cstdint>
#include "common.h"
#include "users.h"
#include "platform.h"

// TODO could this be constexpr if we got rid of the whole platform_get_scale() thing?
// TODO add actual antialiasing from imgui_draw
static void fill_antialiased_textured_circle(ImDrawList* draw_list, ImVec2 centre, float radius, u32 num_segments) {
    draw_list->PrimReserve(num_segments * 3, num_segments + 1);

    draw_list->_VtxWritePtr[0].pos = centre;
    draw_list->_VtxWritePtr[0].uv = { 0.5f, 0.5f };
    draw_list->_VtxWritePtr[0].col = IM_COL32_WHITE;

    draw_list->_VtxWritePtr++;

    for (int i = 0; i < num_segments; i++) {
        float angle = ((float) i / (float) num_segments) * (2.0f * IM_PI);

        ImVec2 xy = ImVec2(centre.x + cosf(angle) * radius, centre.y + sinf(angle) * radius);
        ImVec2 uv = ImVec2(cosf(angle), sinf(angle)) / 2.0f + ImVec2(0.5f, 0.5f);

        draw_list->_VtxWritePtr[0].pos = xy;
        draw_list->_VtxWritePtr[0].uv = uv;
        draw_list->_VtxWritePtr[0].col = IM_COL32_WHITE;
        draw_list->_VtxWritePtr++;
    }

    u32 current_idx = draw_list->_VtxCurrentIdx;

    for (int i0 = num_segments - 1, i1 = 0; i1 < num_segments; i0 = i1++) {
        draw_list->_IdxWritePtr[0] = (ImDrawIdx) (current_idx);
        draw_list->_IdxWritePtr[1] = (ImDrawIdx) (current_idx + 1 + i0);
        draw_list->_IdxWritePtr[2] = (ImDrawIdx) (current_idx + 1 + i1);

        draw_list->_IdxWritePtr += 3;
    }

    draw_list->_VtxCurrentIdx += num_segments + 1;
}

void draw_circular_user_avatar(ImDrawList* draw_list, User* user, ImVec2 top_left, float avatar_side_px) {
    if (check_and_request_user_avatar_if_necessary(user)) {
        float half_avatar_side = avatar_side_px / 2.0f;
        ImTextureID avatar_texture_id = (ImTextureID)(intptr_t) user->avatar.texture_id;

        draw_list->PushTextureID(avatar_texture_id);
        fill_antialiased_textured_circle(draw_list, top_left + ImVec2(half_avatar_side, half_avatar_side), half_avatar_side, 32);
        draw_list->PopTextureID();
    } else {
        // TODO dummy image?
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