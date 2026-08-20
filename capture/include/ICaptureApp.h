#pragma once

// ─── Socket Pattern — capture app layer ───────────────────────────────────────
//
// Every session boundary is defined by an explicit interface contract. Same
// shape as server/include/ISession.h, applied to the capture GUI instead of
// the server's session broadcast:
//
//   ICaptureModel ──┐
//                    ├──► CaptureModelView       (pipeline/sensor/network — data side)
//   ICaptureView  ──┤
//                    │
//   ICaptureView  ──┤ (held by reference)
//                    ├──► CaptureViewController  (ImGui window/render loop — UI side)
//   ICaptureController ──┘
//
// ICaptureView is the socket — the only bridge between the data side and the
// UI side. Neither implementation knows the other's concrete type.

#include <cstdint>
#include <string>
#include <vector>

// ─── Shared state ──────────────────────────────────────────────────────────────

// Pre-populated from CLI args, editable in the UI. Owned by
// CaptureViewController (it's form state the user is editing); passed by
// value into ICaptureModel's action methods when the user submits it.
struct CaptureConfig {
    char host    [256] = "localhost:8080";
    char session [128] = "demo";
    char sensorId[128] = "sensor0";

    bool filterBackground = false;
    bool showPreview      = true;
    bool enableGestures   = true;  // detect hand gestures via the MediaPipe sidecar

    // Depth segmentation range (mm) — points outside this are dropped.
    float segmentMinDepthMm = 200.0f;
    float segmentMaxDepthMm = 2500.0f;
};

// Updated by the capture thread, read by the UI thread.
struct CaptureStatus {
    bool        connected   = false;
    float       fps         = 0.0f;
    int         points      = 0;
    int         frameCount  = 0;
    int         lostTotal   = 0;      // cumulative depth frames lost (sensor-reported)
    float       bgProgress  = 0.0f;   // 0..1 during background learning
    std::string message;              // last info/error line

    // Name of the most recently recognised hand gesture (e.g. "Fist"),
    // empty if none has fired yet this session. Persists until the next one
    // — see CaptureModelView::detectAndSendGesture().
    std::string lastGesture;
};

// RGB preview buffers at the depth resolution (512x424) — produced by the
// model's capture thread, consumed by the UI thread for GL texture upload.
// Empty vectors mean "nothing new since the last takePreviewBuffers() call".
struct PreviewBuffers {
    std::vector<uint8_t> color;        // full-res colour, downsampled
    std::vector<uint8_t> depth;        // false-colour depth
    std::vector<uint8_t> ir;           // grayscale IR
    std::vector<uint8_t> registered;   // colour registered to depth space
};

// ─── ICaptureModel — pipeline/sensor/network operations ───────────────────────
// What the UI's buttons call. Owns Pipeline, Sender, and the capture thread.

class ICaptureModel {
public:
    virtual ~ICaptureModel() = default;

    virtual void startCapture(const CaptureConfig& cfg)      = 0; // "Connect & Stream"
    virtual void startPreviewOnly(const CaptureConfig& cfg)  = 0; // "Preview Only"
    virtual void stopCapture()                                = 0;

    // Blocking — connects and immediately disconnects. Returns reachability.
    virtual bool testServer(const CaptureConfig& cfg) = 0;

    // Live update while streaming — no restart needed.
    virtual void setDepthRange(float minMm, float maxMm) = 0;

    // WGL requires the depth pipeline's shared GL context to be created on
    // the same thread as its parent window's context — the UI thread must
    // call this once, right after creating its window, before the first
    // startCapture()/startPreviewOnly().
    virtual void setParentGLContext(void* ctx) = 0;
};

// ─── ICaptureView — reactive state, read by the UI ────────────────────────────
// CaptureModelView implements this to expose status. CaptureViewController
// holds a reference to it to read state every frame.

class ICaptureView {
public:
    virtual ~ICaptureView() = default;

    virtual CaptureStatus status() const = 0;
    virtual bool isCapturing() const = 0;
    virtual bool isStreaming() const = 0; // Connect & Stream vs. Preview Only

    // True if the capture thread exited on its own (e.g. failed to connect)
    // and stopCapture() hasn't been called yet to actually tear the pipeline
    // down. Pipeline teardown must happen on the UI thread (same GL-context
    // rule as setParentGLContext), so the UI thread has to poll this and
    // call stopCapture() itself rather than the model doing it internally.
    virtual bool needsTeardown() const = 0;

    // Returns and clears the latest preview frame. Empty buffers if nothing
    // new since the last call.
    virtual PreviewBuffers takePreviewBuffers() = 0;
};

// ─── ICaptureController — user actions ─────────────────────────────────────────
// What ImGui's button callbacks call. Implemented by CaptureViewController,
// which forwards each action to its ICaptureModel reference.

class ICaptureController {
public:
    virtual ~ICaptureController() = default;

    virtual void onConnectAndStream(const CaptureConfig& cfg) = 0;
    virtual void onPreviewOnly(const CaptureConfig& cfg)      = 0;
    virtual void onDisconnect()                                = 0;
    virtual bool onTestServer(const CaptureConfig& cfg)        = 0;
    virtual void onDepthRangeChanged(float minMm, float maxMm) = 0;
};
