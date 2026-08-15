#include "Segmentation.h"

void segmentByDepth(Frame& frame, float minDepthMm, float maxDepthMm) {
    for (float& d : frame.depth) {
        if (d < minDepthMm || d > maxDepthMm)
            d = 0.0f;
    }
}
