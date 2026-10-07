// ZeraLands - landscape synthesis and the end-to-end generation pipeline.
#pragma once

#include "core/Director.h"
#include "core/Foliage.h"
#include "core/Materials.h"
#include "core/Spline.h"

namespace zl {

// Synthesizes a normalized (0..1) heightfield for a genome at any resolution. `lava` (optional) receives a
// 0..1 mask of molten areas. `preview` skips expensive erosion (used by the director's candidate search).
void synthesizeHeight(const Genome& g, int res, Grid& out, Grid* lava, bool preview);

struct Scene {
    GenSettings settings;
    Genome genome;
    DirectorReport report;
    Terrain terrain;
    std::vector<Spline> splines;
    std::vector<FoliageInstance> foliage;
    SplatMap splat;
    Atmosphere atmosphere;
    std::vector<std::string> log;
    // bumped whenever the corresponding data changes (renderer re-uploads)
    int heightVersion = 0, splatVersion = 0, foliageVersion = 0, splineVersion = 0;
    bool valid() const { return !terrain.height.empty(); }
};

// Runs director -> synthesis -> erosion -> hydrology -> networks -> splines -> foliage -> materials.
bool generateScene(const GenSettings& s, Scene& out, Progress* progress);

struct ImportOptions {
    float worldSize = 0.f;     // meters (0 = use settings)
    float heightRange = 0.f;   // meters for 0..1 (0 = environment default)
    float seaLevel = -1.f;     // normalized, <0 = none
    float erosion = 0.f;       // 0 keep the source untouched .. 1 full environment erosion
    bool normalize = true;
    bool keepResolution = false;
    std::string sourceLabel;   // shown in the report ("Imported foo.png", "GIS 46.0N 7.7E")
};

// Turns an existing heightfield (imported image, GIS rip, sculpt result) into a full scene using the
// environment/era in `s` for erosion, water, networks, foliage and materials.
bool buildSceneFromHeightmap(const Grid& src, const GenSettings& s, const ImportOptions& opt, Scene& out, Progress* progress);

// Re-applies all splines to the base terrain and refreshes road masks + splat (after spline edits).
void restampSplines(Scene& scene);

// Effective atmosphere for env + era.
Atmosphere blendAtmosphere(const Environment& env, const EraProfile& era);

}  // namespace zl
