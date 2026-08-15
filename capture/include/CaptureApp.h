#pragma once

#include "Pipeline.h"
#include "Sender.h"
#include "filters/BodyFilter.h"
#include "filters/BackgroundSubtractorFilter.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <atomic>
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
};

// ── Live status — updated by the capture thread, read by the UI thread ────────
struct CaptureStatus {
    bool        connected   = false;
    float       fps         = 0.0f;
    int         points      = 0;
    int         frameCount  = 0;
    float       bgProgress  = 0.0f;   // 0..1 during background learning
    std::string message;              // last info/error line
};

// ── CaptureApp ────────────────────────────────────────────────────────────────
// Owns the GLFW window, ImGui context, and the capture worker thread.
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

    // ── Capture thread ────────────────────────────────────────────────────────
    void startCapture();
    void startPreviewOnly();   // opens sensor, shows preview, no server
    void stopCapture();
    void captureLoop();
    void previewOnlyLoop();

    // ── Preview textures (written by capture thread, read by UI thread) ───────
    void uploadPreviewTextures();   // call from UI thread
    void updatePreviewBuffers(const Frame& frame); // call from capture thread

    // ── State ─────────────────────────────────────────────────────────────────
    GLFWwindow*  window_  = nullptr;

    CaptureConfig config_;
    CaptureStatus status_;
    std::mutex    statusMu_;

    bool                capturing_ = false;
    std::thread         captureThread_;
    std::atomic<bool>   stopFlag_{false};

    // Preview texture data — double-buffered via mutex
    std::mutex           texMu_;
    std::vector<uint8_t> colorBuf_;   // BGR  512×424
    std::vector<uint8_t> depthBuf_;   // RGB  512×424 (false-colour)
    bool                 texDirty_ = false;

    GLuint colorTex_ = 0;
    GLuint depthTex_ = 0;
    static constexpr int kDepthW = 512;
    static constexpr int kDepthH = 424;
};
