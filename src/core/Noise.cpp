#include "core/Noise.h"

namespace zl {

Noise::Noise(uint64_t seed) : seed_(seed) {
    std::array<uint8_t, 256> p{};
    for (int i = 0; i < 256; ++i) p[i] = uint8_t(i);
    Rng rng(seed ^ 0xA5A5F00Dull);
    for (int i = 255; i > 0; --i) std::swap(p[i], p[rng.next() % uint64_t(i + 1)]);
    for (int i = 0; i < 512; ++i) perm_[i] = p[i & 255];
}

uint32_t Noise::hashi(int ix, int iy) const {
    return uint32_t(perm_[(perm_[ix & 255] + iy) & 511]) | (uint32_t(perm_[(perm_[(ix + 101) & 255] + iy + 37) & 511]) << 8);
}

float Noise::hash2(int ix, int iy) const {
    return float(hashi(ix, iy)) / 32767.5f - 1.f;
}

float Noise::grad(int hash, float x, float y) const {
    // 8 gradient directions + scaled diagonals
    switch (hash & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
    }
}

float Noise::simplex(float xin, float yin) const {
    const float F2 = 0.36602540378f;   // 0.5*(sqrt(3)-1)
    const float G2 = 0.2113248654f;    // (3-sqrt(3))/6
    float s = (xin + yin) * F2;
    int i = int(std::floor(xin + s));
    int j = int(std::floor(yin + s));
    float t = float(i + j) * G2;
    float x0 = xin - (float(i) - t);
    float y0 = yin - (float(j) - t);
    int i1 = x0 > y0 ? 1 : 0;
    int j1 = x0 > y0 ? 0 : 1;
    float x1 = x0 - float(i1) + G2, y1 = y0 - float(j1) + G2;
    float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
    int ii = i & 255, jj = j & 255;
    float n0 = 0, n1 = 0, n2 = 0;
    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 > 0) { t0 *= t0; n0 = t0 * t0 * grad(perm_[ii + perm_[jj]], x0, y0); }
    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0) { t1 *= t1; n1 = t1 * t1 * grad(perm_[ii + i1 + perm_[jj + j1]], x1, y1); }
    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0) { t2 *= t2; n2 = t2 * t2 * grad(perm_[ii + 1 + perm_[jj + 1]], x2, y2); }
    return 70.f * (n0 + n1 + n2);
}

Vec3 Noise::valueD(float x, float y) const {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - float(ix), fy = y - float(iy);
    float ux = fx * fx * fx * (fx * (fx * 6.f - 15.f) + 10.f);
    float uy = fy * fy * fy * (fy * (fy * 6.f - 15.f) + 10.f);
    float dux = 30.f * fx * fx * (fx * (fx - 2.f) + 1.f);
    float duy = 30.f * fy * fy * (fy * (fy - 2.f) + 1.f);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
    float v = a + k1 * ux + k2 * uy + k4 * ux * uy;
    return {v, dux * (k1 + k4 * uy), duy * (k2 + k4 * ux)};
}

CellResult Noise::cellular(float x, float y, float jitter) const {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    CellResult r;
    r.f1 = r.f2 = 1e9f;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int cx = ix + ox, cy = iy + oy;
            uint32_t h = hashi(cx, cy);
            uint32_t h2 = hashi(cx * 7 + 13, cy * 5 + 3);
            float px = float(cx) + 0.5f + jitter * ((float(h & 0xFFFF) / 65535.f) - 0.5f);
            float py = float(cy) + 0.5f + jitter * ((float(h2 & 0xFFFF) / 65535.f) - 0.5f);
            float dx = px - x, dy = py - y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < r.f1) {
                r.f2 = r.f1;
                r.f1 = d;
                r.id = h ^ (h2 << 16);
                r.nearest = {px, py};
            } else if (d < r.f2) {
                r.f2 = d;
            }
        }
    }
    return r;
}

float Noise::periodicValue(float x, float y, int period) const {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - float(ix), fy = y - float(iy);
    auto wrap = [period](int v) { int m = v % period; return m < 0 ? m + period : m; };
    float ux = fx * fx * (3.f - 2.f * fx), uy = fy * fy * (3.f - 2.f * fy);
    float a = hash2(wrap(ix), wrap(iy)), b = hash2(wrap(ix + 1), wrap(iy));
    float c = hash2(wrap(ix), wrap(iy + 1)), d = hash2(wrap(ix + 1), wrap(iy + 1));
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}

float Noise::fbm(float x, float y, int octaves, float lac, float gain) const {
    float sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * simplex(x, y);
        norm += amp;
        // rotate each octave slightly to break grid alignment
        float nx = x * 1.6f - y * 1.2f, ny = x * 1.2f + y * 1.6f;
        x = nx * (lac / 2.f) + 17.13f;
        y = ny * (lac / 2.f) - 9.71f;
        amp *= gain;
    }
    return sum / norm;
}

float Noise::ridged(float x, float y, int octaves, float lac, float gain) const {
    float sum = 0, amp = 0.5f, weight = 1.f, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        float n = 1.f - std::fabs(simplex(x, y));
        n *= n;
        n *= weight;
        weight = clamp01(n * 2.f);
        sum += n * amp;
        norm += amp;
        x = x * lac + 31.7f;
        y = y * lac - 11.3f;
        amp *= gain;
    }
    return clamp01(sum / norm);
}

float Noise::billow(float x, float y, int octaves, float lac, float gain) const {
    float sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * std::fabs(simplex(x, y));
        norm += amp;
        x = x * lac + 5.3f;
        y = y * lac + 2.9f;
        amp *= gain;
    }
    return sum / norm;
}

float Noise::erodedFbm(float x, float y, int octaves, float lac, float gain, float erosion) const {
    float sum = 0, amp = 1, norm = 0;
    float dx = 0, dy = 0;
    for (int o = 0; o < octaves; ++o) {
        Vec3 n = valueD(x, y);
        dx += n.y;
        dy += n.z;
        float damp = 1.f / (1.f + erosion * (dx * dx + dy * dy));
        sum += amp * n.x * damp;
        norm += amp;
        float nx = x * 1.6f - y * 1.2f, ny = x * 1.2f + y * 1.6f;
        x = nx * (lac / 2.f) + 3.1f;
        y = ny * (lac / 2.f) + 7.7f;
        amp *= gain;
    }
    return sum / norm;
}

float Noise::periodicFbm(float x, float y, int period, int octaves, float gain) const {
    float sum = 0, amp = 1, norm = 0;
    int p = period;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * periodicValue(x, y, p);
        norm += amp;
        x *= 2.f;
        y *= 2.f;
        p *= 2;
        amp *= gain;
    }
    return sum / norm;
}

Vec2 Noise::warp(float x, float y, float strength, float freq, int octaves) const {
    float wx = fbm(x * freq + 5.2f, y * freq + 1.3f, octaves);
    float wy = fbm(x * freq - 8.3f, y * freq + 2.8f, octaves);
    return {x + wx * strength, y + wy * strength};
}

}  // namespace zl
