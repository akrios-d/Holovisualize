#pragma once

#include "Pipeline.h"
#include "Sender.h"
#include "filters/BackgroundSubtractorFilter.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ── Config — pre-populated from CLI args, editable in the UI ─────────────────
struct CaptureConfig {
    char host    [256] = "localhost:8080";
    char session [128] = "demo";
    char sensorId[128] = "sensor0";

    bool filterBody       = false;
    bool filterBackground = false;
    bool showPreview      = true;

    // Depth segmentation range (mm) — points outside this are dropped.
    float segmentMinDepthMm = 200.0f;
    float segmentMaxDepthMm = 2500.0f;
};

// ── Live status — updated by the capture thread, read by the UI thread ────────
struct CaptureStatus {
    bool        connected   = false;
    float       fps         = 0.0f;
    int         points      = 0;
    int         frameCount  = 0;
    int         lostTotal   = 0;      // cumulative depth frames lost (sensor-reported)
    float       bgProgress  = 0.0f;   // 0..1 during background learning
    std::string message;              // last info/error line
};

// ── CaptureApp ────────────────────────────────────────────────────────────────
// Owns the GLFW window, ImGui context, the sensor/pipeline, and the capture
// worker thread.
//
// Threading: the libfreenect2 OpenGL depth pipeline shares a GL context with
// `window_`, and WGL requires context creation/destruction to happen on the
// thread that owns the parent context — so Pipeline construction
// (startCapture/startPreviewOnly) and teardown (stopCapture) always run on
// the UI thread. The capture thread itself makes no GL calls (depth decode
// happens on libfreenect2's own internal thread before waitForNewFrame()
// returns to us) — it only reads frames, builds point clouds, and sends
// them, so blocking waits there never freeze the UI.
//
// Call run() — it blocks until the window is closed.
class CaptureApp {
public:
    explicit CaptureApp(CaptureConfig initial);
    ~CaptureApp();

    void run();

private:
    // ── UI ────────────────────────────────────────────────────────────────────
    bool initWindow();
    void renderFrame();
    void renderConfigPanel();
    void renderStatusPanel();
    void renderPreviewPanel();

    // ── Capture ──────────────────────────────────────────────────────────────
    void startCapture();       // "Connect & Stream" — runs on the UI thread
    void startPreviewOnly();   // "Preview Only"      — runs on the UI thread
    void stopCapture();        // runs on the UI thread
    void testServer();         // "Test Server" — runs on the UI thread, brief blocking
    void captureLoop();        // background thread body (stream mode)
    void previewOnlyLoop();    // background thread body (preview-only mode)

    // ── Preview textures ─────────────────────────────────────────────────────
    void uploadPreviewTextures();                    // UI thread — makes GL calls
    void updatePreviewBuffers(const Frame& frame);    // capture thread

    // ── State ─────────────────────────────────────────────────────────────────
    GLFWwindow*  window_  = nullptr;
    std::string  gpuInfo_;   // GL_RENDERER / GL_VENDOR, filled in initWindow()

    CaptureConfig config_;
    CaptureStatus status_;
    std::mutex    statusMu_;

    bool                capturing_ = false;
    bool                streaming_ = false;  // true for "Connect & Stream", false for "Preview Only"
    std::thread         captureThread_;
    std::atomic<bool>   stopFlag_{false};

    // Constructed and initialize()'d on the UI thread; only read (not
    // constructed/destroyed) from the capture thread while it runs.
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Sender>   sender_;

    // Preview texture data — written by the capture thread, uploaded to GL
    // textures by the UI thread.
    std::mutex            texMu_;
    std::vector<uint8_t>  colorBuf_;        // RGB — full-res colour, downsampled
    std::vector<uint8_t>  depthBuf_;        // RGB — false-colour depth
    std::vector<uint8_t>  irBuf_;           // RGB — grayscale IR
    std::vector<uint8_t>  registeredBuf_;   // RGB — colour registered to depth space
    bool                  texDirty_ = false;

    GLuint colorTex_      = 0;
    GLuint depthTex_      = 0;
    GLuint irTex_         = 0;
    GLuint registeredTex_ = 0;
    static constexpr int kDepthW = 512;
    static constexpr int kDepthH = 424;
};
