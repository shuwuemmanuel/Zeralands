#include "core/Hydrology.h"

#include "core/Noise.h"

#include <queue>

namespace zl {

namespace {
const int kOx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
const int kOy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
const float kOd[8] = {1, 1, 1, 1, 1.41421f, 1.41421f, 1.41421f, 1.41421f};

struct Cell {
    float h;
    int idx;
    bool operator>(const Cell& o) const { return h > o.h || (h == o.h && idx > o.idx); }
};
}  // namespace

Grid fillDepressions(const Grid& src, float epsilon) {
    const int w = src.width(), h = src.height();
    Grid out = src;
    std::vector<uint8_t> closed(size_t(w) * h, 0);
    std::priority_queue<Cell, std::vector<Cell>, std::greater<Cell>> open;
    for (int x = 0; x < w; ++x) {
        for (int y : {0, h - 1}) {
            int i = y * w + x;
            if (!closed[i]) { closed[i] = 1; open.push({out.vec()[i], i}); }
        }
    }
    for (int y = 1; y < h - 1; ++y) {
        for (int x : {0, w - 1}) {
            int i = y * w + x;
            if (!closed[i]) { closed[i] = 1; open.push({out.vec()[i], i}); }
        }
    }
    while (!open.empty()) {
        Cell c = open.top();
        open.pop();
        int cx = c.idx % w, cy = c.idx / w;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + kOx[k], ny = cy + kOy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            int ni = ny * w + nx;
            if (closed[ni]) continue;
            closed[ni] = 1;
            float nh = out.vec()[ni];
            float minH = c.h + epsilon;
            if (nh < minH) nh = minH;
            out.vec()[ni] = nh;
            open.push({nh, ni});
        }
    }
    return out;
}

Grid flowAccumulation(const Grid& filled) {
    const int w = filled.width(), h = filled.height();
    const size_t n = size_t(w) * h;
    Grid acc(w, h, 1.f);
    std::vector<int> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = int(i);
    const auto& v = filled.vec();
    std::sort(order.begin(), order.end(), [&](int a, int b) { return v[a] > v[b] || (v[a] == v[b] && a < b); });
    for (int i : order) {
        int x = i % w, y = i / w;
        float best = 0;
        int target = -1;
        for (int k = 0; k < 8; ++k) {
            int nx = x + kOx[k], ny = y + kOy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            float s = (v[i] - v[ny * w + nx]) / kOd[k];
            if (s > best) { best = s; target = ny * w + nx; }
        }
        if (target >= 0) acc.vec()[target] += acc.vec()[i];
    }
    return acc;
}

void computeHydrology(Terrain& t, const HydrologyParams& p, uint64_t seed, Grid* lavaMask) {
    const int w = t.height.width(), h = t.height.height();
    const size_t n = size_t(w) * h;
    t.water.resize(w, h, -1.f);
    t.flow.resize(w, h, 0.f);
    t.moisture.resize(w, h, p.baseMoisture);
    if (t.lava.empty()) t.lava.resize(w, h, 0.f);

    Grid filled = fillDepressions(t.height, 1e-7f);
    Grid acc = flowAccumulation(filled);
    float maxAcc = 1.f;
    for (float a : acc.vec()) maxAcc = std::max(maxAcc, a);
    const float logMax = std::log1p(maxAcc);
    for (size_t i = 0; i < n; ++i) t.flow.vec()[i] = std::log1p(acc.vec()[i]) / logMax;

    Grid wet(w, h, 0.f);

    // ---- sea
    if (p.seaLevel >= 0.f) {
        for (size_t i = 0; i < n; ++i)
            if (t.height.vec()[i] < p.seaLevel) {
                t.water.vec()[i] = p.seaLevel;
                wet.vec()[i] = 1.f;
            }
    }

    // ---- lakes (connected depression components deeper than lakeMinDepth)
    if (p.lakes) {
        std::vector<int> label(n, -1);
        std::vector<int> stack;
        for (size_t s = 0; s < n; ++s) {
            if (label[s] != -1) continue;
            float depth = filled.vec()[s] - t.height.vec()[s];
            if (depth < p.lakeMinDepth * 0.25f || t.water.vec()[s] >= 0.f) { label[s] = -2; continue; }
            std::vector<int> comp;
            float maxDepth = 0;
            stack.push_back(int(s));
            label[s] = int(s);
            while (!stack.empty()) {
                int c = stack.back();
                stack.pop_back();
                comp.push_back(c);
                maxDepth = std::max(maxDepth, filled.vec()[c] - t.height.vec()[c]);
                int cx = c % w, cy = c / w;
                for (int k = 0; k < 4; ++k) {
                    int nx = cx + kOx[k], ny = cy + kOy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    int ni = ny * w + nx;
                    if (label[ni] != -1) continue;
                    float d = filled.vec()[ni] - t.height.vec()[ni];
                    if (d < p.lakeMinDepth * 0.25f || t.water.vec()[ni] >= 0.f) continue;
                    label[ni] = int(s);
                    stack.push_back(ni);
                }
            }
            const bool tooBig = float(comp.size()) > p.lakeMaxFraction * float(n);
            if (int(comp.size()) >= p.lakeMinCells && maxDepth >= p.lakeMinDepth && !tooBig) {
                for (int c : comp) {
                    if (lavaMask) {
                        lavaMask->vec()[c] = 1.f;
                    } else {
                        t.water.vec()[c] = filled.vec()[c];
                        wet.vec()[c] = 1.f;
                    }
                }
            }
        }
    }

    // ---- rivers
    if (p.rivers) {
        const float thr = p.riverThreshold * float(n);
        Grid river(w, h, 0.f);
        for (size_t i = 0; i < n; ++i) {
            float a = acc.vec()[i];
            if (a > thr && t.water.vec()[i] < 0.f) river.vec()[i] = clamp01(std::log(a / thr) / std::log(maxAcc / thr + 1.f) * 1.5f + 0.35f);
        }
        Grid bed = river;
        bed.blur(std::max(1, w / 700), 2);
        for (size_t i = 0; i < n; ++i) {
            float b = bed.vec()[i];
            if (b <= 0.02f) continue;
            float orig = t.height.vec()[i];
            t.height.vec()[i] = orig - p.riverDepth * std::min(1.f, b * 2.5f);
            if (b > 0.18f && t.water.vec()[i] < 0.f) {
                t.water.vec()[i] = orig - p.riverDepth * 0.3f;
                wet.vec()[i] = 1.f;
            }
        }
    }

    // ---- moisture: climate + proximity to water + flow + noise
    Grid near = wet;
    near.blur(std::max(2, w / 48), 3);
    Noise nz(seed ^ 0x3013ull);
    parallelFor(0, h, [&](int y) {
        for (int x = 0; x < w; ++x) {
            float m = p.baseMoisture;
            m += 0.55f * std::min(1.f, near.at(x, y) * 3.f);
            m += 0.25f * smoothstepf(0.35f, 0.8f, t.flow.at(x, y));
            m += 0.12f * nz.fbm(float(x) / w * 5.f, float(y) / h * 5.f, 4);
            m -= 0.15f * smoothstepf(0.6f, 1.f, t.height.at(x, y));   // rain shadow / altitude dryness
            t.moisture.at(x, y) = clamp01(m);
        }
    });
}

}  // namespace zl
