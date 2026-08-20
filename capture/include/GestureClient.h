#pragma once

// GestureClient — talks to the local Python MediaPipe sidecar
// (capture/gesture_sidecar/sidecar.py), spawned as a child process and
// driven over its stdin/stdout pipes.
//
// This used to be a TCP socket on 127.0.0.1 — switched to pipes after a
// Windows machine reliably had every such loopback socket's connection
// teardown aborted (WinError 10053) by some interceptor never conclusively
// identified (ruled out: Defender real-time protection, ASR rules, Smart
// App Control, endianness, our own connection-lifecycle code — a minimal
// from-scratch C++ Winsock client reproduced it against the same sidecar
// while an equivalent Python client never once failed). Anonymous pipes
// don't touch the network stack at all, sidestepping whatever that was.
//
// Best-effort by design: if the sidecar can't be spawned or dies, every
// detect() call just returns false (no gesture) — it must never block or
// crash the capture loop. See capture/gesture_sidecar/README.md for why
// this lives in a separate Python process instead of embedded C++.

#include "GestureWire.h"

#include <chrono>
#include <cstdint>
#include <string>

struct HandDetection {
    HandGesture gesture    = HandGesture::None;
    float       wristU     = 0.f; // normalized [0,1], image space (MediaPipe convention)
    float       wristV     = 0.f;
    float       confidence = 0.f;
};

class GestureClient {
public:
    GestureClient();
    ~GestureClient();

    GestureClient(const GestureClient&)            = delete;
    GestureClient& operator=(const GestureClient&) = delete;

    // Sends one RGB frame (w*h*3 bytes, row-major, no padding) to the
    // sidecar and blocks for its classification. Returns false — and
    // leaves `out` untouched — on any failure: sidecar couldn't be spawned,
    // died mid-session, or "no hand visible".
    bool detect(const uint8_t* rgb, int w, int h, HandDetection& out);

private:
    bool ensureChildRunning();
    void killChild();
    bool writeAll(const uint8_t* data, size_t len);
    bool readAll(uint8_t* data, size_t len);

    // Opaque HANDLE values (avoid pulling <windows.h> into this header) —
    // reinterpret_cast to HANDLE in the .cpp. nullptr/INVALID_HANDLE_VALUE
    // both map to nothing-open here for simplicity.
    void* childProcess_ = nullptr;
    void* childThread_  = nullptr;
    void* stdinWrite_   = nullptr; // parent's end — write requests here
    void* stdoutRead_   = nullptr; // parent's end — read responses here
    bool  running_      = false;

    // Respawn attempts are throttled — a sidecar that fails to start
    // (missing venv, etc.) shouldn't be retried every captured frame.
    std::chrono::steady_clock::time_point nextSpawnAttempt_{};
};
