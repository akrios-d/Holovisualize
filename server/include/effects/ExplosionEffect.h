#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: OpenHand, TwoHandsPinch (implode then explode)
// Burst of coloured debris particles flying outward.
class ExplosionEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
        particles_.reserve(numParticles_);
        for (int i = 0; i < numParticles_; i++) {
            Particle p;
            p.x = origin_[0]; p.y = origin_[1]; p.z = origin_[2];
            float theta = frand(0.f, static_cast<float>(2*M_PI));
            float phi   = std::acos(frand(-1.f, 1.f));
            float speed = frand(0.5f, 3.0f);
            p.vx = speed * std::sin(phi) * std::cos(theta);
            p.vy = speed * std::cos(phi);
            p.vz = speed * std::sin(phi) * std::sin(theta);
            p.r  = static_cast<uint8_t>(frand(180,255));
            p.g  = static_cast<uint8_t>(frand(60,180));
            p.b  = static_cast<uint8_t>(frand(0,60));
            p.life = frand(0.3f, 1.0f);
            particles_.push_back(p);
        }
    }

    void update(float dt) override {
        life_ -= dt;
        float dts = dt * 0.001f;
        for (auto& p : particles_) {
            p.x += p.vx * dts; p.y += p.vy * dts; p.z += p.vz * dts;
            p.vy -= 2.0f * dts; // gravity
            p.life -= dts;
        }
        particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p){ return p.life <= 0; }), particles_.end());
    }

    std::vector<EffectPoint> getPoints() const override {
        std::vector<EffectPoint> out;
        for (const auto& p : particles_)
            out.push_back({{ p.x, p.y, p.z }, { p.r, p.g, p.b }});
        return out;
    }

    bool isExpired() const override { return life_ <= 0 && particles_.empty(); }
    const char* name() const override { return "Explosion"; }

private:
    struct Particle { float x,y,z,vx,vy,vz,life; uint8_t r,g,b; };
    std::array<float,3>   origin_{};
    std::vector<Particle> particles_;
    float life_         = 0;
    float lifetimeMs_   = 3000.f;
    int   numParticles_ = 250;
};
