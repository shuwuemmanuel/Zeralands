// ZeraLands - generic 2D float grid (heights, masks, weights) with sampling helpers.
#pragma once

#include "core/Common.h"

namespace zl {

class Grid {
public:
    Grid() = default;
    Grid(int w, int h, float fill = 0.f) { resize(w, h, fill); }

    void resize(int w, int h, float fill = 0.f) {
        w_ = w;
        h_ = h;
        d_.assign(size_t(w) * size_t(h), fill);
    }
    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return d_.empty(); }
    size_t size() const { return d_.size(); }

    float& at(int x, int y) { return d_[size_t(y) * size_t(w_) + size_t(x)]; }
    float at(int x, int y) const { return d_[size_t(y) * size_t(w_) + size_t(x)]; }
    float atClamped(int x, int y) const {
        x = x < 0 ? 0 : (x >= w_ ? w_ - 1 : x);
        y = y < 0 ? 0 : (y >= h_ ? h_ - 1 : y);
        return at(x, y);
    }
    float* data() { return d_.data(); }
    const float* data() const { return d_.data(); }
    std::vector<float>& vec() { return d_; }
    const std::vector<float>& vec() const { return d_; }

    // Bilinear sample in grid coordinates (0..w-1).
    float sample(float x, float y) const;
    // Bilinear sample with u,v in [0,1].
    float sampleUV(float u, float v) const { return sample(u * float(w_ - 1), v * float(h_ - 1)); }
    // Central-difference gradient in grid units.
    Vec2 gradient(int x, int y) const {
        return {(atClamped(x + 1, y) - atClamped(x - 1, y)) * 0.5f, (atClamped(x, y + 1) - atClamped(x, y - 1)) * 0.5f};
    }

    void minMax(float& mn, float& mx) const;
    void normalize(float lo = 0.f, float hi = 1.f);
    void fill(float v) { std::fill(d_.begin(), d_.end(), v); }
    void blur(int radius, int passes = 1);   // separable box blur (approximates gaussian with passes)
    Grid resampled(int w, int h) const;      // bilinear
    float mean() const;

private:
    int w_ = 0, h_ = 0;
    std::vector<float> d_;
};

// Heights are stored normalized in [0,1]; worldSize/heightRange convert to meters.
struct Terrain {
    Grid height;          // final height (base + splines/sculpt), 0..1
    Grid baseHeight;      // generated height before spline stamping
    Grid water;           // water surface height (0..1) where >= height, else -1
    Grid flow;            // normalized log flow accumulation (0..1)
    Grid moisture;        // 0..1
    Grid roadMask;        // 0..1 coverage of roads/paths/rails (for foliage & splat)
    Grid lava;            // 0..1 molten rock coverage
    float worldSize = 2017.f;    // meters edge length
    float heightRange = 600.f;   // meters for height 0..1
    float seaLevel = -1.f;       // normalized, <0 means no ocean

    int res() const { return height.width(); }
    float cellSize() const { return worldSize / float(std::max(1, height.width() - 1)); }
    // world (x,z) in meters, origin at terrain corner -> grid coordinates
    Vec2 worldToGrid(float wx, float wz) const { float c = cellSize(); return {wx / c, wz / c}; }
    Vec2 gridToWorld(float gx, float gy) const { float c = cellSize(); return {gx * c, gy * c}; }
    float heightAtWorld(float wx, float wz) const {
        Vec2 g = worldToGrid(wx, wz);
        return height.sample(g.x, g.y) * heightRange;
    }
    // Slope in rise/run (meters/meter) at grid cell.
    float slopeAt(int x, int y) const {
        Vec2 g = height.gradient(x, y);
        return std::sqrt(g.x * g.x + g.y * g.y) * heightRange / cellSize();
    }
    Vec3 normalAt(int x, int y) const {
        Vec2 g = height.gradient(x, y);
        float s = heightRange / cellSize();
        return Vec3(-g.x * s, 1.f, -g.y * s).normalized();
    }
};

}  // namespace zl
