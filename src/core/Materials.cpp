#include "core/Materials.h"

#include "core/Noise.h"

namespace zl {

std::vector<LayerRule> effectiveLayers(const Environment& env, const EraProfile& era, bool hasLava, bool hasWaterShore) {
    std::vector<LayerRule> rules;
    for (LayerRule r : env.layers) {
        for (auto& [from, to] : era.materialSwaps)
            if (r.kind == from) r.kind = to;
        rules.push_back(r);
    }
    auto has = [&](MaterialKind k) {
        for (auto& r : rules)
            if (r.kind == k) return true;
        return false;
    };
    if (hasLava && !has(MaterialKind::Lava)) {
        LayerRule l;
        l.kind = MaterialKind::Lava;
        l.weight = 0.f;
        rules.push_back(l);
    }
    if (hasWaterShore && !has(MaterialKind::Sand) && !has(MaterialKind::Pebbles) && !has(MaterialKind::Mud)) {
        LayerRule l;
        l.kind = MaterialKind::Sand;
        l.hMax = 0.f;   // driven by shore logic only
        l.weight = 0.f;
        rules.push_back(l);
    }
    return rules;
}

static float band(float v, float lo, float hi, float soft) {
    float a = lo <= 0.f ? 1.f : smoothstepf(lo - soft, lo + soft, v);
    float b = hi >= 0.999f ? 1.f : 1.f - smoothstepf(hi - soft, hi + soft, v);
    return a * b;
}

void computeSplat(const Terrain& t, const Environment& env, const EraProfile& era, uint64_t seed, SplatMap& out) {
    const int res = t.res();
    bool hasLava = false;
    for (float v : t.lava.vec())
        if (v > 0.3f) { hasLava = true; break; }
    bool hasWater = t.seaLevel >= 0.f;
    auto rules = effectiveLayers(env, era, hasLava, hasWater);

    // map rules -> unique layers
    out.layerCount = 0;
    std::vector<int> ruleLayer(rules.size(), -1);
    for (size_t i = 0; i < rules.size(); ++i) {
        int found = -1;
        for (int l = 0; l < out.layerCount; ++l)
            if (out.layers[l] == rules[i].kind) found = l;
        if (found < 0 && out.layerCount < kMaxLayers) {
            found = out.layerCount;
            out.layers[out.layerCount++] = rules[i].kind;
        }
        ruleLayer[i] = found;
    }
    if (out.layerCount == 0) {
        out.layers[0] = MaterialKind::Dirt;
        out.layerCount = 1;
    }
    int lavaLayer = -1, shoreLayer = -1;
    for (int l = 0; l < out.layerCount; ++l) {
        if (out.layers[l] == MaterialKind::Lava) lavaLayer = l;
        if (shoreLayer < 0 && (out.layers[l] == MaterialKind::Sand || out.layers[l] == MaterialKind::Pebbles || out.layers[l] == MaterialKind::Mud))
            shoreLayer = l;
    }

    out.res = res;
    out.weights0.assign(size_t(res) * res * 4, 0);
    out.weights1.assign(size_t(res) * res * 4, 0);
    Noise nz(seed ^ 0x5B1A7ull);
    const float invRes = 1.f / float(res);
    parallelFor(0, res, [&](int y) {
        float w[kMaxLayers];
        for (int x = 0; x < res; ++x) {
            std::fill(w, w + kMaxLayers, 0.f);
            float h = t.height.at(x, y);
            float slope = t.slopeAt(x, y);
            float m = t.moisture.empty() ? 0.5f : t.moisture.at(x, y);
            float u = float(x) * invRes, v = float(y) * invRes;
            float n1 = nz.fbm(u * 24.f, v * 24.f, 4);
            float n2 = nz.fbm(u * 7.f + 3.f, v * 7.f - 9.f, 3);
            // jitter the lookup a little so band edges are organic, not contour lines
            float hj = h + n2 * 0.025f;
            float sj = slope * (1.f + n1 * 0.25f);
            for (size_t i = 0; i < rules.size(); ++i) {
                const LayerRule& r = rules[i];
                if (ruleLayer[i] < 0 || r.weight <= 0.f) continue;
                float wt = band(hj, r.hMin, r.hMax, 0.03f) * band(sj, r.slopeMin, r.slopeMax, 0.06f + 0.08f * r.slopeMin) *
                           band(m, r.moistMin, r.moistMax, 0.07f);
                wt *= r.weight * clampf(1.f + r.noise * (n1 * 1.4f + n2 * 0.6f), 0.f, 2.f);
                w[ruleLayer[i]] += wt;
            }
            if (lavaLayer >= 0 && !t.lava.empty()) w[lavaLayer] += t.lava.at(x, y) * 6.f;
            if (shoreLayer >= 0 && t.seaLevel >= 0.f) {
                float sh = 1.f - smoothstepf(0.004f, 0.02f + 0.01f * n1, std::fabs(h - t.seaLevel - 0.004f));
                w[shoreLayer] += sh * 3.f * (1.f - smoothstepf(0.3f, 0.6f, slope));
            }
            float total = 0;
            for (int l = 0; l < out.layerCount; ++l) {
                w[l] = std::pow(std::max(0.f, w[l]), 1.6f);   // crisper dominance
                total += w[l];
            }
            if (total < 1e-5f) { w[0] = 1.f; total = 1.f; }
            size_t idx = (size_t(y) * res + x) * 4;
            for (int l = 0; l < kMaxLayers; ++l) {
                uint8_t val = l < out.layerCount ? uint8_t(std::lround(clamp01(w[l] / total) * 255.f)) : 0;
                if (l < 4) out.weights0[idx + l] = val;
                else out.weights1[idx + (l - 4)] = val;
            }
        }
    });
}

}  // namespace zl
