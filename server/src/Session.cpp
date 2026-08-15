#include "Session.h"
#include "VoxelGrid.h"
#include "MarchingCubes.h"
#include <algorithm>
#include <iostream>

Session::Session(std::string key, BroadcastFn broadcastFn, int voxelRes)
    : key_(std::move(key))
    , broadcastFn_(std::move(broadcastFn))
    , voxelRes_(voxelRes)
{}

void Session::updateCloud(const std::string& sensorId, PointCloud cloud) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& s = sensors_[sensorId];
    s.cloud = std::move(cloud);
}

void Session::setTransform(const std::string& sensorId,
                            const std::array<float, 16>& m) {
    std::lock_guard<std::mutex> lock(mu_);
    auto& s = sensors_[sensorId];
    s.transform    = m;
    s.hasTransform = true;
    std::cout << "[" << key_ << "] calibration stored for sensor " << sensorId << "\n";
}

void Session::process() {
    // --- Snapshot sensor state (minimal lock time) ---
    std::vector<PointCloud>           clouds;
    std::vector<std::array<float,16>> transforms;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (sensors_.empty()) return;

        clouds.reserve(sensors_.size());
        transforms.reserve(sensors_.size());
        for (const auto& [id, s] : sensors_) {
            if (s.cloud.empty()) continue;
            clouds.push_back(s.cloud);
            transforms.push_back(s.hasTransform ? s.transform : kIdentity);
        }
    }

    if (clouds.empty()) return;

    // --- Voxelise ---
    VoxelGrid grid(voxelRes_, voxelRes_, voxelRes_);
    grid.fill(clouds, transforms);

    // --- Marching Cubes ---
    Mesh mesh = marchingCubes(grid, 0.5f);
    if (mesh.vertices.empty()) return;

    // --- Encode & broadcast ---
    auto frame = encodeMesh(mesh);
    broadcastFn_(key_, frame);
}
