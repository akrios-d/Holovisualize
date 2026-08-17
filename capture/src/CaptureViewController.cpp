#include "CaptureViewController.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <chrono>
#include <iostream>
#include <thread>

// Hint to hybrid-graphics drivers (NVIDIA Optimus / AMD PowerXpress) to run
// this process on the discrete GPU instead of the integrated one — without
// this, Windows may default OpenGL to the Intel iGPU on laptops.
#if defined(_WIN32)
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// ── Construction / destruction ────────────────────────────────────────────────

CaptureViewController::CaptureViewController(ICaptureModel& model, ICaptureView& view,
                                               CaptureConfig initial)
    : model_(model), view_(view), config_(initial) {}

CaptureViewController::~CaptureViewController() {
    onDisconnect();
    if (colorTex_)      glDeleteTextures(1, &colorTex_);
    if (depthTex_)      glDeleteTextures(1, &depthTex_);
    if (irTex_)         glDeleteTextures(1, &irTex_);
    if (registeredTex_) glDeleteTextures(1, &registeredTex_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── ICaptureController ────────────────────────────────────────────────────────
// Forward each user action to the model. Local config_ tweaks (showPreview)
// are UI-only display state, not part of the model's contract.

void CaptureViewController::onConnectAndStream(const CaptureConfig& cfg) {
    // Preview texture uploads add another GL context switch on top of the
    // packet processors' own — disable it while actually streaming to keep
    // full decode throughput (see initWindow()'s vsync comment for the rest
    // of this contention story).
    config_.showPreview = false;
    model_.startCapture(cfg);
}

void CaptureViewController::onPreviewOnly(const CaptureConfig& cfg) {
    config_.showPreview = true;
    model_.startPreviewOnly(cfg);
}

void CaptureViewController::onDisconnect() {
    model_.stopCapture();
}

bool CaptureViewController::onTestServer(const CaptureConfig& cfg) {
    return model_.testServer(cfg);
}

void CaptureViewController::onDepthRangeChanged(float minMm, float maxMm) {
    model_.setDepthRange(minMm, maxMm);
}

// ── Window / ImGui init ───────────────────────────────────────────────────────

bool CaptureViewController::initWindow() {
    if (!glfwInit()) { std::cerr << "[UI] glfwInit failed\n"; return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(1280, 820, "Holovisualize — Capture", nullptr, nullptr);
    if (!window_) { std::cerr << "[UI] glfwCreateWindow failed\n"; return false; }

    glfwMakeContextCurrent(window_);
    // vsync's blocking wait inside glfwSwapBuffers stalls the driver on this
    // thread, which contends with the independent GL contexts owned by
    // GLJpegRgbPacketProcessor/OpenGLDepthPacketProcessor (each does its own
    // wglMakeCurrent) — this showed up as decode throughput collapsing while
    // the UI window was open, even with the preview panel hidden. Swap
    // uncapped and pace the loop manually instead (see run()).
    glfwSwapInterval(0);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cerr << "[UI] glewInit failed\n"; return false; }

    {
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        gpuInfo_ = std::string(vendor ? vendor : "?") + " — " + (renderer ? renderer : "?");
        std::cout << "[UI] GPU: " << gpuInfo_ << "\n";
    }

    // Depth pipeline's shared GL context must be created on this thread —
    // hand the window over to the model now that it exists.
    model_.setParentGLContext(window_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    // Slightly larger font for readability
    io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Allocate preview textures
    auto makeTexture = [](GLuint& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    makeTexture(colorTex_);
    makeTexture(depthTex_);
    makeTexture(irTex_);
    makeTexture(registeredTex_);

    return true;
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void CaptureViewController::run() {
    if (!initWindow()) return;

    // UI redraw rate cap (vsync is off — see initWindow()). 30Hz is plenty
    // for an ImGui status panel and leaves the GPU free for the capture
    // pipeline's own GL contexts instead of racing them every vblank.
    const auto frameInterval = std::chrono::milliseconds(1000 / 30);

    while (!glfwWindowShouldClose(window_)) {
        auto frameStart = std::chrono::steady_clock::now();

        glfwPollEvents();

        // The capture thread self-signals if it exits on its own (e.g.
        // failed to connect to the server) — finish tearing it down here,
        // since pipeline teardown must happen on this (UI) thread.
        if (view_.needsTeardown()) onDisconnect();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderFrame();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameInterval)
            std::this_thread::sleep_for(frameInterval - elapsed);
    }
    onDisconnect();
}

// ── UI panels ─────────────────────────────────────────────────────────────────

void CaptureViewController::renderFrame() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return; // first frame not ready

    // Left panel — config + status
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({380, io.DisplaySize.y}, ImGuiCond_Always);
    ImGui::Begin("##left", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    renderConfigPanel();

    ImGui::End();

    // Right panel — status + preview
    ImGui::SetNextWindowPos({380, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({io.DisplaySize.x - 380, io.DisplaySize.y}, ImGuiCond_Always);
    ImGui::Begin("##right", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    renderStatusPanel();
    ImGui::Spacing();
    if (config_.showPreview) renderPreviewPanel();

    ImGui::End();
}

void CaptureViewController::renderConfigPanel() {
    ImGui::Text("Holovisualize Capture");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", gpuInfo_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::SeparatorText("Connection");
    ImGui::InputText("Host",      config_.host,     sizeof(config_.host));
    ImGui::InputText("Session",   config_.session,  sizeof(config_.session));
    ImGui::InputText("Sensor ID", config_.sensorId, sizeof(config_.sensorId));
    ImGui::Spacing();
    if (ImGui::Button("Test Server")) onTestServer(config_);

    ImGui::Spacing();
    ImGui::SeparatorText("Scene Filters");
    ImGui::Checkbox("Background subtraction", &config_.filterBackground);

    ImGui::Spacing();
    ImGui::SeparatorText("Depth Range");
    bool rangeChanged = false;
    rangeChanged |= ImGui::SliderFloat("Min (mm)", &config_.segmentMinDepthMm, 0.0f, 8000.0f, "%.0f");
    rangeChanged |= ImGui::SliderFloat("Max (mm)", &config_.segmentMaxDepthMm, 0.0f, 8000.0f, "%.0f");
    if (config_.segmentMinDepthMm > config_.segmentMaxDepthMm)
        config_.segmentMaxDepthMm = config_.segmentMinDepthMm;
    if (rangeChanged)
        onDepthRangeChanged(config_.segmentMinDepthMm, config_.segmentMaxDepthMm);

    ImGui::Spacing();
    ImGui::SeparatorText("Display");
    ImGui::BeginDisabled(view_.isStreaming());
    ImGui::Checkbox("Show preview", &config_.showPreview);
    ImGui::EndDisabled();
    if (view_.isStreaming())
        ImGui::TextDisabled("Preview disabled while streaming (avoids GPU contention).");

    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::Button("Connect & Stream")) onConnectAndStream(config_);
    ImGui::Spacing();
    if (ImGui::Button("Preview Only")) onPreviewOnly(config_);
    ImGui::Spacing();
    if (ImGui::Button("Disconnect")) onDisconnect();
}

void CaptureViewController::renderStatusPanel() {
    ImGui::SeparatorText("Status");

    CaptureStatus s = view_.status();

    // Connection indicator
    ImVec4 dot = s.connected
        ? ImVec4{0.2f, 0.9f, 0.2f, 1.0f}
        : ImVec4{0.9f, 0.3f, 0.3f, 1.0f};
    ImGui::TextColored(dot, s.connected ? "● Connected" : "● Disconnected");

    // Background learning progress bar
    if (config_.filterBackground && s.bgProgress < 1.0f && view_.isCapturing()) {
        ImGui::Spacing();
        ImGui::Text("Learning background…");
        ImGui::ProgressBar(s.bgProgress, {-1, 0});
    }

    ImGui::Spacing();
    ImGui::Text("FPS:    %.1f", s.fps);
    ImGui::Text("Points: %d",   s.points);
    ImGui::Text("Frames: %d",   s.frameCount);
    ImGui::Text("Lost:   %d",   s.lostTotal);

    if (!s.message.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", s.message.c_str());
    }
}

void CaptureViewController::renderPreviewPanel() {
    uploadPreviewTextures();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float imgW  = avail * 0.5f - 4.0f;
    const float imgH  = imgW * kDepthH / kDepthW;

    auto showImg = [&](const char* label, GLuint tex) {
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s", label);
        if (tex) ImGui::Image((ImTextureID)(uintptr_t)tex, {imgW, imgH});
        else     ImGui::Dummy({imgW, imgH});
        ImGui::EndGroup();
    };

    // 2x2 grid, matching Protonect's viewer layout: Color, Depth, IR, Registered
    showImg("Color",      colorTex_);
    ImGui::SameLine();
    showImg("Depth (false colour)", depthTex_);

    showImg("IR",         irTex_);
    ImGui::SameLine();
    showImg("Registered", registeredTex_);
}

// ── Preview textures ──────────────────────────────────────────────────────────

void CaptureViewController::uploadPreviewTextures() {
    PreviewBuffers buf = view_.takePreviewBuffers();
    if (buf.color.empty() || buf.depth.empty()) return;

    auto upload = [](GLuint tex, const std::vector<uint8_t>& b, int w, int h) {
        if (b.empty()) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, b.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    upload(colorTex_,      buf.color,      kDepthW, kDepthH);
    upload(depthTex_,      buf.depth,      kDepthW, kDepthH);
    upload(irTex_,         buf.ir,         kDepthW, kDepthH);
    upload(registeredTex_, buf.registered, kDepthW, kDepthH);
}
