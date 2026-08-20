#pragma once

#include "IEffect.h"
#include <cmath>
#include <cstdlib>
#include <vector>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ── Random helpers ────────────────────────────────────────────────────────────
inline float frand()              { return static_cast<float>(rand()) / RAND_MAX; }
inline float frand(float lo, float hi) { return lo + frand() * (hi - lo); }

// ── Colour helpers ────────────────────────────────────────────────────────────
inline std::array<uint8_t,3> rgb(uint8_t r, uint8_t g, uint8_t b) { return {r,g,b}; }
inline std::array<uint8_t,3> lerp(std::array<uint8_t,3> a,
                                   std::array<uint8_t,3> b, float t) {
    return { static_cast<uint8_t>(a[0] + (b[0]-a[0])*t),
             static_cast<uint8_t>(a[1] + (b[1]-a[1])*t),
             static_cast<uint8_t>(a[2] + (b[2]-a[2])*t) };
}

// ── 3-D object generators (return coloured point clouds) ─────────────────────

// Sphere — uniformly sampled surface points
inline std::vector<EffectPoint> makeSphere(
        std::array<float,3> centre, float radius, int n,
        std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    pts.reserve(n);
    for (int i = 0; i < n; i++) {
        float theta = frand(0.f, static_cast<float>(2*M_PI));
        float phi   = std::acos(frand(-1.f, 1.f));
        float sp = std::sin(phi);
        pts.push_back({{ centre[0] + radius*sp*std::cos(theta),
                         centre[1] + radius*std::cos(phi),
                         centre[2] + radius*sp*std::sin(theta) }, col});
    }
    return pts;
}

// Cube wireframe — edges sampled as points
inline std::vector<EffectPoint> makeCube(
        std::array<float,3> centre, float half, int perEdge,
        std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    // 12 edges of a cube
    const float s = half;
    const float signs[2] = {-s, s};
    for (float a : signs) for (float b : signs) {
        for (int i = 0; i <= perEdge; i++) {
            float t = -s + 2*s*i/perEdge;
            pts.push_back({{ centre[0]+t, centre[1]+a, centre[2]+b }, col});
            pts.push_back({{ centre[0]+a, centre[1]+t, centre[2]+b }, col});
            pts.push_back({{ centre[0]+a, centre[1]+b, centre[2]+t }, col});
        }
    }
    return pts;
}

// Crystal / bipyramid — two opposing pyramids
inline std::vector<EffectPoint> makeCrystal(
        std::array<float,3> centre, float height, float radius, int n,
        std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    pts.reserve(n);
    for (int i = 0; i < n; i++) {
        float t     = frand(-1.f, 1.f);     // -1=bottom tip, 0=equator, 1=top tip
        float r     = radius * (1.f - std::abs(t));
        float theta = frand(0.f, static_cast<float>(2*M_PI));
        pts.push_back({{ centre[0] + r*std::cos(theta),
                         centre[1] + t*height*0.5f,
                         centre[2] + r*std::sin(theta) }, col});
    }
    return pts;
}

// Torus — surface points
inline std::vector<EffectPoint> makeTorus(
        std::array<float,3> centre, float R, float r, int n,
        std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    pts.reserve(n);
    for (int i = 0; i < n; i++) {
        float u = frand(0.f, static_cast<float>(2*M_PI));
        float v = frand(0.f, static_cast<float>(2*M_PI));
        pts.push_back({{ centre[0] + (R + r*std::cos(v))*std::cos(u),
                         centre[1] + r*std::sin(v),
                         centre[2] + (R + r*std::cos(v))*std::sin(u) }, col});
    }
    return pts;
}

// Heart — parametric 2D heart curve, extruded to a thin 3D shell.
// x(t) = 16 sin³t, y(t) = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t (classic
// "heart curve"), scaled to unit size and given a little Z-thickness so it
// reads as a point-cloud volume rather than a flat outline.
inline std::vector<EffectPoint> makeHeart(
        std::array<float,3> centre, float size, int n,
        std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    pts.reserve(n);
    constexpr float kNorm = 1.f / 16.f; // normalises x(t)'s amplitude to ~[-1,1]
    for (int i = 0; i < n; i++) {
        float t = frand(0.f, static_cast<float>(2*M_PI));
        float x = 16.f * std::pow(std::sin(t), 3.f);
        float y = 13.f*std::cos(t) - 5.f*std::cos(2*t) - 2.f*std::cos(3*t) - std::cos(4*t);
        float z = frand(-0.15f, 0.15f);
        pts.push_back({{ centre[0] + x*kNorm*size,
                         centre[1] + y*kNorm*size,
                         centre[2] + z*size }, col});
    }
    return pts;
}

// Star / burst — rays emanating from centre
inline std::vector<EffectPoint> makeStar(
        std::array<float,3> centre, float innerR, float outerR,
        int rays, int perRay, std::array<uint8_t,3> col) {
    std::vector<EffectPoint> pts;
    for (int r = 0; r < rays; r++) {
        float theta = static_cast<float>(2*M_PI) * r / rays;
        float cx = std::cos(theta), cz = std::sin(theta);
        for (int i = 0; i <= perRay; i++) {
            float d = innerR + (outerR-innerR)*i/perRay;
            pts.push_back({{ centre[0]+d*cx, centre[1], centre[2]+d*cz }, col});
        }
    }
    return pts;
}
