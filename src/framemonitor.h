#pragma once
#include <vector>
class FrameMonitor
{
    bool bShow = true;
    std::vector<float> values;
    int frame = 0;
    double lastTime = 0.0;

public:
    FrameMonitor();
    void drawAverage();
    void debugDraw();
    void startFrame();
    void endFrame();
};
