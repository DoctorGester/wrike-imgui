#pragma once

struct ImGuiContext;
#define GImGui global_imgui_context
extern ImGuiContext* global_imgui_context;

void create_imgui_context();


