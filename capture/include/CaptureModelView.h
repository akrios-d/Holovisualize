#pragma once

// CaptureModelView — data side of the Socket Pattern (see ICaptureApp.h)
//
// Implements ICaptureModel (UI-facing port for actions) and ICaptureView
// (the shared socket exposing state). Owns the sensor pipeline, the
// WebSocket sender, and the capture worker thread. Never touches ImGui or
// GLFW — the UI thread only reaches it through the two interfaces.

#include "ICaptureApp.h"
#include "Pipeline.h"
#include "Sender.h"
#include "Frame.h"
#include "GestureClient.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class CaptureModelView final : public ICaptureModel, public ICaptureView {
public:
    CaptureModelView() = default;
    ~CaptureModelView() override;

    // ── ICaptureModel ─────────────────────────────────────────────────────────
    void startCapture(const CaptureConfig& cfg) override;
    void startPreviewOnly(const CaptureConfig& cfg) override;
    void stopCapture() override;
    bool testServer(const CaptureConfig& cfg) override;
    void setDepthRange(float minMm, float maxMm) override;
    void setParentGLContext(void* ctx) override { parentGLContext_ = ctx; }

    // ── ICaptureView ─────────────────────────────────────────────────────────
    CaptureStatus status() const override;
    bool isCapturing() const override { return capturing_; }
    bool isStreaming() const override { return streaming_; }
    bool needsTeardown() const override { return capturing_ && stopFlag_; }
    PreviewBuffers takePreviewBuffers() override;

private:
    void captureLoop();        // background thread body (stream mode)
    void previewOnlyLoop();    // background thread body (preview-only mode)
    void updatePreviewBuffers(const Frame& frame);

    // Round-trips one frame through the sidecar, updates status_.lastGesture,
    // and returns true (with `det` filled) on an edge-triggered gesture
    // change. Used by both captureLoop() and previewOnlyLoop() — see its
    // definition in CaptureModelView.cpp for why it's split out this way.
    bool runGestureDetection(const Frame& frame, HandDetection& det);

    // Runs gesture detection against `frame` (throttled — see captureLoop())
    // and, on an edge-triggered gesture change, sends a GEVT frame.
    void detectAndSendGesture(const Frame& frame);

    void* parentGLContext_ = nullptr; // set via setParentGLContext() before first start*()

    CaptureConfig activeConfig_; // snapshot taken at start*(); depth range kept live after

    mutable std::mutex statusMu_;
    CaptureStatus       status_;

    bool                capturing_ = false;
    bool                streaming_ = false; // true for startCapture, false for startPreviewOnly
    std::thread         captureThread_;
    std::atomic<bool>   stopFlag_{false};

    // Constructed and initialize()'d on the UI thread; only read (not
    // constructed/destroyed) from the capture thread while it runs.
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Sender>   sender_;

    // Best-effort, capture-thread-only — never touched by the UI thread.
    GestureClient gestureClient_;
    HandGesture   lastSentGesture_ = HandGesture::None; // for edge-triggering
    int           gestureFrameSkip_ = 0;

    // Preview texture data — written by the capture thread, taken by the UI
    // thread for GL texture upload.
    std::mutex     texMu_;
    PreviewBuffers previewBuf_;
    bool           texDirty_ = false;

    static constexpr int kDepthW = 512;
    static constexpr int kDepthH = 424;
};
