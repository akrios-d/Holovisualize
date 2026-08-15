#pragma once

#include "ISensor.h"
#include "PointCloud.h"
#include "IPointCloudFilter.h"
#include "filters/BackgroundSubtractorFilter.h"

#include <memory>
#include <vector>

// Orchestrates the full capture pipeline:
//   sensor → filter → segment → point cloud → [IPointCloudFilter chain]
class Pipeline {
public:
    explicit Pipeline(std::unique_ptr<ISensor> sensor);

    bool initialize();
    void shutdown();

    // Captures one frame, runs built-in processing, then applies all
    // registered IPointCloudFilter instances in order.
    // Returns empty cloud if capture fails.
    PointCloud process();

    // ── Filter chain (Socket Pattern) ────────────────────────────────────────
    // Filters are applied in registration order after point cloud generation.
    void addFilter(std::unique_ptr<IPointCloudFilter> filter);
    void clearFilters();

    // Convenience accessors for filters added by the UI.
    // Returns nullptr if no filter of that type is registered.
    template<typename T>
    T* getFilter() {
        for (auto& f : filters_)
            if (auto* p = dynamic_cast<T*>(f.get())) return p;
        return nullptr;
    }

    // ── Built-in depth processing settings ───────────────────────────────────
    bool  filterEnabled     = true;
    int   filterNeighbours  = 1;
    float filterThresholdMm = 20.0f;

    float segmentMinDepthMm = 200.0f;
    float segmentMaxDepthMm = 2500.0f;

    // ── Accessors ─────────────────────────────────────────────────────────────
    // Direct sensor access — used by calibration mode.
    ISensor& sensor() { return *sensor_; }

    // Last captured frame — used by preview and background subtraction.
    const Frame& lastFrame() const { return frame_; }

private:
    std::unique_ptr<ISensor>                     sensor_;
    Frame                                        frame_;
    std::vector<std::unique_ptr<IPointCloudFilter>> filters_;
};
