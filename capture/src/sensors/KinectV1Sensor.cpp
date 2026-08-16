#include "sensors/KinectV1Sensor.h"
#include "Frame.h"

#include <libfreenect/libfreenect.h>

#include <iostream>
#include <chrono>
#include <cstring>

// ── Kinect v1 constants ───────────────────────────────────────────────────────
static constexpr int kW = 640;
static constexpr int kH = 480;

// Approximate Kinect v1 depth intrinsics (factory calibration average)
static constexpr float kFx = 594.21f;
static constexpr float kFy = 591.04f;
static constexpr float kCx = 339.50f;
static constexpr float kCy = 242.70f;

// ── libfreenect C callbacks ───────────────────────────────────────────────────
static void depthCb(freenect_device* dev, void* data, uint32_t /*timestamp*/) {
    auto* self = static_cast<KinectV1Sensor*>(freenect_get_user(dev));
    self->onDepth(data);
}

static void videoCb(freenect_device* dev, void* data, uint32_t /*timestamp*/) {
    auto* self = static_cast<KinectV1Sensor*>(freenect_get_user(dev));
    self->onVideo(data);
}

// ── KinectV1Sensor ────────────────────────────────────────────────────────────
KinectV1Sensor::KinectV1Sensor() {
    depthBuf_.resize(kW * kH);
    colorBuf_.resize(kW * kH * 3);
}

KinectV1Sensor::~KinectV1Sensor() {
    shutdown();
}

bool KinectV1Sensor::initialize() {
    shutdown();

    if (freenect_init(&ctx_, nullptr) < 0) {
        std::cerr << "[KinectV1] freenect_init failed.\n";
        return false;
    }

    freenect_set_log_level(ctx_, FREENECT_LOG_WARNING);
    freenect_select_subdevices(ctx_,
        static_cast<freenect_device_flags>(FREENECT_DEVICE_CAMERA));

    if (freenect_num_devices(ctx_) < 1) {
        std::cerr << "[KinectV1] No Kinect v1 devices found.\n";
        freenect_shutdown(ctx_); ctx_ = nullptr;
        return false;
    }

    if (freenect_open_device(ctx_, &dev_, 0) < 0) {
        std::cerr << "[KinectV1] Could not open device.\n";
        freenect_shutdown(ctx_); ctx_ = nullptr;
        return false;
    }

    freenect_set_user(dev_, this);
    freenect_set_depth_callback(dev_, depthCb);
    freenect_set_video_callback(dev_, videoCb);

    freenect_set_depth_mode(dev_,
        freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_MM));
    freenect_set_video_mode(dev_,
        freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB));

    freenect_start_depth(dev_);
    freenect_start_video(dev_);

    running_ = true;
    eventThread_ = std::thread([this] {
        timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000; // 100 ms
        while (running_) {
            int ret = freenect_process_events_timeout(ctx_, &tv);
            if (ret < 0 && ret != -10) // -10 = LIBUSB_ERROR_INTERRUPTED, ignore
                break;
        }
    });

    std::cout << "[KinectV1] Ready.\n";
    return true;
}

void KinectV1Sensor::onDepth(void* data) {
    std::lock_guard<std::mutex> lk(frameMutex_);
    std::memcpy(depthBuf_.data(), data, kW * kH * sizeof(uint16_t));
    depthReady_ = true;
    frameCv_.notify_one();
}

void KinectV1Sensor::onVideo(void* data) {
    std::lock_guard<std::mutex> lk(frameMutex_);
    std::memcpy(colorBuf_.data(), data, kW * kH * 3);
    colorReady_ = true;
    frameCv_.notify_one();
}

bool KinectV1Sensor::captureFrame(Frame& out) {
    std::unique_lock<std::mutex> lk(frameMutex_);
    depthReady_ = false;
    colorReady_ = false;
    lk.unlock();

    // Wait for both depth and color frames (5 s timeout)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    {
        std::unique_lock<std::mutex> lk2(frameMutex_);
        bool ok = frameCv_.wait_until(lk2, deadline,
            [this] { return depthReady_ && colorReady_; });
        if (!ok) {
            std::cerr << "[KinectV1] Timeout waiting for frame.\n";
            return false;
        }

        // ── Depth (uint16 mm → float mm) ──────────────────────────────────
        out.depth.resize(kW * kH);
        for (int i = 0; i < kW * kH; ++i)
            out.depth[i] = depthBuf_[i] > 0 ? static_cast<float>(depthBuf_[i]) : 0.f;

        // ── Color (RGB → BGRX) ────────────────────────────────────────────
        out.colorWidth  = kW;
        out.colorHeight = kH;
        out.color.resize(kW * kH * 4);
        for (int i = 0; i < kW * kH; ++i) {
            out.color[i * 4 + 0] = colorBuf_[i * 3 + 2]; // B
            out.color[i * 4 + 1] = colorBuf_[i * 3 + 1]; // G
            out.color[i * 4 + 2] = colorBuf_[i * 3 + 0]; // R
            out.color[i * 4 + 3] = 0;                     // X
        }

        // ── colorAligned: not implemented — use color as-is ───────────────
        out.colorAligned = out.color;
    }

    out.depthIntrinsics  = { kFx, kFy, kCx, kCy, kW, kH };
    out.colorIntrinsics  = { kFx, kFy, kCx, kCy, kW, kH };
    out.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    return true;
}

void KinectV1Sensor::shutdown() {
    running_ = false;
    if (eventThread_.joinable())
        eventThread_.join();
    if (dev_) {
        freenect_stop_depth(dev_);
        freenect_stop_video(dev_);
        freenect_close_device(dev_);
        dev_ = nullptr;
    }
    if (ctx_) {
        freenect_shutdown(ctx_);
        ctx_ = nullptr;
    }
}
