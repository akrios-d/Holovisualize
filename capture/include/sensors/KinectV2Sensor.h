#pragma once

#include "ISensor.h"
#include <string>

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>

class KinectV2Sensor : public ISensor {
public:
    // parentGLContext: a GLFWwindow* to share GL resources with. Pass the
    // app's own window so OpenGLPacketPipeline reuses it instead of creating
    // a competing context. Leave null for headless use.
    explicit KinectV2Sensor(void* parentGLContext = nullptr);
    ~KinectV2Sensor();

    bool initialize() override;
    bool captureFrame(Frame& frame) override;
    void shutdown() override;
    std::string name() const override { return "KinectV2"; }

private:
    void* parentGLContext_ = nullptr;

    libfreenect2::Freenect2               freenect2_;
    libfreenect2::Freenect2Device*        device_       = nullptr;
    libfreenect2::PacketPipeline*         pipeline_     = nullptr;
    libfreenect2::SyncMultiFrameListener* listener_     = nullptr;
    libfreenect2::Registration*           registration_ = nullptr;

    // Tracks the depth frame sequence number to detect skipped frames.
    uint32_t lastSequence_    = 0;
    bool     haveSequence_    = false;
};
