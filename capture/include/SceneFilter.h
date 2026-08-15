#pragma once

#include "Frame.h"
#include "PointCloud.h"

#include <vector>

// ── Body filter ───────────────────────────────────────────────────────────────
// Removes points outside typical human-body spatial bounds (in metres).
// Works on a PointCloud after projection — fast, no depth map needed.
struct BodyBounds {
    float minX = -0.7f;  float maxX =  0.7f;  // lateral   (m)
    float minY = -0.2f;  float maxY =  2.1f;  // vertical  (m, Y grows up)
    float minZ =  0.2f;  float maxZ =  2.5f;  // depth     (m)
};

void filterBody(PointCloud& cloud, const BodyBounds& bounds = {});

// ── Background subtraction ────────────────────────────────────────────────────
// Accumulates N depth frames into a background model, then zeroes out depth
// pixels that match the background within a threshold.
class BackgroundSubtractor {
public:
    // thresholdMm  — depth difference required to be considered foreground
    // numFrames    — how many frames to average for the background model
    explicit BackgroundSubtractor(float thresholdMm = 15.0f, int numFrames = 30);

    // Feed a frame during the learning phase.
    // Returns true when the model is ready (numFrames accumulated).
    bool learn(const Frame& frame);

    bool isReady() const { return ready_; }

    // Zero out depth pixels that belong to the background.
    // No-op if the model is not ready yet.
    void apply(Frame& frame) const;

    // Reset — restart learning.
    void reset();

private:
    float             threshold_;
    int               numFrames_;
    bool              ready_    = false;
    int               count_    = 0;
    int               width_    = 0;
    int               height_   = 0;
    std::vector<float> background_; // average depth per pixel (mm)
    std::vector<float> accumulator_;
};
