#pragma once

#include <cstdint>

struct alignas(16) GlobalsData
{
    float resolution[2];  // x, y

    int32_t showGrid;     // 1 = show, 0 = hide
    int32_t selectedIndex;         // alignment padding

    float cameraPos[4];
    float cameraTarget[4];
};
