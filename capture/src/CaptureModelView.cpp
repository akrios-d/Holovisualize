#include "CaptureModelView.h"
#include "PointCloud.h"
#include "filters/BackgroundSubtractorFilter.h"
#include "sensors/KinectV2Sensor.h"
#define MAKE_SENSOR() std::make_unique<KinectV2Sensor>(parentGLContext_)

#include <algorithm>
#include <chrono>
#include <thread>

namespace {
const char* gestureName(HandGesture g) {
    switch (g) {
        case HandGesture::Fist:        return "Fist";
        case HandGesture::OpenHand:    return "Open Hand";
        case HandGesture::Pinch:       return "Pinch";
        case HandGesture::ThumbsUp:    return "Thumbs Up";
        case HandGesture::PointFinger: return "Point Finger";
        case HandGesture::Peace:       return "Peace";
        default:                       return "None";
    }
}
} // namespace

CaptureModelView::~CaptureModelView() {
    stopCapture();
}

// ── ICaptureView ────────────────────────────────────────────────────────────

CaptureStatus CaptureModelView::status() const {
    std::lock_guard<std::mutex> lk(statusMu_);
    return status_;
}

PreviewBuffers CaptureModelView::takePreviewBuffers() {
    std::lock_guard<std::mutex> lk(texMu_);
    if (!texDirty_) return {};
    texDirty_ = false;
    return previewBuf_;
}

// ── ICaptureModel ────────────────────────────────────────────────────────────

void CaptureModelView::startCapture(const CaptureConfig& cfg) {
    if (capturing_) return;

    activeConfig_ = cfg;

    // Pipeline construction (and therefore the OpenGL depth context, which
    // shares resources with the UI's window) must happen on this — the UI —
    // thread. Callers are required to invoke startCapture() from there.
    pipeline_ = std::make_unique<Pipeline>(MAKE_SENSOR());
    if (cfg.filterBackground)
        pipeline_->addFilter(std::make_unique<BackgroundSubtractorFilter>());
    pipeline_->segmentMinDepthMm = cfg.segmentMinDepthMm;
    pipeline_->segmentMaxDepthMm = cfg.segmentMaxDepthMm;

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

    stopFlag_  = false;
    capturing_ = true;
    streaming_ = true;
    captureThread_ = std::thread(&CaptureModelView::captureLoop, this);
}

void CaptureModelView::startPreviewOnly(const CaptureConfig& cfg) {
    if (capturing_) return;

    activeConfig_ = cfg;
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

    stopFlag_  = false;
    capturing_ = true;
    streaming_ = false;
    captureThread_ = std::thread(&CaptureModelView::previewOnlyLoop, this);
}

void CaptureModelView::stopCapture() {
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

void CaptureModelView::setDepthRange(float minMm, float maxMm) {
    activeConfig_.segmentMinDepthMm = minMm;
    activeConfig_.segmentMaxDepthMm = maxMm;
    if (pipeline_) {
        pipeline_->segmentMinDepthMm = minMm;
        pipeline_->segmentMaxDepthMm = maxMm;
    }
}

bool CaptureModelView::testServer(const CaptureConfig& cfg) {
    const std::string wsUrl = "ws://" + std::string(cfg.host) +
        "/ws?session=" + cfg.session +
        "&role=producer&sensor=" + cfg.sensorId;
    {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.message = "Testing " + wsUrl + "…";
    }

    Sender testSender(wsUrl);
    bool ok = testSender.connect();
    testSender.disconnect();

    std::lock_guard<std::mutex> lk(statusMu_);
    status_.message = ok ? "Server reachable." : "ERROR: Server unreachable.";
    return ok;
}

// ── Background threads ────────────────────────────────────────────────────────

void CaptureModelView::previewOnlyLoop() {
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

        if (activeConfig_.enableGestures) {
            if (++gestureFrameSkip_ >= 4) {
                gestureFrameSkip_ = 0;
                HandDetection det;
                runGestureDetection(frame, det); // status_.lastGesture only — no server, nothing to send
            }
        }

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

void CaptureModelView::captureLoop() {
    const std::string host      = activeConfig_.host;
    const std::string session   = activeConfig_.session;
    const std::string sensorId  = activeConfig_.sensorId;
    const bool        useBg     = activeConfig_.filterBackground;

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
        // must happen on the UI thread — see ICaptureView::needsTeardown().
        stopFlag_ = true;
        return;
    }
    { std::lock_guard<std::mutex> lk(statusMu_); status_.connected = true; }

    auto fpsStart  = std::chrono::steady_clock::now();
    int  fpsCnt    = 0;
    int  frameCnt  = 0;
    int  lostCnt   = 0;

    setMsg(useBg ? "Learning background — keep scene empty…" : "Streaming.");

    // Preview is always off while actually streaming — one less GL context
    // switch competing with the packet decoders' own contexts (see
    // CaptureViewController::initWindow()'s vsync comment for the rest of
    // this contention story) — so no updatePreviewBuffers() call here.
    while (!stopFlag_) {
        PointCloud cloud = pipeline_->process();
        if (cloud.empty()) continue;

        sender_->send(cloud);

        if (activeConfig_.enableGestures) {
            // MediaPipe inference is far slower than depth capture — only
            // sample every few frames (~8-10Hz at 30fps capture) so the
            // sidecar round-trip never becomes the bottleneck.
            if (++gestureFrameSkip_ >= 4) {
                gestureFrameSkip_ = 0;
                detectAndSendGesture(pipeline_->lastFrame());
            }
        }

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

// ── Gesture detection ──────────────────────────────────────────────────────────

// Round-trips one frame through the sidecar and updates status_.lastGesture.
// Returns true (with `det` filled) on an edge-triggered gesture change —
// i.e. the same held gesture only returns true once, not every call — so
// callers that go on to spawn a server-side effect don't spam it every
// ~100ms (EffectGenerator::onGestures spawns unconditionally per event).
// Split out from detectAndSendGesture() so previewOnlyLoop() can exercise
// the capture<->sidecar round trip without needing a live server connection
// — useful for isolating sidecar/network issues from the streaming path.
bool CaptureModelView::runGestureDetection(const Frame& frame, HandDetection& det) {
    const auto& intr = frame.depthIntrinsics;
    const int   w    = intr.width;
    const int   h    = intr.height;
    const size_t expected = static_cast<size_t>(w) * h;
    if (frame.colorAligned.size() < expected * 4 || frame.depth.size() < expected)
        return false;

    // colorAligned is BGRX (see PointCloud.cpp) — the sidecar wants plain RGB.
    std::vector<uint8_t> rgb(expected * 3);
    for (size_t i = 0; i < expected; i++) {
        rgb[i * 3 + 0] = frame.colorAligned[i * 4 + 2]; // R
        rgb[i * 3 + 1] = frame.colorAligned[i * 4 + 1]; // G
        rgb[i * 3 + 2] = frame.colorAligned[i * 4 + 0]; // B
    }

    if (!gestureClient_.detect(rgb.data(), w, h, det)) {
        lastSentGesture_ = HandGesture::None; // hand lost — re-arm edge trigger
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(statusMu_);
        status_.lastGesture = gestureName(det.gesture);
    }

    if (det.gesture == lastSentGesture_) return false;
    lastSentGesture_ = det.gesture;
    return true;
}

void CaptureModelView::detectAndSendGesture(const Frame& frame) {
    HandDetection det;
    if (!runGestureDetection(frame, det)) return;

    const auto& intr = frame.depthIntrinsics;
    const int   w    = intr.width;
    const int   h    = intr.height;

    // Look up real depth at the wrist landmark and project to camera space —
    // same formula as generatePointCloud() (including the Y-up flip), so
    // the position lands in the same space as the point cloud it's mixed
    // with server-side.
    int u = static_cast<int>(det.wristU * w);
    int v = static_cast<int>(det.wristV * h);
    u = std::clamp(u, 0, w - 1);
    v = std::clamp(v, 0, h - 1);
    const float d = frame.depth[static_cast<size_t>(v) * w + u];
    if (d <= 0.f) return; // no depth at that pixel — skip rather than send garbage

    const float z = d / 1000.0f;
    const float x = (u - intr.cx) * z / intr.fx;
    const float y = -(v - intr.cy) * z / intr.fy;

    sender_->sendGesture(det.gesture, x, y, z, det.confidence);
}

// ── Preview textures ──────────────────────────────────────────────────────────

void CaptureModelView::updatePreviewBuffers(const Frame& frame) {
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
    previewBuf_.color      = std::move(color);
    previewBuf_.depth      = std::move(depth);
    previewBuf_.ir         = std::move(ir);
    previewBuf_.registered = std::move(registered);
    texDirty_ = true;
}
