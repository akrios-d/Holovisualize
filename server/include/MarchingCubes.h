#pragma once

#include "VoxelGrid.h"
#include "MeshFrame.h"

// Extracts an isosurface from `grid`'s signed distance field at the given
// isoLevel (0 = the zero crossing, i.e. the actual observed surface) using
// the classic Marching Cubes algorithm (Lorensen & Cline, 1987).
//
// No current viewer does real lighting/shading (they render unlit points/
// vertices), so — matching SessionModelView's point-passthrough mode and
// keeping the wire format unchanged — output vertices carry colour
// (interpolated from the grid's per-voxel colour) in the nx/ny/nz slot
// instead of a true surface normal.
Mesh marchingCubes(const VoxelGrid& grid, float isoLevel = 0.0f);
