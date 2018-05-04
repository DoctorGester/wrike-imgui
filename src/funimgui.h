#pragma once

#include <imgui.h>
#include "common.h"

class ImDrawData;
class EmscriptenMouseEvent;
class EmscriptenWheelEvent;
class EmscriptenKeyboardEvent;
class FunImGui {
public:
    static void initGraphics(const char* vertex_shader_source, const char* fragment_shader_source);
    static void initFont();
    static void RenderDrawLists(ImDrawData* drawData);
    static int m_shaderHandle;
    static int m_texture;
    static int m_projectionMatrix;
    static int m_position;
    static int m_uv;
    static int m_color;
    static unsigned int m_vao;
    static unsigned int m_vbo;
    static unsigned int m_elements;
    static unsigned int m_fontTexture;
};

extern ImFont* font_header;
extern ImFont* font_regular;
extern ImFont* font_bold;
extern ImFont* font_italic;
extern ImFont* font_bold_italic;