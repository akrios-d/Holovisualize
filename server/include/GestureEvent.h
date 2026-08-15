#pragma once

#include <array>
#include <cstdint>

// Types of gestures the detector can recognise.
// Add new entries here as new gestures are implemented.
enum class GestureType : uint8_t {
    None,

    // ── Single-hand static ────────────────────────────────────────────────────
    PalmUp,         // palm facing upward          → fire / levitate
    PalmDown,       // palm facing downward         → suppress / ground
    Fist,           // closed hand                  → grab / compress
    OpenHand,       // fingers spread               → release / explode
    Pinch,          // thumb + index close together → select / scale
    ThumbsUp,       // thumb extended upward        → boost / confirm
    PointFinger,    // index extended (ray cast)    → target / shoot
    Peace,          // V sign (index + middle up)   → split / fork effect

    // ── Single-hand motion ────────────────────────────────────────────────────
    SwipeLeft,      // lateral sweep left           → push left
    SwipeRight,     // lateral sweep right          → push right
    SwipeUp,        // sweep upward                 → lift / ascend
    SwipeDown,      // sweep downward               → slam / descend
    Push,           // hand thrusting forward       → repel / shockwave
    Pull,           // hand pulling back            → attract / vacuum
    Circle,         // circular wrist motion        → portal / vortex
    Cross,          // X movement pattern           → cancel / destroy

    // ── Two-hand ─────────────────────────────────────────────────────────────
    Clap,               // palms rapidly approaching   → lightning / shockwave
    TwoHandsStretch,    // hands moving apart          → expand / zoom out
    TwoHandsPinch,      // hands moving together       → collapse / implode
    TwoHandsClap,       // parallel palms clapping     → mega explosion

    // ── Full-body ─────────────────────────────────────────────────────────────
    ArmsRaised,     // both arms above shoulders   → area-of-effect spell
    Bow,            // torso tilting forward        → charge / channel
    Jump,           // rapid upward body shift      → ground slam on land
};

struct GestureEvent {
    GestureType           type        = GestureType::None;
    std::array<float, 3>  position    = {};   // world position of gesture (m)
    std::array<float, 3>  velocity    = {};   // movement velocity (m/s) — for motion gestures
    float                 confidence  = 0.0f; // 0..1
    uint64_t              timestampMs = 0;
};
