#include "filters/BackgroundSubtractorFilter.h"
#include "PointCloud.h"
#include <cmath>
#include <algorithm>

void BackgroundSubtractorFilter::reset() {
    ready_       = false;
    count_       = 0;
    width_       = 0;
    height_      = 0;
    background_.clear();
    accumulator_.clear();
}

void BackgroundSubtractorFilter::apply(PointCloud& cloud, const Frame& frame) {
    const int w = frame.depthIntrinsics.width;
    const int h = frame.depthIntrinsics.height;
    const int n = w * h;

    // ── Learning phase ────────────────────────────────────────────────────────
    if (!ready_) {
        if (count_ == 0) {
            width_  = w;
            height_ = h;
            accumulator_.assign(n, 0.0f);
        }
        for (int i = 0; i < n; i++) {
            float d = frame.depth[i];
            if (d > 0.0f) accumulator_[i] += d;
        }
        if (++count_ >= numFrames) {
            background_.resize(n);
            for (int i = 0; i < n; i++)
                background_[i] = accumulator_[i] / static_cast<float>(numFrames);
            ready_ = true;
        }
        // During learning, pass cloud through unchanged.
        return;
    }

    // ── Apply phase: rebuild cloud from foreground pixels only ────────────────
    // We rebuild based on the frame's depth map — pixels that differ from
    // background by more than threshold are foreground.
    // (The cloud passed in was already generated from the same frame, so we
    // filter points whose projected pixel index matches a background pixel.)
    //
    // Simpler and cheaper: just remove points whose depth (z in metres)
    // matches the background depth at any pixel within threshold.
    // Since we don't have per-point pixel coords here, we apply the
    // background mask to the frame depth inline via pixel-space subtraction
    // and regenerate — but to avoid an extra generatePointCloud call we
    // filter points by their z distance to the nearest background depth sample.
    //
    // Accurate approach: mask the depth map before generatePointCloud (done in
    // Pipeline::process when this filter is active — see Pipeline.cpp).
    // This apply() is therefore a no-op in the accurate path; kept for
    // standalone use.
    (void)cloud;
}

// Called by Pipeline to get the masked depth frame before cloud generation.
// Returns a copy of frame with background pixels zeroed out.
Frame BackgroundSubtractorFilter::applyToFrame(const Frame& frame) const {
    if (!ready_) return frame;

    Frame out = frame;
    const int n = width_ * height_;
    for (int i = 0; i < n && i < static_cast<int>(out.depth.size()); i++) {
        float d = out.depth[i];
        if (d > 0.0f && std::abs(d - background_[i]) < thresholdMm)
            out.depth[i] = 0.0f;
    }
    return out;
}
