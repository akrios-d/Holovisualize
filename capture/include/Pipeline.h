#pragma once

#include "ISensor.h"
#include "PointCloud.h"
#include <memory>

// Orchestrates the full capture pipeline:
//   sensor → filter → segment → point cloud
class Pipeline {
public:
    explicit Pipeline(std::unique_ptr<ISensor> sensor);

    bool initialize();
    void shutdown();

    // Captures one frame and runs the full processing chain.
    // Returns empty cloud if capture fails.
    PointCloud process();

    // Settings (adjust before calling process)
    bool  filterEnabled     = true;
    int   filterNeighbours  = 1;
    float filterThresholdMm = 20.0f;

    float segmentMinDepthMm = 200.0f;
    float segmentMaxDepthMm = 2500.0f;

    // Direct sensor access — used by calibration mode to get raw frames.
    ISensor& sensor() { return *sensor_; }

private:
    std::unique_ptr<ISensor> sensor_;
    Frame frame_;
};
