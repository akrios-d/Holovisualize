#pragma once

#include "Frame.h"
#include <vector>

struct Point {
    float   x, y, z;   // meters
    uint8_t r, g, b;
};

using PointCloud = std::vector<Point>;

// Projects each valid depth pixel into 3D using the camera intrinsics,
// and samples its color from frame.colorAligned.
// Depth is converted from mm to meters.
PointCloud generatePointCloud(const Frame& frame);
