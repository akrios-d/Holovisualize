#pragma once

// CaptureViewController — UI side of the Socket Pattern (see ICaptureApp.h)
//
// Implements ICaptureController. Holds a reference to ICaptureView (the
// socket) to read state, and to ICaptureModel to forward user actions —
// never touches Pipeline/Sender/threads directly (see CaptureModelView).
// Owns the GLFW window, ImGui context, and preview GL textures.
//
// Call run() — it blocks until the window is closed.

#include "ICaptureApp.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <string>

class CaptureViewController final : public ICaptureController {
public:
    CaptureViewController(ICaptureModel& model, ICaptureView& view, CaptureConfig initial);
    ~CaptureViewController() override;

    void run();

    // ── ICaptureController ────────────────────────────────────────────────────
    void onConnectAndStream(const CaptureConfig& cfg) override;
    void onPreviewOnly(const CaptureConfig& cfg)      override;
    void onDisconnect()                                override;
    bool onTestServer(const CaptureConfig& cfg)        override;
    void onDepthRangeChanged(float minMm, float maxMm) override;

private:
    // ── UI ────────────────────────────────────────────────────────────────────
    bool initWindow();
    void renderFrame();
    void renderConfigPanel();
    void renderStatusPanel();
    void renderPreviewPanel();
    void uploadPreviewTextures(); // UI thread — makes GL calls

    ICaptureModel& model_;
    ICaptureView&  view_;

    GLFWwindow*  window_ = nullptr;
    std::string  gpuInfo_; // GL_RENDERER / GL_VENDOR, filled in initWindow()

    CaptureConfig config_; // editable form state, bound directly to ImGui widgets

    GLuint colorTex_      = 0;
    GLuint depthTex_      = 0;
    GLuint irTex_         = 0;
    GLuint registeredTex_ = 0;
    static constexpr int kDepthW = 512;
    static constexpr int kDepthH = 424;
};
