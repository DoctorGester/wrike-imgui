#pragma once

#include <imgui.h>
#include "common.h"

void renderer_draw_lists(ImDrawData* drawData);
void renderer_init(const char* vertex_shader_source, const char* fragment_shader_source);

extern ImFont* font_header;
extern ImFont* font_regular;
extern ImFont* font_bold;
extern ImFont* font_italic;
extern ImFont* font_bold_italic;