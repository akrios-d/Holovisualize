#pragma once

// ─── Socket Pattern — server session layer ────────────────────────────────────
//
// Every session boundary is defined by an explicit interface contract.
// Three contracts, two implementation classes:
//
//   ISessionModel ──┐
//                    ├──► SessionModelView   (sensor data + mesh generation)
//   ISessionView  ──┤
//                    │
//   ISessionView  ──┤ (held by reference)
//                    ├──► SessionViewController (KCP consumers + broadcast)
//   ISessionController ──┘
//
// ISessionView is the socket — the only bridge between the data side and the
// consumer side. Neither implementation knows the other's concrete type.

#include "Frame.h"
#include "GestureEvent.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─── Security limits (applied at all boundaries) ──────────────────────────────

namespace Limits {
    inline constexpr size_t   kMaxSessionKeyLen  = 63;     // bytes, ASCII
    inline constexpr size_t   kMaxSensorIdLen    = 63;
    inline constexpr uint32_t kMaxPointsPerFrame = 300'000; // ~5 MB HOLO frame
    inline constexpr int      kMaxSensorsPerSession  = 16;
    inline constexpr int      kMaxConsumersPerSession = 32;
    inline constexpr int      kMaxWaitSndPackets = 512;    // KCP stale consumer threshold
    inline constexpr int      kConsumerTimeoutSec = 30;    // remove if queue stalls this long
}

// ─── ISessionModel — producer-facing port ─────────────────────────────────────
// What capture clients call. Validated at the WebSocket boundary before use.

class ISessionModel {
public:
    virtual ~ISessionModel() = default;

    // Update the latest point cloud for a sensor.
    // Caller must have validated sensorId length and cloud size.
    virtual void updateCloud(const std::string& sensorId, PointCloud cloud) = 0;

    // Store a camera-to-world calibration transform for a sensor.
    virtual void setTransform(const std::string& sensorId,
                              const std::array<float, 16>& m) = 0;

    // Report an externally-recognised gesture (e.g. from the capture
    // client's MediaPipe sidecar — see capture/gesture_sidecar). `ev`'s
    // position is in the sensor's own camera space; the model transforms it
    // to world space using the sensor's calibration before merging it into
    // the same effect pipeline server-side IGestureDetectors feed.
    virtual void pushGestureEvent(const std::string& sensorId, GestureEvent ev) = 0;
};

// ─── ISessionView — the shared socket / bridge ────────────────────────────────
// SessionModelView implements this to produce a mesh frame.
// SessionViewController holds a reference to this to read the frame.

class ISessionView {
public:
    virtual ~ISessionView() = default;

    // Generate, encode, and return the current MESH binary frame.
    // Returns an empty vector if no sensor data is available.
    // Must be called only from the 30fps timer thread.
    virtual std::vector<uint8_t> buildFrame() = 0;
};

// ─── ISessionController — consumer-facing port ────────────────────────────────
// What the 30fps timer and the KCP server call.

struct sockaddr_in;  // forward declaration — callers include socket headers

class ISessionController {
public:
    virtual ~ISessionController() = default;

    // Called at 30fps: read a frame from ISessionView, broadcast to consumers.
    virtual void tick() = 0;

    // Register a new KCP consumer. conv must be unique within this session.
    // addr is the consumer's UDP endpoint. udpFd is the shared server socket.
    virtual void addConsumer(uint32_t conv,
                             const sockaddr_in& addr,
                             int udpFd) = 0;

    // Deregister a consumer (graceful disconnect or stale detection).
    virtual void removeConsumer(uint32_t conv) = 0;

    // Number of currently active consumers.
    virtual int consumerCount() const = 0;

    // Register/deregister a browser dashboard viewer — receives the same
    // MESH binary frame as KCP consumers, but pushed over its WebSocket
    // connection instead. id must be unique per viewer (e.g. the ws
    // connection id); sender may be called from the tick() thread.
    virtual void addWsViewer(const std::string& id,
                             std::function<void(const std::vector<uint8_t>&)> sender) = 0;
    virtual void removeWsViewer(const std::string& id) = 0;

    // Stats for the dashboard.
    virtual uint64_t frameCount() const = 0;
    virtual float    outFps()     const = 0;
};
