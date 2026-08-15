#pragma once

#include "Frame.h"
#include <string>

// Abstract interface for any depth sensor.
// Implementations must not leak SDK-specific types through this header.
class ISensor {
public:
    virtual ~ISensor() = default;

    // Initialize hardware. Returns false on failure.
    virtual bool initialize() = 0;

    // Capture a new frame into `frame`. Returns false if capture fails.
    virtual bool captureFrame(Frame& frame) = 0;

    // Release hardware resources.
    virtual void shutdown() = 0;

    virtual std::string name() const = 0;
};
