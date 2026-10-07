// ZeraLands - shared enums for materials, foliage and splines plus their display metadata.
#pragma once

#include "core/Common.h"

#include <string>

namespace zl {

enum class MaterialKind : int {
    Grass, LushGrass, DryGrass, ForestFloor, Moss, Dirt, Mud, Sand, RedSand, Pebbles, Gravel,
    Rock, Cliff, Sandstone, Basalt, Snow, Ice, Lava, Ash, Salt, Crystal,
    // network surfaces
    Asphalt, Cobblestone, RomanStone, PackedDirt, Concrete, GlowPanel, RailBed,
    Count
};
constexpr int kMaterialCount = int(MaterialKind::Count);

struct MaterialInfo {
    const char* name;
    Color3 albedo;       // fallback base colour (linear-ish sRGB)
    Color3 albedo2;      // secondary colour for procedural variation
    float roughness;
    float tileMeters;    // default world-space tile size
    const char* keywords;  // space separated keywords for texture-library auto matching
};
const MaterialInfo& materialInfo(MaterialKind k);

enum class FoliageKind : int {
    Conifer, Broadleaf, Birch, Palm, JungleTree, Acacia, Baobab, DeadTree, Cactus, Shrub, DesertShrub,
    Fern, Cycad, TreeFern, GrassTuft, Reeds, Flowers, Boulder, SmallRocks, GiantMushroom, Crystal,
    CharredStump, Mangrove,
    // props (era / network dressing)
    StreetLamp, TelegraphPole, Torch, Milestone, Column, NeonPylon, WindTurbine, Ruin,
    Count
};
constexpr int kFoliageCount = int(FoliageKind::Count);

enum class ProxyShape : int { ConeTree, RoundTree, Palm, Cactus, Bush, Fern, Grass, Rock, Mushroom, Crystal, Pole, Lamp, Turbine, Column, Stump };

struct FoliageInfo {
    const char* name;
    ProxyShape shape;
    Color3 primary;     // canopy / main colour
    Color3 secondary;   // trunk / accent colour
    float baseHeight;   // meters at scale 1
    float minSpacing;   // meters, for painting / poisson rejection
    bool isProp;
};
const FoliageInfo& foliageInfo(FoliageKind k);

enum class SplineKind : int { Road, Rail, Path, Racetrack, Canal, Aqueduct, Maglev, LavaChannel, Custom, Count };
const char* splineKindName(SplineKind k);

}  // namespace zl
