#include "sensors/KinectV2Sensor.h"

#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

KinectV2Sensor::KinectV2Sensor(void* parentGLContext) : parentGLContext_(parentGLContext) {}

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

        pipeline_ = new libfreenect2::OpenGLPacketPipeline(parentGLContext_);
        device_   = freenect2_.openDevice(0, pipeline_);
        if (!device_) {
            std::cerr << "[KinectV2] Could not open device (attempt " << attempt << ").\n";
            delete pipeline_; pipeline_ = nullptr;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        listener_ = new libfreenect2::SyncMultiFrameListener(
            libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);
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
    libfreenect2::Frame* ir    = frames[libfreenect2::Frame::Ir];
    libfreenect2::Frame* depth = frames[libfreenect2::Frame::Depth];

    libfreenect2::Frame undistorted(512, 424, 4);
    libfreenect2::Frame registered(512, 424, 4);
    registration_->apply(rgb, depth, &undistorted, &registered);

    const int depthPixels = 512 * 424;
    frame.depth.resize(depthPixels);
    std::memcpy(frame.depth.data(), undistorted.data, depthPixels * sizeof(float));

    frame.ir.resize(depthPixels);
    std::memcpy(frame.ir.data(), ir->data, depthPixels * sizeof(float));

    auto irParams = device_->getIrCameraParams();
    frame.depthIntrinsics = { irParams.fx, irParams.fy, irParams.cx, irParams.cy, 512, 424 };

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
        // close() does not delete the pipeline passed to openDevice() — only
        // the Freenect2DeviceImpl destructor does. Previously this just
        // nulled the pointer, leaking the device (and its pipeline). Harmless
        // with CpuPacketPipeline, but OpenGLPacketPipeline owns a GL context
        // sharing resources with the app window; leaving it alive past
        // window teardown segfaults on exit.
        delete device_;
        device_ = nullptr;
    }
    delete listener_;     listener_     = nullptr;
    delete registration_; registration_ = nullptr;
}
