#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: Circle
// Spiraling purple/cyan particles forming a vortex/portal.
class VortexEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
    }

    void update(float dt) override {
        life_  -= dt;
        angle_ += dt * 0.004f; // rotation speed
    }

    std::vector<EffectPoint> getPoints() const override {
        std::vector<EffectPoint> out;
        const int rings  = 12;
        const int perRing = 32;
        for (int ri = 0; ri < rings; ri++) {
            float frac  = static_cast<float>(ri) / rings;
            float r     = frac * maxRadius_;
            float yOff  = std::sin(frac * static_cast<float>(M_PI)) * 0.4f;
            float twist = angle_ + frac * static_cast<float>(4*M_PI); // spiral twist

            for (int pi = 0; pi < perRing; pi++) {
                float theta = twist + static_cast<float>(2*M_PI) * pi / perRing;
                auto col = lerp(rgb(160,0,255), rgb(0,220,255), frac);
                out.push_back({{
                    origin_[0] + r * std::cos(theta),
                    origin_[1] + yOff,
                    origin_[2] + r * std::sin(theta)
                }, col});
            }
        }
        return out;
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Vortex"; }

private:
    std::array<float,3> origin_{};
    float life_       = 0;
    float lifetimeMs_ = 5000.f;
    float maxRadius_  = 0.8f;
    float angle_      = 0;
};
