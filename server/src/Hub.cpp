#include "Hub.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

// Registration packet layout:
//   [4]  magic "HVKC"
//   [n]  session_key (null-terminated, max kMaxSessionKeyLen chars)
//
// Server response (plain UDP, not KCP):
//   [4]  magic "HVKC"
//   [4]  assigned conv (u32 LE)
//   [4]  "OK\0\0" or "FAIL"

Hub::Hub(int voxelRes) : voxelRes_(voxelRes) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    udpFd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
    if (udpFd_ < 0) {
        std::cerr << "[Hub] Failed to create UDP socket\n";
        return;
    }

    // 10 ms receive timeout so readLoop can check running_ periodically.
#ifdef _WIN32
    DWORD tv = 10;
    setsockopt(udpFd_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{0, 10'000};
    setsockopt(udpFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(kKcpPort));

    if (bind(udpFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Hub] Failed to bind UDP port " << kKcpPort << "\n";
        closesocket(udpFd_);
        udpFd_ = -1;
        return;
    }

    running_ = true;
    readThread_ = std::thread(&Hub::readLoop, this);
    std::cout << "[Hub] KCP listening on UDP :" << kKcpPort << "\n";
}

Hub::~Hub() {
    running_ = false;
    if (readThread_.joinable()) readThread_.join();
    if (udpFd_ >= 0) closesocket(udpFd_);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ── Producer interface ────────────────────────────────────────────────────────

ISessionModel& Hub::producerPort(const std::string& sessionKey) {
    return *getOrCreate(sessionKey).mv;
}

void Hub::setTransform(const std::string& sessionKey,
                        const std::string& sensorId,
                        const std::array<float, 16>& m) {
    getOrCreate(sessionKey).mv->setTransform(sensorId, m);
}

// ── Timer interface ───────────────────────────────────────────────────────────

void Hub::tick() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [key, s] : sessions_)
        s.vc->tick();
}

int Hub::totalConsumers() const {
    std::lock_guard<std::mutex> lock(mu_);
    int total = 0;
    for (const auto& [key, s] : sessions_)
        total += s.vc->consumerCount();
    return total;
}

Hub::HubStats Hub::getStats() const {
    HubStats stats;
    stats.wsPort   = wsPort_;
    stats.kcpPort  = kKcpPort;
    stats.voxelRes = voxelRes_;
    stats.uptimeS  = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime_).count());

    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& [key, s] : sessions_) {
        SessionStats ss;
        ss.key        = key;
        ss.sensors    = s.mv->sensorStats();
        ss.consumers  = s.vc->consumerCount();
        ss.frameCount = s.vc->frameCount();
        ss.fps        = s.vc->outFps();
        stats.sessions.push_back(std::move(ss));
    }
    return stats;
}

// ── Private ───────────────────────────────────────────────────────────────────

Hub::Session& Hub::getOrCreate(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = sessions_.find(key);
    if (it != sessions_.end()) return it->second;

    Session s;
    s.mv = std::make_unique<SessionModelView>(voxelRes_);
    s.vc = std::make_unique<SessionViewController>(*s.mv, key);
    sessions_[key] = std::move(s);
    std::cout << "[Hub] session created: " << key << "\n";
    return sessions_[key];
}

bool Hub::isValidKey(const std::string& key) {
    if (key.empty() || key.size() > Limits::kMaxSessionKeyLen) return false;
    for (char c : key) {
        if (!std::isalnum(static_cast<unsigned char>(c))
            && c != '-' && c != '_') return false;
    }
    return true;
}

void Hub::readLoop() {
    char        buf[65536];
    sockaddr_in from{};
    socklen_t   fromLen = sizeof(from);

    while (running_) {
        int n = recvfrom(udpFd_, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue; // timeout or error — loop and check running_

        // ── Registration packet? ──────────────────────────────────────────────
        if (n >= 4 && memcmp(buf, "HVKC", 4) == 0) {
            handleRegistration(buf, n, from);
            continue;
        }

        // ── KCP data/ACK packet — route by conv (first 4 bytes of KCP header) ─
        if (n < 4) continue; // malformed, discard
        uint32_t conv;
        memcpy(&conv, buf, 4);

        std::lock_guard<std::mutex> lock(mu_);
        for (auto& [key, s] : sessions_) {
            // Feed into the right KCP context.
            // SessionViewController doesn't expose ikcp_input directly —
            // we route by asking each session if it owns this conv.
            // For performance, a flat conv→session map could replace this loop;
            // with kMaxConsumersPerSession=32 and typical session counts <10 it's fine.
            (void)conv; // routing via ikcp_input handled in SessionViewController below
        }
        // Delegate raw input feeding to each VC — it checks if it owns conv.
        for (auto& [key, s] : sessions_) {
            s.vc->feedKcpInput(conv, buf, n);
        }
    }
}

void Hub::handleRegistration(const char* buf, int len, const sockaddr_in& from) {
    // Validate minimum length: 4 (magic) + at least 1 (key char) + 1 (null)
    if (len < 6) return;

    // Extract session_key (null-terminated, starting at byte 4).
    // Security: enforce max length and character whitelist before using.
    std::string key(buf + 4, strnlen(buf + 4,
                    std::min(static_cast<int>(Limits::kMaxSessionKeyLen), len - 4)));

    if (!isValidKey(key)) {
        std::cerr << "[Hub] rejected registration: invalid session key\n";
        // Respond with FAIL (plain UDP, no KCP) — don't reveal why.
        char resp[8]; memcpy(resp, "HVKC", 4); memcpy(resp + 4, "FAIL", 4);
        sendto(udpFd_, resp, 8, 0,
               reinterpret_cast<const sockaddr*>(&from), sizeof(from));
        return;
    }

    // Assign conv — skip the "HVKC" magic value.
    uint32_t conv;
    {
        std::lock_guard<std::mutex> lock(mu_);
        do { conv = nextConv_++; } while (conv == 0x48564B43u); // skip "HVKC"
    }

    // Register consumer in the session.
    getOrCreate(key).vc->addConsumer(conv, from, udpFd_);

    // Respond with assigned conv (plain UDP).
    char resp[12];
    memcpy(resp,     "HVKC", 4);
    memcpy(resp + 4, &conv,   4);
    memcpy(resp + 8, "OK\0\0", 4);
    sendto(udpFd_, resp, 12, 0,
           reinterpret_cast<const sockaddr*>(&from), sizeof(from));

    std::cout << "[Hub] consumer registered: session=" << key
              << " conv=" << conv << "\n";
}
