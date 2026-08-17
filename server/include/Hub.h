#pragma once

// Hub — session registry and shared UDP server for KCP consumers.
//
// Owns:
//   - a map of session_key → (SessionModelView, SessionViewController)
//   - a single UDP socket on kKcpPort shared by all sessions
//   - a background thread that reads incoming UDP and routes KCP ACKs
//
// The Hub is the only place that does network I/O for the KCP path.

#include "ISession.h"
#include "SessionModelView.h"
#include "SessionViewController.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  define closesocket close
#endif

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class Hub {
public:
    static constexpr int kKcpPort = 8081;

    explicit Hub(int voxelRes = 128);
    ~Hub();

    // ── Producer interface (called from WebSocket handlers) ──────────────────
    ISessionModel& producerPort(const std::string& sessionKey);
    void setTransform(const std::string& sessionKey,
                      const std::string& sensorId,
                      const std::array<float, 16>& m);

    // ── Timer interface (called from 30fps processing loop) ──────────────────
    void tick(); // process all sessions

    // ── Browser dashboard viewers (WebSocket, not KCP) ────────────────────────
    void addWsViewer(const std::string& sessionKey, const std::string& id,
                     std::function<void(const std::vector<uint8_t>&)> sender);
    void removeWsViewer(const std::string& sessionKey, const std::string& id);

    // ── Stats (for dashboard) ────────────────────────────────────────────────
    struct SessionStats {
        std::string                           key;
        std::vector<SessionModelView::SensorInfo> sensors;
        int      consumers   = 0;
        uint64_t frameCount  = 0;
        float    fps         = 0.f;
    };
    struct BoundBox {
        float minX=-100, maxX=100, minY=-100, maxY=100, minZ=-100, maxZ=100;
    };
    struct HubStats {
        int                      wsPort    = 0;
        int                      kcpPort   = 0;
        int                      voxelRes  = 0;
        float                    pointSize = 0.f;
        BoundBox                 bounds;
        uint64_t                 uptimeS   = 0;
        std::vector<SessionStats> sessions;
    };
    HubStats getStats() const;

    int totalConsumers() const;
    void setWsPort(int p) { wsPort_ = p; }

    // Point size (metres) that viewers (dashboard, AR) should render with —
    // server-authoritative so every viewer stays in sync from one setting
    // instead of each guessing its own.
    void  setPointSize(float p) { pointSize_ = p; }
    float pointSize() const { return pointSize_; }

    // Same idea for the visual bound-box clip — server-authoritative, shared
    // by every viewer of a session.
    void      setBoundBox(const BoundBox& b) { bounds_ = b; }
    BoundBox  boundBox() const { return bounds_; }

    // UDP socket fd — exposed so SessionViewController::KcpConsumer can use it.
    int udpFd() const { return udpFd_; }

    // Called once on every newly created session (Socket Pattern hook).
    // Use this to register gesture detectors and effect factories.
    // Example:
    //   hub.onSessionCreated([](SessionModelView& mv) {
    //       mv.registerEffect(GestureType::PalmUp,
    //           [] { return std::make_unique<FireEffect>(); });
    //   });
    using SessionConfigurator = std::function<void(SessionModelView&)>;
    void onSessionCreated(SessionConfigurator cfg) { configurator_ = std::move(cfg); }

private:
    struct Session {
        std::unique_ptr<SessionModelView>    mv;
        std::unique_ptr<SessionViewController> vc;
    };

    Session& getOrCreate(const std::string& key);

    // Validates a session key: ASCII alphanumeric + dash/underscore, max 63 chars.
    static bool isValidKey(const std::string& key);

    // UDP read loop — routes incoming packets to the right KCP consumer.
    void readLoop();

    // Handle a consumer registration packet ("HVKC" magic).
    void handleRegistration(const char* buf, int len,
                            const sockaddr_in& from);

    int voxelRes_;
    float pointSize_ = 0.02f;
    BoundBox bounds_;
    int wsPort_  = 8080;
    int udpFd_   = -1;
    std::chrono::steady_clock::time_point startTime_{std::chrono::steady_clock::now()};

    mutable std::mutex mu_;
    std::unordered_map<std::string, Session> sessions_;

    // Monotonically increasing conv ID counter (starts at 1, never 0x48564B43 "HVKC").
    uint32_t nextConv_ = 1;

    std::atomic<bool>   running_{false};
    std::thread         readThread_;
    SessionConfigurator configurator_;
};
