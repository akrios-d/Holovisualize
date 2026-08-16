#pragma once

#include "ISensor.h"
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

struct _freenect_context;
struct _freenect_device;

class KinectV1Sensor : public ISensor {
public:
    KinectV1Sensor();
    ~KinectV1Sensor();

    bool initialize() override;
    bool captureFrame(Frame& frame) override;
    void shutdown() override;
    std::string name() const override { return "KinectV1"; }

    // Called by libfreenect callbacks (must be public)
    void onDepth(void* data);
    void onVideo(void* data);

private:
    _freenect_context* ctx_ = nullptr;
    _freenect_device*  dev_ = nullptr;

    std::thread        eventThread_;
    std::atomic<bool>  running_{ false };

    std::mutex              frameMutex_;
    std::condition_variable frameCv_;

    std::vector<uint16_t> depthBuf_;   // 640x480 uint16 mm
    std::vector<uint8_t>  colorBuf_;   // 640x480 RGB
    bool depthReady_ = false;
    bool colorReady_ = false;
};
