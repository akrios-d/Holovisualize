#include "VoxelGrid.h"
#include <algorithm>
#include <cmath>

namespace {

// Applies a row-major 4x4 rigid transform.
void applyTransform(const std::array<float,16>& m,
                     float ix, float iy, float iz,
                     float& ox, float& oy, float& oz) {
    ox = m[0]*ix + m[1]*iy + m[2]*iz  + m[3];
    oy = m[4]*ix + m[5]*iy + m[6]*iz  + m[7];
    oz = m[8]*ix + m[9]*iy + m[10]*iz + m[11];
}

// Inverts a rigid (rotation + translation) camera-to-world transform to get
// world-to-camera. Calibration transforms are always rigid (see
// Calibration.cpp's solvePnP), so R^T is a valid inverse of the rotation —
// cheaper and more numerically stable than a general 4x4 inverse.
void applyInverseTransform(const std::array<float,16>& m,
                            float ix, float iy, float iz,
                            float& ox, float& oy, float& oz) {
    const float tx = ix - m[3], ty = iy - m[7], tz = iz - m[11];
    ox = m[0]*tx + m[4]*ty + m[8]*tz;
    oy = m[1]*tx + m[5]*ty + m[9]*tz;
    oz = m[2]*tx + m[6]*ty + m[10]*tz;
}

// Coarse synthetic depth+colour image, reprojected from a point cloud via
// angular (yaw/pitch) binning instead of the sensor's real pixel grid
// (which the server never receives — only the already-projected cloud).
// Keeping the *nearest* point per bin reproduces what a real depth camera
// would see: the closest surface along each ray occludes what's behind it.
struct AngularImage {
    static constexpr int kBinsU = 128, kBinsV = 128;
    // Kinect v2 depth FOV is ~70.6° x 60° — a little headroom over the
    // real half-angles (35.3°/30°) so real points never get clamped away,
    // without wasting so much bin resolution on empty margin that the
    // actual subject ends up angularly coarser than the bin count implies.
    static constexpr float kHalfFovU = 38.f * 3.14159265f / 180.f;
    static constexpr float kHalfFovV = 33.f * 3.14159265f / 180.f;

    struct Bin { float minZ = 1e9f; std::array<uint8_t,3> color{0,0,0}; bool hasData = false; };
    std::array<Bin, kBinsU * kBinsV> bins;

    static bool angleToBin(float u, float v, int& bi, int& bj) {
        if (u < -kHalfFovU || u > kHalfFovU || v < -kHalfFovV || v > kHalfFovV) return false;
        bi = static_cast<int>((u + kHalfFovU) / (2*kHalfFovU) * kBinsU);
        bj = static_cast<int>((v + kHalfFovV) / (2*kHalfFovV) * kBinsV);
        bi = std::clamp(bi, 0, kBinsU - 1);
        bj = std::clamp(bj, 0, kBinsV - 1);
        return true;
    }

    void build(const PointCloud& cloud) {
        for (const auto& p : cloud) {
            if (p.z <= 0.001f) continue; // behind/at the camera — invalid
            float u = std::atan2(p.x, p.z);
            float v = std::atan2(p.y, p.z);
            int bi, bj;
            if (!angleToBin(u, v, bi, bj)) continue;
            Bin& b = bins[bj * kBinsU + bi];
            if (p.z < b.minZ) {
                b.minZ = p.z;
                b.color = {p.r, p.g, p.b};
                b.hasData = true;
            }
        }
    }

    // Looks up the bin a camera-space point (cx,cy,cz) projects to.
    // Returns nullptr if out of FOV or the bin never saw a point.
    const Bin* lookup(float cx, float cy, float cz) const {
        if (cz <= 0.001f) return nullptr;
        float u = std::atan2(cx, cz);
        float v = std::atan2(cy, cz);
        int bi, bj;
        if (!angleToBin(u, v, bi, bj)) return nullptr;
        const Bin& b = bins[bj * kBinsU + bi];
        return b.hasData ? &b : nullptr;
    }
};

} // namespace

void VoxelGrid::fill(const std::vector<PointCloud>& clouds,
                     const std::vector<std::array<float,16>>& transforms)
{
    std::fill(data_.begin(), data_.end(), kTrunc);
    std::fill(color_.begin(), color_.end(), std::array<uint8_t,3>{0,0,0});

    // --- 1. Fixed-size volume centred on the combined centroid ---
    // Fixed size (not fit to point extents) so a single stray/outlier point
    // can't blow up the box and coarsen every voxel — it just falls outside
    // the box and is ignored, which is the outlier rejection we want.
    double sx = 0, sy = 0, sz = 0;
    size_t totalPoints = 0;
    for (size_t s = 0; s < clouds.size(); ++s) {
        const auto& m = transforms[s];
        for (const auto& pt : clouds[s]) {
            float wx, wy, wz;
            applyTransform(m, pt.x, pt.y, pt.z, wx, wy, wz);
            sx += wx; sy += wy; sz += wz;
        }
        totalPoints += clouds[s].size();
    }
    if (totalPoints == 0) return; // grid already reset to kTrunc (all-empty)

    const float centreX = static_cast<float>(sx / totalPoints);
    const float centreY = static_cast<float>(sy / totalPoints);
    const float centreZ = static_cast<float>(sz / totalPoints);

    constexpr float kHalfX = 1.0f, kHalfY = 1.2f, kHalfZ = 1.0f; // metres — fits one standing/gesturing person
    minX_ = centreX - kHalfX; maxX_ = centreX + kHalfX;
    minY_ = centreY - kHalfY; maxY_ = centreY + kHalfY;
    minZ_ = centreZ - kHalfZ; maxZ_ = centreZ + kHalfZ;

    // --- 2. Build each sensor's synthetic depth+colour image ---
    std::vector<AngularImage> images(clouds.size());
    for (size_t s = 0; s < clouds.size(); ++s) images[s].build(clouds[s]);

    // --- 3. Per-voxel signed distance — nearest-|sdf| sensor wins ---
    for (int k = 0; k < resZ_; ++k) {
        const float wz = worldZ(k);
        for (int j = 0; j < resY_; ++j) {
            const float wy = worldY(j);
            for (int i = 0; i < resX_; ++i) {
                const float wx = worldX(i);

                float bestSdf = kTrunc;
                std::array<uint8_t,3> bestColor{0,0,0};
                bool found = false;

                for (size_t s = 0; s < clouds.size(); ++s) {
                    float cx, cy, cz;
                    applyInverseTransform(transforms[s], wx, wy, wz, cx, cy, cz);
                    const auto* bin = images[s].lookup(cx, cy, cz);
                    if (!bin) continue;

                    // Standard TSDF convention: positive in front of the
                    // observed surface (empty space), negative behind it.
                    // Only clamp the *positive* side (far in front = safely
                    // empty, the camera would have seen anything closer).
                    // A voxel far *behind* the observed surface is not
                    // "confidently solid" — the camera has no information
                    // back there at all (occluded) — so treat it the same
                    // as a truly-unobserved bin (`continue`, leaving the
                    // kTrunc "empty/unknown" sentinel) instead of pinning
                    // it to -kTrunc. Without this, every voxel behind a
                    // person forms one uniform "solid" block all the way to
                    // the fixed volume's back wall, and Marching Cubes
                    // finds a false surface wherever that block happens to
                    // border an unrelated unobserved bin — the "fan of
                    // triangles" artifact this fixes.
                    float raw = bin->minZ - cz;
                    if (raw < -kTrunc) continue;
                    float sdf = std::min(raw, kTrunc);
                    if (!found || std::abs(sdf) < std::abs(bestSdf)) {
                        bestSdf = sdf;
                        bestColor = bin->color;
                        found = true;
                    }
                }

                if (found) {
                    data_[idx(i,j,k)]  = bestSdf;
                    color_[idx(i,j,k)] = bestColor;
                }
            }
        }
    }
}
