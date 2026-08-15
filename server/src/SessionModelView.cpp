#include "SessionModelView.h"
#include "VoxelGrid.h"
#include "MarchingCubes.h"
#include "MeshFrame.h"

#include <chrono>

SessionModelView::SessionModelView(int voxelRes)
    : voxelRes_(voxelRes)
{}

// ── ISessionModel ─────────────────────────────────────────────────────────────

void SessionModelView::updateCloud(const std::string& sensorId, PointCloud cloud) {
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

int SessionModelView::activeEffects() const {
    return effects_.activeCount();
}

// ── Gesture / Effects ─────────────────────────────────────────────────────────

void SessionModelView::addGestureDetector(std::unique_ptr<IGestureDetector> d) {
    detectors_.push_back(std::move(d));
}

void SessionModelView::registerEffect(GestureType type, EffectGenerator::Factory factory) {
    effects_.registerFactory(type, std::move(factory));
}

// ── ISessionView (the socket) ─────────────────────────────────────────────────

std::vector<uint8_t> SessionModelView::buildFrame() {
    // 1. Snapshot sensor state
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

    // 2. Voxelise + Marching Cubes
    VoxelGrid grid(voxelRes_, voxelRes_, voxelRes_);
    grid.fill(clouds, transforms);
    Mesh mesh = marchingCubes(grid, 0.5f);
    if (mesh.vertices.empty()) return {};

    // 3. Gesture detection — run all detectors on the current mesh
    if (!detectors_.empty()) {
        uint64_t nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

        std::vector<GestureEvent> events;
        for (auto& det : detectors_) {
            if (!det->enabled) continue;
            auto ev = det->detect(mesh, nowMs);
            events.insert(events.end(), ev.begin(), ev.end());
        }

        // 4. Spawn new effects from events
        effects_.onGestures(events);
    }
    effects_.update(1000.0f / 30.0f); // ~33 ms per tick at 30 fps

    // 5. Merge effect points into the mesh as extra vertices (normal = 0,1,0)
    auto effectPts = effects_.collect();
    for (const auto& ep : effectPts) {
        Vertex v{};
        v.x = ep.position[0];
        v.y = ep.position[1];
        v.z = ep.position[2];
        v.ny = 1.0f; // upward normal as placeholder
        mesh.vertices.push_back(v);
        // Note: effect points are unindexed — they appear as loose vertices.
        // Preview clients render them as a point cloud overlay.
    }

    // 6. Encode
    return encodeMesh(mesh);
}
