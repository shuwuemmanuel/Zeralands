// ZeraLands - erosion models: droplet hydraulic, thermal, stream-power fluvial and aeolian smoothing.
#pragma once

#include "core/Heightfield.h"

namespace zl {

struct HydraulicParams {
    int droplets = 100000;
    int maxLifetime = 48;
    float inertia = 0.06f;
    float capacity = 5.f;
    float minCapacity = 0.01f;
    float erodeSpeed = 0.35f;
    float depositSpeed = 0.3f;
    float evaporate = 0.015f;
    float gravity = 4.f;
    int radius = 3;
    float heightScale = 1.f;   // height units per cell (for slope)
};

void hydraulicErosion(Grid& h, const HydraulicParams& p, uint64_t seed, Progress* progress = nullptr, float p0 = 0, float p1 = 1);
// talus in height-units per cell.
void thermalErosion(Grid& h, int iterations, float talus, float rate = 0.5f);
// Stream-power incision driven by D8 flow accumulation.
void fluvialErosion(Grid& h, int iterations, float strength);
// Tectonic uplift + implicit stream-power incision (Braun & Willett 2013) with talus-limited hillslopes.
// `uplift` (same size, 0..1) says where rock is pushed up; the result has real drainage networks:
// dendritic valleys, sharp ridgelines and graded rivers. Heights stay normalized.
void upliftStreamPower(Grid& h, const Grid& uplift, int iterations, float erodibility, float talus, const Grid* fixedMask = nullptr);
void windSmoothing(Grid& h, Vec2 windDir, float amount);

}  // namespace zl
