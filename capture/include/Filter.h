#pragma once

#include "Frame.h"

// Removes "flying pixels" — depth artefacts that appear on the edges of objects
// caused by the IR projector hitting two surfaces at once.
//
// neighbourhoodSize: radius of the pixel neighbourhood to check (1 = 3x3, 2 = 5x5)
// threshold:         max depth difference (mm) before a neighbour is considered non-fitting
//
// Pixels where more than half the neighbours differ by more than `threshold` are zeroed out.
void filterFlyingPixels(Frame& frame, int neighbourhoodSize = 1, float threshold = 20.0f);
