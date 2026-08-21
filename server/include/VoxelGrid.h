#pragma once

#include "Frame.h"
#include <vector>
#include <array>
#include <limits>
#include <cmath>
#include <cstdint>

// 3-D truncated signed distance field (TSDF) used as input to Marching Cubes.
//
// Each voxel holds the signed distance from that point to the nearest
// observed surface, truncated to ±truncDist(). Positive = in front of the
// surface (empty space the camera can see through), negative = behind it
// (inside the object, trusted only near the zero crossing). The isosurface
// sits at the zero crossing, extracted with isoLevel=0 in MarchingCubes.
//
// Why TSDF and not an occupancy/density grid (this file's previous
// approach): a density grid needs normalising by its own peak value, and a
// single stray near-field point (common Kinect multipath/reflection noise)
// can dominate that peak and collapse the whole reconstruction. A signed
// distance is a physical quantity in metres — no normalisation step, so one
// bad point only pollutes the handful of voxels along its own ray.
//
// This also has no temporal fusion across frames (unlike Microsoft's
// KinectFusion, which assumes a moving camera over a static scene) — our
// camera is fixed and the *subject* moves, so averaging across frames would
// smear a moving person. Each frame's TSDF is computed fresh. See
// server/README.md's Marching Cubes section for the fuller writeup
// (KinectFusion vs. Holoportation vs. this single-camera fallback).
//
// Since capture only sends the projected point cloud (not the raw depth
// image + intrinsics), the "measured depth" this TSDF compares voxels
// against is reconstructed by re-projecting the cloud into an angular
// (yaw/pitch) bin grid per sensor and keeping the nearest point per bin —
// effectively rebuilding a coarse synthetic depth image. Each bin also
// keeps its point's colour, which MarchingCubes reuses for output vertex
// colour (bins are looked up again per-voxel — no separate 3-D colour
// grid needed).
class VoxelGrid {
public:
    // resolution: voxels per side. The grid now covers a small, fixed-size
    // volume (see fill()) rather than the whole scene, so this can stay
    // fairly modest — 96-128 is plenty for a single person at a few metres.
    explicit VoxelGrid(int resX = 128, int resY = 128, int resZ = 128)
        : resX_(resX), resY_(resY), resZ_(resZ)
        , data_(static_cast<size_t>(resX) * resY * resZ, kTrunc)
        , color_(static_cast<size_t>(resX) * resY * resZ, {0, 0, 0})
    {}

    // Fills the grid from one or more sensors' point clouds (camera space —
    // NOT yet transformed to world) plus their camera-to-world transforms.
    // The volume is a fixed-size box centred on the clouds' combined
    // centroid (in world space) each call — bounded regardless of any
    // single outlier point, unlike computing bounds from point extents.
    void fill(const std::vector<PointCloud>& clouds,
              const std::vector<std::array<float,16>>& transforms);

    // Signed distance at voxel (i,j,k), truncated to ±truncDist(). Out of
    // range returns +truncDist() (definitely-empty sentinel) rather than 0,
    // so grid edges never falsely read as "near the surface".
    float value(int i, int j, int k) const {
        if (i < 0 || j < 0 || k < 0 || i >= resX_ || j >= resY_ || k >= resZ_)
            return kTrunc;
        return data_[idx(i, j, k)];
    }

    std::array<uint8_t, 3> color(int i, int j, int k) const {
        if (i < 0 || j < 0 || k < 0 || i >= resX_ || j >= resY_ || k >= resZ_)
            return {0, 0, 0};
        return color_[idx(i, j, k)];
    }

    static constexpr float kTrunc = 0.05f; // truncation distance, metres

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
    std::vector<std::array<uint8_t, 3>> color_;

    // World bounds (set during fill — a fixed-size box around the centroid).
    float minX_ = -1.f, maxX_ = 1.f;
    float minY_ = -1.f, maxY_ = 2.f;
    float minZ_ =  0.f, maxZ_ = 3.f;

    size_t idx(int i, int j, int k) const {
        return static_cast<size_t>(k) * resY_ * resX_
             + static_cast<size_t>(j) * resX_
             + static_cast<size_t>(i);
    }
};
