#include "CaptureApp.h"
#include "PointCloud.h"
#include "sensors/KinectV2Sensor.h"
#define MAKE_SENSOR() std::make_unique<KinectV2Sensor>(window_)

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstring>
#include <iostream>

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

CaptureApp::CaptureApp(CaptureConfig initial) : config_(initial) {}

CaptureApp::~CaptureApp() {
    stopCapture();
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

// ── Window / ImGui init ───────────────────────────────────────────────────────

bool CaptureApp::initWindow() {
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

void CaptureApp::run() {
    if (!initWindow()) return;

    // UI redraw rate cap (vsync is off — see initWindow()). 30Hz is plenty
    // for an ImGui status panel and leaves the GPU free for the capture
    // pipeline's own GL contexts instead of racing them every vblank.
    const auto frameInterval = std::chrono::milliseconds(1000 / 30);

    while (!glfwWindowShouldClose(window_)) {
        auto frameStart = std::chrono::steady_clock::now();

        glfwPollEvents();

        // The capture thread self-signals stopFlag_ if it exits on its own
        // (e.g. failed to connect to the server) — finish tearing it down
        // here, since pipeline_ teardown must happen on this (UI) thread.
        if (capturing_ && stopFlag_) stopCapture();

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
    stopCapture();
}

// ── UI panels ─────────────────────────────────────────────────────────────────

void CaptureApp::renderFrame() {
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

void CaptureApp::renderConfigPanel() {
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
    if (ImGui::Button("Test Server")) testServer();

    ImGui::Spacing();
    ImGui::SeparatorText("Scene Filters");
    ImGui::Checkbox("Body bounds filter",     &config_.filterBody);
    ImGui::Checkbox("Background subtraction", &config_.filterBackground);

    ImGui::Spacing();
    ImGui::SeparatorText("Display");
    ImGui::BeginDisabled(streaming_);
    ImGui::Checkbox("Show preview", &config_.showPreview);
    ImGui::EndDisabled();
    if (streaming_)
        ImGui::TextDisabled("Preview disabled while streaming (avoids GPU contention).");

    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::Button("Connect & Stream")) startCapture();
    ImGui::Spacing();
    if (ImGui::Button("Preview Only")) startPreviewOnly();
    ImGui::Spacing();
    if (ImGui::Button("Disconnect")) stopCapture();
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
    ImGui::Text("Lost:   %d",   s.lostTotal);

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

void CaptureApp::updatePreviewBuffers(const Frame& frame) {
    const int expected = kDepthW * kDepthH;
    if (frame.colorAligned.size() < static_cast<size_t>(expected * 4)) return;
    if (frame.depth.size()        < static_cast<size_t>(expected))     return;

    // libfreenect2 registered frame is BGRX on Windows (B=0, G=1, R=2, X=3).
    std::vector<uint8_t> registered(kDepthW * kDepthH * 3);
    bool allBlack = true;
    for (int i = 0; i < kDepthW * kDepthH; i++) {
        uint8_t b = frame.colorAligned[i * 4 + 0];
        uint8_t g = frame.colorAligned[i * 4 + 1];
        uint8_t r = frame.colorAligned[i * 4 + 2];
        registered[i * 3 + 0] = r;
        registered[i * 3 + 1] = g;
        registered[i * 3 + 2] = b;
        if (r || g || b) allBlack = false;
    }
    // If all black, try RGBX layout (some libfreenect2 builds use this)
    if (allBlack) {
        for (int i = 0; i < kDepthW * kDepthH; i++) {
            registered[i * 3 + 0] = frame.colorAligned[i * 4 + 0]; // R
            registered[i * 3 + 1] = frame.colorAligned[i * 4 + 1]; // G
            registered[i * 3 + 2] = frame.colorAligned[i * 4 + 2]; // B
        }
    }

    // Full-res colour, nearest-neighbour downsampled to the depth resolution
    // for display purposes.
    std::vector<uint8_t> color(kDepthW * kDepthH * 3, 0);
    if (frame.colorWidth > 0 && frame.colorHeight > 0 &&
        frame.color.size() >= static_cast<size_t>(frame.colorWidth * frame.colorHeight * 4)) {
        for (int y = 0; y < kDepthH; y++) {
            int sy = y * frame.colorHeight / kDepthH;
            for (int x = 0; x < kDepthW; x++) {
                int sx = x * frame.colorWidth / kDepthW;
                size_t src = (static_cast<size_t>(sy) * frame.colorWidth + sx) * 4;
                size_t dst = (static_cast<size_t>(y) * kDepthW + x) * 3;
                color[dst + 0] = frame.color[src + 2]; // R
                color[dst + 1] = frame.color[src + 1]; // G
                color[dst + 2] = frame.color[src + 0]; // B
            }
        }
    }

    // False-colour depth — JET colormap implemented inline (no OpenCV needed).
    // Maps depth [200 mm .. 2500 mm] → blue(near) .. red(far).
    std::vector<uint8_t> depth(kDepthW * kDepthH * 3);
    for (int i = 0; i < kDepthW * kDepthH; i++) {
        float d = frame.depth[i];
        float t = (d <= 0.f) ? 0.f
                             : std::max(0.f, std::min(1.f, (d - 200.f) / 2100.f));
        // JET: blue→cyan→green→yellow→red
        float r = std::min(1.f, std::max(0.f, 1.5f - std::abs(t * 4.f - 3.f)));
        float g = std::min(1.f, std::max(0.f, 1.5f - std::abs(t * 4.f - 2.f)));
        float b = std::min(1.f, std::max(0.f, 1.5f - std::abs(t * 4.f - 1.f)));
        depth[i * 3 + 0] = static_cast<uint8_t>(r * 255);
        depth[i * 3 + 1] = static_cast<uint8_t>(g * 255);
        depth[i * 3 + 2] = static_cast<uint8_t>(b * 255);
    }

    // IR — grayscale, normalised the same way Protonect's viewer does (/4500).
    std::vector<uint8_t> ir(kDepthW * kDepthH * 3, 0);
    if (frame.ir.size() >= static_cast<size_t>(expected)) {
        for (int i = 0; i < kDepthW * kDepthH; i++) {
            float v = std::max(0.f, std::min(1.f, frame.ir[i] / 4500.f));
            uint8_t g = static_cast<uint8_t>(v * 255);
            ir[i * 3 + 0] = g;
            ir[i * 3 + 1] = g;
            ir[i * 3 + 2] = g;
        }
    }

    std::lock_guard<std::mutex> lk(texMu_);
    colorBuf_      = std::move(color);
    depthBuf_      = std::move(depth);
    irBuf_         = std::move(ir);
    registeredBuf_ = std::move(registered);
    texDirty_      = true;
}

void CaptureApp::uploadPreviewTextures() {
    std::lock_guard<std::mutex> lk(texMu_);
    if (!texDirty_ || colorBuf_.empty() || depthBuf_.empty()) return;
    texDirty_ = false;

    auto upload = [](GLuint tex, const std::vector<uint8_t>& buf, int w, int h) {
        if (buf.empty()) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, buf.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    upload(colorTex_,      colorBuf_,      kDepthW, kDepthH);
    upload(depthTex_,      depthBuf_,      kDepthW, kDepthH);
    upload(irTex_,         irBuf_,         kDepthW, kDepthH);
    upload(registeredTex_, registeredBuf_, kDepthW, kDepthH);
}

// ── Capture thread ────────────────────────────────────────────────────────────

void CaptureApp::startCapture() {
    if (capturing_) return;

    // Pipeline construction (and therefore the OpenGL depth context, which
    // shares resources with window_) must happen on this — the UI — thread.
    pipeline_ = std::make_unique<Pipeline>(MAKE_SENSOR());
    if (config_.filterBackground)
        pipeline_->addFilter(std::make_unique<BackgroundSubtractorFilter>());
    if (config_.filterBody)
        pipeline_->addFilter(std::make_unique<BodyFilter>());

    {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_ = {};
        status_.message = "Initialising sensor…";
    }
    if (!pipeline_->initialize()) {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = "ERROR: Failed to initialise sensor.";
        pipeline_.reset();
        return;
    }

    // Preview texture uploads add another GL context switch on top of the
    // packet processors' own — disable it while actually streaming to keep
    // full decode throughput (see initWindow()'s vsync comment for the rest
    // of this contention story).
    config_.showPreview = false;

    stopFlag_  = false;
    capturing_ = true;
    streaming_ = true;
    captureThread_ = std::thread(&CaptureApp::captureLoop, this);
}

void CaptureApp::startPreviewOnly() {
    if (capturing_) return;

    pipeline_ = std::make_unique<Pipeline>(MAKE_SENSOR());

    {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_ = {};
        status_.message = "Initialising sensor…";
    }
    if (!pipeline_->initialize()) {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = "ERROR: Failed to initialise sensor.";
        pipeline_.reset();
        return;
    }

    config_.showPreview = true;
    stopFlag_  = false;
    capturing_ = true;
    captureThread_ = std::thread(&CaptureApp::previewOnlyLoop, this);
}

void CaptureApp::stopCapture() {
    if (!capturing_) return;
    stopFlag_ = true;
    if (captureThread_.joinable()) captureThread_.join();
    capturing_ = false;
    streaming_ = false;

    // Runs on the UI thread — matches where the GL-context-owning pipeline
    // was constructed, which OpenGLPacketPipeline's teardown requires.
    if (sender_)   { sender_->disconnect(); sender_.reset(); }
    if (pipeline_) { pipeline_->shutdown(); pipeline_.reset(); }

    std::lock_guard<std::mutex> lk(statusMu_);
    status_ = {};
}

void CaptureApp::previewOnlyLoop() {
    auto setMsg = [&](const std::string& m) {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = m;
    };

    // Give the Kinect hardware a moment to stabilise after start — first
    // packets are often malformed and can crash registration_->apply() if
    // consumed immediately.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    { std::lock_guard<std::mutex> lk(statusMu_); status_.connected = true; }
    setMsg("Preview only — not connected to server.");

    auto fpsStart = std::chrono::steady_clock::now();
    int  fpsCnt   = 0;
    int  frameCnt = 0;
    int  lostCnt  = 0;
    int  warmup   = 10; // discard the first few frames — still unstable

    while (!stopFlag_) {
        Frame frame;
        if (!pipeline_->sensor().captureFrame(frame)) continue;
        if (warmup > 0) { --warmup; continue; }

        updatePreviewBuffers(frame);

        fpsCnt++;
        frameCnt++;
        lostCnt += frame.framesLost;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - fpsStart).count();
        if (elapsed >= 1.0f) {
            float fps = fpsCnt / elapsed;
            fpsCnt    = 0;
            fpsStart  = now;
            std::lock_guard<std::mutex> lk(statusMu_);
            status_.fps        = fps;
            status_.frameCount = frameCnt;
            status_.lostTotal  = lostCnt;
            status_.points     = 0;
            status_.message    = "Preview only — not connected to server.";
        }
    }

    std::lock_guard<std::mutex> lk(statusMu_);
    status_.connected = false;
}

void CaptureApp::captureLoop() {
    const std::string host      = config_.host;
    const std::string session   = config_.session;
    const std::string sensorId  = config_.sensorId;
    const bool        useBg     = config_.filterBackground;

    auto setMsg = [&](const std::string& m) {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = m;
    };

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    const std::string wsUrl =
        "ws://" + host + "/ws?session=" + session +
        "&role=producer&sensor=" + sensorId;
    sender_ = std::make_unique<Sender>(wsUrl);
    setMsg("Connecting to " + wsUrl + "…");
    if (!sender_->connect()) {
        setMsg("ERROR: Connection failed.");
        sender_.reset();
        // Self-signal stop: this thread is exiting, but pipeline_ teardown
        // must happen on the UI thread (see run()'s stopFlag_ poll below).
        stopFlag_ = true;
        return;
    }
    { std::lock_guard<std::mutex> lk(statusMu_); status_.connected = true; }

    auto fpsStart  = std::chrono::steady_clock::now();
    int  fpsCnt    = 0;
    int  frameCnt  = 0;
    int  lostCnt   = 0;

    double processMsSum = 0.0;
    double sendMsSum    = 0.0;

    setMsg(useBg ? "Learning background — keep scene empty…" : "Streaming.");

    while (!stopFlag_) {
        auto t0 = std::chrono::steady_clock::now();
        PointCloud cloud = pipeline_->process();
        auto t1 = std::chrono::steady_clock::now();
        if (cloud.empty()) continue;

        sender_->send(cloud);
        auto t2 = std::chrono::steady_clock::now();

        processMsSum += std::chrono::duration<double, std::milli>(t1 - t0).count();
        sendMsSum    += std::chrono::duration<double, std::milli>(t2 - t1).count();

        if (config_.showPreview)
            updatePreviewBuffers(pipeline_->lastFrame());

        frameCnt++;
        fpsCnt++;
        lostCnt += pipeline_->lastFrame().framesLost;

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - fpsStart).count();
        if (elapsed >= 1.0f) {
            float fps = fpsCnt / elapsed;
            fpsCnt    = 0;
            fpsStart  = now;

            auto* bgFilter = pipeline_->getFilter<BackgroundSubtractorFilter>();
            float bgProg = bgFilter ? bgFilter->progress() : 1.0f;
            std::string msg = (bgFilter && !bgFilter->isReady())
                ? "Learning background — keep scene empty…"
                : "Streaming.";

            std::cout << "[Timing] process avg: " << (processMsSum / frameCnt)
                      << "ms | send avg: " << (sendMsSum / frameCnt) << "ms\n";

            std::lock_guard<std::mutex> lk(statusMu_);
            status_.fps        = fps;
            status_.points     = static_cast<int>(cloud.size());
            status_.frameCount = frameCnt;
            status_.lostTotal  = lostCnt;
            status_.bgProgress = bgProg;
            status_.message    = msg;
        }
    }
}

void CaptureApp::testServer() {
    const std::string wsUrl = "ws://" + std::string(config_.host) +
        "/ws?session=" + config_.session +
        "&role=producer&sensor=" + config_.sensorId;
    {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = "Testing " + wsUrl + "…";
    }

    Sender testSender(wsUrl);
    bool ok = testSender.connect();
    testSender.disconnect();

    std::lock_guard<std::mutex> lk(statusMu_);
    status_.message = ok ? "Server reachable." : "ERROR: Server unreachable.";
}
