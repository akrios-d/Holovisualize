#pragma once

#include "VoxelGrid.h"
#include "MeshFrame.h"

// Extracts an isosurface from `grid` at the given isoLevel using
// the classic Marching Cubes algorithm (Lorensen & Cline, 1987).
//
// Returns a triangle mesh with per-vertex normals computed from the
// scalar field gradient (central finite differences).
Mesh marchingCubes(const VoxelGrid& grid, float isoLevel = 0.5f);
