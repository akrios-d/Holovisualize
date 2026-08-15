#pragma once

#include <string>
#include <vector>

// SessionModelView — data side of the Socket Pattern
//
// Implements ISessionModel (producer-facing port) and
// ISessionView (the shared socket). Owns sensor state and mesh generation.
// Never knows how the mesh is broadcast or how many consumers exist.

#include "ISession.h"
#include <mutex>
#include <string>
#include <unordered_map>

class SessionModelView final : public ISessionModel, public ISessionView {
public:
    explicit SessionModelView(int voxelRes = 128);

    // ── ISessionModel ─────────────────────────────────────────────────────────
    void updateCloud(const std::string& sensorId, PointCloud cloud) override;
    void setTransform(const std::string& sensorId,
                      const std::array<float, 16>& m) override;

    // ── ISessionView (the socket) ─────────────────────────────────────────────
    std::vector<uint8_t> buildFrame() override;

    // ── Stats (read-only, for dashboard) ─────────────────────────────────────
    struct SensorInfo {
        std::string id;
        size_t      pointCount  = 0;
        bool        calibrated  = false; // has non-identity transform
    };
    std::vector<SensorInfo> sensorStats() const;

private:
    struct SensorState {
        PointCloud            cloud;
        std::array<float, 16> transform{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // identity
    };

    int voxelRes_;

    mutable std::mutex mu_;
    std::unordered_map<std::string, SensorState> sensors_;
};
