// ZeraLands - depression filling, flow routing, rivers, lakes and moisture.
#pragma once

#include "core/Heightfield.h"

namespace zl {

// Priority-flood (Barnes et al.) depression filling with a tiny epsilon gradient. Edges drain.
Grid fillDepressions(const Grid& h, float epsilon = 1e-6f);
// D8 flow accumulation (in cells) over a (filled) surface.
Grid flowAccumulation(const Grid& filled);

struct HydrologyParams {
    float seaLevel = -1.f;
    float lakeMinDepth = 0.004f;     // normalized
    int lakeMinCells = 40;
    float lakeMaxFraction = 0.04f;   // larger depressions are treated as dry basins (alluvial flats)
    float riverThreshold = 0.002f;   // fraction of all cells draining through a cell to become a river
    float riverDepth = 0.003f;       // normalized carve depth for river beds
    bool lakes = true;
    bool rivers = true;
    float baseMoisture = 0.5f;
};

// Computes t.water (surface heights or -1), t.flow, t.moisture. Carves river beds into t.height.
void computeHydrology(Terrain& t, const HydrologyParams& p, uint64_t seed, Grid* lavaMask);

}  // namespace zl
