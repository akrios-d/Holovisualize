#include "Sender.h"

#include <cstring>
#include <iostream>

// Little-endian write helpers
static void writeU32(uint8_t* dst, uint32_t v) {
    dst[0] = (v >>  0) & 0xFF;
    dst[1] = (v >>  8) & 0xFF;
    dst[2] = (v >> 16) & 0xFF;
    dst[3] = (v >> 24) & 0xFF;
}

static void writeF32(uint8_t* dst, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    writeU32(dst, bits);
}

std::vector<uint8_t> Sender::serialise(const PointCloud& cloud) {
    const size_t headerSize = 8;
    const size_t pointSize  = 16;

    std::vector<uint8_t> buf(headerSize + cloud.size() * pointSize, 0);

    // Magic
    buf[0] = 'H'; buf[1] = 'O'; buf[2] = 'L'; buf[3] = 'O';

    // Point count
    writeU32(buf.data() + 4, static_cast<uint32_t>(cloud.size()));

    // Points
    for (size_t i = 0; i < cloud.size(); i++) {
        uint8_t* dst = buf.data() + headerSize + i * pointSize;
        const Point& p = cloud[i];
        writeF32(dst + 0,  p.x);
        writeF32(dst + 4,  p.y);
        writeF32(dst + 8,  p.z);
        dst[12] = p.r;
        dst[13] = p.g;
        dst[14] = p.b;
        // dst[15] = 0 (padding, already zero)
    }

    return buf;
}

Sender::Sender(const std::string& url) : url_(url) {
    ws_.setUrl(url_);

    ws_.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Error)
            std::cerr << "[Sender] WebSocket error: " << msg->errorInfo.reason << "\n";
        if (msg->type == ix::WebSocketMessageType::Close)
            std::cerr << "[Sender] Connection closed.\n";
    });
}

Sender::~Sender() {
    disconnect();
}

bool Sender::connect() {
    ws_.start();

    // Wait up to 5 seconds for the connection to open.
    for (int i = 0; i < 50; i++) {
        if (ws_.getReadyState() == ix::ReadyState::Open)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cerr << "[Sender] Could not connect to " << url_ << "\n";
    return false;
}

void Sender::disconnect() {
    ws_.stop();
}

bool Sender::send(const PointCloud& cloud) {
    if (ws_.getReadyState() != ix::ReadyState::Open)
        return false;

    auto buf = serialise(cloud);

    // IXWebSocket sendBinary takes a std::string (binary-safe).
    std::string payload(reinterpret_cast<const char*>(buf.data()), buf.size());
    auto result = ws_.sendBinary(payload);
    return result.success;
}

bool Sender::sendGesture(HandGesture type, float x, float y, float z, float confidence) {
    if (ws_.getReadyState() != ix::ReadyState::Open)
        return false;

    auto buf = encodeGestureEvent(type, x, y, z, confidence);
    std::string payload(reinterpret_cast<const char*>(buf.data()), buf.size());
    auto result = ws_.sendBinary(payload);
    return result.success;
}
