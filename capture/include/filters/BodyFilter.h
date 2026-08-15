#pragma once

#include "IPointCloudFilter.h"

// Removes points outside a human-body spatial bounding box.
// Bounds are in metres; defaults match a typical standing person
// centred in front of the sensor.
class BodyFilter final : public IPointCloudFilter {
public:
    float minX = -0.7f;  float maxX =  0.7f;  // lateral   (m)
    float minY = -0.2f;  float maxY =  2.1f;  // vertical  (m)
    float minZ =  0.2f;  float maxZ =  2.5f;  // depth     (m)

    void apply(PointCloud& cloud, const Frame&) override;
    const char* name() const override { return "Body Bounds"; }
};
