#include "Pipeline.h"
#include "Filter.h"
#include "Segmentation.h"
#include "filters/BackgroundSubtractorFilter.h"

Pipeline::Pipeline(std::unique_ptr<ISensor> sensor)
    : sensor_(std::move(sensor)) {}

bool Pipeline::initialize() {
    for (auto& f : filters_) f->initialize();
    return sensor_->initialize();
}

void Pipeline::shutdown() {
    sensor_->shutdown();
}

void Pipeline::addFilter(std::unique_ptr<IPointCloudFilter> filter) {
    filters_.push_back(std::move(filter));
}

void Pipeline::clearFilters() {
    filters_.clear();
}

PointCloud Pipeline::process() {
    if (!sensor_->captureFrame(frame_))
        return {};

    // ── Built-in depth processing ─────────────────────────────────────────────
    if (filterEnabled)
        filterFlyingPixels(frame_, filterNeighbours, filterThresholdMm);

    segmentByDepth(frame_, segmentMinDepthMm, segmentMaxDepthMm);

    // ── Background subtraction (depth-map level, before cloud generation) ─────
    // BackgroundSubtractorFilter needs to zero depth pixels before projection,
    // so we handle it specially here rather than in the generic filter chain.
    Frame* frameToUse = &frame_;
    Frame  maskedFrame;

    if (auto* bg = getFilter<BackgroundSubtractorFilter>()) {
        if (bg->enabled) {
            maskedFrame = bg->applyToFrame(frame_);
            frameToUse  = &maskedFrame;
        }
    }

    PointCloud cloud = generatePointCloud(*frameToUse);

    // ── Generic IPointCloudFilter chain ───────────────────────────────────────
    for (auto& f : filters_) {
        if (f->enabled && dynamic_cast<BackgroundSubtractorFilter*>(f.get()) == nullptr)
            f->apply(cloud, frame_);
    }

    return cloud;
}
