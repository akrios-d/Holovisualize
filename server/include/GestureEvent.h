#pragma once

#include <array>
#include <cstdint>

// Types of gestures the detector can recognise.
// Add new entries here as new gestures are implemented.
enum class GestureType : uint8_t {
    None,
    PalmUp,    // palm facing upward — e.g. trigger fire effect
    PalmDown,  // palm facing downward — e.g. suppress effect
    Clap,      // rapid approach of two hand centroids — e.g. trigger lightning
    SwipeLeft,
    SwipeRight,
    Fist,      // closed hand
    OpenHand,  // spread fingers
};

struct GestureEvent {
    GestureType           type        = GestureType::None;
    std::array<float, 3>  position    = {};   // world position (m)
    float                 confidence  = 0.0f; // 0..1
    uint64_t              timestampMs = 0;
};
