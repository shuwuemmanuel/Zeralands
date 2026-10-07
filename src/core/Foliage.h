// ZeraLands - procedural foliage scattering and the foliage paint brush.
#pragma once

#include "core/Eras.h"
#include "core/Heightfield.h"
#include "core/Spline.h"

#include <vector>

namespace zl {

struct FoliageInstance {
    uint16_t kind = 0;   // FoliageKind
    uint16_t flags = 0;  // bit0: user painted
    float x = 0, y = 0, z = 0;   // world meters
    float scale = 1.f;
    float rotation = 0.f;        // radians around Y
};

struct ScatterSettings {
    float density = 1.f;   // global multiplier
    bool props = true;
    uint64_t seed = 1;
    size_t maxInstances = 900000;
};

// Effective foliage rules for env + era (swaps, density, extra rules).
std::vector<FoliageRule> effectiveFoliageRules(const Environment& env, const EraProfile& era);

void scatterFoliage(const Terrain& t, const std::vector<FoliageRule>& rules, const std::vector<Spline>& splines,
                    const ScatterSettings& s, std::vector<FoliageInstance>& out, Progress* progress);

// Props (lamps, poles, columns...) along splines that request them.
void placeSplineProps(const Terrain& t, const std::vector<Spline>& splines, std::vector<FoliageInstance>& out);

struct PaintBrush {
    std::vector<FoliageKind> kinds;  // types painted (randomly mixed)
    float radius = 25.f;             // meters
    float density = 40.f;            // instances per hectare at brush centre
    float falloff = 0.5f;            // 0 hard edge .. 1 soft
    float scaleMin = 0.8f, scaleMax = 1.3f;
    float slopeMax = 1.0f;           // rise/run limit
    bool avoidRoads = true;
    bool avoidWater = true;
};

// One brush dab. Returns number of instances added.
int paintFoliage(const Terrain& t, std::vector<FoliageInstance>& inst, Vec2 centerXZ, const PaintBrush& b, Rng& rng, float strength);
// Erase within radius (only listed kinds if `onlyKinds` non-empty). Returns removed count.
int eraseFoliage(std::vector<FoliageInstance>& inst, Vec2 centerXZ, float radius, const std::vector<FoliageKind>& onlyKinds,
                 float strength, Rng& rng);
// Re-seat instances on the terrain after height edits.
void dropFoliageToTerrain(const Terrain& t, std::vector<FoliageInstance>& inst);

}  // namespace zl
