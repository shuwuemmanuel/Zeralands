// ZeraLands - seeded noise primitives (simplex, value-with-derivatives, cellular) and fractal combinators.
#pragma once

#include "core/Common.h"

#include <array>

namespace zl {

struct CellResult {
    float f1 = 0, f2 = 0;   // distance to nearest / second nearest feature point
    uint32_t id = 0;        // hash of the nearest cell (stable per seed)
    Vec2 nearest;           // nearest feature point
};

class Noise {
public:
    explicit Noise(uint64_t seed = 0);

    // 2D simplex noise, output roughly in [-1,1].
    float simplex(float x, float y) const;
    // Value noise with analytic derivatives: returns {value in [-1,1], d/dx, d/dy}.
    Vec3 valueD(float x, float y) const;
    // Worley / cellular noise with jitter in [0,1].
    CellResult cellular(float x, float y, float jitter = 1.f) const;
    // Periodic value noise (period in lattice cells) used for seamless texture synthesis.
    float periodicValue(float x, float y, int period) const;

    // ---- fractals (all roughly in [-1,1] unless noted)
    float fbm(float x, float y, int octaves, float lacunarity = 2.f, float gain = 0.5f) const;
    float ridged(float x, float y, int octaves, float lacunarity = 2.f, float gain = 0.5f) const;   // [0,1]
    float billow(float x, float y, int octaves, float lacunarity = 2.f, float gain = 0.5f) const;   // [0,1]
    // Derivative-dampened fbm: features shrink on steep slopes, giving an eroded look for free.
    float erodedFbm(float x, float y, int octaves, float lacunarity = 2.f, float gain = 0.5f, float erosion = 1.f) const;
    float periodicFbm(float x, float y, int period, int octaves, float gain = 0.5f) const;            // [-1,1]
    // Domain warp helper: offsets (x,y) by two decorrelated fbm fields.
    Vec2 warp(float x, float y, float strength, float freq, int octaves = 4) const;

    uint64_t seed() const { return seed_; }

private:
    float grad(int hash, float x, float y) const;
    float hash2(int ix, int iy) const;   // [-1,1]
    uint32_t hashi(int ix, int iy) const;

    uint64_t seed_;
    std::array<uint8_t, 512> perm_{};
};

}  // namespace zl
