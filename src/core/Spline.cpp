#include "core/Spline.h"

namespace zl {

namespace {

Vec3 lerp3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Centripetal Catmull-Rom (Barry-Goldman pyramid), alpha = 0.5.
Vec3 catmull(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float u) {
    auto knot = [](float ti, const Vec3& a, const Vec3& b) {
        Vec3 d = b - a;
        float l = std::sqrt(d.x * d.x + d.z * d.z + 0.01f * d.y * d.y);
        return ti + std::sqrt(std::max(l, 1e-4f));
    };
    float t0 = 0, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
    float t = lerpf(t1, t2, u);
    Vec3 a1 = lerp3(p0, p1, (t - t0) / (t1 - t0));
    Vec3 a2 = lerp3(p1, p2, (t - t1) / (t2 - t1));
    Vec3 a3 = lerp3(p2, p3, (t - t2) / (t3 - t2));
    Vec3 b1 = lerp3(a1, a2, (t - t0) / (t2 - t0));
    Vec3 b2 = lerp3(a2, a3, (t - t1) / (t3 - t1));
    return lerp3(b1, b2, (t - t1) / (t2 - t1));
}

float horizDist(const Vec3& a, const Vec3& b) {
    float dx = b.x - a.x, dz = b.z - a.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

std::vector<SplineSample> Spline::sample(float step) const {
    std::vector<SplineSample> out;
    const int n = int(points.size());
    if (n < 2) return out;
    step = std::max(0.25f, step);
    const int segs = closed ? n : n - 1;
    auto P = [&](int i) -> const SplinePoint& {
        if (closed) return points[size_t(((i % n) + n) % n)];
        return points[size_t(std::clamp(i, 0, n - 1))];
    };
    for (int s = 0; s < segs; ++s) {
        const SplinePoint &a = P(s - 1), &b = P(s), &c = P(s + 1), &d = P(s + 2);
        Vec3 p0 = a.pos, p3 = d.pos;
        if (!closed && s == 0) p0 = b.pos * 2.f - c.pos;
        if (!closed && s == segs - 1) p3 = c.pos * 2.f - b.pos;
        float len = horizDist(b.pos, c.pos);
        int k = std::max(1, int(std::ceil(len / step)));
        float wb = b.width > 0 ? b.width : width, wc = c.width > 0 ? c.width : width;
        for (int i = 0; i < k; ++i) {
            float u = float(i) / float(k);
            SplineSample sm;
            sm.pos = catmull(p0, b.pos, c.pos, p3, u);
            sm.width = lerpf(wb, wc, u);
            out.push_back(sm);
        }
    }
    if (!closed) {
        SplineSample last;
        last.pos = points.back().pos;
        last.width = points.back().width > 0 ? points.back().width : width;
        out.push_back(last);
    }
    // tangents, distances, curvature
    const int m = int(out.size());
    float dist = 0;
    for (int i = 0; i < m; ++i) {
        int ip = closed ? (i - 1 + m) % m : std::max(0, i - 1);
        int in = closed ? (i + 1) % m : std::min(m - 1, i + 1);
        Vec2 d{out[in].pos.x - out[ip].pos.x, out[in].pos.z - out[ip].pos.z};
        out[i].dir = d.normalized();
        if (i > 0) dist += horizDist(out[i - 1].pos, out[i].pos);
        out[i].distance = dist;
    }
    for (int i = 0; i < m; ++i) {
        int ip = closed ? (i - 1 + m) % m : std::max(0, i - 1);
        int in = closed ? (i + 1) % m : std::min(m - 1, i + 1);
        float ds = horizDist(out[ip].pos, out[in].pos);
        float c = cross(out[ip].dir, out[in].dir);
        float dd = dot(out[ip].dir, out[in].dir);
        out[i].curvature = ds > 1e-3f ? std::atan2(c, dd) / ds : 0.f;
    }
    if (maxBank > 0) {
        std::vector<float> bank(m);
        for (int i = 0; i < m; ++i) bank[i] = clampf(out[i].curvature * 70.f, -1.f, 1.f) * maxBank;
        // smooth the banking so it rolls in/out
        for (int pass = 0; pass < 6; ++pass) {
            std::vector<float> nb(m);
            for (int i = 0; i < m; ++i) {
                int ip = closed ? (i - 1 + m) % m : std::max(0, i - 1);
                int in = closed ? (i + 1) % m : std::min(m - 1, i + 1);
                nb[i] = (bank[ip] + bank[i] * 2.f + bank[in]) * 0.25f;
            }
            bank.swap(nb);
        }
        for (int i = 0; i < m; ++i) out[i].bank = bank[i];
    }
    return out;
}

float Spline::length() const {
    auto s = sample(4.f);
    if (s.empty()) return 0.f;
    float l = s.back().distance;
    if (closed && s.size() > 1) l += horizDist(s.back().pos, s.front().pos);
    return l;
}

void conformSplineHeights(Spline& s, const Terrain& t, float maxGrade, float smoothMeters) {
    if (s.points.size() < 2 || t.baseHeight.empty()) return;
    const Grid& base = t.baseHeight;
    auto groundAt = [&](float x, float z) {
        Vec2 g = t.worldToGrid(x, z);
        float hb = base.sample(g.x, g.y);
        float wl = t.water.empty() ? -1.f : t.water.sample(g.x, g.y);
        float y = hb * t.heightRange;
        if (wl > hb + 1e-5f) y = std::max(y, wl * t.heightRange + 3.f);   // bridge deck clearance
        return y;
    };
    // dense samples along the plan (heights ignored for now)
    for (auto& p : s.points) p.pos.y = groundAt(p.pos.x, p.pos.z);
    auto smp = s.sample(std::max(2.f, t.cellSize()));
    const int m = int(smp.size());
    if (m < 2) return;
    std::vector<float> y(m), d(m);
    for (int i = 0; i < m; ++i) {
        y[i] = groundAt(smp[i].pos.x, smp[i].pos.z);
        d[i] = smp[i].distance;
    }
    // moving average in distance
    if (smoothMeters > 0) {
        std::vector<float> ys(m);
        size_t lo = 0, hi = 0;
        double acc = 0;
        for (int i = 0; i < m; ++i) {
            while (hi < size_t(m) && d[hi] <= d[i] + smoothMeters) acc += y[hi++];
            while (d[lo] < d[i] - smoothMeters) acc -= y[lo++];
            ys[i] = float(acc / double(hi - lo));
        }
        y.swap(ys);
    }
    // grade limiting (forward + backward)
    if (maxGrade > 0) {
        std::vector<float> f = y, b = y;
        for (int i = 1; i < m; ++i) {
            float ds = d[i] - d[i - 1];
            f[i] = clampf(f[i], f[i - 1] - maxGrade * ds, f[i - 1] + maxGrade * ds);
        }
        for (int i = m - 2; i >= 0; --i) {
            float ds = d[i + 1] - d[i];
            b[i] = clampf(b[i], b[i + 1] - maxGrade * ds, b[i + 1] + maxGrade * ds);
        }
        for (int i = 0; i < m; ++i) y[i] = 0.5f * (f[i] + b[i]);
    }
    if (s.elevated) {
        float clearance = s.kind == SplineKind::Maglev ? 12.f : 9.f;
        for (int i = 0; i < m; ++i) y[i] = std::max(y[i], groundAt(smp[i].pos.x, smp[i].pos.z) + clearance);
    }
    // write back to control points (nearest sample)
    for (auto& p : s.points) {
        float best = 1e30f;
        int bi = 0;
        for (int i = 0; i < m; ++i) {
            float dd = horizDist(p.pos, smp[i].pos);
            if (dd < best) { best = dd; bi = i; }
        }
        p.pos.y = y[bi];
    }
}

void stampSplines(Terrain& t, const std::vector<Spline>& splines) {
    const int res = t.baseHeight.width();
    if (res == 0) return;
    t.height = t.baseHeight;
    t.roadMask.resize(res, res, 0.f);
    Grid weight(res, res, 0.f), target(res, res, 0.f);
    const float cell = t.cellSize();

    for (const auto& s : splines) {
        auto smp = s.sample(cell * 0.75f);
        const int m = int(smp.size());
        if (m < 2) continue;
        const int segs = s.closed ? m : m - 1;
        const bool carve = !s.elevated;
        const float canalDepth = s.waterFilled ? 2.5f : 0.f;
        const float lavaDepth = s.kind == SplineKind::LavaChannel ? 1.2f : 0.f;
        for (int i = 0; i < segs; ++i) {
            const SplineSample& a = smp[i];
            const SplineSample& b = smp[(i + 1) % m];
            const float halfA = a.width * 0.5f, halfB = b.width * 0.5f;
            const float reach = std::max(halfA, halfB) + s.shoulder;
            // bridges: don't stamp where the deck crosses water
            Vec2 ga = t.worldToGrid(a.pos.x, a.pos.z);
            bool overWater = !t.water.empty() && t.water.sample(ga.x, ga.y) > t.baseHeight.sample(ga.x, ga.y) + 0.5f / t.heightRange;
            float minX = std::min(a.pos.x, b.pos.x) - reach, maxX = std::max(a.pos.x, b.pos.x) + reach;
            float minZ = std::min(a.pos.z, b.pos.z) - reach, maxZ = std::max(a.pos.z, b.pos.z) + reach;
            int x0 = std::max(0, int(std::floor(minX / cell))), x1 = std::min(res - 1, int(std::ceil(maxX / cell)));
            int y0 = std::max(0, int(std::floor(minZ / cell))), y1 = std::min(res - 1, int(std::ceil(maxZ / cell)));
            Vec2 A{a.pos.x, a.pos.z}, B{b.pos.x, b.pos.z};
            Vec2 AB = B - A;
            float len2 = std::max(1e-6f, dot(AB, AB));
            for (int gy = y0; gy <= y1; ++gy) {
                for (int gx = x0; gx <= x1; ++gx) {
                    Vec2 P{float(gx) * cell, float(gy) * cell};
                    float tt = clamp01(dot(P - A, AB) / len2);
                    Vec2 C = A + AB * tt;
                    Vec2 off = P - C;
                    float dist = off.length();
                    float half = lerpf(halfA, halfB, tt);
                    if (dist > half + s.shoulder) continue;
                    // signed lateral offset (right of travel direction positive)
                    Vec2 dir = AB / std::sqrt(len2);
                    float lateral = -cross(dir, off);
                    float w = dist <= half ? 1.f : 1.f - smoothstepf(half, half + s.shoulder, dist);
                    float cover = dist <= half + 0.5f ? 1.f : (1.f - smoothstepf(half, half + s.shoulder * 0.6f, dist));
                    float& rm = t.roadMask.at(gx, gy);
                    rm = std::max(rm, cover);
                    if (!carve || overWater) continue;
                    float y = lerpf(a.pos.y, b.pos.y, tt);
                    float bank = lerpf(a.bank, b.bank, tt);
                    if (dist <= half) y -= std::tan(bank) * lateral;
                    if (canalDepth > 0 || lavaDepth > 0) {
                        float depth = canalDepth > 0 ? canalDepth : lavaDepth;
                        float k = clamp01(dist / std::max(0.5f, half));
                        y -= depth * (1.f - k * k);
                    }
                    float& wg = weight.at(gx, gy);
                    if (w > wg) {
                        wg = w;
                        target.at(gx, gy) = y / t.heightRange;
                    }
                }
            }
        }
    }
    parallelFor(0, res, [&](int y) {
        for (int x = 0; x < res; ++x) {
            float w = weight.at(x, y);
            if (w > 0.f) t.height.at(x, y) = lerpf(t.baseHeight.at(x, y), target.at(x, y), w);
        }
    });
}

int pickControlPoint(const Spline& s, Vec2 p, float maxDist) {
    int best = -1;
    float bd = maxDist;
    for (size_t i = 0; i < s.points.size(); ++i) {
        float d = (Vec2(s.points[i].pos.x, s.points[i].pos.z) - p).length();
        if (d < bd) { bd = d; best = int(i); }
    }
    return best;
}

int insertControlPoint(Spline& s, Vec2 p, const Terrain& t) {
    SplinePoint np;
    np.pos = {p.x, t.heightAtWorld(p.x, p.y), p.y};
    const int n = int(s.points.size());
    if (n < 2) {
        s.points.push_back(np);
        return int(s.points.size()) - 1;
    }
    // closest segment between consecutive control points; otherwise append to the nearer end
    int bestSeg = -1;
    float bd = 1e30f;
    int segs = s.closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        Vec2 a{s.points[i].pos.x, s.points[i].pos.z};
        Vec2 b{s.points[(i + 1) % n].pos.x, s.points[(i + 1) % n].pos.z};
        Vec2 ab = b - a;
        float tt = clamp01(dot(p - a, ab) / std::max(1e-6f, dot(ab, ab)));
        if (tt <= 0.02f || tt >= 0.98f) continue;
        float d = (a + ab * tt - p).length();
        if (d < bd) { bd = d; bestSeg = i; }
    }
    float dEnd = (Vec2(s.points.back().pos.x, s.points.back().pos.z) - p).length();
    float dStart = (Vec2(s.points.front().pos.x, s.points.front().pos.z) - p).length();
    if (bestSeg >= 0 && bd < std::min(dEnd, dStart) * 0.5f) {
        s.points.insert(s.points.begin() + bestSeg + 1, np);
        return bestSeg + 1;
    }
    if (!s.closed && dStart < dEnd) {
        s.points.insert(s.points.begin(), np);
        return 0;
    }
    s.points.push_back(np);
    return int(s.points.size()) - 1;
}

}  // namespace zl
