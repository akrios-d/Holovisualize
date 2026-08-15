#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: PalmUp
// Rising column of fire particles (orange→yellow→white at tips).
class FireEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
    }

    void update(float dt) override {
        life_ -= dt;
        age_  += dt;
        // Spawn new particles
        int toSpawn = static_cast<int>(dt / 16.f) + 1;
        for (int i = 0; i < toSpawn && static_cast<int>(pts_.size()) < maxPts_; i++) {
            Particle p;
            p.x  = origin_[0] + frand(-0.08f, 0.08f);
            p.y  = origin_[1];
            p.z  = origin_[2] + frand(-0.08f, 0.08f);
            p.vy = frand(0.4f, 1.2f);
            p.vx = frand(-0.05f, 0.05f);
            p.vz = frand(-0.05f, 0.05f);
            p.life = frand(0.4f, 1.0f); // seconds
            pts_.push_back(p);
        }
        float dts = dt * 0.001f;
        for (auto& p : pts_) {
            p.x += p.vx * dts; p.y += p.vy * dts; p.z += p.vz * dts;
            p.vy *= 0.98f;
            p.life -= dts;
        }
        pts_.erase(std::remove_if(pts_.begin(), pts_.end(),
            [](const Particle& p){ return p.life <= 0; }), pts_.end());
    }

    std::vector<EffectPoint> getPoints() const override {
        std::vector<EffectPoint> out;
        out.reserve(pts_.size());
        for (const auto& p : pts_) {
            float t = 1.f - p.life; // 0=fresh(orange) 1=old(white)
            auto col = lerp(rgb(255,80,0), rgb(255,240,180), std::min(t*1.5f,1.f));
            out.push_back({{ p.x, p.y, p.z }, col});
        }
        return out;
    }

    bool isExpired() const override { return life_ <= 0 && pts_.empty(); }
    const char* name() const override { return "Fire"; }

private:
    struct Particle { float x,y,z, vx,vy,vz, life; };
    std::array<float,3>  origin_{};
    std::vector<Particle> pts_;
    float life_       = 0;
    float age_        = 0;
    float lifetimeMs_ = 3000.f;
    int   maxPts_     = 300;
};
