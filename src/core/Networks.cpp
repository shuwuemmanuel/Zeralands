#include "core/Networks.h"

#include "core/Noise.h"

#include <cstdio>
#include <numeric>
#include <queue>

namespace zl {

namespace {

const int kOx[8] = {1, 1, 0, -1, -1, -1, 0, 1};   // directions ordered by angle (45 deg steps)
const int kOy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const float kOd[8] = {1, 1.41421f, 1, 1.41421f, 1, 1.41421f, 1, 1.41421f};

struct Coarse {
    int n = 0;
    float cellM = 1;         // meters per coarse cell
    float scale = 1;         // fine cells per coarse cell
    std::vector<float> hM;   // ground height (m)
    std::vector<uint8_t> water, lava;
    std::vector<float> slope;
    std::vector<float> road;   // existing network reuse (0/1)
    std::vector<float> wig;    // 0..1 meander noise for winding trails
    int idx(int x, int y) const { return y * n + x; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < n && y < n; }
    Vec2 toWorld(int x, int y) const { return {float(x) * cellM, float(y) * cellM}; }
    void fromWorld(Vec2 p, int& x, int& y) const {
        x = std::clamp(int(std::lround(p.x / cellM)), 0, n - 1);
        y = std::clamp(int(std::lround(p.y / cellM)), 0, n - 1);
    }
};

Coarse buildCoarse(const Terrain& t) {
    Coarse c;
    const int res = t.res();
    c.n = std::clamp(res / 4, 96, 320);
    c.scale = float(res - 1) / float(c.n - 1);
    c.cellM = t.worldSize / float(c.n - 1);
    const size_t N = size_t(c.n) * c.n;
    c.hM.resize(N);
    c.water.resize(N);
    c.lava.resize(N);
    c.slope.resize(N);
    c.road.assign(N, 0.f);
    c.wig.resize(N);
    {
        Noise nz(0x5EEDull);
        for (int y = 0; y < c.n; ++y)
            for (int x = 0; x < c.n; ++x) c.wig[size_t(y) * c.n + x] = 0.5f + 0.5f * nz.fbm(float(x) * 0.08f, float(y) * 0.08f, 3);
    }
    for (int y = 0; y < c.n; ++y)
        for (int x = 0; x < c.n; ++x) {
            float gx = float(x) * c.scale, gy = float(y) * c.scale;
            float h = t.height.sample(gx, gy);
            size_t i = size_t(y) * c.n + x;
            c.hM[i] = h * t.heightRange;
            c.water[i] = (!t.water.empty() && t.water.sample(gx, gy) > h + 1e-5f) ? 1 : 0;
            c.lava[i] = (!t.lava.empty() && t.lava.sample(gx, gy) > 0.4f) ? 1 : 0;
        }
    for (int y = 0; y < c.n; ++y)
        for (int x = 0; x < c.n; ++x) {
            float hx = c.hM[c.idx(std::min(x + 1, c.n - 1), y)] - c.hM[c.idx(std::max(x - 1, 0), y)];
            float hy = c.hM[c.idx(x, std::min(y + 1, c.n - 1))] - c.hM[c.idx(x, std::max(y - 1, 0))];
            c.slope[c.idx(x, y)] = std::sqrt(hx * hx + hy * hy) / (2.f * c.cellM);
        }
    return c;
}

struct AStarCfg {
    float slopeCost, maxGrade, turnCost, waterCost, reuse = 1.f, wiggle = 0.f;
    const Noise* noise = nullptr;
};

// A* over (cell, incoming direction) so turning can be penalised -> smooth, era-appropriate alignments.
std::vector<int> astar(const Coarse& c, int sx, int sy, int gx, int gy, const AStarCfg& cfg) {
    const int n = c.n;
    const size_t states = size_t(n) * n * 9;   // dir 8 = "start"
    std::vector<float> g(states, 1e30f);
    std::vector<int> parent(states, -1);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto sid = [&](int cell, int dir) { return cell * 9 + dir; };
    int start = sid(c.idx(sx, sy), 8);
    g[start] = 0;
    auto heur = [&](int x, int y) { float dx = float(x - gx), dy = float(y - gy); return std::sqrt(dx * dx + dy * dy) * c.cellM; };
    open.push({heur(sx, sy), start});
    int goalState = -1;
    while (!open.empty()) {
        auto [f, s] = open.top();
        open.pop();
        int cell = s / 9, dir = s % 9;
        int x = cell % n, y = cell / n;
        float gs = g[s];
        if (f > gs + heur(x, y) + 1e-3f) continue;
        if (x == gx && y == gy) { goalState = s; break; }
        for (int k = 0; k < 8; ++k) {
            int nx = x + kOx[k], ny = y + kOy[k];
            if (!c.inside(nx, ny)) continue;
            int ni = c.idx(nx, ny);
            float len = kOd[k] * c.cellM;
            float grade = std::fabs(c.hM[ni] - c.hM[cell]) / len;
            float gc = grade <= cfg.maxGrade ? cfg.slopeCost * grade * grade / cfg.maxGrade
                                             : cfg.slopeCost * grade * grade / cfg.maxGrade * 8.f;
            float cost = len * (1.f + gc);
            if (c.water[ni]) cost += len * cfg.waterCost;
            if (c.lava[ni]) cost += len * cfg.waterCost * 2.f;
            if (c.road[ni] > 0.5f) cost *= cfg.reuse;
            if (cfg.wiggle > 0) cost *= 1.f + cfg.wiggle * c.wig[ni];
            if (dir < 8) {
                int turn = std::abs(k - dir);
                turn = std::min(turn, 8 - turn);
                cost += cfg.turnCost * float(turn * turn) * c.cellM * 2.f;
            }
            int ns = sid(ni, k);
            float ng = gs + cost;
            if (ng < g[ns]) {
                g[ns] = ng;
                parent[ns] = s;
                open.push({ng + heur(nx, ny), ns});
            }
        }
    }
    std::vector<int> path;
    for (int s = goalState; s >= 0; s = parent[s]) path.push_back(s / 9);
    std::reverse(path.begin(), path.end());
    return path;
}

void douglasPeucker(const std::vector<Vec2>& pts, float tol, std::vector<Vec2>& out) {
    if (pts.size() < 3) { out = pts; return; }
    std::vector<uint8_t> keep(pts.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack{{0, pts.size() - 1}};
    while (!stack.empty()) {
        auto [a, b] = stack.back();
        stack.pop_back();
        Vec2 A = pts[a], B = pts[b];
        Vec2 AB = B - A;
        float L = std::max(1e-6f, AB.length());
        float best = 0;
        size_t bi = 0;
        for (size_t i = a + 1; i < b; ++i) {
            float d = std::fabs(cross(AB, pts[i] - A)) / L;
            if (d > best) { best = d; bi = i; }
        }
        if (best > tol && bi) {
            keep[bi] = 1;
            stack.push_back({a, bi});
            stack.push_back({bi, b});
        }
    }
    out.clear();
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i]) out.push_back(pts[i]);
}

std::vector<Vec2> densify(const std::vector<Vec2>& pts, float maxSeg) {
    std::vector<Vec2> out;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        out.push_back(pts[i]);
        float L = (pts[i + 1] - pts[i]).length();
        int k = int(L / maxSeg);
        for (int j = 1; j <= k; ++j) out.push_back(pts[i] + (pts[i + 1] - pts[i]) * (float(j) / float(k + 1)));
    }
    if (!pts.empty()) out.push_back(pts.back());
    return out;
}

void relax(std::vector<Vec2>& pts, int passes, float amount, bool closed) {
    const int n = int(pts.size());
    if (n < 3) return;
    for (int p = 0; p < passes; ++p) {
        std::vector<Vec2> nx = pts;
        for (int i = 0; i < n; ++i) {
            if (!closed && (i == 0 || i == n - 1)) continue;
            Vec2 a = pts[(i - 1 + n) % n], b = pts[(i + 1) % n];
            nx[i] = pts[i] + ((a + b) * 0.5f - pts[i]) * amount;
        }
        pts.swap(nx);
    }
}

Spline styleToSpline(const NetworkStyle& st, const char* name) {
    Spline s;
    s.name = name;
    s.kind = st.kind;
    s.material = st.material;
    s.width = st.width;
    s.shoulder = st.shoulder;
    s.markings = st.markings;
    s.elevated = st.elevated;
    s.waterFilled = st.waterFilled;
    s.prop = st.prop;
    s.propSpacing = st.propSpacing;
    return s;
}

void markRoad(Coarse& c, const std::vector<int>& path) {
    for (int i : path) c.road[i] = 1.f;
}

// Converts a coarse path into a spline with era-appropriate alignment, then conforms heights.
bool pathToSpline(const Coarse& c, const std::vector<int>& path, const NetworkStyle& st, const Terrain& t, Spline& out) {
    if (path.size() < 3) return false;
    std::vector<Vec2> pts;
    for (int i : path) pts.push_back(c.toWorld(i % c.n, i / c.n));
    std::vector<Vec2> simp;
    douglasPeucker(pts, c.cellM * (0.6f + st.straightness * 3.5f), simp);
    float spacing = lerpf(35.f, 120.f, st.smoothing);
    simp = densify(simp, spacing);
    relax(simp, 2 + int(st.smoothing * 4.f), 0.5f * (1.f - st.straightness * 0.7f), false);
    for (auto& p : simp) {
        p.x = clampf(p.x, 0.f, t.worldSize);
        p.y = clampf(p.y, 0.f, t.worldSize);
        SplinePoint sp;
        sp.pos = {p.x, 0.f, p.y};
        out.points.push_back(sp);
    }
    conformSplineHeights(out, t, st.maxGrade, lerpf(30.f, 140.f, st.smoothing));
    return out.points.size() >= 2;
}

std::vector<Vec2> pickSettlements(const Coarse& c, int count, Rng& rng, const Terrain& t) {
    std::vector<std::pair<float, int>> cand;
    const int margin = c.n / 12;
    for (int y = margin; y < c.n - margin; y += 2)
        for (int x = margin; x < c.n - margin; x += 2) {
            int i = c.idx(x, y);
            if (c.water[i] || c.lava[i]) continue;
            float flat = 1.f - smoothstepf(0.03f, 0.18f, c.slope[i]);
            float hn = c.hM[i] / t.heightRange;
            float alt = 1.f - smoothstepf(0.55f, 0.9f, hn);
            // proximity to water (but not in it) is attractive for settlements
            float nearWater = 0;
            for (int k = 0; k < 8; ++k) {
                int nx = x + kOx[k] * 4, ny = y + kOy[k] * 4;
                if (c.inside(nx, ny) && c.water[c.idx(nx, ny)]) nearWater = 1.f;
            }
            float s = flat * alt * (0.7f + 0.3f * nearWater) * (0.75f + 0.25f * rng.uniform());
            if (s > 0.15f) cand.push_back({s, i});
        }
    std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Vec2> out;
    const float minD = t.worldSize / std::sqrt(float(std::max(1, count))) * 0.55f;
    for (auto& [s, i] : cand) {
        Vec2 p = c.toWorld(i % c.n, i / c.n);
        bool ok = true;
        for (auto& o : out) ok = ok && (o - p).length() > minD;
        if (ok) out.push_back(p);
        if (int(out.size()) >= count) break;
    }
    return out;
}

Vec2 edgePoint(const Coarse& c, int side, Rng& rng, bool* dry = nullptr) {
    // pick the flattest of a few random spots along a map edge
    Vec2 best;
    float bestS = 1e9f;
    for (int k = 0; k < 12; ++k) {
        float u = rng.range(0.15f, 0.85f);
        int x = 0, y = 0;
        switch (side & 3) {
            case 0: x = int(u * (c.n - 1)); y = 0; break;
            case 1: x = c.n - 1; y = int(u * (c.n - 1)); break;
            case 2: x = int(u * (c.n - 1)); y = c.n - 1; break;
            default: x = 0; y = int(u * (c.n - 1)); break;
        }
        int i = c.idx(x, y);
        float s = c.slope[i] + (c.water[i] ? 5.f : 0.f);
        if (s < bestS) { bestS = s; best = c.toWorld(x, y); }
    }
    if (dry) *dry = bestS < 5.f;
    return best;
}

struct Edge { int a, b; float d; };

std::vector<Edge> spanningEdges(const std::vector<Vec2>& nodes, float extraLoops, Rng& rng) {
    const int n = int(nodes.size());
    std::vector<Edge> all, out;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) all.push_back({i, j, (nodes[i] - nodes[j]).length()});
    std::sort(all.begin(), all.end(), [](const Edge& a, const Edge& b) { return a.d < b.d; });
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
    std::vector<Edge> rest;
    for (auto& e : all) {
        int ra = find(e.a), rb = find(e.b);
        if (ra != rb) { parent[ra] = rb; out.push_back(e); }
        else rest.push_back(e);
    }
    // relative-neighbourhood style loops: short non-tree edges with a probability
    float longest = 0;
    for (auto& e : out) longest = std::max(longest, e.d);
    for (auto& e : rest)
        if (e.d < longest * 1.15f && rng.chance(0.3f * extraLoops)) out.push_back(e);
    return out;
}

float splineKm(const Spline& s) { return s.length() / 1000.f; }

// ------------------------------------------------------------------ racetrack
bool buildRacetrack(const Terrain& t, const Coarse& c, const RaceStyle& rs, Rng& rng, Spline& out) {
    float targetLen = std::min(rs.lengthKm * 1000.f, t.worldSize * 2.4f);
    float R = targetLen / kTau * (rs.oval ? 0.75f : 0.95f);
    R = std::min(R, t.worldSize * 0.32f);
    // choose the flattest dry site that fits
    Vec2 bestC{t.worldSize * 0.5f, t.worldSize * 0.5f};
    float bestScore = 1e30f;
    const int step = std::max(2, c.n / 24);
    for (int gy = 0; gy < c.n; gy += step)
        for (int gx = 0; gx < c.n; gx += step) {
            Vec2 cc = c.toWorld(gx, gy);
            if (cc.x - R < 20 || cc.y - R < 20 || cc.x + R > t.worldSize - 20 || cc.y + R > t.worldSize - 20) continue;
            double sum = 0, sum2 = 0;
            float penalty = 0;
            int cnt = 0;
            for (float rr : {R * 0.4f, R * 0.75f, R}) {
                for (int a = 0; a < 20; ++a) {
                    float ang = kTau * float(a) / 20.f;
                    int x, y;
                    c.fromWorld(cc + Vec2(std::cos(ang), std::sin(ang)) * rr, x, y);
                    int i = c.idx(x, y);
                    sum += c.hM[i];
                    sum2 += double(c.hM[i]) * c.hM[i];
                    penalty += (c.water[i] || c.lava[i]) ? 1000.f : 0.f;
                    ++cnt;
                }
            }
            double mean = sum / cnt;
            float var = float(sum2 / cnt - mean * mean);
            float sc = var + penalty + rng.uniform() * 50.f;
            if (sc < bestScore) { bestScore = sc; bestC = cc; }
        }

    std::vector<Vec2> loop;
    float rot = rng.range(0.f, kPi);
    Vec2 ax{std::cos(rot), std::sin(rot)}, ay = ax.perp();
    if (rs.oval) {
        float r0 = R * 0.38f;
        float straight = std::max(R * 0.4f, (targetLen - kTau * r0) * 0.5f);
        straight = std::min(straight, R * 1.6f);
        const int nArc = 14;
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.f : -1.f;
            for (int i = 0; i <= nArc; ++i) {
                float a = -kPi * 0.5f + kPi * float(i) / float(nArc);
                Vec2 local{sgn * (straight * 0.5f + r0 * std::cos(a)), sgn * r0 * std::sin(a)};
                loop.push_back(bestC + ax * local.x + ay * local.y);
            }
        }
    } else {
        const int N = 10 + rng.irange(0, 4);
        std::vector<float> angles;
        for (int i = 0; i < N; ++i) angles.push_back(kTau * (float(i) + rng.range(-0.3f, 0.3f)) / float(N));
        std::sort(angles.begin(), angles.end());
        std::vector<Vec2> poly;
        for (float a : angles) {
            float rr = R * rng.range(0.5f, 1.f);
            poly.push_back(bestC + ax * (std::cos(a) * rr) + ay * (std::sin(a) * rr * 0.75f));
        }
        // midpoint displacement -> hairpins & chicanes (keeps the polygon star-shaped)
        std::vector<Vec2> disp;
        for (size_t i = 0; i < poly.size(); ++i) {
            Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            disp.push_back(a);
            Vec2 mid = (a + b) * 0.5f;
            Vec2 toC = (bestC - mid).normalized();
            float amt = rng.range(-0.18f, 0.32f) * R;
            Vec2 m2 = mid + toC * amt;
            if ((m2 - bestC).length() > R * 0.25f) disp.push_back(m2);
        }
        // Chaikin smoothing
        for (int it = 0; it < 3; ++it) {
            std::vector<Vec2> nx;
            for (size_t i = 0; i < disp.size(); ++i) {
                Vec2 a = disp[i], b = disp[(i + 1) % disp.size()];
                nx.push_back(a * 0.75f + b * 0.25f);
                nx.push_back(a * 0.25f + b * 0.75f);
            }
            disp.swap(nx);
        }
        relax(disp, 4, 0.35f, true);
        loop = disp;
    }
    // scale to target length
    float L = 0;
    for (size_t i = 0; i < loop.size(); ++i) L += (loop[(i + 1) % loop.size()] - loop[i]).length();
    float k = clampf(targetLen / std::max(1.f, L), 0.6f, 1.4f);
    for (auto& p : loop) {
        p = bestC + (p - bestC) * k;
        p.x = clampf(p.x, 10.f, t.worldSize - 10.f);
        p.y = clampf(p.y, 10.f, t.worldSize - 10.f);
    }
    // resample to evenly spaced control points
    std::vector<Vec2> even;
    {
        float total = 0;
        for (size_t i = 0; i < loop.size(); ++i) total += (loop[(i + 1) % loop.size()] - loop[i]).length();
        int count = std::max(16, int(total / 30.f));
        float stepLen = total / float(count);
        float carry = 0;
        even.push_back(loop[0]);
        for (size_t i = 0; i < loop.size() && int(even.size()) < count; ++i) {
            Vec2 a = loop[i], b = loop[(i + 1) % loop.size()];
            float seg = (b - a).length();
            float pos = stepLen - carry;
            while (pos < seg && int(even.size()) < count) {
                even.push_back(a + (b - a) * (pos / seg));
                pos += stepLen;
            }
            carry = seg - (pos - stepLen);
        }
    }
    // heights: smoothed terrain around the loop, then circular grade limit
    const int m = int(even.size());
    std::vector<float> y(m);
    for (int i = 0; i < m; ++i) y[i] = t.heightAtWorld(even[i].x, even[i].y);
    int win = std::max(1, int(lerpf(float(m) * 0.5f, 2.f, rs.elevationFollow)));
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> ys(m);
        for (int i = 0; i < m; ++i) {
            double acc = 0;
            for (int j = -win; j <= win; ++j) acc += y[(i + j + m * 4) % m];
            ys[i] = float(acc / double(2 * win + 1));
        }
        y.swap(ys);
    }
    out.points.clear();
    for (int i = 0; i < m; ++i) {
        SplinePoint sp;
        sp.pos = {even[i].x, y[i], even[i].y};
        out.points.push_back(sp);
    }
    out.closed = true;
    return m >= 8;
}

// steepest-descent trace (lava channels)
std::vector<int> descend(const Coarse& c, int sx, int sy, int maxLen) {
    std::vector<int> path;
    int x = sx, y = sy;
    std::vector<uint8_t> seen(size_t(c.n) * c.n, 0);
    for (int i = 0; i < maxLen; ++i) {
        int ci = c.idx(x, y);
        path.push_back(ci);
        seen[ci] = 1;
        float best = 1e30f;
        int bx = -1, by = -1;
        for (int k = 0; k < 8; ++k) {
            int nx = x + kOx[k], ny = y + kOy[k];
            if (!c.inside(nx, ny) || seen[c.idx(nx, ny)]) continue;
            float h = c.hM[c.idx(nx, ny)];
            if (h < best) { best = h; bx = nx; by = ny; }
        }
        if (bx < 0 || c.water[c.idx(bx, by)]) break;
        x = bx;
        y = by;
        if (x == 0 || y == 0 || x == c.n - 1 || y == c.n - 1) break;
    }
    return path;
}

}  // namespace

Spline makeSplineForKind(SplineKind kind, const EraProfile& era) {
    Spline s;
    switch (kind) {
        case SplineKind::Road: s = styleToSpline(era.road.available ? era.road : NetworkStyle{}, "Road"); s.kind = SplineKind::Road; break;
        case SplineKind::Rail: s = styleToSpline(era.rail.available ? era.rail : NetworkStyle{}, "Railway"); break;
        case SplineKind::Path: s = styleToSpline(era.path, "Path"); s.kind = SplineKind::Path; break;
        case SplineKind::Racetrack:
            s.name = era.race.label;
            s.kind = SplineKind::Racetrack;
            s.material = era.race.material;
            s.width = era.race.width;
            s.shoulder = era.race.width * 0.8f;
            s.kerbs = era.race.kerbs;
            s.maxBank = era.race.banking;
            s.closed = true;
            break;
        case SplineKind::Canal:
            s.name = "Canal"; s.kind = kind; s.material = MaterialKind::Mud; s.width = 8.f; s.shoulder = 4.f; s.waterFilled = true;
            break;
        case SplineKind::Aqueduct:
            s.name = "Aqueduct"; s.kind = kind; s.material = MaterialKind::RomanStone; s.width = 3.f; s.shoulder = 1.f; s.elevated = true;
            s.prop = FoliageKind::Column; s.propSpacing = 18.f;
            break;
        case SplineKind::Maglev:
            s.name = "Maglev"; s.kind = kind; s.material = MaterialKind::Concrete; s.width = 6.f; s.shoulder = 1.f; s.elevated = true;
            s.prop = FoliageKind::Column; s.propSpacing = 60.f;
            break;
        case SplineKind::LavaChannel:
            s.name = "Lava channel"; s.kind = kind; s.material = MaterialKind::Lava; s.width = 5.f; s.shoulder = 5.f;
            break;
        default:
            s.name = "Custom road"; s.kind = SplineKind::Custom; s.material = era.road.material; s.width = 6.f; s.shoulder = 4.f;
            break;
    }
    if (s.width <= 0) s.width = 6.f;
    return s;
}

void buildNetworks(const Terrain& t, const EraProfile& era, const NetworkRequest& req, std::vector<Spline>& out,
                   std::vector<std::string>& log, Progress* progress, float p0, float p1) {
    Coarse c = buildCoarse(t);
    Rng rng(req.seed);
    Noise wig(req.seed ^ 0x9A7Bull);
    char buf[256];
    auto stage = [&](const char* s, float f) { if (progress) progress->set(s, p0 + (p1 - p0) * f); };

    const float areaScale = t.worldSize / 4033.f;
    int nSettle = std::clamp(int(std::lround((3.f + 5.f * req.roadDensity) * std::sqrt(std::max(0.2f, areaScale)))), 2, 40);
    std::vector<Vec2> settlements = pickSettlements(c, nSettle, rng, t);

    // ---------------- roads (or the era's equivalent)
    if (req.roads && era.road.available && settlements.size() >= 1) {
        stage("Routing road network", 0.05f);
        const NetworkStyle& st = era.road;
        if (st.kind == SplineKind::LavaChannel) {
            // primordial: molten fissures pour downhill from the highest ground
            std::vector<std::pair<float, int>> peaks;
            for (int y = 2; y < c.n - 2; y += 3)
                for (int x = 2; x < c.n - 2; x += 3) peaks.push_back({c.hM[c.idx(x, y)] + rng.uniform() * 30.f, c.idx(x, y)});
            std::sort(peaks.begin(), peaks.end(), [](auto& a, auto& b) { return a.first > b.first; });
            int made = 0;
            for (size_t i = 0; i < peaks.size() && made < 3 + int(req.roadDensity * 4); i += 7) {
                auto path = descend(c, peaks[i].second % c.n, peaks[i].second / c.n, c.n * 2);
                Spline s = styleToSpline(st, "Molten fissure");
                if (pathToSpline(c, path, st, t, s)) { out.push_back(std::move(s)); ++made; }
            }
            std::snprintf(buf, sizeof(buf), "%s: %d channels", st.label.c_str(), made);
            log.push_back(buf);
        } else {
            std::vector<Vec2> nodes = settlements;
            int exits = 1 + int(rng.uniform() * 2.f + req.roadDensity * 0.5f);
            int firstSide = rng.irange(0, 3);
            for (int e = 0; e < exits; ++e) {
                bool dry = false;
                Vec2 p = edgePoint(c, firstSide + e * 2 + (e > 1 ? 1 : 0), rng, &dry);
                if (dry) nodes.push_back(p);   // islands: no roads running off into the sea
            }
            auto edges = spanningEdges(nodes, req.roadDensity, rng);
            AStarCfg cfg{st.slopeCost, st.maxGrade, st.turnCost, st.waterCost, 0.35f, st.straightness < 0.3f ? 0.6f : 0.15f, &wig};
            int links = 0;
            float km = 0;
            for (size_t ei = 0; ei < edges.size(); ++ei) {
                int ax, ay, bx, by;
                c.fromWorld(nodes[edges[ei].a], ax, ay);
                c.fromWorld(nodes[edges[ei].b], bx, by);
                auto path = astar(c, ax, ay, bx, by, cfg);
                markRoad(c, path);
                Spline s = styleToSpline(st, st.label.c_str());
                if (pathToSpline(c, path, st, t, s)) {
                    km += splineKm(s);
                    out.push_back(std::move(s));
                    ++links;
                }
                stage("Routing road network", 0.05f + 0.35f * float(ei + 1) / float(edges.size()));
            }
            std::snprintf(buf, sizeof(buf), "%s: %zu settlements, %d links, %.1f km", st.label.c_str(), settlements.size(), links, km);
            log.push_back(buf);
        }
    } else if (req.roads && !era.road.available) {
        log.push_back("Roads: " + era.road.label);
    }

    // ---------------- railway (or era equivalent: canals, aqueducts, maglev)
    if (req.rail) {
        const NetworkStyle& st = era.rail;
        if (!st.available) {
            log.push_back("Railway: " + st.label);
        } else {
            stage("Surveying railway alignment", 0.45f);
            AStarCfg cfg{st.slopeCost, st.maxGrade, st.turnCost, st.waterCost, 0.6f, 0.f, nullptr};
            std::vector<Vec2> chain;
            if (st.kind == SplineKind::Aqueduct) {
                // spring on high ground -> lowest-lying settlement
                int bestI = 0;
                float bestH = -1e30f;
                for (int y = c.n / 8; y < c.n * 7 / 8; y += 2)
                    for (int x = c.n / 8; x < c.n * 7 / 8; x += 2) {
                        float h = c.hM[c.idx(x, y)] - c.slope[c.idx(x, y)] * 200.f;
                        if (h > bestH) { bestH = h; bestI = c.idx(x, y); }
                    }
                Vec2 spring = c.toWorld(bestI % c.n, bestI / c.n);
                Vec2 town = settlements.empty() ? Vec2(t.worldSize * 0.5f, t.worldSize * 0.5f) : settlements[0];
                for (auto& sp : settlements)
                    if (t.heightAtWorld(sp.x, sp.y) < t.heightAtWorld(town.x, town.y)) town = sp;
                chain = {spring, town};
            } else if (st.kind == SplineKind::Canal) {
                // connect the two lowest settlements (or settlement and a map edge)
                std::vector<Vec2> s2 = settlements;
                std::sort(s2.begin(), s2.end(), [&](Vec2 a, Vec2 b) { return t.heightAtWorld(a.x, a.y) < t.heightAtWorld(b.x, b.y); });
                if (s2.size() >= 2) chain = {s2[0], s2[1]};
                if (s2.size() >= 3) chain.push_back(s2[2]);
                if (chain.size() < 2) chain = {edgePoint(c, 0, rng), edgePoint(c, 2, rng)};
            } else {
                // trunk line from edge to opposite edge via the settlements nearest the line
                int side = rng.irange(0, 3);
                Vec2 a = edgePoint(c, side, rng), b = edgePoint(c, side + 2, rng);
                chain.push_back(a);
                std::vector<Vec2> via = settlements;
                Vec2 ab = (b - a).normalized();
                std::sort(via.begin(), via.end(), [&](Vec2 p, Vec2 q) { return std::fabs(cross(ab, p - a)) < std::fabs(cross(ab, q - a)); });
                if (via.size() > 2) via.resize(2);
                std::sort(via.begin(), via.end(), [&](Vec2 p, Vec2 q) { return dot(p - a, ab) < dot(q - a, ab); });
                for (auto& v : via) chain.push_back(v);
                chain.push_back(b);
            }
            std::vector<int> full;
            for (size_t i = 0; i + 1 < chain.size(); ++i) {
                int ax, ay, bx, by;
                c.fromWorld(chain[i], ax, ay);
                c.fromWorld(chain[i + 1], bx, by);
                auto seg = astar(c, ax, ay, bx, by, cfg);
                if (!full.empty() && !seg.empty()) seg.erase(seg.begin());
                full.insert(full.end(), seg.begin(), seg.end());
            }
            Spline s = styleToSpline(st, st.label.c_str());
            if (pathToSpline(c, full, st, t, s)) {
                std::snprintf(buf, sizeof(buf), "%s: %.1f km", st.label.c_str(), splineKm(s));
                log.push_back(buf);
                out.push_back(std::move(s));
            }
        }
    }

    // ---------------- small paths / trails
    if (req.paths && era.path.available && !settlements.empty()) {
        stage("Wandering trails", 0.7f);
        const NetworkStyle& st = era.path;
        // points of interest: local peaks and shorelines
        std::vector<Vec2> pois;
        const int r = 6;
        for (int y = r; y < c.n - r; y += 3)
            for (int x = r; x < c.n - r; x += 3) {
                int i = c.idx(x, y);
                if (c.water[i]) continue;
                bool peak = true, shore = false;
                for (int k = 0; k < 8 && peak; ++k) {
                    int nx = x + kOx[k] * r, ny = y + kOy[k] * r;
                    if (c.hM[c.idx(nx, ny)] >= c.hM[i]) peak = false;
                }
                for (int k = 0; k < 8; ++k)
                    if (c.water[c.idx(x + kOx[k] * 2, y + kOy[k] * 2)]) shore = true;
                if (peak || (shore && rng.chance(0.08f))) pois.push_back(c.toWorld(x, y));
            }
        AStarCfg cfg{st.slopeCost, st.maxGrade, st.turnCost, st.waterCost * 3.f, 0.8f, 0.9f, &wig};
        int made = 0;
        float km = 0;
        int perTown = std::max(1, int(std::lround(1.5f * req.pathDensity)));
        float maxReach = t.worldSize * 0.35f;
        for (size_t si = 0; si < settlements.size(); ++si) {
            Vec2 town = settlements[si];
            std::vector<Vec2> near = pois;
            std::sort(near.begin(), near.end(), [&](Vec2 a, Vec2 b) { return (a - town).length() < (b - town).length(); });
            int used = 0;
            for (auto& p : near) {
                if (used >= perTown) break;
                float d = (p - town).length();
                if (d < 150.f || d > maxReach) continue;
                int ax, ay, bx, by;
                c.fromWorld(town, ax, ay);
                c.fromWorld(p, bx, by);
                auto path = astar(c, ax, ay, bx, by, cfg);
                Spline s = styleToSpline(st, st.label.c_str());
                if (pathToSpline(c, path, st, t, s)) {
                    km += splineKm(s);
                    out.push_back(std::move(s));
                    ++made;
                    ++used;
                }
            }
            stage("Wandering trails", 0.7f + 0.15f * float(si + 1) / float(settlements.size()));
        }
        std::snprintf(buf, sizeof(buf), "%s: %d trails, %.1f km", st.label.c_str(), made, km);
        log.push_back(buf);
    }

    // ---------------- racetrack
    if (req.racetrack) {
        stage("Designing racetrack", 0.9f);
        Spline s = makeSplineForKind(SplineKind::Racetrack, era);
        if (buildRacetrack(t, c, era.race, rng, s)) {
            std::snprintf(buf, sizeof(buf), "%s: %.2f km lap%s", era.race.label.c_str(), splineKm(s), era.race.banking > 0.2f ? ", banked" : "");
            log.push_back(buf);
            out.push_back(std::move(s));
        }
    }
    stage("Networks done", 1.f);
}

}  // namespace zl
