#include "Pipeline.h"
#include "Filter.h"
#include "Segmentation.h"

Pipeline::Pipeline(std::unique_ptr<ISensor> sensor)
    : sensor_(std::move(sensor)) {}

bool Pipeline::initialize() {
    return sensor_->initialize();
}

void Pipeline::shutdown() {
    sensor_->shutdown();
}

PointCloud Pipeline::process() {
    if (!sensor_->captureFrame(frame_))
        return {};

    if (filterEnabled)
        filterFlyingPixels(frame_, filterNeighbours, filterThresholdMm);

    segmentByDepth(frame_, segmentMinDepthMm, segmentMaxDepthMm);

    return generatePointCloud(frame_);
}
