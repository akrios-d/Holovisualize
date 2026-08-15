#pragma once
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include "IEffect.h"
#include "effects/EffectUtils.h"
#include <algorithm>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ── Shape types ───────────────────────────────────────────────────────────────
enum class SpawnShape {
    Sphere,
    Cube,
    Crystal,
    Torus,
    Star,
    ObjFile,    // loaded from a .obj file
};

struct SpawnConfig {
    SpawnShape             shape       = SpawnShape::Sphere;
    std::array<uint8_t,3>  color       = {255, 255, 255};
    float                  scale       = 0.3f;
    float                  lifetimeMs  = 5000.f;
    float                  gravity     = -0.5f;    // m/s² (0 = float)
    float                  spinSpeed   = 1.5f;     // radians/s
    std::string            objPath;                // for SpawnShape::ObjFile
    int                    pointCount  = 400;
};

// Triggered by: PointFinger, ThumbsUp, Peace, or any custom mapping.
// Spawns a 3D object at the gesture position. Supports procedural shapes
// and .obj file loading. The object spins and optionally falls under gravity.
class SpawnedObjectEffect final : public IEffect {
public:
    explicit SpawnedObjectEffect(SpawnConfig cfg = {}) : cfg_(std::move(cfg)) {}

    void trigger(const GestureEvent& ev) override {
        pos_  = ev.position;
        life_ = cfg_.lifetimeMs;
        buildGeometry();
    }

    void update(float dt) override {
        life_    -= dt;
        angle_   += cfg_.spinSpeed * dt * 0.001f;
        pos_[1]  += cfg_.gravity * dt * 0.001f * dt * 0.001f * 0.5f; // kinematic
    }

    std::vector<EffectPoint> getPoints() const override {
        float fade = std::min(life_ / 500.f, 1.f); // fade out in last 500 ms
        std::vector<EffectPoint> out;
        out.reserve(base_.size());
        float cs = std::cos(angle_), sn = std::sin(angle_);
        for (const auto& p : base_) {
            // Rotate around Y axis
            float rx = p.position[0]*cs - p.position[2]*sn;
            float rz = p.position[0]*sn + p.position[2]*cs;
            auto col = p.color;
            col[0] = static_cast<uint8_t>(col[0] * fade);
            col[1] = static_cast<uint8_t>(col[1] * fade);
            col[2] = static_cast<uint8_t>(col[2] * fade);
            out.push_back({{ pos_[0]+rx, pos_[1]+p.position[1], pos_[2]+rz }, col});
        }
        return out;
    }

    bool isExpired() const override { return life_ <= 0; }
    const char* name() const override { return "Spawned Object"; }

private:
    void buildGeometry() {
        base_.clear();
        const auto& c = cfg_.color;
        float s = cfg_.scale;
        int   n = cfg_.pointCount;

        switch (cfg_.shape) {
        case SpawnShape::Sphere:
            base_ = makeSphere({0,0,0}, s, n, c);
            break;
        case SpawnShape::Cube:
            base_ = makeCube({0,0,0}, s, n/12, c);
            break;
        case SpawnShape::Crystal:
            base_ = makeCrystal({0,0,0}, s*2.f, s*0.4f, n, c);
            break;
        case SpawnShape::Torus:
            base_ = makeTorus({0,0,0}, s, s*0.3f, n, c);
            break;
        case SpawnShape::Star:
            base_ = makeStar({0,0,0}, s*0.2f, s, 8, n/8, c);
            break;
        case SpawnShape::ObjFile:
            loadObj(cfg_.objPath, s, c);
            break;
        }
    }

    // Minimal .obj loader — reads 'v' lines only, samples points from vertices.
    void loadObj(const std::string& path, float scale,
                 std::array<uint8_t,3> col) {
        std::ifstream f(path);
        if (!f.is_open()) {
            // Fallback to sphere if file not found
            base_ = makeSphere({0,0,0}, scale, cfg_.pointCount, col);
            return;
        }
        std::vector<std::array<float,3>> verts;
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() < 2 || line[0] != 'v' || line[1] != ' ') continue;
            std::istringstream ss(line.substr(2));
            float x,y,z;
            if (ss >> x >> y >> z)
                verts.push_back({x*scale, y*scale, z*scale});
        }
        if (verts.empty()) { base_ = makeSphere({0,0,0}, scale, cfg_.pointCount, col); return; }
        // Sample up to pointCount vertices
        for (int i = 0; i < cfg_.pointCount; i++) {
            const auto& v = verts[i % verts.size()];
            base_.push_back({v, col});
        }
    }

    SpawnConfig              cfg_;
    std::array<float,3>      pos_{};
    std::vector<EffectPoint> base_;   // geometry in local space (centred at 0)
    float                    life_  = 0;
    float                    angle_ = 0;
};
