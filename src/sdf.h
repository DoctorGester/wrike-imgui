#include <imgui.h>
#include "common.h"

struct SDF_Vert {
    ImVec2  pos;
    ImVec2  uv;
    float   sdf_size;
};

// Default roboto values
struct SDF_Font {
    float ix = 0.009766;
    float iy = 0.009766;
    float aspect = 1.000000;
    float row_height = 0.121094;
    float ascent = 0.080078;
    float descent = 0.021484;
    float line_gap = 0.000000;
    float cap_height = 0.061615;
    float x_height = 0.047000;
    float space_advance = 0.021455;
};

struct SDF_Font_Metrics {
    float cap_scale;
    float low_scale;
    float pixel_size;
    float ascent;
    float line_height;
};

struct SDF_Glyph {
    u32 codepoint;
    ImVec2 a;
    ImVec2 c;
    float bearing_x;
    float bearing_y;
    float advance_x;
    u32 flags;
};

void init_sdf();
void render_test_sdf_text(float font_size, const float* orthProjection);