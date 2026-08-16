#pragma once

#include <vector>
#include <cstdint>

// Intrinsic parameters of a depth camera.
// Used to project depth pixels into 3D space.
struct Intrinsics {
    float fx, fy;   // focal length in pixels
    float cx, cy;   // principal point in pixels
    int width, height;
};

// A single captured frame from a depth sensor.
// depth:        float mm (0 = invalid), size = depthIntrinsics.width * height
// color:        BGRX full resolution (e.g. 1920x1080 * 4 bytes)
// colorAligned: BGRX registered to depth space (same size as depth, 4 bytes/pixel)
// ir:           raw IR intensity, same size/resolution as depth (empty if unsupported)
struct Frame {
    std::vector<float>   depth;
    std::vector<uint8_t> color;
    std::vector<uint8_t> colorAligned;
    std::vector<float>   ir;

    Intrinsics depthIntrinsics;
    Intrinsics colorIntrinsics;  // full-res colour camera — used for calibration (solvePnP)
    int colorWidth  = 0;
    int colorHeight = 0;

    uint64_t timestamp = 0;  // microseconds

    // Depth frames skipped between this frame and the previous one, detected
    // via the sensor's frame sequence number (0 if unsupported/first frame).
    int framesLost = 0;
};
