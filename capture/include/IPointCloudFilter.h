#pragma once

#include "PointCloud.h"

// Socket Pattern — filter port.
//
// Implement this interface to plug any custom filter into the capture pipeline.
// Filters are applied in registration order after point cloud generation.
//
// Example:
//   pipeline.addFilter(std::make_unique<BodyFilter>());
//   pipeline.addFilter(std::make_unique<BackgroundSubtractorFilter>());
class IPointCloudFilter {
public:
    virtual ~IPointCloudFilter() = default;

    // Called once before the capture loop starts.
    // Override to do one-time initialisation (e.g. allocate GPU buffers).
    virtual void initialize() {}

    // Called every frame. Modify `cloud` in-place — remove, add, or transform
    // points as needed.  `frame` is read-only raw sensor data available for
    // filters that need depth-map context (e.g. background subtraction).
    virtual void apply(PointCloud& cloud, const Frame& frame) = 0;

    // Human-readable name shown in the UI.
    virtual const char* name() const = 0;

    // Whether this filter is currently active.
    // The pipeline skips `apply` for inactive filters.
    bool enabled = true;
};
