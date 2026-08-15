#pragma once

#include "Frame.h"
#include "MeshFrame.h"
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// One holographic session: N producers feeding point clouds → server merges,
// generates a mesh, and broadcasts to M consumers at 30 fps.
//
// All public methods are thread-safe.
class Session {
public:
    // Called by main on each 30fps tick. Merges clouds, runs Marching Cubes,
    // and invokes broadcastFn with the encoded MESH binary frame.
    using BroadcastFn = std::function<void(const std::string& sessionKey,
                                           const std::vector<uint8_t>& frame)>;

    explicit Session(std::string key, BroadcastFn broadcastFn,
                     int voxelRes = 128);

    // --- Producer management ---
    void updateCloud(const std::string& sensorId, PointCloud cloud);
    void setTransform(const std::string& sensorId,
                      const std::array<float, 16>& m);

    // --- Processing (called from 30fps timer thread) ---
    void process();

    // Key used for the broadcast callback.
    const std::string& key() const { return key_; }

private:
    struct SensorState {
        PointCloud               cloud;
        std::array<float, 16>    transform;
        bool                     hasTransform = false;
    };

    std::string  key_;
    BroadcastFn  broadcastFn_;
    int          voxelRes_;

    mutable std::mutex                           mu_;
    std::unordered_map<std::string, SensorState> sensors_;

    // Identity matrix (default transform for uncalibrated sensors).
    static constexpr std::array<float,16> kIdentity = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
};
