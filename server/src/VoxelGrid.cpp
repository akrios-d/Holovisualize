#include "VoxelGrid.h"
#include <algorithm>
#include <cmath>

void VoxelGrid::fill(const std::vector<PointCloud>& clouds,
                     const std::vector<std::array<float,16>>& transforms)
{
    // --- 1. Compute world-space bounds from all transformed points ---
    float wxMin =  std::numeric_limits<float>::max();
    float wyMin =  std::numeric_limits<float>::max();
    float wzMin =  std::numeric_limits<float>::max();
    float wxMax = -std::numeric_limits<float>::max();
    float wyMax = -std::numeric_limits<float>::max();
    float wzMax = -std::numeric_limits<float>::max();

    size_t totalPoints = 0;
    for (size_t s = 0; s < clouds.size(); ++s) {
        const auto& cloud = clouds[s];
        const auto& m     = transforms[s];
        totalPoints += cloud.size();
        for (const auto& pt : cloud) {
            float wx, wy, wz;
            transform(m, pt.x, pt.y, pt.z, wx, wy, wz);
            wxMin = std::min(wxMin, wx); wxMax = std::max(wxMax, wx);
            wyMin = std::min(wyMin, wy); wyMax = std::max(wyMax, wy);
            wzMin = std::min(wzMin, wz); wzMax = std::max(wzMax, wz);
        }
    }

    if (totalPoints == 0) {
        std::fill(data_.begin(), data_.end(), 0.f);
        return;
    }

    // Add padding so the isosurface has room at the edges.
    const float padX = (wxMax - wxMin) * 0.05f + 0.05f;
    const float padY = (wyMax - wyMin) * 0.05f + 0.05f;
    const float padZ = (wzMax - wzMin) * 0.05f + 0.05f;
    minX_ = wxMin - padX; maxX_ = wxMax + padX;
    minY_ = wyMin - padY; maxY_ = wyMax + padY;
    minZ_ = wzMin - padZ; maxZ_ = wzMax + padZ;

    // --- 2. Splat each point into the grid with a 3-voxel Gaussian kernel ---
    std::fill(data_.begin(), data_.end(), 0.f);

    const float vx = voxelSizeX();
    const float vy = voxelSizeY();
    const float vz = voxelSizeZ();

    // Sigma = 1 voxel; splat radius = 2 voxels.
    const int R = 2;
    const float sigma2 = 1.0f; // in voxel units

    for (size_t s = 0; s < clouds.size(); ++s) {
        const auto& cloud = clouds[s];
        const auto& m     = transforms[s];

        for (const auto& pt : cloud) {
            float wx, wy, wz;
            transform(m, pt.x, pt.y, pt.z, wx, wy, wz);

            // Centre voxel
            int ci = static_cast<int>((wx - minX_) / vx);
            int cj = static_cast<int>((wy - minY_) / vy);
            int ck = static_cast<int>((wz - minZ_) / vz);

            for (int dk = -R; dk <= R; ++dk) {
                int k = ck + dk;
                if (k < 0 || k >= resZ_) continue;
                float dz2 = (float)(dk * dk);

                for (int dj = -R; dj <= R; ++dj) {
                    int j = cj + dj;
                    if (j < 0 || j >= resY_) continue;
                    float dy2 = (float)(dj * dj);

                    for (int di = -R; di <= R; ++di) {
                        int i = ci + di;
                        if (i < 0 || i >= resX_) continue;
                        float dx2 = (float)(di * di);

                        float d2 = dx2 + dy2 + dz2;
                        float w  = std::exp(-d2 / (2.f * sigma2));
                        data_[idx(i, j, k)] += w;
                    }
                }
            }
        }
    }

    // --- 3. Normalise to [0,1] ---
    float peak = *std::max_element(data_.begin(), data_.end());
    if (peak > 0.f) {
        float inv = 1.f / peak;
        for (auto& v : data_) v *= inv;
    }
}
