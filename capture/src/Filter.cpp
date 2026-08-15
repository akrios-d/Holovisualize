#include "Filter.h"

#include <vector>
#include <cmath>

// Ported from LiveScan3D (MIT License) — adapted for float depth and Frame struct.
void filterFlyingPixels(Frame& frame, int neighbourhoodSize, float threshold) {
    const int width  = frame.depthIntrinsics.width;
    const int height = frame.depthIntrinsics.height;
    float* depth     = frame.depth.data();

    // Precompute flat index offsets for every neighbour in the neighbourhood.
    const int nNeighbours = (neighbourhoodSize * 2 + 1) * (neighbourhoodSize * 2 + 1) - 1;
    std::vector<int> shifts;
    shifts.reserve(nNeighbours);

    for (int dx = -neighbourhoodSize; dx <= neighbourhoodSize; dx++)
        for (int dy = -neighbourhoodSize; dy <= neighbourhoodSize; dy++) {
            if (dx == 0 && dy == 0) continue;
            shifts.push_back(dx * width + dy);
        }

    const int maxNonFitting = nNeighbours / 2;

    std::vector<int> toRemove;
    for (int y = neighbourhoodSize; y < height - neighbourhoodSize; y++) {
        const int rowOffset  = y * width;
        const float* rowPtr  = depth + rowOffset;

        for (int x = neighbourhoodSize; x < width - neighbourhoodSize; x++) {
            const float val = rowPtr[x];
            if (val == 0.0f) continue;  // already invalid, skip

            int nonFitting = 0;
            for (int shift : shifts) {
                if (std::abs(rowPtr[x + shift] - val) > threshold)
                    nonFitting++;
            }

            if (nonFitting > maxNonFitting)
                toRemove.push_back(rowOffset + x);
        }
    }

    for (int idx : toRemove)
        depth[idx] = 0.0f;
}
