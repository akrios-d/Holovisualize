#pragma once

#include "Frame.h"
#include <vector>
#include <array>
#include <limits>
#include <cmath>

// 3-D occupancy grid used as input to Marching Cubes.
//
// Voxels are stored in Z-major order: index(i,j,k) = k*resY*resX + j*resX + i
// Values range [0,1]: 0 = empty, 1 = fully occupied.

class VoxelGrid {
public:
    // resolution: voxels per side (default 128³ balances quality and latency)
    explicit VoxelGrid(int resX = 128, int resY = 128, int resZ = 128)
        : resX_(resX), resY_(resY), resZ_(resZ)
        , data_(static_cast<size_t>(resX) * resY * resZ, 0.f)
    {}

    // Fills the grid from one or more transformed point clouds.
    // transforms: row-major 4×4 camera-to-world matrices (identity if uncalibrated).
    void fill(const std::vector<PointCloud>& clouds,
              const std::vector<std::array<float,16>>& transforms);

    // Value at voxel (i,j,k) in [0,1].
    float value(int i, int j, int k) const {
        if (i < 0 || j < 0 || k < 0 || i >= resX_ || j >= resY_ || k >= resZ_)
            return 0.f;
        return data_[idx(i,j,k)];
    }

    int resX() const { return resX_; }
    int resY() const { return resY_; }
    int resZ() const { return resZ_; }

    // World-space bounds.
    float minX() const { return minX_; } float maxX() const { return maxX_; }
    float minY() const { return minY_; } float maxY() const { return maxY_; }
    float minZ() const { return minZ_; } float maxZ() const { return maxZ_; }

    // Convert voxel indices to world position (voxel centre).
    float worldX(int i) const { return minX_ + (i + 0.5f) * voxelSizeX(); }
    float worldY(int j) const { return minY_ + (j + 0.5f) * voxelSizeY(); }
    float worldZ(int k) const { return minZ_ + (k + 0.5f) * voxelSizeZ(); }

    float voxelSizeX() const { return (maxX_ - minX_) / resX_; }
    float voxelSizeY() const { return (maxY_ - minY_) / resY_; }
    float voxelSizeZ() const { return (maxZ_ - minZ_) / resZ_; }

private:
    int resX_, resY_, resZ_;
    std::vector<float> data_;

    // World bounds (set during fill).
    float minX_ = -1.f, maxX_ = 1.f;
    float minY_ = -1.f, maxY_ = 2.f;
    float minZ_ =  0.f, maxZ_ = 3.f;

    size_t idx(int i, int j, int k) const {
        return static_cast<size_t>(k) * resY_ * resX_
             + static_cast<size_t>(j) * resX_
             + static_cast<size_t>(i);
    }

    // Transform a point with a 4×4 row-major matrix.
    static void transform(const std::array<float,16>& m,
                          float ix, float iy, float iz,
                          float& ox, float& oy, float& oz) {
        ox = m[0]*ix + m[1]*iy + m[2]*iz  + m[3];
        oy = m[4]*ix + m[5]*iy + m[6]*iz  + m[7];
        oz = m[8]*ix + m[9]*iy + m[10]*iz + m[11];
    }
};
