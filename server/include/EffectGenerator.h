#pragma once

#include "IEffect.h"
#include "IGestureDetector.h"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Manages the lifecycle of active 3D effects.
//
// Usage:
//   EffectGenerator fx;
//   fx.registerFactory(GestureType::PalmUp,  [] { return std::make_unique<FireEffect>(); });
//   fx.registerFactory(GestureType::Clap,    [] { return std::make_unique<LightningEffect>(); });
//
//   // Each server tick:
//   fx.onGestures(events);   // spawn new effects
//   fx.update(dtMs);         // advance all active effects
//   auto pts = fx.collect(); // gather points for the frame
class EffectGenerator {
public:
    using Factory = std::function<std::unique_ptr<IEffect>()>;

    // Register a factory for a specific gesture type.
    // Only one factory per gesture type — last registration wins.
    void registerFactory(GestureType type, Factory factory);

    // Spawn effects for any gesture events that have a registered factory.
    void onGestures(const std::vector<GestureEvent>& events);

    // Advance all active effects by `dtMs` milliseconds and prune expired ones.
    void update(float dtMs);

    // Collect all current effect points (merged from all active effects).
    std::vector<EffectPoint> collect() const;

    int activeCount() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<uint8_t, Factory> factories_; // keyed by GestureType
    std::vector<std::unique_ptr<IEffect>> active_;
};
