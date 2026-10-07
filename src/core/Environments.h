// ZeraLands - environment (biome) catalogue: terrain recipe, material layering and foliage rules.
#pragma once

#include "core/Types.h"

#include <string>
#include <vector>

namespace zl {

// Every field is a "gene" the TerrainDirector may mutate. Units are normalized unless noted.
struct TerrainRecipe {
    float baseFreq = 2.5f;      // macro feature frequency (cycles across the map)
    int octaves = 7;
    float lacunarity = 2.f;
    float gain = 0.5f;
    float wFbm = 0.5f, wRidged = 0.3f, wBillow = 0.f, wEroded = 0.2f;   // noise mixture
    float warp = 0.25f;         // domain warp strength (cycles)
    float relief = 0.8f;        // macro amplitude
    float heightRange = 600.f;  // meters represented by 0..1
    float seaLevel = -1.f;      // normalized sea level, <0 = no ocean
    float islandMask = 0.f;     // radial falloff -> islands / continents
    float ranges = 1.f;         // expected number of tectonic mountain ranges
    float rangeStrength = 0.4f;
    float basins = 0.f;         // expected number of basins (lakes)
    float terraces = 0.f;       // terrace steps (0 = off)
    float terraceStrength = 0.f;
    float mesas = 0.f;          // plateau quantisation strength
    float dunes = 0.f;          // dune field strength
    float duneScale = 1.f;      // dune wavelength multiplier
    float craters = 0.f;        // number of impact craters
    float volcanoes = 0.f;      // number of volcanic cones
    float lavaFill = 0.f;       // 0..1 lava in calderas / low ground
    float canyons = 0.f;        // canyon incision strength
    float karst = 0.f;          // tower karst strength
    float spikes = 0.f;         // extreme spires
    float glacial = 0.f;        // U-valley carving
    float flatten = 0.f;        // pull toward a flat plain (0..1)
    float plateau = 0.f;        // clamp top of terrain (0 = off, else level)
    float rivers = 0.3f;        // stream-power carving
    float hydraulic = 0.5f;     // droplet erosion amount
    float thermal = 0.3f;       // thermal erosion amount
    float talus = 0.6f;         // talus slope (rise/run) for thermal erosion
    float wind = 0.f;           // aeolian smoothing
    float crystals = 0.f;       // crystal spires
    float moisture = 0.5f;      // base climate moisture
    float temperature = 0.5f;   // 0 frozen .. 1 hot
};

// What the director should aim for when scoring candidate layouts.
struct DirectorTargets {
    float relief = 0.55f;       // stddev of normalized height * 4
    float slope = 0.25f;        // mean slope (rise/run) of the final terrain
    float water = 0.0f;         // fraction under sea level
    float flat = 0.3f;          // fraction of buildable (slope < 0.12) land
    float interest = 0.5f;      // curvature diversity
};

struct LayerRule {
    MaterialKind kind;
    float hMin = 0.f, hMax = 1.f;           // normalized height band
    float slopeMin = 0.f, slopeMax = 99.f;  // rise/run band
    float moistMin = 0.f, moistMax = 1.f;
    float noise = 0.3f;                      // breakup amount
    float weight = 1.f;
};

struct FoliageRule {
    FoliageKind kind;
    float density = 10.f;        // instances per hectare (100m x 100m) at full suitability
    float slopeMax = 0.6f;
    float hMin = 0.f, hMax = 1.f;
    float moistMin = 0.f, moistMax = 1.f;
    float cluster = 0.5f;        // 0 uniform .. 1 strongly clumped
    float scaleMin = 0.8f, scaleMax = 1.25f;
};

struct Atmosphere {
    Color3 skyTop{0.30f, 0.50f, 0.85f};
    Color3 skyHorizon{0.75f, 0.83f, 0.92f};
    Color3 sun{1.0f, 0.95f, 0.85f};
    Color3 water{0.10f, 0.28f, 0.35f};
    float fogDensity = 0.35f;
};

struct Environment {
    std::string id, name, category, description;
    std::vector<std::string> aliases;   // older / alternative names (incl. ZeraLands 1.x preset ids)
    TerrainRecipe recipe;
    DirectorTargets targets;
    std::vector<LayerRule> layers;   // up to 8 used for rendering
    std::vector<FoliageRule> foliage;
    Atmosphere atmosphere;
};

const std::vector<Environment>& environments();
int findEnvironment(const std::string& nameOrId);   // fuzzy, -1 if none
std::vector<std::string> environmentCategories();

}  // namespace zl
