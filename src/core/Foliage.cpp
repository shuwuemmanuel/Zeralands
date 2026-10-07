#include "core/Foliage.h"

#include "core/Noise.h"

namespace zl {

std::vector<FoliageRule> effectiveFoliageRules(const Environment& env, const EraProfile& era) {
    std::vector<FoliageRule> rules;
    for (FoliageRule r : env.foliage) {
        for (auto& [from, to] : era.foliageSwaps)
            if (r.kind == from) r.kind = to;
        r.density *= era.foliageDensity;
        if (r.density > 0) rules.push_back(r);
    }
    for (const auto& r : era.extraFoliage) rules.push_back(r);
    return rules;
}

static float band(float v, float lo, float hi, float soft) {
    if (lo <= 0.f && hi >= 1.f) return 1.f;
    return smoothstepf(lo - soft, lo + soft, v) * (1.f - smoothstepf(hi - soft, hi + soft, v));
}

void scatterFoliage(const Terrain& t, const std::vector<FoliageRule>& rules, const std::vector<Spline>&, const ScatterSettings& s,
                    std::vector<FoliageInstance>& out, Progress* progress) {
    const float world = t.worldSize;
    const float areaHa = world * world / 10000.f;
    const float cs = t.cellSize();
    Noise cluster(s.seed ^ 0xC1A55ull);
    const size_t budgetPerRule = s.maxInstances / std::max<size_t>(1, rules.size());

    for (size_t ri = 0; ri < rules.size(); ++ri) {
        const FoliageRule& r = rules[ri];
        const FoliageInfo& info = foliageInfo(r.kind);
        if (info.isProp && !s.props) continue;
        float density = r.density * s.density;
        if (density <= 0.f) continue;
        float expected = density * areaHa;
        if (expected > float(budgetPerRule)) density *= float(budgetPerRule) / expected;
        float cell = std::sqrt(10000.f / density);
        int cells = std::max(1, int(world / cell));
        cell = world / float(cells);
        Rng rng(hashCombine(s.seed, uint64_t(ri) * 131 + uint64_t(r.kind)));
        const float clusterFreq = 1.f / std::max(40.f, info.minSpacing * 18.f);
        for (int cy = 0; cy < cells; ++cy) {
            for (int cx = 0; cx < cells; ++cx) {
                float wx = (float(cx) + rng.uniform()) * cell;
                float wz = (float(cy) + rng.uniform()) * cell;
                float pr = rng.uniform();
                Vec2 g{wx / cs, wz / cs};
                int ix = std::clamp(int(g.x + 0.5f), 0, t.res() - 1), iy = std::clamp(int(g.y + 0.5f), 0, t.res() - 1);
                float h = t.height.sample(g.x, g.y);
                if (!t.water.empty() && t.water.at(ix, iy) > h - 0.0005f) continue;
                if (!t.lava.empty() && t.lava.at(ix, iy) > 0.3f) continue;
                if (!t.roadMask.empty() && t.roadMask.at(ix, iy) > 0.15f) continue;
                float slope = t.slopeAt(ix, iy);
                float suit = 1.f - smoothstepf(r.slopeMax * 0.7f, r.slopeMax, slope);
                suit *= band(h, r.hMin, r.hMax, 0.03f);
                float m = t.moisture.empty() ? 0.5f : t.moisture.at(ix, iy);
                suit *= band(m, r.moistMin, r.moistMax, 0.06f);
                if (r.cluster > 0) {
                    float n = 0.5f + 0.5f * cluster.fbm(wx * clusterFreq + float(ri) * 17.f, wz * clusterFreq, 3);
                    suit *= lerpf(1.f, smoothstepf(0.4f, 0.72f, n) * 2.f, r.cluster);
                }
                if (pr >= suit) continue;
                FoliageInstance fi;
                fi.kind = uint16_t(r.kind);
                fi.x = wx;
                fi.z = wz;
                fi.y = h * t.heightRange;
                fi.scale = rng.range(r.scaleMin, r.scaleMax);
                fi.rotation = rng.range(0.f, kTau);
                out.push_back(fi);
            }
        }
        if (progress) progress->set("Scattering foliage", 0.82f + 0.1f * float(ri + 1) / float(rules.size()));
    }
}

void placeSplineProps(const Terrain& t, const std::vector<Spline>& splines, std::vector<FoliageInstance>& out) {
    for (const auto& s : splines) {
        if (s.prop == FoliageKind::Count || s.propSpacing <= 0) continue;
        auto smp = s.sample(2.f);
        if (smp.size() < 2) continue;
        const FoliageInfo& info = foliageInfo(s.prop);
        float next = s.propSpacing * 0.5f;
        int side = 1;
        for (const auto& p : smp) {
            if (p.distance < next) continue;
            next += s.propSpacing;
            FoliageInstance fi;
            fi.kind = uint16_t(s.prop);
            fi.flags = 2;   // spline prop (regenerated on spline edits)
            fi.rotation = std::atan2(p.dir.x, p.dir.y);
            if (s.elevated) {
                fi.x = p.pos.x;
                fi.z = p.pos.z;
                float ground = t.heightAtWorld(fi.x, fi.z);
                fi.y = ground;
                fi.scale = std::max(0.2f, (p.pos.y - ground) / info.baseHeight);
            } else {
                Vec2 n = p.dir.perp() * float(side);
                float off = p.width * 0.5f + s.shoulder * 0.35f + 1.f;
                fi.x = clampf(p.pos.x + n.x * off, 0.f, t.worldSize);
                fi.z = clampf(p.pos.z + n.y * off, 0.f, t.worldSize);
                fi.y = t.heightAtWorld(fi.x, fi.z);
                fi.scale = 1.f;
                side = -side;
            }
            out.push_back(fi);
        }
    }
}

int paintFoliage(const Terrain& t, std::vector<FoliageInstance>& inst, Vec2 c, const PaintBrush& b, Rng& rng, float strength) {
    if (b.kinds.empty() || t.height.empty()) return 0;
    float maxSpacing = 0;
    for (auto k : b.kinds) maxSpacing = std::max(maxSpacing, foliageInfo(k).minSpacing);
    // local neighbourhood snapshot for spacing checks
    std::vector<Vec2> nearby;
    std::vector<float> nearbySp;
    float reach = b.radius + maxSpacing;
    for (const auto& f : inst) {
        float dx = f.x - c.x, dz = f.z - c.y;
        if (dx * dx + dz * dz < reach * reach) {
            nearby.push_back({f.x, f.z});
            nearbySp.push_back(foliageInfo(FoliageKind(f.kind)).minSpacing);
        }
    }
    float area = kPi * b.radius * b.radius;
    int attempts = std::max(1, int(b.density / 10000.f * area * strength * 1.6f + rng.uniform()));
    const float cs = t.cellSize();
    int added = 0;
    for (int a = 0; a < attempts; ++a) {
        float ang = rng.range(0.f, kTau);
        float rr = std::sqrt(rng.uniform()) * b.radius;
        float fall = 1.f - smoothstepf(b.radius * (1.f - b.falloff), b.radius, rr);
        if (rng.uniform() > fall) continue;
        Vec2 p{c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr};
        if (p.x < 0 || p.y < 0 || p.x > t.worldSize || p.y > t.worldSize) continue;
        FoliageKind k = b.kinds[size_t(rng.next() % b.kinds.size())];
        float sp = foliageInfo(k).minSpacing;
        int ix = std::clamp(int(p.x / cs + 0.5f), 0, t.res() - 1), iy = std::clamp(int(p.y / cs + 0.5f), 0, t.res() - 1);
        float h = t.height.sample(p.x / cs, p.y / cs);
        if (b.avoidWater && !t.water.empty() && t.water.at(ix, iy) > h - 0.0005f) continue;
        if (b.avoidRoads && !t.roadMask.empty() && t.roadMask.at(ix, iy) > 0.3f) continue;
        if (t.slopeAt(ix, iy) > b.slopeMax) continue;
        bool ok = true;
        for (size_t i = 0; i < nearby.size() && ok; ++i) {
            float need = 0.5f * (sp + nearbySp[i]);
            if ((nearby[i] - p).length() < need) ok = false;
        }
        if (!ok) continue;
        FoliageInstance fi;
        fi.kind = uint16_t(k);
        fi.flags = 1;
        fi.x = p.x;
        fi.z = p.y;
        fi.y = h * t.heightRange;
        fi.scale = rng.range(b.scaleMin, b.scaleMax);
        fi.rotation = rng.range(0.f, kTau);
        inst.push_back(fi);
        nearby.push_back(p);
        nearbySp.push_back(sp);
        ++added;
    }
    return added;
}

int eraseFoliage(std::vector<FoliageInstance>& inst, Vec2 c, float radius, const std::vector<FoliageKind>& only, float strength, Rng& rng) {
    size_t before = inst.size();
    float r2 = radius * radius;
    inst.erase(std::remove_if(inst.begin(), inst.end(),
                              [&](const FoliageInstance& f) {
                                  float dx = f.x - c.x, dz = f.z - c.y;
                                  if (dx * dx + dz * dz > r2) return false;
                                  if (!only.empty() && std::find(only.begin(), only.end(), FoliageKind(f.kind)) == only.end()) return false;
                                  return rng.uniform() < strength;
                              }),
               inst.end());
    return int(before - inst.size());
}

void dropFoliageToTerrain(const Terrain& t, std::vector<FoliageInstance>& inst) {
    for (auto& f : inst) f.y = t.heightAtWorld(f.x, f.z);
}

}  // namespace zl
