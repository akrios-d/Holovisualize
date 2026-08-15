#pragma once

#include "Frame.h"

// Zeroes out depth pixels outside [minDepthMm, maxDepthMm].
// Default range isolates a person standing within 2.5m of the sensor.
void segmentByDepth(Frame& frame, float minDepthMm = 200.0f, float maxDepthMm = 2500.0f);
