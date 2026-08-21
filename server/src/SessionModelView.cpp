#include "SessionModelView.h"
#include "VoxelGrid.h"
#include "MarchingCubes.h"
#include "MeshFrame.h"

#include <chrono>
#include <cmath>

SessionModelView::SessionModelView(int voxelRes)
    : voxelRes_(voxelRes)
    , voxelGrid_(voxelRes, voxelRes, voxelRes)
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

void SessionModelView::pushGestureEvent(const std::string& sensorId, GestureEvent ev) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sensors_.find(sensorId);
    if (it != sensors_.end()) {
        // Same row-major 4x4 transform used for point cloud vertices in
        // buildFrame() — camera space to world space.
        const auto& m = it->second.transform;
        const float x = ev.position[0], y = ev.position[1], z = ev.position[2];
        ev.position[0] = m[0]*x + m[1]*y + m[2]*z  + m[3];
        ev.position[1] = m[4]*x + m[5]*y + m[6]*z  + m[7];
        ev.position[2] = m[8]*x + m[9]*y + m[10]*z + m[11];
    }
    ev.timestampMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    externalEvents_.push_back(ev);
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

    // Gamma-lift shadows: the Kinect's colour sensor underexposes anything
    // not front-lit (common with overhead-only room lighting), so subjects
    // render as near-black silhouettes. This doesn't fix real exposure,
    // just brightens dark tones for display without blowing out already-
    // bright ones. Shared by both paths below.
    constexpr float kShadowGamma = 0.55f;

    Mesh mesh;

    if (meshMode_) {
        // Closed-surface reconstruction — see VoxelGrid.h for why this is a
        // TSDF (signed distance), not the density-grid approach that used
        // to live here (collapsed under near-field outlier points).
        voxelGrid_.fill(clouds, transforms);
        mesh = marchingCubes(voxelGrid_);
        for (auto& v : mesh.vertices) {
            v.nx = std::pow(v.nx, kShadowGamma);
            v.ny = std::pow(v.ny, kShadowGamma);
            v.nz = std::pow(v.nz, kShadowGamma);
        }
    } else {
        // Point cloud passthrough — transformed points go straight out as
        // loose vertices, no triangles.
        size_t totalPoints = 0;
        for (const auto& c : clouds) totalPoints += c.size();
        mesh.vertices.reserve(totalPoints);

        for (size_t s = 0; s < clouds.size(); ++s) {
            const auto& m = transforms[s];
            for (const auto& pt : clouds[s]) {
                Vertex v{};
                v.x = m[0]*pt.x + m[1]*pt.y + m[2]*pt.z  + m[3];
                v.y = m[4]*pt.x + m[5]*pt.y + m[6]*pt.z  + m[7];
                v.z = m[8]*pt.x + m[9]*pt.y + m[10]*pt.z + m[11];
                // Raw points have no real normal — repurpose those 3 floats
                // to carry RGB (0..1) instead, so the wire format doesn't
                // need to change to add colour.
                v.nx = std::pow(pt.r / 255.0f, kShadowGamma);
                v.ny = std::pow(pt.g / 255.0f, kShadowGamma);
                v.nz = std::pow(pt.b / 255.0f, kShadowGamma);
                mesh.vertices.push_back(v);
            }
        }
    }
    if (mesh.vertices.empty()) return {};

    // 3. Gesture detection — run all mesh-based detectors on the current mesh
    uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    std::vector<GestureEvent> events;
    for (auto& det : detectors_) {
        if (!det->enabled) continue;
        auto ev = det->detect(mesh, nowMs);
        events.insert(events.end(), ev.begin(), ev.end());
    }

    // Merge in externally-recognised events (e.g. MediaPipe sidecar via
    // pushGestureEvent) — already world-space, queued since the last tick.
    {
        std::lock_guard<std::mutex> lock(mu_);
        events.insert(events.end(), externalEvents_.begin(), externalEvents_.end());
        externalEvents_.clear();
    }

    // 4. Spawn new effects from events
    if (!events.empty()) effects_.onGestures(events);
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
