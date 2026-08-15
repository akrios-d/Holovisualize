#include "sensors/KinectV2Sensor.h"

#include <iostream>
#include <cstring>

KinectV2Sensor::KinectV2Sensor() = default;

KinectV2Sensor::~KinectV2Sensor() {
    shutdown();
}

bool KinectV2Sensor::initialize() {
    if (freenect2_.enumerateDevices() == 0) {
        std::cerr << "[KinectV2] No devices found.\n";
        return false;
    }

    // Try OpenGL pipeline first (GPU decode), fall back to CPU.
    pipeline_ = new libfreenect2::OpenGLPacketPipeline();
    device_   = freenect2_.openDevice(0, pipeline_);

    if (!device_) {
        std::cerr << "[KinectV2] OpenGL pipeline failed, trying CPU.\n";
        delete pipeline_;
        pipeline_ = new libfreenect2::CpuPacketPipeline();
        device_   = freenect2_.openDevice(0, pipeline_);
    }

    if (!device_) {
        std::cerr << "[KinectV2] Could not open device.\n";
        return false;
    }

    listener_ = new libfreenect2::SyncMultiFrameListener(
        libfreenect2::Frame::Color | libfreenect2::Frame::Depth
    );

    device_->setColorFrameListener(listener_);
    device_->setIrAndDepthFrameListener(listener_);

    if (!device_->start()) {
        std::cerr << "[KinectV2] Failed to start device.\n";
        return false;
    }

    registration_ = new libfreenect2::Registration(
        device_->getIrCameraParams(),
        device_->getColorCameraParams()
    );

    std::cout << "[KinectV2] Ready. Serial: " << device_->getSerialNumber() << "\n";
    return true;
}

bool KinectV2Sensor::captureFrame(Frame& frame) {
    libfreenect2::FrameMap frames;

    if (!listener_->waitForNewFrame(frames, 10 * 1000)) {
        std::cerr << "[KinectV2] Timeout waiting for frame.\n";
        return false;
    }

    libfreenect2::Frame* rgb   = frames[libfreenect2::Frame::Color];
    libfreenect2::Frame* depth = frames[libfreenect2::Frame::Depth];

    // Registration: undistort depth + align color to depth space.
    // Both output frames are 512x424, 4 bytes/pixel.
    libfreenect2::Frame undistorted(512, 424, 4);
    libfreenect2::Frame registered(512, 424, 4);
    registration_->apply(rgb, depth, &undistorted, &registered);

    // Depth (undistorted, float mm)
    const int depthPixels = 512 * 424;
    frame.depth.resize(depthPixels);
    std::memcpy(frame.depth.data(), undistorted.data, depthPixels * sizeof(float));

    auto ir = device_->getIrCameraParams();
    frame.depthIntrinsics = { ir.fx, ir.fy, ir.cx, ir.cy, 512, 424 };

    // Full-resolution color (BGRX, 1920x1080)
    frame.colorWidth  = (int)rgb->width;
    frame.colorHeight = (int)rgb->height;
    auto color = device_->getColorCameraParams();
    frame.colorIntrinsics = { color.fx, color.fy, color.cx, color.cy,
                              frame.colorWidth, frame.colorHeight };
    frame.color.resize(rgb->width * rgb->height * 4);
    std::memcpy(frame.color.data(), rgb->data, frame.color.size());

    // Color aligned to depth space (BGRX, 512x424)
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
    // pipeline_ lifetime is managed by freenect2_ after openDevice
}
