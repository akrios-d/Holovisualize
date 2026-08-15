#pragma once

// SessionViewController — consumer side of the Socket Pattern
//
// Implements ISessionController (consumer-facing port). Holds a reference to
// ISessionView (the socket) to pull the latest mesh frame, then broadcasts it
// to all registered KCP consumers. Never knows how the mesh is generated.
//
// KCP (ikcp) runs over a shared UDP socket owned by the Hub. Each consumer is
// identified by a 32-bit conv ID assigned by the server.

#include "ISession.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  define closesocket close
#endif

#include <ikcp.h>    // vendored via FetchContent / CMake

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class SessionViewController final : public ISessionController {
public:
    // view    — the ISessionView socket (SessionModelView).
    // session — human-readable label for logs.
    explicit SessionViewController(ISessionView& view,
                                   std::string session);
    ~SessionViewController() override;

    // ── ISessionController ────────────────────────────────────────────────────
    void tick() override;
    void addConsumer(uint32_t conv,
                     const sockaddr_in& addr,
                     int udpFd) override;
    void removeConsumer(uint32_t conv) override;
    int      consumerCount() const override;
    uint64_t frameCount()    const override;
    float    outFps()        const override;

    // Feed raw UDP bytes into the KCP context for the given conv.
    // No-op if this session doesn't own that conv.
    void feedKcpInput(uint32_t conv, const char* buf, int len);

private:
    struct KcpConsumer {
        uint32_t       conv;
        sockaddr_in    addr;
        int            udpFd;    // shared socket — do NOT close
        ikcpcb*        kcp;
        std::chrono::steady_clock::time_point lastFrameSent;

        // ikcp output callback: sends raw UDP packet to the consumer.
        static int kcpOutput(const char* buf, int len,
                             ikcpcb* kcp, void* user);
    };

    // Prune consumers whose KCP send queue is overflowing (dead or too slow).
    void pruneStaleConsumers();

    ISessionView&  view_;
    std::string    session_;

    mutable std::mutex mu_;
    std::unordered_map<uint32_t, KcpConsumer> consumers_;

    // FPS tracking
    std::atomic<uint64_t> frameCount_{0};
    std::atomic<float>    outFps_{0.f};
    std::chrono::steady_clock::time_point fpsWindowStart_{std::chrono::steady_clock::now()};
    uint64_t fpsWindowFrames_ = 0; // guarded by mu_

    // Milliseconds since an arbitrary epoch — fed to ikcp_update.
    static uint32_t nowMs();
};
