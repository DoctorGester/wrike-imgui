#pragma once
#include <vector>
#include "common.h"

class FrameMonitor
{
    bool bShow = true;
    std::vector<float> values;
    int frame = 0;
    u64 lastTime = 0;

public:
    FrameMonitor();
    void drawAverage();
    void debugDraw();
    void startFrame();
    void endFrame();
};
