#pragma once

#include "ISensor.h"

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>

class KinectV2Sensor : public ISensor {
public:
    KinectV2Sensor();
    ~KinectV2Sensor();

    bool initialize() override;
    bool captureFrame(Frame& frame) override;
    void shutdown() override;
    std::string name() const override { return "KinectV2"; }

private:
    libfreenect2::Freenect2               freenect2_;
    libfreenect2::Freenect2Device*        device_       = nullptr;
    libfreenect2::PacketPipeline*         pipeline_     = nullptr;
    libfreenect2::SyncMultiFrameListener* listener_     = nullptr;
    libfreenect2::Registration*           registration_ = nullptr;
};
