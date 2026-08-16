#include "sensors/KinectV2Sensor.h"

#ifdef HOLOVISUALIZE_KINECT_SDK

// ── Official Kinect for Windows SDK 2.0 implementation ───────────────────────

#include <Kinect.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

KinectV2Sensor::KinectV2Sensor() = default;

KinectV2Sensor::~KinectV2Sensor() {
    shutdown();
}

bool KinectV2Sensor::initialize() {
    shutdown();

    HRESULT hr = GetDefaultKinectSensor(&sensor_);
    if (FAILED(hr) || !sensor_) {
        std::cerr << "[KinectV2] No Kinect sensor found (hr=0x" << std::hex << hr << ").\n";
        return false;
    }

    hr = sensor_->Open();
    if (FAILED(hr)) {
        std::cerr << "[KinectV2] Failed to open sensor (hr=0x" << std::hex << hr << ").\n";
        sensor_->Release(); sensor_ = nullptr;
        return false;
    }

    hr = sensor_->OpenMultiSourceFrameReader(
        FrameSourceTypes_Color | FrameSourceTypes_Depth,
        &reader_);
    if (FAILED(hr)) {
        std::cerr << "[KinectV2] Failed to open frame reader (hr=0x" << std::hex << hr << ").\n";
        shutdown();
        return false;
    }

    hr = sensor_->get_CoordinateMapper(&mapper_);
    if (FAILED(hr)) {
        std::cerr << "[KinectV2] Failed to get coordinate mapper (hr=0x" << std::hex << hr << ").\n";
        shutdown();
        return false;
    }

    // The SDK needs a moment after Open() before frames are available.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::cout << "[KinectV2] Ready (Kinect SDK 2.0).\n";
    return true;
}

bool KinectV2Sensor::captureFrame(Frame& out) {
    constexpr int kDepthW = 512;
    constexpr int kDepthH = 424;
    constexpr int kColorW = 1920;
    constexpr int kColorH = 1080;

    // Poll until a frame arrives or timeout (5 s).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    IMultiSourceFrame* msFrame = nullptr;
    HRESULT hr = E_PENDING;

    while (hr == E_PENDING) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "[KinectV2] Timeout waiting for frame.\n";
            return false;
        }
        hr = reader_->AcquireLatestFrame(&msFrame);
        if (hr == E_PENDING)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (FAILED(hr)) {
        std::cerr << "[KinectV2] AcquireLatestFrame failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // ── Depth ─────────────────────────────────────────────────────────────────
    IDepthFrameReference* depthRef = nullptr;
    IDepthFrame*          depthFrame = nullptr;
    msFrame->get_DepthFrameReference(&depthRef);
    if (depthRef) {
        depthRef->AcquireFrame(&depthFrame);
        depthRef->Release();
    }

    // ── Color ─────────────────────────────────────────────────────────────────
    IColorFrameReference* colorRef = nullptr;
    IColorFrame*          colorFrame = nullptr;
    msFrame->get_ColorFrameReference(&colorRef);
    if (colorRef) {
        colorRef->AcquireFrame(&colorFrame);
        colorRef->Release();
    }

    msFrame->Release();

    if (!depthFrame || !colorFrame) {
        if (depthFrame) depthFrame->Release();
        if (colorFrame) colorFrame->Release();
        return false;
    }

    // ── Fill depth (uint16 mm → float mm) ────────────────────────────────────
    constexpr int kDepthPixels = kDepthW * kDepthH;
    std::vector<UINT16> depthBuf(kDepthPixels);
    hr = depthFrame->CopyFrameDataToArray(kDepthPixels, depthBuf.data());
    depthFrame->Release();
    if (FAILED(hr)) { colorFrame->Release(); return false; }

    out.depth.resize(kDepthPixels);
    for (int i = 0; i < kDepthPixels; ++i)
        out.depth[i] = depthBuf[i] > 0 ? static_cast<float>(depthBuf[i]) : 0.f;

    // ── Fill color (BGRA 1920×1080) ───────────────────────────────────────────
    constexpr int kColorPixels = kColorW * kColorH;
    out.color.resize(kColorPixels * 4);
    hr = colorFrame->CopyConvertedFrameDataToArray(
        static_cast<UINT>(out.color.size()), out.color.data(), ColorImageFormat_Bgra);
    colorFrame->Release();
    if (FAILED(hr)) return false;

    out.colorWidth  = kColorW;
    out.colorHeight = kColorH;

    // ── Color aligned to depth (BGRX 512×424) ────────────────────────────────
    std::vector<ColorSpacePoint> colorPoints(kDepthPixels);
    mapper_->MapDepthFrameToColorSpace(kDepthPixels, depthBuf.data(),
                                       kDepthPixels, colorPoints.data());

    out.colorAligned.assign(kDepthPixels * 4, 0);
    for (int i = 0; i < kDepthPixels; ++i) {
        int cx = static_cast<int>(colorPoints[i].X + 0.5f);
        int cy = static_cast<int>(colorPoints[i].Y + 0.5f);
        if (cx >= 0 && cx < kColorW && cy >= 0 && cy < kColorH) {
            int src = (cy * kColorW + cx) * 4;
            int dst = i * 4;
            out.colorAligned[dst + 0] = out.color[src + 0]; // B
            out.colorAligned[dst + 1] = out.color[src + 1]; // G
            out.colorAligned[dst + 2] = out.color[src + 2]; // R
            out.colorAligned[dst + 3] = 0;                  // X (padding)
        }
    }

    // ── Intrinsics ────────────────────────────────────────────────────────────
    CameraIntrinsics ki{};
    mapper_->GetDepthCameraIntrinsics(&ki);
    out.depthIntrinsics = { ki.FocalLengthX, ki.FocalLengthY,
                            ki.PrincipalPointX, ki.PrincipalPointY,
                            kDepthW, kDepthH };

    // Kinect SDK doesn't expose colour intrinsics directly; use known constants.
    out.colorIntrinsics = { 1081.37f, 1081.37f, 959.5f, 539.5f, kColorW, kColorH };

    out.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    return true;
}

void KinectV2Sensor::shutdown() {
    if (mapper_)  { mapper_->Release();  mapper_  = nullptr; }
    if (reader_)  { reader_->Release();  reader_  = nullptr; }
    if (sensor_)  { sensor_->Close(); sensor_->Release(); sensor_ = nullptr; }
}

#else

// ── libfreenect2 implementation ───────────────────────────────────────────────

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

KinectV2Sensor::KinectV2Sensor() = default;

KinectV2Sensor::~KinectV2Sensor() {
    shutdown();
}

bool KinectV2Sensor::initialize() {
    shutdown();

    constexpr int kMaxRetries = 3;
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        if (freenect2_.enumerateDevices() == 0) {
            std::cerr << "[KinectV2] No devices found (attempt " << attempt << ").\n";
            if (attempt < kMaxRetries)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        pipeline_ = new libfreenect2::CpuPacketPipeline();
        device_   = freenect2_.openDevice(0, pipeline_);
        if (!device_) {
            std::cerr << "[KinectV2] Could not open device (attempt " << attempt << ").\n";
            delete pipeline_; pipeline_ = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        listener_ = new libfreenect2::SyncMultiFrameListener(
            libfreenect2::Frame::Color | libfreenect2::Frame::Depth);
        device_->setColorFrameListener(listener_);
        device_->setIrAndDepthFrameListener(listener_);

        if (!device_->start()) {
            std::cerr << "[KinectV2] Failed to start device (attempt " << attempt << ").\n";
            device_->close();
            device_ = nullptr;
            delete listener_; listener_ = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        registration_ = new libfreenect2::Registration(
            device_->getIrCameraParams(),
            device_->getColorCameraParams());

        std::cout << "[KinectV2] Ready. Serial: " << device_->getSerialNumber() << "\n";
        return true;
    }

    std::cerr << "[KinectV2] Failed to initialise after " << kMaxRetries << " attempts.\n";
    return false;
}

bool KinectV2Sensor::captureFrame(Frame& frame) {
    libfreenect2::FrameMap frames;

    if (!listener_->waitForNewFrame(frames, 10 * 1000)) {
        std::cerr << "[KinectV2] Timeout waiting for frame.\n";
        return false;
    }

    libfreenect2::Frame* rgb   = frames[libfreenect2::Frame::Color];
    libfreenect2::Frame* depth = frames[libfreenect2::Frame::Depth];

    libfreenect2::Frame undistorted(512, 424, 4);
    libfreenect2::Frame registered(512, 424, 4);
    registration_->apply(rgb, depth, &undistorted, &registered);

    const int depthPixels = 512 * 424;
    frame.depth.resize(depthPixels);
    std::memcpy(frame.depth.data(), undistorted.data, depthPixels * sizeof(float));

    auto ir = device_->getIrCameraParams();
    frame.depthIntrinsics = { ir.fx, ir.fy, ir.cx, ir.cy, 512, 424 };

    frame.colorWidth  = (int)rgb->width;
    frame.colorHeight = (int)rgb->height;
    auto color = device_->getColorCameraParams();
    frame.colorIntrinsics = { color.fx, color.fy, color.cx, color.cy,
                              frame.colorWidth, frame.colorHeight };
    frame.color.resize(rgb->width * rgb->height * 4);
    std::memcpy(frame.color.data(), rgb->data, frame.color.size());

    frame.colorAligned.resize(depthPixels * 4);
    std::memcpy(frame.colorAligned.data(), registered.data, frame.colorAligned.size());

    frame.timestamp = depth->timestamp;

    listener_->release(frames);
    return true;
}

void KinectV2Sensor::shutdown() {
    if (device_) {
        device_->stop();
        device_->close();
        device_ = nullptr;
    }
    delete listener_;     listener_     = nullptr;
    delete registration_; registration_ = nullptr;
}

#endif // HOLOVISUALIZE_KINECT_SDK
