#include "core/Generator.h"

#include "core/Erosion.h"
#include "core/Hydrology.h"
#include "core/Networks.h"
#include "core/Noise.h"

#include <chrono>
#include <cstdio>

namespace zl {

namespace {

struct Polyline {
    std::vector<Vec2> pts;
    float width, height;
};

Polyline bezierPolyline(const RangeFeature& f, int segs) {
    Polyline p;
    p.width = f.width;
    p.height = f.height;
    for (int i = 0; i <= segs; ++i) {
        float t = float(i) / float(segs);
        float a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, c = t * t;
        p.pts.push_back(f.p0 * a + f.p1 * b + f.p2 * c);
    }
    return p;
}

// distance to polyline + normalized arc parameter of the closest point
void polyDistance(const Polyline& pl, Vec2 q, float& dist, float& param) {
    dist = 1e9f;
    param = 0.f;
    const int n = int(pl.pts.size()) - 1;
    for (int i = 0; i < n; ++i) {
        Vec2 a = pl.pts[i], b = pl.pts[i + 1];
        Vec2 ab = b - a;
        float t = clamp01(dot(q - a, ab) / std::max(1e-9f, dot(ab, ab)));
        float d = (q - (a + ab * t)).length();
        if (d < dist) {
            dist = d;
            param = (float(i) + t) / float(n);
        }
    }
}

float mixNoise(const Noise& n, const TerrainRecipe& r, float x, float y, int octaves) {
    if (octaves <= 0) return 0.f;
    float wsum = r.wFbm + r.wRidged + r.wBillow + r.wEroded;
    if (wsum < 1e-4f) return n.fbm(x, y, octaves, r.lacunarity, r.gain);
    float v = 0;
    if (r.wFbm > 0) v += r.wFbm * n.fbm(x, y, octaves, r.lacunarity, r.gain);
    if (r.wRidged > 0) v += r.wRidged * (n.ridged(x, y, octaves, r.lacunarity, r.gain) * 2.f - 1.f);
    if (r.wBillow > 0) v += r.wBillow * (n.billow(x, y, octaves, r.lacunarity, r.gain) * 2.f - 1.f);
    if (r.wEroded > 0) v += r.wEroded * n.erodedFbm(x, y, octaves, r.lacunarity, r.gain, 1.5f);
    return v / wsum;
}

float sumGain(float gain, int from, int to) {
    float s = 0, a = std::pow(gain, float(from));
    for (int i = from; i < to; ++i) { s += a; a *= gain; }
    return s;
}

float smoothQuantize(float h, float levels, float edge) {
    float x = h * levels;
    float f = x - std::floor(x);
    return (std::floor(x) + smoothstepf(1.f - edge, 1.f, f)) / levels;
}

}  // namespace

void synthesizeHeight(const Genome& g, int res, Grid& out, Grid* lava, bool preview) {
    const TerrainRecipe& r = g.r;
    out.resize(res, res, 0.f);
    if (lava) lava->resize(res, res, 0.f);
    const Noise macro(g.macroSeed), detail(g.detailSeed), aux(g.macroSeed ^ 0x77AA55ull);

    // Never synthesize octaves finer than ~3 px: they alias into streaks.
    auto octBudget = [&](float freq) { return std::max(1, int(std::floor(std::log2(float(res) / (freq * 3.f))))); };
    const int totalOct = std::min(preview ? std::min(r.octaves, 6) : r.octaves, octBudget(r.baseFreq));
    const int rangeOct = std::min(preview ? 4 : 6, octBudget(r.baseFreq * 4.f));
    const int spikeOct = std::min(preview ? 4 : 6, octBudget(r.baseFreq * 1.7f));
    const int macroOct = std::min(3, totalOct);
    const int detailOct = totalOct - macroOct;
    const float am = sumGain(r.gain, 0, macroOct), ad = sumGain(r.gain, macroOct, totalOct);
    const float df = std::pow(r.lacunarity, float(macroOct));
    const float inv = 1.f / float(res - 1);

    std::vector<Polyline> ranges;
    for (const auto& f : g.ranges) ranges.push_back(bezierPolyline(f, 12));

    const float duneFreq = 14.f / std::max(0.2f, r.duneScale) * std::sqrt(std::max(0.3f, g.featureScale));
    const float karstFreq = r.baseFreq * 3.2f;
    const float crystalFreq = r.baseFreq * 4.f;
    const Vec2 wind = g.windDir.normalized();

    parallelFor(0, res, [&](int y) {
        for (int x = 0; x < res; ++x) {
            const float u = float(x) * inv, v = float(y) * inv;
            Vec2 p{u * r.baseFreq, v * r.baseFreq};
            Vec2 q = r.warp > 0 ? macro.warp(p.x, p.y, r.warp, 0.6f, 3) : p;
            float n = mixNoise(macro, r, q.x, q.y, macroOct) * am;
            if (detailOct > 0) n += mixNoise(detail, r, q.x * df + 31.3f, q.y * df - 17.9f, detailOct) * ad;
            n /= (am + ad);
            float h = 0.5f + n * 0.5f * r.relief;

            const Vec2 uq{q.x / r.baseFreq, q.y / r.baseFreq};   // warped uv

            // tectonic ranges
            for (const auto& pl : ranges) {
                float d, t;
                polyDistance(pl, uq, d, t);
                float k = d / pl.width;
                if (k > 3.f) continue;
                float bump = std::exp(-k * k * 2.2f);
                float taper = smoothstepf(0.f, 0.18f, t) * smoothstepf(1.f, 0.82f, t);
                float rn = macro.ridged(uq.x * 9.f + 3.7f, uq.y * 9.f - 1.3f, rangeOct);
                h += pl.height * bump * taper * (0.45f + 0.55f * rn);
            }
            // basins / lake beds
            for (const auto& b : g.basins) {
                float d = (uq - b.pos).length() / b.radius;
                if (d < 2.5f) h -= b.amount * std::exp(-d * d * 1.6f);
            }
            // islands / continent falloff
            if (r.islandMask > 0) {
                float d = (Vec2(u, v) - g.islandCenter).length();
                float rr = 0.40f + 0.14f * macro.fbm(u * 2.3f + 9.1f, v * 2.3f - 4.2f, 4);
                float m = smoothstepf(rr + 0.12f, rr - 0.22f, d);
                h = lerpf(h, h * (0.35f + 0.65f * m) - (1.f - m) * 0.25f, r.islandMask);
            }
            // volcanoes
            for (size_t i = 0; i < g.volcanoes.size(); ++i) {
                const auto& vo = g.volcanoes[i];
                Vec2 dv = Vec2(u, v) - vo.pos;
                float d = dv.length() / vo.radius;
                if (d > 1.6f) continue;
                float jag = 0.85f + 0.15f * macro.fbm(u * 18.f, v * 18.f, 3);
                float cone = vo.amount * std::pow(std::max(0.f, 1.f - d * jag), 2.1f);
                float rc = 0.13f;
                float caldera = vo.amount * 0.35f * smoothstepf(rc * 1.7f, rc * 0.7f, d);
                h += cone - caldera;
                if (lava && r.lavaFill > 0) {
                    float pool = smoothstepf(rc * 1.0f, rc * 0.5f, d);
                    float ang = std::atan2(dv.y, dv.x);
                    float streak = std::pow(clamp01(1.f - std::fabs(std::sin(ang * 3.f + 2.f * macro.fbm(u * 6.f, v * 6.f, 3)))), 24.f);
                    float flow = streak * smoothstepf(0.95f, 0.2f, d) * smoothstepf(rc * 0.8f, rc * 1.6f, d) * r.lavaFill;
                    lava->at(x, y) = std::max(lava->at(x, y), std::max(pool * r.lavaFill, flow));
                    h -= flow * 0.01f;
                }
            }
            // impact / blast craters
            for (const auto& c : g.craters) {
                float d = (Vec2(u, v) - c.pos).length() / c.radius;
                if (d > 1.8f) continue;
                float depth = c.amount * c.radius * 1.4f;
                float bowl = d < 1.f ? (d * d - 1.f) * depth : 0.f;
                float rim = 0.4f * depth * std::exp(-((d - 1.f) / 0.22f) * ((d - 1.f) / 0.22f));
                float peak = c.radius > 0.07f ? 0.3f * depth * std::exp(-(d / 0.16f) * (d / 0.16f)) : 0.f;
                h += bowl + rim + peak;
            }
            // open-pit quarries (stepped benches)
            for (const auto& qy : g.quarries) {
                float d = (Vec2(u, v) - qy.pos).length() / qy.radius;
                if (d < 1.f) h -= qy.amount * smoothQuantize(1.f - d, 5.f, 0.25f);
            }
            // spires
            if (r.spikes > 0) {
                float s = macro.ridged(q.x * 1.7f + 11.f, q.y * 1.7f - 5.f, spikeOct);
                h += r.spikes * 0.9f * s * s * s * s;
            }
            if (r.karst > 0) {
                CellResult c = aux.cellular(u * karstFreq, v * karstFreq, 0.9f);
                float hh = float(c.id & 1023) / 1023.f;
                if (hh > 0.3f) {
                    float tower = smoothstepf(0.44f, 0.26f, c.f1 + 0.05f * detail.fbm(u * 40.f, v * 40.f, 2));
                    h += r.karst * 0.42f * tower * (0.45f + 0.55f * hh);
                }
            }
            if (r.crystals > 0) {
                CellResult c = aux.cellular(u * crystalFreq + 50.f, v * crystalFreq, 1.f);
                float hh = float((c.id >> 10) & 1023) / 1023.f;
                if (hh > 0.55f) h += r.crystals * 0.5f * hh * std::pow(std::max(0.f, 1.f - c.f1 / 0.38f), 2.4f);
            }
            if (r.dunes > 0) {
                // two crossing dune systems with drifting wavelength -> transverse ridges, barchans and star-like knots
                float wl = 1.f + 0.35f * macro.fbm(u * 1.5f + 2.f, v * 1.5f, 2);
                auto dune = [&](Vec2 dir, float freq, float ph0) {
                    float along = (u * dir.x + v * dir.y) * freq * wl + 0.9f * detail.fbm(u * 3.f + ph0, v * 3.f, 3) +
                                  0.35f * std::sin((u * dir.y - v * dir.x) * freq * 0.7f + ph0);
                    float ph = along - std::floor(along);
                    float prof = ph < 0.78f ? ph / 0.78f : (1.f - ph) / 0.22f;   // gentle windward, steep slip face
                    return prof * prof * (3.f - 2.f * prof);
                };
                Vec2 wind2{wind.x * 0.5f - wind.y * 0.866f, wind.x * 0.866f + wind.y * 0.5f};
                float primary = dune(wind, duneFreq, 0.f);
                float secondary = dune(wind2, duneFreq * 0.6f, 4.f);
                float m = 0.55f + 0.45f * macro.fbm(u * 2.f + 7.f, v * 2.f, 3);
                float cross = 0.5f + 0.5f * macro.fbm(u * 1.2f - 3.f, v * 1.2f + 8.f, 2);
                float ripple = 0.04f * std::sin((u * wind.x + v * wind.y) * duneFreq * 9.f + detail.fbm(u * 12.f, v * 12.f, 2) * 2.f);
                h += r.dunes * 0.2f * (std::max(primary, secondary * cross) + ripple) * m;
            }
            if (r.canyons > 0 || r.glacial > 0) {
                float rn = std::fabs(macro.fbm(q.x * 0.55f + 13.f, q.y * 0.55f - 7.f, 4));
                if (r.canyons > 0) {
                    float canyon = 1.f - smoothstepf(0.f, 0.07f, rn);
                    h -= r.canyons * 0.38f * canyon;
                }
                if (r.glacial > 0) {
                    float gv = 1.f - smoothstepf(0.f, 0.17f, rn);
                    h -= r.glacial * 0.42f * std::sqrt(std::max(0.f, gv));
                }
            }
            out.at(x, y) = h;
        }
    });

    out.normalize(0.f, 1.f);

    // Landscape evolution: let rivers, not noise, decide where the valleys are. The macro shape becomes a
    // tectonic uplift map; relief then emerges from uplift vs. stream-power incision, giving dendritic
    // drainage, graded valleys and sharp ridgelines like real ranges. Noise is kept only as fine detail.
    const float evoAmt = clampf(r.rivers * 1.2f, 0.f, 0.9f) * (1.f - clamp01(r.dunes)) * (1.f - 0.8f * clamp01(r.karst));
    if (evoAmt > 0.05f) {
        const int lr = std::min(res, preview ? 112 : 256);
        const int pad = lr / 8, pr = lr + 2 * pad;
        Grid coarse = out.resampled(lr, lr);
        // mirror-padded working domain: rivers can leave the map naturally and the boundary walls get cropped
        auto mirror = [&](int i) { i -= pad; if (i < 0) i = -i; if (i >= lr) i = 2 * lr - 2 - i; return std::clamp(i, 0, lr - 1); };
        Grid padded(pr, pr);
        for (int y = 0; y < pr; ++y)
            for (int x = 0; x < pr; ++x) padded.at(x, y) = coarse.at(mirror(x), mirror(y));
        Grid uplift = padded;
        uplift.blur(std::max(1, lr / 48), 2);
        uplift.normalize(0.f, 1.f);
        for (auto& v : uplift.vec()) v = 0.12f + 0.88f * std::pow(v, 1.2f);
        Grid evoP = padded;
        for (auto& v : evoP.vec()) v *= 0.03f;
        Grid fixedMask(pr, pr, 0.f);
        if (r.seaLevel >= 0.f)
            for (size_t i = 0; i < padded.size(); ++i) fixedMask.vec()[i] = padded.vec()[i] < r.seaLevel ? 1.f : 0.f;
        const float talusN = std::max(0.25f, r.talus) * (4033.f / float(lr - 1)) / std::max(50.f, r.heightRange) * 1.4f;
        upliftStreamPower(evoP, uplift, preview ? 50 : 110, 2.5f, talusN, &fixedMask);
        Grid evo(lr, lr);
        for (int y = 0; y < lr; ++y)
            for (int x = 0; x < lr; ++x) evo.at(x, y) = evoP.at(x + pad, y + pad);
        evo.blur(1, 1);
        evo.normalize(0.f, 1.f);
        if (r.seaLevel >= 0.f)
            for (size_t i = 0; i < evo.size(); ++i)
                evo.vec()[i] = coarse.vec()[i] < r.seaLevel ? coarse.vec()[i] : r.seaLevel + evo.vec()[i] * (1.f - r.seaLevel);
        Grid coarseUp = coarse.resampled(res, res);
        Grid target = coarse;
        for (size_t i = 0; i < target.size(); ++i) target.vec()[i] = lerpf(coarse.vec()[i], evo.vec()[i], evoAmt);
        Grid targetUp = target.resampled(res, res);
        const float detailKeep = 1.f - 0.55f * evoAmt;
        for (size_t i = 0; i < out.size(); ++i) out.vec()[i] = targetUp.vec()[i] + (out.vec()[i] - coarseUp.vec()[i]) * detailKeep;
        out.normalize(0.f, 1.f);
    }

    // shaping on normalized heights
    if (r.mesas > 0 || r.terraces > 0 || r.plateau > 0 || r.flatten > 0) {
        const int mesaLevels = 3 + int(r.mesas * 2.f);
        parallelFor(0, res, [&](int y) {
            for (int x = 0; x < res; ++x) {
                float h = out.at(x, y);
                if (r.flatten > 0) h = std::pow(std::max(0.f, h), 1.f + r.flatten * 2.5f);
                if (r.mesas > 0) h = lerpf(h, smoothQuantize(h, float(mesaLevels), 0.18f), r.mesas);
                if (r.terraces > 0 && r.terraceStrength > 0) h = lerpf(h, smoothQuantize(h, r.terraces, 0.45f), r.terraceStrength);
                if (r.plateau > 0 && h > r.plateau) h = r.plateau + (h - r.plateau) * 0.12f;
                out.at(x, y) = h;
            }
        });
        out.normalize(0.f, 1.f);
    }

    if (preview) {
        float cell = 1.f / float(res - 1);
        thermalErosion(out, 6, std::max(0.05f, r.talus) * cell * 4033.f / std::max(50.f, r.heightRange), 0.4f);
        out.normalize(0.f, 1.f);
    }
}

Atmosphere blendAtmosphere(const Environment& env, const EraProfile& era) {
    auto mix = [&](Color3 a, Color3 b) {
        float t = era.atmosphereBlend;
        return Color3{lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)};
    };
    Atmosphere a = env.atmosphere;
    a.skyTop = mix(a.skyTop, era.atmosphere.skyTop);
    a.skyHorizon = mix(a.skyHorizon, era.atmosphere.skyHorizon);
    a.sun = mix(a.sun, era.atmosphere.sun);
    a.water = mix(a.water, era.atmosphere.water);
    a.fogDensity = lerpf(a.fogDensity, era.atmosphere.fogDensity, era.atmosphereBlend);
    return a;
}

static void applyFarmTerraces(Terrain& t, float amount, uint64_t seed) {
    if (amount <= 0) return;
    Noise n(seed ^ 0xFA4Bull);
    const int res = t.res();
    const float stepsPerUnit = t.heightRange / 3.0f;   // ~3 m benches
    Grid src = t.height;
    parallelFor(0, res, [&](int y) {
        for (int x = 0; x < res; ++x) {
            float s = t.slopeAt(x, y);
            float h = src.at(x, y);
            if (t.seaLevel >= 0 && h < t.seaLevel + 0.01f) continue;
            float band = smoothstepf(0.04f, 0.1f, s) * smoothstepf(0.45f, 0.25f, s);
            float patches = smoothstepf(0.1f, 0.35f, n.fbm(float(x) / res * 6.f, float(y) / res * 6.f, 3));
            float m = band * patches * amount;
            if (m <= 0.001f) continue;
            float q = smoothQuantize(h, stepsPerUnit, 0.3f);
            t.height.at(x, y) = lerpf(h, q, m);
        }
    });
}

namespace {

// Everything after the raw heightfield exists: erosion, hydrology, networks, foliage, materials.
bool finishScene(Scene& sc, const GenSettings& s, Progress* progress, float erosionMul, bool evolve) {
    auto stage = [&](const char* name, float f) { if (progress) progress->set(name, f); };
    auto cancelled = [&]() { return progress && progress->cancel.load(); };
    const auto& env = environments()[std::clamp(s.envIndex, 0, int(environments().size()) - 1)];
    const auto& era = eras()[std::clamp(s.eraIndex, 0, int(eras().size()) - 1)];
    const Genome& g = sc.genome;
    Terrain& t = sc.terrain;
    const int res = t.res();
    if (t.lava.empty()) t.lava.resize(res, res, 0.f);

    // ---- erosion (at <= 1537 px; upsampled delta for big maps)
    if (erosionMul > 0.f) {
        const int eroRes = res > 1537 ? (res + 1) / 2 : res;
        Grid work = eroRes == res ? t.height : t.height.resampled(eroRes, eroRes);
        Grid before = work;
        const float cell = t.worldSize / float(eroRes - 1);
        const float hScale = t.heightRange / cell;   // height units -> rise/run
        uint64_t eseed = hashCombine(g.detailSeed, 0xE205ull);

        if (evolve && g.r.rivers * erosionMul > 0.01f) {
            // Landscape evolution: tectonic uplift vs. stream-power incision gives real drainage networks.
            stage("Landscape evolution (uplift + river incision)", 0.28f);
            const int lr = std::min(eroRes, 320);
            Grid coarse = work.resampled(lr, lr);
            Grid uplift = coarse;
            for (auto& v : uplift.vec()) v = std::pow(clamp01(v), 1.3f);
            Grid evo = coarse;
            for (auto& v : evo.vec()) v *= 0.25f;
            Grid fixedMask(lr, lr, 0.f);
            if (t.seaLevel >= 0.f)
                for (size_t i = 0; i < coarse.size(); ++i) fixedMask.vec()[i] = coarse.vec()[i] < t.seaLevel ? 1.f : 0.f;
            const float coarseScale = t.heightRange / (t.worldSize / float(lr - 1));
            const int iters = 30 + int(g.r.rivers * 25.f);
            upliftStreamPower(evo, uplift, iters, 0.9f + 0.6f * g.r.rivers, std::max(0.3f, g.r.talus) / coarseScale * 1.2f, &fixedMask);
            evo.normalize(0.f, 1.f);
            if (t.seaLevel >= 0.f) {
                // keep the coastline where the director put it
                for (size_t i = 0; i < evo.size(); ++i)
                    evo.vec()[i] = coarse.vec()[i] < t.seaLevel ? coarse.vec()[i] : t.seaLevel + evo.vec()[i] * (1.f - t.seaLevel);
            }
            const float blend = clampf(0.35f + 0.4f * g.r.rivers, 0.f, 0.85f) * std::min(1.f, erosionMul);
            Grid delta(lr, lr);
            for (size_t i = 0; i < delta.size(); ++i) delta.vec()[i] = (evo.vec()[i] - coarse.vec()[i]) * blend;
            Grid up = delta.resampled(eroRes, eroRes);
            for (size_t i = 0; i < up.size(); ++i) work.vec()[i] += up.vec()[i];
            fluvialErosion(work, 1 + int(g.r.rivers * erosionMul * 2.f), 0.002f * g.r.rivers * erosionMul);
        }
        if (g.r.hydraulic * erosionMul > 0.01f) {
            HydraulicParams hp;
            hp.droplets = int(float(eroRes) * float(eroRes) * 0.45f * g.r.hydraulic * erosionMul);
            hp.heightScale = hScale;
            hp.erodeSpeed = 0.3f;
            hp.radius = eroRes > 1200 ? 4 : 3;
            hydraulicErosion(work, hp, eseed, progress, 0.32f, 0.5f);
        }
        if (cancelled()) return false;
        if (g.r.thermal * erosionMul > 0.01f) {
            stage("Thermal weathering", 0.5f);
            thermalErosion(work, 8 + int(g.r.thermal * erosionMul * 30.f), g.r.talus / hScale, 0.5f);
        }
        if (g.r.wind > 0.01f) windSmoothing(work, g.windDir, g.r.wind * erosionMul);

        if (eroRes == res) {
            t.height = work;
        } else {
            Grid delta(eroRes, eroRes);
            for (size_t i = 0; i < delta.size(); ++i) delta.vec()[i] = work.vec()[i] - before.vec()[i];
            Grid up = delta.resampled(res, res);
            for (size_t i = 0; i < up.size(); ++i) t.height.vec()[i] += up.vec()[i];
        }
        applyFarmTerraces(t, g.farmTerraces, g.detailSeed);
        t.height.normalize(0.f, 1.f);
    }
    if (cancelled()) return false;

    // ---- hydrology
    stage("Hydrology: lakes, rivers, moisture", 0.55f);
    {
        HydrologyParams hp;
        hp.seaLevel = t.seaLevel;
        hp.lakes = g.r.moisture >= 0.2f || !g.basins.empty() || g.r.lavaFill > 0.9f;
        hp.rivers = g.r.rivers > 0.05f && g.r.moisture > 0.08f;
        hp.riverThreshold = 0.0035f / std::max(0.3f, g.r.rivers);
        hp.baseMoisture = g.r.moisture;
        computeHydrology(t, hp, g.detailSeed, g.r.lavaFill > 0.9f ? &t.lava : nullptr);
    }
    t.baseHeight = t.height;
    t.roadMask.resize(res, res, 0.f);
    if (cancelled()) return false;

    // ---- networks
    if (s.roads || s.rail || s.paths || s.racetrack) {
        NetworkRequest nr;
        nr.roads = s.roads; nr.rail = s.rail; nr.paths = s.paths; nr.racetrack = s.racetrack;
        nr.roadDensity = s.roadDensity * era.settlementDensity;
        nr.pathDensity = s.pathDensity;
        nr.seed = hashCombine(g.macroSeed, uint64_t(s.variation) * 7919ull + 3);
        buildNetworks(t, era, nr, sc.splines, sc.log, progress, 0.6f, 0.8f);
    }
    stampSplines(t, sc.splines);
    if (cancelled()) return false;

    // ---- foliage
    if (s.foliage) {
        stage("Scattering foliage", 0.82f);
        ScatterSettings ss;
        ss.density = s.foliageDensity;
        ss.props = s.props;
        ss.seed = hashCombine(g.detailSeed, 0xF011A6Eull);
        scatterFoliage(t, effectiveFoliageRules(env, era), sc.splines, ss, sc.foliage, progress);
    }
    if (s.props) placeSplineProps(t, sc.splines, sc.foliage);

    // ---- materials
    stage("Painting material layers", 0.94f);
    computeSplat(t, env, era, g.detailSeed, sc.splat);
    sc.atmosphere = blendAtmosphere(env, era);
    return true;
}

void commitScene(Scene& sc, Scene& out, double secs) {
    char buf[320];
    std::snprintf(buf, sizeof(buf), "Built %dx%d terrain in %.1fs: %zu splines, %zu foliage instances.", sc.terrain.res(), sc.terrain.res(), secs,
                  sc.splines.size(), sc.foliage.size());
    sc.log.push_back(buf);
    int hv = out.heightVersion + 1, sv = out.splatVersion + 1, fv = out.foliageVersion + 1, spv = out.splineVersion + 1;
    out = std::move(sc);
    out.heightVersion = hv;
    out.splatVersion = sv;
    out.foliageVersion = fv;
    out.splineVersion = spv;
}

}  // namespace

bool generateScene(const GenSettings& s, Scene& out, Progress* progress) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    Scene sc;
    sc.settings = s;
    sc.genome = directTerrain(s, sc.report, progress, 0.f, 0.18f);
    if (progress && progress->cancel) return false;
    const Genome& g = sc.genome;
    const int res = std::clamp(s.resolution, 65, 8193);
    Terrain& t = sc.terrain;
    t.worldSize = s.worldSize;
    t.heightRange = g.r.heightRange;
    t.seaLevel = g.r.seaLevel;
    if (progress) progress->set("Synthesizing landforms", 0.2f);
    synthesizeHeight(g, res, t.height, &t.lava, false);
    if (!finishScene(sc, s, progress, 1.f, false)) return false;
    commitScene(sc, out, std::chrono::duration<double>(clock::now() - t0).count());
    if (progress) progress->set("Done", 1.f);
    return true;
}

bool buildSceneFromHeightmap(const Grid& src, const GenSettings& s, const ImportOptions& opt, Scene& out, Progress* progress) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    if (src.empty()) return false;
    Scene sc;
    sc.settings = s;
    std::vector<std::string> intent;
    TerrainRecipe r = composeRecipe(s, &intent);
    sc.genome = makeGenome(r, s, seedFromText(s.seedText), uint64_t(s.variation), 0.f);
    sc.genome.ranges.clear();
    sc.genome.basins.clear();
    sc.genome.volcanoes.clear();
    sc.genome.craters.clear();
    sc.genome.quarries.clear();
    sc.report.intent = intent;
    sc.report.summary = opt.sourceLabel;
    Terrain& t = sc.terrain;
    const int res = opt.keepResolution ? src.width() : std::clamp(s.resolution, 65, 8193);
    if (progress) progress->set("Resampling source heightmap", 0.1f);
    t.height = (src.width() == res && src.height() == res) ? src : src.resampled(res, res);
    if (opt.normalize) t.height.normalize(0.f, 1.f);
    t.worldSize = opt.worldSize > 0 ? opt.worldSize : s.worldSize;
    t.heightRange = opt.heightRange > 0 ? opt.heightRange : r.heightRange;
    t.seaLevel = opt.seaLevel;
    sc.genome.r.heightRange = t.heightRange;
    sc.genome.r.seaLevel = t.seaLevel;
    sc.report.metrics = measure(t.height, sc.genome.r, t.worldSize);
    if (!finishScene(sc, s, progress, opt.erosion, true)) return false;
    commitScene(sc, out, std::chrono::duration<double>(clock::now() - t0).count());
    if (progress) progress->set("Done", 1.f);
    return true;
}

void restampSplines(Scene& scene) {
    if (!scene.valid()) return;
    stampSplines(scene.terrain, scene.splines);
    const auto& env = environments()[std::clamp(scene.settings.envIndex, 0, int(environments().size()) - 1)];
    const auto& era = eras()[std::clamp(scene.settings.eraIndex, 0, int(eras().size()) - 1)];
    computeSplat(scene.terrain, env, era, scene.genome.detailSeed, scene.splat);
    auto& fol = scene.foliage;
    fol.erase(std::remove_if(fol.begin(), fol.end(), [](const FoliageInstance& f) { return (f.flags & 2) != 0; }), fol.end());
    dropFoliageToTerrain(scene.terrain, fol);
    if (scene.settings.props) placeSplineProps(scene.terrain, scene.splines, fol);
    ++scene.heightVersion;
    ++scene.splatVersion;
    ++scene.foliageVersion;
    ++scene.splineVersion;
}

}  // namespace zl
