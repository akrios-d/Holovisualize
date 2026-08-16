#include "PointCloud.h"

PointCloud generatePointCloud(const Frame& frame) {
    const auto& intr = frame.depthIntrinsics;
    const int width  = intr.width;
    const int height = intr.height;

    PointCloud cloud;
    cloud.reserve(width * height);

    for (int v = 0; v < height; v++) {
        for (int u = 0; u < width; u++) {
            const int idx = v * width + u;
            const float d = frame.depth[idx];
            if (d <= 0.0f) continue;

            const float z = d / 1000.0f;                  // mm → meters
            const float x = (u - intr.cx) * z / intr.fx;
            // Pixel row v grows downward, but every consumer (server/preview
            // renderers) expects a Y-up world — flip sign here once instead
            // of patching every viewer separately.
            const float y = -(v - intr.cy) * z / intr.fy;

            // colorAligned is BGRX — byte order: B=0, G=1, R=2, X=3
            const uint8_t* px = frame.colorAligned.data() + idx * 4;
            cloud.push_back({ x, y, z, px[2], px[1], px[0] });
        }
    }

    return cloud;
}
