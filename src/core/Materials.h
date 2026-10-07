// ZeraLands - material layer (splat) weights from height, slope, moisture, flow and roads.
#pragma once

#include "core/Eras.h"
#include "core/Heightfield.h"

#include <array>
#include <vector>

namespace zl {

constexpr int kMaxLayers = 8;

struct SplatMap {
    int res = 0;
    int layerCount = 0;
    std::array<MaterialKind, kMaxLayers> layers{};
    std::vector<uint8_t> weights0;   // RGBA8: layers 0-3
    std::vector<uint8_t> weights1;   // RGBA8: layers 4-7
    float weight(int layer, int x, int y) const {
        const auto& v = layer < 4 ? weights0 : weights1;
        return v[(size_t(y) * res + x) * 4 + (layer & 3)] / 255.f;
    }
};

// Resolves the env's layer rules through era swaps into at most kMaxLayers distinct rules.
std::vector<LayerRule> effectiveLayers(const Environment& env, const EraProfile& era, bool hasLava, bool hasWaterShore);

void computeSplat(const Terrain& t, const Environment& env, const EraProfile& era, uint64_t seed, SplatMap& out);

}  // namespace zl
