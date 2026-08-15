#pragma once
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <vector>

// Triggered by: Clap
// Branching lightning bolt from gesture position upward.
class LightningEffect final : public IEffect {
public:
    void trigger(const GestureEvent& ev) override {
        origin_ = ev.position;
        life_   = lifetimeMs_;
        buildBolt(origin_, {origin_[0], origin_[1]+2.f, origin_[2]}, 5);
    }

    void update(float dt) override { life_ -= dt; }

    std::vector<EffectPoint> getPoints() const override {
        if (pts_.empty()) return {};
        // Flicker by randomly skipping some points
        std::vector<EffectPoint> out;
        out.reserve(pts_.size());
        for (const auto& p : pts_)
            if (rand() % 3 != 0)  // ~67% visible each frame = flicker
                out.push_back(p);
        return out;
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Lightning"; }

private:
    void buildBolt(std::array<float,3> a, std::array<float,3> b, int depth) {
        if (depth == 0) {
            // Leaf segment — fill with white/blue points
            int n = 12;
            for (int i = 0; i <= n; i++) {
                float t = static_cast<float>(i)/n;
                pts_.push_back({{
                    a[0]+(b[0]-a[0])*t + frand(-0.02f,0.02f),
                    a[1]+(b[1]-a[1])*t,
                    a[2]+(b[2]-a[2])*t + frand(-0.02f,0.02f)
                }, lerp(rgb(180,180,255), rgb(255,255,255), frand())});
            }
            return;
        }
        // Midpoint displacement
        float mx = (a[0]+b[0])*0.5f + frand(-0.15f,0.15f);
        float my = (a[1]+b[1])*0.5f + frand(-0.05f,0.05f);
        float mz = (a[2]+b[2])*0.5f + frand(-0.15f,0.15f);
        std::array<float,3> mid{mx,my,mz};
        buildBolt(a, mid, depth-1);
        buildBolt(mid, b, depth-1);
        // Random branch
        if (depth == 3 && frand() > 0.4f) {
            std::array<float,3> bdir{
                mid[0]+frand(-0.6f,0.6f),
                mid[1]+frand(0.2f,0.8f),
                mid[2]+frand(-0.6f,0.6f)
            };
            buildBolt(mid, bdir, depth-2);
        }
    }

    std::array<float,3>      origin_{};
    std::vector<EffectPoint> pts_;
    float life_       = 0;
    float lifetimeMs_ = 1200.f;
};
