#include "SessionViewController.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

// ─── KcpConsumer::kcpOutput ──────────────────────────────────────────────────

int SessionViewController::KcpConsumer::kcpOutput(
    const char* buf, int len, ikcpcb* /*kcp*/, void* user)
{
    auto* self = static_cast<KcpConsumer*>(user);
    sendto(self->udpFd,
           buf, len, 0,
           reinterpret_cast<const sockaddr*>(&self->addr),
           sizeof(self->addr));
    return 0;
}

// ─── SessionViewController ────────────────────────────────────────────────────

SessionViewController::SessionViewController(ISessionView& view,
                                              std::string session)
    : view_(view)
    , session_(std::move(session))
{}

SessionViewController::~SessionViewController() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [conv, c] : consumers_)
        ikcp_release(c.kcp);
    consumers_.clear();
}

// ── ISessionController ────────────────────────────────────────────────────────

void SessionViewController::tick() {
    std::vector<uint8_t> frame;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (consumers_.empty()) return;
    }

    // Build mesh frame via the ISessionView socket (calls SessionModelView).
    frame = view_.buildFrame();
    if (frame.empty()) return;

    uint32_t now = nowMs();
    std::lock_guard<std::mutex> lock(mu_);

    pruneStaleConsumers();

    for (auto& [conv, c] : consumers_) {
        ikcp_update(c.kcp, now);
        // ikcp_send fragments the frame into MTU-sized UDP packets automatically.
        int ret = ikcp_send(c.kcp,
                            reinterpret_cast<const char*>(frame.data()),
                            static_cast<int>(frame.size()));
        if (ret < 0) {
            // Negative return means the frame was too large — shouldn't happen with
            // default settings, but log so it's visible.
            std::cerr << "[" << session_ << "] ikcp_send error " << ret
                      << " for conv " << conv << "\n";
        } else {
            c.lastFrameSent = std::chrono::steady_clock::now();
        }
    }

    // FPS tracking (guarded by mu_ already held above)
    ++fpsWindowFrames_;
    ++frameCount_;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fpsWindowStart_).count();
    if (elapsed >= 1000) {
        outFps_.store(static_cast<float>(fpsWindowFrames_ * 1000) / elapsed);
        fpsWindowFrames_ = 0;
        fpsWindowStart_  = std::chrono::steady_clock::now();
    }
}

void SessionViewController::addConsumer(uint32_t conv,
                                         const sockaddr_in& addr,
                                         int udpFd) {
    std::lock_guard<std::mutex> lock(mu_);

    // Enforce max consumer limit (security: prevent resource exhaustion).
    if (static_cast<int>(consumers_.size()) >= Limits::kMaxConsumersPerSession) {
        std::cerr << "[" << session_ << "] max consumers reached, rejecting conv "
                  << conv << "\n";
        return;
    }

    KcpConsumer c{};
    c.conv          = conv;
    c.addr          = addr;
    c.udpFd         = udpFd;
    c.lastFrameSent = std::chrono::steady_clock::now();

    c.kcp = ikcp_create(conv, &consumers_[conv]); // will be fixed up below
    ikcp_setoutput(c.kcp, KcpConsumer::kcpOutput);
    // Low-latency profile: no delay, 10ms update interval, fast resend x2, no congestion ctrl.
    ikcp_nodelay(c.kcp, 1, 10, 2, 1);
    ikcp_setmtu(c.kcp, 1400);
    // Large window to handle multi-MB mesh frames (4096 × 1400 B ≈ 5.7 MB).
    ikcp_wndsize(c.kcp, 4096, 4096);

    consumers_[conv] = std::move(c);
    // Fix up the user pointer to point into the stable map node.
    ikcp_release(consumers_[conv].kcp);
    consumers_[conv].kcp = ikcp_create(conv, &consumers_[conv]);
    ikcp_setoutput(consumers_[conv].kcp, KcpConsumer::kcpOutput);
    ikcp_nodelay(consumers_[conv].kcp, 1, 10, 2, 1);
    ikcp_setmtu(consumers_[conv].kcp, 1400);
    ikcp_wndsize(consumers_[conv].kcp, 4096, 4096);

    std::cout << "[" << session_ << "] consumer added conv=" << conv << "\n";
}

void SessionViewController::removeConsumer(uint32_t conv) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = consumers_.find(conv);
    if (it == consumers_.end()) return;
    ikcp_release(it->second.kcp);
    consumers_.erase(it);
    std::cout << "[" << session_ << "] consumer removed conv=" << conv << "\n";
}

int SessionViewController::consumerCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<int>(consumers_.size());
}

uint64_t SessionViewController::frameCount() const { return frameCount_.load(); }
float    SessionViewController::outFps()     const { return outFps_.load(); }

void SessionViewController::feedKcpInput(uint32_t conv,
                                           const char* buf, int len) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = consumers_.find(conv);
    if (it == consumers_.end()) return;
    ikcp_input(it->second.kcp, buf, len);
}

// ── Private helpers ───────────────────────────────────────────────────────────

void SessionViewController::pruneStaleConsumers() {
    // Called with mu_ already held.
    auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> toRemove;

    for (auto& [conv, c] : consumers_) {
        // If KCP send queue is backed up, the consumer isn't ACK-ing.
        if (ikcp_waitsnd(c.kcp) > Limits::kMaxWaitSndPackets) {
            toRemove.push_back(conv);
            continue;
        }
        // Also prune by wall-clock time since last successful send.
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - c.lastFrameSent).count();
        if (elapsed > Limits::kConsumerTimeoutSec) {
            toRemove.push_back(conv);
        }
    }

    for (uint32_t conv : toRemove) {
        std::cout << "[" << session_ << "] pruning stale consumer conv=" << conv << "\n";
        ikcp_release(consumers_[conv].kcp);
        consumers_.erase(conv);
    }
}

uint32_t SessionViewController::nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count());
}
