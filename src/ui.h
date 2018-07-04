#include <imgui.h>
#include "common.h"
#include "users.h"

struct Button_State {
    bool pressed;
    bool clipped;
    bool hovered;
    bool held;
};

Button_State button(const char* string_id, ImVec2 top_left, ImVec2 size);
Button_State button(const void* pointer_id, ImVec2 top_left, ImVec2 size);

void draw_circular_user_avatar(ImDrawList* draw_list, User* user, ImVec2 top_left, float avatar_side_px);
void draw_loading_indicator(ImVec2 center, u32 started_showing_at, ImVec2 offset = { 0, 0 });