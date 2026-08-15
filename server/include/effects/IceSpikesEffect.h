#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: Fist (then OpenHand to shatter)
// Ice crystals growing upward from the gesture position.
class IceSpikesEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
        // Spawn 7 spikes in a circle + one central spike
        spikes_.push_back({origin_[0], origin_[1], origin_[2], frand(0.6f,1.0f)});
        for (int i = 0; i < 6; i++) {
            float a = static_cast<float>(2*M_PI) * i / 6;
            float r = frand(0.15f, 0.35f);
            spikes_.push_back({
                origin_[0] + r*std::cos(a),
                origin_[1],
                origin_[2] + r*std::sin(a),
                frand(0.3f, 0.7f)
            });
        }
    }

    void update(float dt) override {
        life_ -= dt;
        grow_ = std::min(grow_ + dt * 0.002f, 1.0f); // 0.5s to full height
    }

    std::vector<EffectPoint> getPoints() const override {
        std::vector<EffectPoint> out;
        for (const auto& s : spikes_) {
            float h = s.height * grow_;
            auto pts = makeCrystal(
                {s.x, s.y + h*0.5f, s.z},
                h, 0.06f, 40,
                lerp(rgb(180,230,255), rgb(255,255,255), frand()*0.5f)
            );
            out.insert(out.end(), pts.begin(), pts.end());
        }
        return out;
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Ice Spikes"; }

private:
    struct Spike { float x,y,z,height; };
    std::array<float,3>  origin_{};
    std::vector<Spike>   spikes_;
    float life_     = 0;
    float lifetimeMs_ = 4000.f;
    float grow_     = 0;
};
