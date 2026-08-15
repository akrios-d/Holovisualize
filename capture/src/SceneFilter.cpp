#include "SceneFilter.h"
#include <cmath>
#include <algorithm>

// ── Body filter ───────────────────────────────────────────────────────────────

void filterBody(PointCloud& cloud, const BodyBounds& bounds) {
    cloud.erase(
        std::remove_if(cloud.begin(), cloud.end(), [&](const Point& p) {
            return p.x < bounds.minX || p.x > bounds.maxX
                || p.y < bounds.minY || p.y > bounds.maxY
                || p.z < bounds.minZ || p.z > bounds.maxZ;
        }),
        cloud.end()
    );
}

// ── BackgroundSubtractor ──────────────────────────────────────────────────────

BackgroundSubtractor::BackgroundSubtractor(float thresholdMm, int numFrames)
    : threshold_(thresholdMm), numFrames_(numFrames) {}

bool BackgroundSubtractor::learn(const Frame& frame) {
    if (ready_) return true;

    const int w = frame.depthIntrinsics.width;
    const int h = frame.depthIntrinsics.height;
    const int n = w * h;

    if (count_ == 0) {
        width_  = w;
        height_ = h;
        accumulator_.assign(n, 0.0f);
    }

    // Accumulate valid (non-zero) depth values.
    for (int i = 0; i < n; i++) {
        float d = frame.depth[i];
        if (d > 0.0f)
            accumulator_[i] += d;
    }

    count_++;
    if (count_ >= numFrames_) {
        background_.resize(n);
        for (int i = 0; i < n; i++)
            background_[i] = accumulator_[i] / static_cast<float>(numFrames_);
        ready_ = true;
    }
    return ready_;
}

void BackgroundSubtractor::apply(Frame& frame) const {
    if (!ready_) return;

    const int n = width_ * height_;
    for (int i = 0; i < n; i++) {
        float d = frame.depth[i];
        if (d <= 0.0f) continue;
        // Zero out pixel if it matches background within threshold.
        if (std::abs(d - background_[i]) < threshold_)
            frame.depth[i] = 0.0f;
    }
}

void BackgroundSubtractor::reset() {
    ready_  = false;
    count_  = 0;
    width_  = 0;
    height_ = 0;
    background_.clear();
    accumulator_.clear();
}
