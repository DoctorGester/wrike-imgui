#include "framemonitor.h"
#include "imgui.h"
#include <string>
#include <numeric>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "platform.h"

FrameMonitor::FrameMonitor()
{
    values.resize(60,0);
    lastTime = platform_get_app_time_ms();
}

void FrameMonitor::debugDraw()
{
    double avg = std::accumulate( values.begin(), values.end(), 0.0)/values.size();
    std::string overlay = std::to_string(avg);
    ImGui::Begin("Performance", &bShow);
    ImGui::PlotHistogram(
            "frametime",
            values.data(),
            values.size(),
            0,
            overlay.c_str(),
            0.0f,
            100.0f,
            ImVec2(0,100)
    );
    ImGui::End();
}

void FrameMonitor::drawAverage() {
    if (values.size() == 0) {
        return;
    }

    double avg = std::accumulate(values.begin(), values.end(), 0.0)/values.size();
    char test[48];

    sprintf(test, "Loop time: %fms", avg);

    ImGui::GetOverlayDrawList()->AddText(ImGui::GetCursorScreenPos() + ImVec2(800.0f, -50.0f), 0x80FFFFFF, test, test + strlen(test));
}

void FrameMonitor::startFrame() {
    lastTime = platform_get_app_time_ms();
}

void FrameMonitor::endFrame()
{
    if(bShow)
    {
        double newTime = platform_get_app_time_ms();
        values[frame++%values.size()] = newTime - lastTime;
        //lastTime = newTime;
    }
}
