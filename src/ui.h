#include <imgui.h>
#include "common.h"
#include "users.h"

struct Button_State {
    bool pressed;
    bool clipped;
    bool hovered;
    bool held;
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

struct Vertical_Layout {
    ImVec2 top_left;
    ImVec2 cursor;
    float maximum_width;
    float scale;
};

Button_State button(const char* string_id, ImVec2 top_left, ImVec2 size);
Button_State button(const void* pointer_id, ImVec2 top_left, ImVec2 size);

void draw_circular_user_avatar(ImDrawList* draw_list, User* user, ImVec2 top_left, float avatar_side_px);
void draw_loading_indicator(ImVec2 center, u32 started_showing_at, ImVec2 offset = { 0, 0 });
void draw_window_loading_indicator();
void draw_loading_spinner(ImDrawList* draw_list, ImVec2 top_left, float radius, int thickness, const u32 color);
bool draw_expand_arrow_button(ImDrawList* draw_list, ImVec2 arrow_point, float height, bool is_expanded);

Vertical_Layout vertical_layout(ImVec2 top_left);
Horizontal_Layout horizontal_layout(ImVec2 top_left, float row_height);
Wrapping_Horizontal_Layout wrapping_horizontal_layout(ImVec2 top_left, float row_height, float wrap_width);

void layout_advance(Horizontal_Layout& layout, float value);
void layout_advance(Vertical_Layout& layout, float value);

ImVec2 layout_center_vertically(Horizontal_Layout& layout, float known_height);

bool layout_check_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size);
void layout_wrap(Wrapping_Horizontal_Layout& layout);
bool layout_check_and_wrap(Wrapping_Horizontal_Layout& layout, ImVec2 item_size);

void layout_push_max_width(Vertical_Layout& layout, float width);
void layout_push_item_size(Vertical_Layout& layout);