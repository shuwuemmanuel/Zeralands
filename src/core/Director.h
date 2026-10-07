// ZeraLands - TerrainDirector: turns (environment, era, seed, variation, prompt) into a concrete genome by
// interpreting intent, breeding candidate layouts and keeping the one that best fits the environment's targets.
#pragma once

#include "core/Eras.h"
#include "core/Heightfield.h"

#include <string>
#include <vector>

namespace zl {

struct RangeFeature {
    Vec2 p0, p1, p2;     // quadratic spine in uv space
    float width = 0.1f;
    float height = 0.5f;
};

struct BlobFeature {
    Vec2 pos;
    float radius = 0.05f;
    float amount = 0.3f;
};

struct Genome {
    TerrainRecipe r;
    uint64_t macroSeed = 1;    // drives large-scale shapes (stable for a seed)
    uint64_t detailSeed = 1;   // drives medium / fine detail (changes with each variation)
    Vec2 windDir{1.f, 0.2f};
    Vec2 islandCenter{0.5f, 0.5f};
    float featureScale = 1.f;
    std::vector<RangeFeature> ranges;
    std::vector<BlobFeature> basins, volcanoes, craters, quarries, karstSeeds;
    float farmTerraces = 0.f;
};

struct GenSettings {
    int envIndex = 0;
    int eraIndex = 8;           // present day
    std::string seedText = "1337";
    int variation = 0;          // same seed + new variation = a close but different landscape
    std::string prompt;         // free-text intent ("huge snowy peaks with a river and a few lakes")
    int resolution = 1009;
    float worldSize = 4033.f;   // meters
    float heightScale = 1.f;    // multiplies environment height range
    float roughness = 1.f;
    float erosion = 1.f;
    float waterLevel = 0.f;     // offset added to environment sea level (-0.3..0.3)
    float featureScale = 1.f;   // <1 bigger landforms, >1 more, smaller landforms
    float riverAmount = 1.f;
    bool roads = true, rail = false, paths = true, racetrack = false;
    float roadDensity = 1.f, pathDensity = 1.f;
    bool foliage = true, props = true;
    float foliageDensity = 1.f;
    int candidates = 10;        // director population per generation
    int refineSteps = 3;        // hill-climb rounds after the best candidate
};

struct TerrainMetrics {
    float relief = 0, slope = 0, water = 0, flat = 0, interest = 0, balance = 0;
};

struct DirectorReport {
    std::vector<std::string> intent;     // interpreted prompt modifiers
    std::string summary;                 // chosen candidate & scores
    TerrainMetrics metrics;
    float score = 0;
    int evaluated = 0;
};

uint64_t seedFromText(const std::string& text);

// Applies environment + era + slider + prompt modifiers to produce the base recipe.
TerrainRecipe composeRecipe(const GenSettings& s, std::vector<std::string>* intentLog);

// Builds the genome for a recipe (feature layout from macroSeed, jittered by variation).
Genome makeGenome(const TerrainRecipe& r, const GenSettings& s, uint64_t macroSeed, uint64_t variationSeed, float jitter);

// Measures a synthesized heightfield.
TerrainMetrics measure(const Grid& h, const TerrainRecipe& r, float worldSize);
float scoreMetrics(const TerrainMetrics& m, const DirectorTargets& t, const TerrainRecipe& r);

// Full search. Returns the winning genome. Progress is reported in [p0,p1].
Genome directTerrain(const GenSettings& s, DirectorReport& report, Progress* progress, float p0, float p1);

}  // namespace zl
