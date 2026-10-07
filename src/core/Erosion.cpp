#include "core/Erosion.h"

#include "core/Hydrology.h"

namespace zl {

void hydraulicErosion(Grid& grid, const HydraulicParams& p, uint64_t seed, Progress* progress, float p0, float p1) {
    const int w = grid.width(), h = grid.height();
    if (w < 8 || p.droplets <= 0) return;
    const float hs = p.heightScale;
    // Work in "cell units" so slopes are rise/run; convert back at the end.
    std::vector<float> H(grid.vec().size());
    for (size_t i = 0; i < H.size(); ++i) H[i] = grid.vec()[i] * hs;

    // erosion brush
    std::vector<int> bOff;
    std::vector<float> bW;
    {
        float wsum = 0;
        for (int dy = -p.radius; dy <= p.radius; ++dy)
            for (int dx = -p.radius; dx <= p.radius; ++dx) {
                float d = std::sqrt(float(dx * dx + dy * dy));
                if (d <= float(p.radius)) {
                    float wgt = 1.f - d / float(p.radius);
                    bOff.push_back(dy * w + dx);
                    bW.push_back(wgt);
                    wsum += wgt;
                }
            }
        for (auto& v : bW) v /= wsum;
    }
    std::vector<int> bDx, bDy;
    for (int dy = -p.radius; dy <= p.radius; ++dy)
        for (int dx = -p.radius; dx <= p.radius; ++dx)
            if (std::sqrt(float(dx * dx + dy * dy)) <= float(p.radius)) { bDx.push_back(dx); bDy.push_back(dy); }

    auto heightGrad = [&](float px, float py, float& gx, float& gy) {
        int ix = int(px), iy = int(py);
        float fx = px - float(ix), fy = py - float(iy);
        size_t i = size_t(iy) * w + ix;
        float nw = H[i], ne = H[i + 1], sw = H[i + w], se = H[i + w + 1];
        gx = (ne - nw) * (1 - fy) + (se - sw) * fy;
        gy = (sw - nw) * (1 - fx) + (se - ne) * fx;
        return nw * (1 - fx) * (1 - fy) + ne * fx * (1 - fy) + sw * (1 - fx) * fy + se * fx * fy;
    };

    Rng rng(seed);
    const int reportEvery = std::max(1, p.droplets / 50);
    for (int d = 0; d < p.droplets; ++d) {
        if (progress && (d % reportEvery) == 0) {
            if (progress->cancel) break;
            progress->set("Hydraulic erosion (droplets)", p0 + (p1 - p0) * float(d) / float(p.droplets));
        }
        float px = rng.range(1.f, float(w - 2)), py = rng.range(1.f, float(h - 2));
        float dx = 0, dy = 0, speed = 1, water = 1, sediment = 0;
        for (int life = 0; life < p.maxLifetime; ++life) {
            int ix = int(px), iy = int(py);
            float fx = px - float(ix), fy = py - float(iy);
            size_t idx = size_t(iy) * w + ix;
            float gx, gy;
            float hOld = heightGrad(px, py, gx, gy);
            dx = dx * p.inertia - gx * (1 - p.inertia);
            dy = dy * p.inertia - gy * (1 - p.inertia);
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-9f) break;
            dx /= len;
            dy /= len;
            px += dx;
            py += dy;
            if (px < 1 || px >= float(w - 2) || py < 1 || py >= float(h - 2)) break;
            float ngx, ngy;
            float hNew = heightGrad(px, py, ngx, ngy);
            float dh = hNew - hOld;
            float cap = std::max(-dh * speed * water * p.capacity, p.minCapacity);
            if (sediment > cap || dh > 0) {
                float dep = dh > 0 ? std::min(dh, sediment) : (sediment - cap) * p.depositSpeed;
                sediment -= dep;
                H[idx] += dep * (1 - fx) * (1 - fy);
                H[idx + 1] += dep * fx * (1 - fy);
                H[idx + w] += dep * (1 - fx) * fy;
                H[idx + w + 1] += dep * fx * fy;
            } else {
                float amount = std::min((cap - sediment) * p.erodeSpeed, -dh);
                for (size_t b = 0; b < bOff.size(); ++b) {
                    int cx = ix + bDx[b], cy = iy + bDy[b];
                    if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
                    size_t ci = size_t(int64_t(idx) + bOff[b]);
                    float e = amount * bW[b];
                    float take = H[ci] < e ? H[ci] : e;
                    H[ci] -= take;
                    sediment += take;
                }
            }
            speed = std::sqrt(std::max(0.f, speed * speed + dh * p.gravity * -1.f));
            if (speed < 1e-3f) speed = 1e-3f;
            water *= (1 - p.evaporate);
        }
    }
    for (size_t i = 0; i < H.size(); ++i) grid.vec()[i] = H[i] / hs;
}

void thermalErosion(Grid& g, int iterations, float talus, float rate) {
    const int w = g.width(), h = g.height();
    if (w < 3 || iterations <= 0) return;
    static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float od[8] = {1, 1, 1, 1, 1.41421f, 1.41421f, 1.41421f, 1.41421f};
    Grid next(w, h);
    for (int it = 0; it < iterations; ++it) {
        parallelFor(0, h, [&](int y) {
            for (int x = 0; x < w; ++x) {
                float c = g.at(x, y);
                float delta = 0;
                for (int k = 0; k < 8; ++k) {
                    int nx = x + ox[k], ny = y + oy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    float n = g.at(nx, ny);
                    float t = talus * od[k];
                    float diff = c - n;
                    if (diff > t) delta -= rate * (diff - t) * 0.0625f;
                    else if (-diff > t) delta += rate * (-diff - t) * 0.0625f;
                }
                next.at(x, y) = c + delta;
            }
        });
        std::swap(g, next);
    }
}

void fluvialErosion(Grid& g, int iterations, float strength) {
    const int w = g.width(), h = g.height();
    if (w < 8) return;
    static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float od[8] = {1, 1, 1, 1, 1.41421f, 1.41421f, 1.41421f, 1.41421f};
    const float norm = 1.f / float(w);
    for (int it = 0; it < iterations; ++it) {
        Grid filled = fillDepressions(g);
        Grid acc = flowAccumulation(filled);
        Grid next = g;
        parallelFor(1, h - 1, [&](int y) {
            for (int x = 1; x < w - 1; ++x) {
                float c = filled.at(x, y);
                float best = 0;
                float hd = c;
                for (int k = 0; k < 8; ++k) {
                    float n = filled.at(x + ox[k], y + oy[k]);
                    float s = (c - n) / od[k];
                    if (s > best) { best = s; hd = n; }
                }
                if (best <= 0) continue;
                float a = acc.at(x, y) * norm;
                float slope = best * float(w);   // uv-space slope
                float e = strength * std::sqrt(a) * std::min(slope, 4.f) * 0.02f;
                e = std::min(e, (g.at(x, y) - hd) * 0.5f + 1e-6f);
                if (e > 0) next.at(x, y) = g.at(x, y) - e;
            }
        });
        g = std::move(next);
    }
}

void upliftStreamPower(Grid& h, const Grid& uplift, int iterations, float kf, float talus, const Grid* fixedMask) {
    const int w = h.width(), hh = h.height();
    const size_t n = size_t(w) * hh;
    static const int ox[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int oy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float od[8] = {1, 1, 1, 1, 1.41421f, 1.41421f, 1.41421f, 1.41421f};
    std::vector<int> rcv(n), order(n);
    std::vector<float> dist(n), area(n);
    const float cellArea = 1.f / float(w * hh);
    const float dt = 1.f / float(std::max(1, iterations));
    auto isFixed = [&](size_t i) {
        int x = int(i % w), y = int(i / w);
        if (x == 0 || y == 0 || x == w - 1 || y == hh - 1) return true;
        return fixedMask && fixedMask->vec()[i] > 0.5f;
    };
    for (int it = 0; it < iterations; ++it) {
        // uplift (never at base level)
        for (size_t i = 0; i < n; ++i)
            if (!isFixed(i)) h.vec()[i] += uplift.vec()[i] * dt * 0.6f;
        // route over the depression-filled surface; lakes fill with sediment. A tiny random perturbation
        // breaks D8 ties so flats drain along organic, not grid-aligned, channels.
        for (size_t i = 0; i < n; ++i)
            if (!isFixed(i)) h.vec()[i] += float(splitmix64(uint64_t(i) * 2654435761ull + uint64_t(it)) >> 40) * (2e-5f / 16777216.f);
        Grid filled = fillDepressions(h, 1e-6f / float(w));
        h = filled;
        for (size_t i = 0; i < n; ++i) {
            rcv[i] = int(i);
            dist[i] = 1.f;
            if (isFixed(i)) continue;
            int x = int(i % w), y = int(i / w);
            float best = 0;
            for (int k = 0; k < 8; ++k) {
                size_t j = size_t(y + oy[k]) * w + size_t(x + ox[k]);
                float s = (h.vec()[i] - h.vec()[j]) / od[k];
                if (s > best) { best = s; rcv[i] = int(j); dist[i] = od[k]; }
            }
        }
        for (size_t i = 0; i < n; ++i) order[i] = int(i);
        const auto& hv = h.vec();
        std::sort(order.begin(), order.end(), [&](int a, int b) { return hv[a] < hv[b] || (hv[a] == hv[b] && a < b); });
        std::fill(area.begin(), area.end(), cellArea);
        for (size_t k = n; k-- > 0;) {
            int i = order[k];
            if (rcv[i] != i) area[size_t(rcv[i])] += area[size_t(i)];
        }
        // implicit incision, downstream to upstream: h_i = (h_i + F h_r) / (1 + F)
        for (size_t k = 0; k < n; ++k) {
            int i = order[k];
            int r = rcv[i];
            if (r == i) continue;
            float F = kf * dt * std::sqrt(area[size_t(i)]) * float(w) / dist[size_t(i)];
            h.vec()[size_t(i)] = (h.vec()[size_t(i)] + F * h.vec()[size_t(r)]) / (1.f + F);
        }
        // hillslopes can't be steeper than talus
        thermalErosion(h, 2, talus, 0.5f);
    }
}

void windSmoothing(Grid& g, Vec2 windDir, float amount) {
    const int w = g.width(), h = g.height();
    Vec2 d = windDir.normalized();
    Grid next(w, h);
    const int passes = 1 + int(amount * 3.f);
    for (int p = 0; p < passes; ++p) {
        parallelFor(0, h, [&](int y) {
            for (int x = 0; x < w; ++x) {
                float a = g.sample(float(x) + d.x * 1.5f, float(y) + d.y * 1.5f);
                float b = g.sample(float(x) - d.x * 1.5f, float(y) - d.y * 1.5f);
                float c = g.at(x, y);
                next.at(x, y) = lerpf(c, (a + b + c) / 3.f, amount * 0.5f);
            }
        });
        std::swap(g, next);
    }
}

}  // namespace zl
