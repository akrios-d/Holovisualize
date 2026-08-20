#pragma once

#include "PointCloud.h"
#include "GestureWire.h"
#include <ixwebsocket/IXWebSocket.h>
#include <string>
#include <vector>

// Serialises point clouds to the HOLO binary format and streams them
// to the relay server over WebSocket.
//
// Wire format (matches server/internal/frame/frame.go):
//   [4]  magic     = "HOLO"
//   [4]  n         = number of points (uint32 little-endian)
//   [n*16] points  = X(f32) Y(f32) Z(f32) R(u8) G(u8) B(u8) _(u8 pad)
class Sender {
public:
    // url      — full WebSocket URL including query params, e.g.:
    //            ws://192.168.1.10:8080/ws?session=demo&role=producer&sensor=0
    explicit Sender(const std::string& url);
    ~Sender();

    // Opens the WebSocket connection. Returns false if it fails.
    bool connect();

    // Closes the connection.
    void disconnect();

    // Serialises `cloud` and sends it as a binary frame.
    // Returns false if not connected or send fails.
    bool send(const PointCloud& cloud);

    // Sends a GEVT gesture event frame (see GestureWire.h). x/y/z are
    // camera-space metres — the server transforms to world space using this
    // sensor's calibration.
    bool sendGesture(HandGesture type, float x, float y, float z, float confidence);

private:
    static std::vector<uint8_t> serialise(const PointCloud& cloud);

    std::string    url_;
    ix::WebSocket  ws_;
};
