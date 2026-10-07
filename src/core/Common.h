// ZeraLands - shared math, RNG and threading helpers used by every module.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace zl {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
    float t = clamp01((x - e0) / (e1 - e0 + 1e-12f));
    return t * t * (3.f - 2.f * t);
}
inline float remapf(float v, float a0, float a1, float b0, float b1) {
    return b0 + (v - a0) / (a1 - a0 + 1e-12f) * (b1 - b0);
}

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    float length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const { float l = length(); return l > 1e-12f ? Vec2(x / l, y / l) : Vec2(0, 0); }
    Vec2 perp() const { return {-y, x}; }
};
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { float l = length(); return l > 1e-12f ? Vec3(x / l, y / l, z / l) : Vec3(0, 1, 0); }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

struct Color3 {
    float r = 0, g = 0, b = 0;
};

// ---------------------------------------------------------------- hashing / RNG
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline uint64_t hashCombine(uint64_t a, uint64_t b) { return splitmix64(a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))); }
inline uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return splitmix64(h);
}

// Small, fast, deterministic PRNG (xoshiro256**).
class Rng {
public:
    explicit Rng(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) {
        uint64_t x = seed;
        for (auto& v : s_) { x = splitmix64(x); v = x; }
    }
    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t; s_[3] = rotl(s_[3], 45);
        return result;
    }
    float uniform() { return float((next() >> 40) * (1.0 / 16777216.0)); }        // [0,1)
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int irange(int a, int b) { return a + int(next() % uint64_t(b - a + 1)); }       // inclusive
    float normal() {                                                                 // N(0,1)
        float u1 = std::max(uniform(), 1e-7f), u2 = uniform();
        return std::sqrt(-2.f * std::log(u1)) * std::cos(kTau * u2);
    }
    bool chance(float p) { return uniform() < p; }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

// ---------------------------------------------------------------- threading
inline unsigned workerCount() {
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 4u : n;
}

// Runs fn(i) for i in [begin,end) across all cores. Deterministic as long as fn(i) only writes row i.
inline void parallelFor(int begin, int end, const std::function<void(int)>& fn) {
    const int count = end - begin;
    if (count <= 0) return;
    unsigned threads = std::min<unsigned>(workerCount(), unsigned(count));
    if (threads <= 1 || count < 8) {
        for (int i = begin; i < end; ++i) fn(i);
        return;
    }
    std::atomic<int> next{begin};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&]() {
            for (;;) {
                int i = next.fetch_add(1);
                if (i >= end) break;
                fn(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

// Progress / cancellation channel shared between the generator and the UI thread.
struct Progress {
    std::atomic<float> fraction{0.f};
    std::atomic<bool> cancel{false};
    std::string stage;   // written by the worker, read by UI (best-effort, guarded by stageMutex in App)
    std::function<void(const std::string&, float)> onStage;
    void set(const std::string& s, float f) {
        fraction = f;
        if (onStage) onStage(s, f);
    }
};

}  // namespace zl
