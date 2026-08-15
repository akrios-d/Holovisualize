#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: TwoHandsStretch
// Dark singularity that pulls particles inward in an accretion disk.
class BlackHoleEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
        // Seed accretion disk particles
        for (int i = 0; i < 500; i++) {
            float r     = frand(0.3f, 1.2f);
            float theta = frand(0.f, static_cast<float>(2*M_PI));
            float yoff  = frand(-0.05f, 0.05f);
            particles_.push_back({
                origin_[0] + r*std::cos(theta),
                origin_[1] + yoff,
                origin_[2] + r*std::sin(theta),
                theta, r
            });
        }
    }

    void update(float dt) override {
        life_ -= dt;
        float dts = dt * 0.001f;
        for (auto& p : particles_) {
            // Orbital speed increases as radius shrinks (conservation of momentum)
            float omega = 0.8f / (p.r * p.r + 0.01f);
            p.theta += omega * dts;
            p.r    -= 0.05f * dts; // slowly spiral inward
            if (p.r < 0.02f) {     // reset particle to outer ring
                p.r     = frand(0.8f, 1.2f);
                p.theta = frand(0.f, static_cast<float>(2*M_PI));
            }
            p.x = origin_[0] + p.r * std::cos(p.theta);
            p.z = origin_[2] + p.r * std::sin(p.theta);
        }
    }

    std::vector<EffectPoint> getPoints() const override {
        std::vector<EffectPoint> out;
        for (const auto& p : particles_) {
            float heat = 1.f - p.r / 1.2f; // hotter near centre
            auto col = lerp(rgb(60,0,120), rgb(255,120,0), heat);
            out.push_back({{ p.x, p.y, p.z }, col});
        }
        // Black centre void (dark sphere to occlude)
        auto void_ = makeSphere(origin_, 0.08f, 80, rgb(5,0,10));
        out.insert(out.end(), void_.begin(), void_.end());
        return out;
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Black Hole"; }

private:
    struct Particle { float x,y,z,theta,r; };
    std::array<float,3>   origin_{};
    std::vector<Particle> particles_;
    float life_       = 0;
    float lifetimeMs_ = 6000.f;
};
