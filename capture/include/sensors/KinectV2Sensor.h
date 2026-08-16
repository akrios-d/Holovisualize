#pragma once

#include "ISensor.h"
#include <string>

// Private members differ by backend; SDK types are forward-declared to avoid
// pulling Windows/Kinect headers into every translation unit.
#ifdef HOLOVISUALIZE_KINECT_SDK
struct IKinectSensor;
struct IMultiSourceFrameReader;
struct ICoordinateMapper;
#else
#  include <libfreenect2/libfreenect2.hpp>
#  include <libfreenect2/frame_listener_impl.h>
#  include <libfreenect2/registration.h>
#  include <libfreenect2/packet_pipeline.h>
#endif

class KinectV2Sensor : public ISensor {
public:
    KinectV2Sensor();
    ~KinectV2Sensor();

    bool initialize() override;
    bool captureFrame(Frame& frame) override;
    void shutdown() override;
    std::string name() const override { return "KinectV2"; }

private:
#ifdef HOLOVISUALIZE_KINECT_SDK
    IKinectSensor*           sensor_ = nullptr;
    IMultiSourceFrameReader* reader_ = nullptr;
    ICoordinateMapper*       mapper_ = nullptr;
#else
    libfreenect2::Freenect2               freenect2_;
    libfreenect2::Freenect2Device*        device_       = nullptr;
    libfreenect2::PacketPipeline*         pipeline_     = nullptr;
    libfreenect2::SyncMultiFrameListener* listener_     = nullptr;
    libfreenect2::Registration*           registration_ = nullptr;
#endif
};
