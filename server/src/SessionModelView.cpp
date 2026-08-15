#include "SessionModelView.h"
#include "VoxelGrid.h"
#include "MarchingCubes.h"
#include "MeshFrame.h"

SessionModelView::SessionModelView(int voxelRes)
    : voxelRes_(voxelRes)
{}

// ── ISessionModel ─────────────────────────────────────────────────────────────

void SessionModelView::updateCloud(const std::string& sensorId, PointCloud cloud) {
    // Caller (WebSocket handler in main.cpp) already validated sensorId length
    // and point count against Limits before calling here.
    std::lock_guard<std::mutex> lock(mu_);
    sensors_[sensorId].cloud = std::move(cloud);
}

void SessionModelView::setTransform(const std::string& sensorId,
                                     const std::array<float, 16>& m) {
    std::lock_guard<std::mutex> lock(mu_);
    sensors_[sensorId].transform = m;
}

std::vector<SessionModelView::SensorInfo> SessionModelView::sensorStats() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<SensorInfo> out;
    out.reserve(sensors_.size());

    // Identity matrix for calibration check
    static constexpr std::array<float,16> kIdentity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    for (const auto& [id, s] : sensors_) {
        SensorInfo info;
        info.id         = id;
        info.pointCount = s.cloud.size();
        info.calibrated = (s.transform != kIdentity);
        out.push_back(std::move(info));
    }
    return out;
}

// ── ISessionView (the socket) ─────────────────────────────────────────────────

std::vector<uint8_t> SessionModelView::buildFrame() {
    // --- 1. Snapshot sensor state (minimal lock time) ---
    std::vector<PointCloud>           clouds;
    std::vector<std::array<float,16>> transforms;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (sensors_.empty()) return {};
        clouds.reserve(sensors_.size());
        transforms.reserve(sensors_.size());
        for (const auto& [id, s] : sensors_) {
            if (s.cloud.empty()) continue;
            clouds.push_back(s.cloud);
            transforms.push_back(s.transform);
        }
    }

    if (clouds.empty()) return {};

    // --- 2. Voxelise ---
    VoxelGrid grid(voxelRes_, voxelRes_, voxelRes_);
    grid.fill(clouds, transforms);

    // --- 3. Marching Cubes ---
    Mesh mesh = marchingCubes(grid, 0.5f);
    if (mesh.vertices.empty()) return {};

    // --- 4. Encode to MESH binary frame ---
    return encodeMesh(mesh);
}
