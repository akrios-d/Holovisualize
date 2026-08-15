#pragma once

#include "IPointCloudFilter.h"
#include <atomic>
#include <vector>

// Captures N frames of an empty scene to build a background depth model,
// then removes points whose depth matches the background within a threshold.
//
// Learning phase: call apply() with no person present until isReady() == true.
// The UI shows a "Learning background…" indicator during this phase.
class BackgroundSubtractorFilter final : public IPointCloudFilter {
public:
    float thresholdMm = 15.0f;  // depth tolerance to count as background
    int   numFrames   = 30;     // frames to average for background model

    bool isReady()    const { return ready_; }
    float progress()  const {
        return ready_ ? 1.0f
                      : static_cast<float>(count_) / static_cast<float>(numFrames);
    }

    void reset();

    // Returns a copy of frame with background pixels zeroed out.
    // Called by Pipeline before generatePointCloud for accurate subtraction.
    Frame applyToFrame(const Frame& frame) const;

    void apply(PointCloud& cloud, const Frame& frame) override;
    const char* name() const override { return "Background Subtraction"; }

private:
    bool               ready_       = false;
    int                count_       = 0;
    int                width_       = 0;
    int                height_      = 0;
    std::vector<float> background_;
    std::vector<float> accumulator_;
};
