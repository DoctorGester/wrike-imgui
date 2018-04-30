ImGuiContext* global_imgui_context = nullptr;

void create_imgui_context()
{
    global_imgui_context = ImGui::CreateContext();
}
