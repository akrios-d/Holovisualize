#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"

// Triggered by: Push, TwoHandsClap
// Expanding ring/sphere of white particles.
class ShockwaveEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
    }

    void update(float dt) override { life_ -= dt; }

    std::vector<EffectPoint> getPoints() const override {
        float t    = 1.f - life_ / lifetimeMs_; // 0→1
        float r    = t * maxRadius_;
        float fade = 1.f - t;
        auto col   = lerp(rgb(255,255,255), rgb(100,180,255), t);
        col[0] = static_cast<uint8_t>(col[0] * fade);
        col[1] = static_cast<uint8_t>(col[1] * fade);
        col[2] = static_cast<uint8_t>(col[2] * fade);
        return makeSphere(origin_, r, 400, col);
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Shockwave"; }

private:
    std::array<float,3> origin_{};
    float life_       = 0;
    float lifetimeMs_ = 1500.f;
    float maxRadius_  = 2.5f;
};
