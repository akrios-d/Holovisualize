#include "CaptureApp.h"
#include "PointCloud.h"
#include "sensors/KinectV2Sensor.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstring>
#include <iostream>

// ── Construction / destruction ────────────────────────────────────────────────

CaptureApp::CaptureApp(CaptureConfig initial) : config_(initial) {}

CaptureApp::~CaptureApp() {
    stopCapture();
    if (colorTex_) glDeleteTextures(1, &colorTex_);
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_) glfwDestroyWindow(window_);
    glfwTerminate();
}

// ── Window / ImGui init ───────────────────────────────────────────────────────

bool CaptureApp::initWindow() {
    if (!glfwInit()) { std::cerr << "[UI] glfwInit failed\n"; return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(1100, 700, "Holovisualize — Capture", nullptr, nullptr);
    if (!window_) { std::cerr << "[UI] glfwCreateWindow failed\n"; return false; }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cerr << "[UI] glewInit failed\n"; return false; }

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

    return true;
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void CaptureApp::run() {
    if (!initWindow()) return;

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

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
    }
    stopCapture();
}

// ── UI panels ─────────────────────────────────────────────────────────────────

void CaptureApp::renderFrame() {
    ImGuiIO& io = ImGui::GetIO();

    // Left panel — config + status
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({380, io.DisplaySize.y}, ImGuiCond_Always);
    ImGui::Begin("##left", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);

    renderConfigPanel();
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    renderStatusPanel();

    ImGui::End();

    // Right panel — preview
    if (config_.showPreview) {
        ImGui::SetNextWindowPos({380, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - 380, io.DisplaySize.y}, ImGuiCond_Always);
        ImGui::Begin("Preview", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse);
        renderPreviewPanel();
        ImGui::End();
    }
}

void CaptureApp::renderConfigPanel() {
    ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "Holovisualize — Capture");
    ImGui::Spacing();

    ImGui::SeparatorText("Connection");
    ImGui::InputText("Host",      config_.host,     sizeof(config_.host));
    ImGui::InputText("Session",   config_.session,  sizeof(config_.session));
    ImGui::InputText("Sensor ID", config_.sensorId, sizeof(config_.sensorId));

    ImGui::Spacing();
    ImGui::SeparatorText("Scene Filters");
    ImGui::Checkbox("Body bounds filter",        &config_.filterBody);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keeps only points inside a human-body\nbounding box (+-0.7m lateral, 0-2.1m height).");

    ImGui::Checkbox("Background subtraction",    &config_.filterBackground);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Captures 30 empty-scene frames on start,\nthen streams only foreground (people/objects).");

    ImGui::Spacing();
    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Show preview", &config_.showPreview);

    ImGui::Spacing();
    ImGui::Spacing();

    bool busy = capturing_;
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("  Connect & Stream  ", {-1, 40})) startCapture();
    if (busy) ImGui::EndDisabled();

    if (!busy) ImGui::BeginDisabled();
    if (ImGui::Button("  Disconnect  ", {-1, 30})) stopCapture();
    if (!busy) ImGui::EndDisabled();
}

void CaptureApp::renderStatusPanel() {
    ImGui::SeparatorText("Status");

    CaptureStatus s;
    { std::lock_guard<std::mutex> lk(statusMu_); s = status_; }

    // Connection indicator
    ImVec4 dot = s.connected
        ? ImVec4{0.2f, 0.9f, 0.2f, 1.0f}
        : ImVec4{0.9f, 0.3f, 0.3f, 1.0f};
    ImGui::TextColored(dot, s.connected ? "● Connected" : "● Disconnected");

    // Background learning progress bar
    if (config_.filterBackground && s.bgProgress < 1.0f && capturing_) {
        ImGui::Spacing();
        ImGui::Text("Learning background…");
        ImGui::ProgressBar(s.bgProgress, {-1, 0});
    }

    ImGui::Spacing();
    ImGui::Text("FPS:    %.1f", s.fps);
    ImGui::Text("Points: %d",   s.points);
    ImGui::Text("Frames: %d",   s.frameCount);

    if (!s.message.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", s.message.c_str());
    }
}

void CaptureApp::renderPreviewPanel() {
    uploadPreviewTextures();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float imgW  = avail * 0.5f - 4.0f;
    const float imgH  = imgW * kDepthH / kDepthW;

    ImGui::TextDisabled("Color (aligned to depth)");
    if (colorTex_)
        ImGui::Image((ImTextureID)(uintptr_t)colorTex_, {imgW, imgH});
    else
        ImGui::Dummy({imgW, imgH});

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextDisabled("Depth (false colour)");
    if (depthTex_)
        ImGui::Image((ImTextureID)(uintptr_t)depthTex_, {imgW, imgH});
    else
        ImGui::Dummy({imgW, imgH});
    ImGui::EndGroup();

    if (!colorTex_ || !depthTex_) {
        ImGui::Spacing();
        ImGui::TextDisabled("Waiting for first frame…");
    }
}

// ── Preview textures ──────────────────────────────────────────────────────────

void CaptureApp::updatePreviewBuffers(const Frame& frame) {
    if (frame.colorAligned.empty() || frame.depth.empty()) return;

    std::vector<uint8_t> color(kDepthW * kDepthH * 3);
    for (int i = 0; i < kDepthW * kDepthH; i++) {
        color[i * 3 + 0] = frame.colorAligned[i * 4 + 2]; // R
        color[i * 3 + 1] = frame.colorAligned[i * 4 + 1]; // G
        color[i * 3 + 2] = frame.colorAligned[i * 4 + 0]; // B
    }

    // False-colour depth via OpenCV JET colormap
    cv::Mat depthF(kDepthH, kDepthW, CV_32FC1,
                   const_cast<float*>(frame.depth.data()));
    cv::Mat depth8, heatmap;
    depthF.convertTo(depth8, CV_8UC1, 255.0 / 2300.0, -200.0 * 255.0 / 2300.0);
    cv::applyColorMap(depth8, heatmap, cv::COLORMAP_JET);

    std::vector<uint8_t> depth(kDepthW * kDepthH * 3);
    for (int i = 0; i < kDepthW * kDepthH; i++) {
        depth[i * 3 + 0] = heatmap.data[i * 3 + 2]; // R
        depth[i * 3 + 1] = heatmap.data[i * 3 + 1]; // G
        depth[i * 3 + 2] = heatmap.data[i * 3 + 0]; // B
    }

    std::lock_guard<std::mutex> lk(texMu_);
    colorBuf_ = std::move(color);
    depthBuf_ = std::move(depth);
    texDirty_ = true;
}

void CaptureApp::uploadPreviewTextures() {
    std::lock_guard<std::mutex> lk(texMu_);
    if (!texDirty_ || colorBuf_.empty() || depthBuf_.empty()) return;
    texDirty_ = false;

    auto upload = [](GLuint tex, const std::vector<uint8_t>& buf, int w, int h) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, buf.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    upload(colorTex_, colorBuf_, kDepthW, kDepthH);
    upload(depthTex_, depthBuf_, kDepthW, kDepthH);
}

// ── Capture thread ────────────────────────────────────────────────────────────

void CaptureApp::startCapture() {
    if (capturing_) return;
    stopFlag_ = false;
    capturing_ = true;
    captureThread_ = std::thread(&CaptureApp::captureLoop, this);
}

void CaptureApp::stopCapture() {
    if (!capturing_) return;
    stopFlag_ = true;
    if (captureThread_.joinable()) captureThread_.join();
    capturing_ = false;
    std::lock_guard<std::mutex> lk(statusMu_);
    status_ = {};
}

void CaptureApp::captureLoop() {
    // Snapshot config at thread start
    const std::string host      = config_.host;
    const std::string session   = config_.session;
    const std::string sensorId  = config_.sensorId;
    const bool        useBg     = config_.filterBackground;
    const bool        useBody   = config_.filterBody;

    Pipeline pipeline(std::make_unique<KinectV2Sensor>());

    // Register filters
    BackgroundSubtractorFilter* bgFilter = nullptr;
    if (useBg) {
        auto f = std::make_unique<BackgroundSubtractorFilter>();
        bgFilter = f.get();
        pipeline.addFilter(std::move(f));
    }
    if (useBody)
        pipeline.addFilter(std::make_unique<BodyFilter>());

    auto setMsg = [&](const std::string& m) {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = m;
    };

    setMsg("Initialising sensor…");
    if (!pipeline.initialize()) {
        setMsg("ERROR: Failed to initialise sensor.");
        capturing_ = false;
        return;
    }

    // Connect WebSocket
    const std::string wsUrl =
        "ws://" + host + "/ws?session=" + session +
        "&role=producer&sensor=" + sensorId;
    Sender sender(wsUrl);
    setMsg("Connecting to " + wsUrl + "…");
    if (!sender.connect()) {
        setMsg("ERROR: Connection failed.");
        pipeline.shutdown();
        capturing_ = false;
        return;
    }
    { std::lock_guard<std::mutex> lk(statusMu_); status_.connected = true; }

    // FPS tracking
    auto fpsStart  = std::chrono::steady_clock::now();
    int  fpsCnt    = 0;
    int  frameCnt  = 0;

    setMsg(useBg ? "Learning background — keep scene empty…" : "Streaming.");

    while (!stopFlag_) {
        PointCloud cloud = pipeline.process();
        if (cloud.empty()) continue;

        sender.send(cloud);

        if (config_.showPreview)
            updatePreviewBuffers(pipeline.lastFrame());

        frameCnt++;
        fpsCnt++;

        // Update status every second
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - fpsStart).count();
        if (elapsed >= 1.0f) {
            float fps = fpsCnt / elapsed;
            fpsCnt    = 0;
            fpsStart  = now;

            float bgProg = bgFilter ? bgFilter->progress() : 1.0f;
            std::string msg = (bgFilter && !bgFilter->isReady())
                ? "Learning background — keep scene empty…"
                : "Streaming.";

            std::lock_guard<std::mutex> lk(statusMu_);
            status_.fps        = fps;
            status_.points     = static_cast<int>(cloud.size());
            status_.frameCount = frameCnt;
            status_.bgProgress = bgProg;
            status_.message    = msg;
        }
    }

    sender.disconnect();
    pipeline.shutdown();
    std::lock_guard<std::mutex> lk(statusMu_);
    status_.connected = false;
}
