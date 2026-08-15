#pragma once

#include "Frame.h"
#include <array>
#include <string>

// Detects a single ArUco marker in the colour image and computes the
// camera-to-world transform using solvePnP.
//
// Usage:
//   Calibration cal(0.05f);          // 5 cm marker, ID 0
//   while (!cal.isCalibrated())
//       cal.detect(frame);           // keep feeding frames
//   auto m = cal.getTransform();     // 4x4 row-major, camera → world
class Calibration {
public:
    // markerId:     ArUco marker ID to look for (DICT_4X4_250)
    // markerSizeM:  physical side length of the printed marker in metres
    explicit Calibration(int markerId = 0, float markerSizeM = 0.05f);

    // Tries to detect the marker in `frame` and compute the transform.
    // Returns true when calibration succeeds (same as isCalibrated()).
    bool detect(const Frame& frame);

    bool isCalibrated() const { return calibrated_; }

    // 4x4 row-major matrix that transforms points from camera space to
    // world space (marker coordinate system). Valid only after detect() returns true.
    const std::array<float, 16>& getTransform() const { return transform_; }

private:
    int   markerId_;
    float markerSizeM_;

    bool                 calibrated_ = false;
    std::array<float,16> transform_  = {};
};
