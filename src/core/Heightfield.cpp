#include "core/Heightfield.h"

namespace zl {

float Grid::sample(float x, float y) const {
    if (d_.empty()) return 0.f;
    x = clampf(x, 0.f, float(w_ - 1));
    y = clampf(y, 0.f, float(h_ - 1));
    int x0 = int(x), y0 = int(y);
    int x1 = std::min(x0 + 1, w_ - 1), y1 = std::min(y0 + 1, h_ - 1);
    float fx = x - float(x0), fy = y - float(y0);
    float a = at(x0, y0), b = at(x1, y0), c = at(x0, y1), d = at(x1, y1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

void Grid::minMax(float& mn, float& mx) const {
    mn = 1e30f;
    mx = -1e30f;
    for (float v : d_) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (d_.empty()) mn = mx = 0.f;
}

void Grid::normalize(float lo, float hi) {
    float mn, mx;
    minMax(mn, mx);
    float range = mx - mn;
    if (range < 1e-12f) {
        fill(lo);
        return;
    }
    float s = (hi - lo) / range;
    for (float& v : d_) v = lo + (v - mn) * s;
}

float Grid::mean() const {
    double s = 0;
    for (float v : d_) s += v;
    return d_.empty() ? 0.f : float(s / double(d_.size()));
}

void Grid::blur(int radius, int passes) {
    if (radius <= 0 || d_.empty()) return;
    std::vector<float> tmp(d_.size());
    const float inv = 1.f / float(2 * radius + 1);
    for (int p = 0; p < passes; ++p) {
        // horizontal
        parallelFor(0, h_, [&](int y) {
            const float* row = &d_[size_t(y) * w_];
            float* out = &tmp[size_t(y) * w_];
            float acc = 0;
            for (int i = -radius; i <= radius; ++i) acc += row[std::clamp(i, 0, w_ - 1)];
            for (int x = 0; x < w_; ++x) {
                out[x] = acc * inv;
                acc += row[std::min(x + radius + 1, w_ - 1)] - row[std::max(x - radius, 0)];
            }
        });
        // vertical
        parallelFor(0, w_, [&](int x) {
            float acc = 0;
            for (int i = -radius; i <= radius; ++i) acc += tmp[size_t(std::clamp(i, 0, h_ - 1)) * w_ + x];
            for (int y = 0; y < h_; ++y) {
                d_[size_t(y) * w_ + x] = acc * inv;
                acc += tmp[size_t(std::min(y + radius + 1, h_ - 1)) * w_ + x] - tmp[size_t(std::max(y - radius, 0)) * w_ + x];
            }
        });
    }
}

Grid Grid::resampled(int w, int h) const {
    Grid out(w, h);
    if (d_.empty()) return out;
    const float sx = w > 1 ? float(w_ - 1) / float(w - 1) : 0.f;
    const float sy = h > 1 ? float(h_ - 1) / float(h - 1) : 0.f;
    parallelFor(0, h, [&](int y) {
        for (int x = 0; x < w; ++x) out.at(x, y) = sample(float(x) * sx, float(y) * sy);
    });
    return out;
}

}  // namespace zl
