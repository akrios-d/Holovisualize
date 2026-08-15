#pragma once

#include "GestureEvent.h"

#include <cstdint>
#include <vector>
#include <array>

// A single generated point for an effect (colour + position).
struct EffectPoint {
    std::array<float,   3> position;  // metres
    std::array<uint8_t, 3> color;     // RGB
};

// Socket Pattern — effect port.
//
// Implement this interface to add a new 3D effect triggered by a gesture.
// The EffectGenerator creates one IEffect instance per GestureEvent and
// updates all active effects every tick. Expired effects are destroyed.
//
// Each effect produces a list of EffectPoints that are merged into the
// MeshFrame sent to connected preview clients.
class IEffect {
public:
    virtual ~IEffect() = default;

    // Called once when the effect is created.
    virtual void trigger(const GestureEvent& event) = 0;

    // Called every server tick (~30 fps). `dtMs` is elapsed ms since last tick.
    virtual void update(float dtMs) = 0;

    // Returns the current geometry of this effect as coloured points.
    virtual std::vector<EffectPoint> getPoints() const = 0;

    // When true, the EffectGenerator removes and destroys this effect.
    virtual bool isExpired() const = 0;

    virtual const char* name() const = 0;
};
