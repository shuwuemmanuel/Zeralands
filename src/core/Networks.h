// ZeraLands - era-styled road, railway, path, canal and racetrack generation.
#pragma once

#include "core/Director.h"
#include "core/Spline.h"

namespace zl {

struct NetworkRequest {
    bool roads = true, rail = false, paths = true, racetrack = false;
    float roadDensity = 1.f, pathDensity = 1.f;
    uint64_t seed = 1;
};

// Builds splines (heights already conformed) for the requested networks.
void buildNetworks(const Terrain& t, const EraProfile& era, const NetworkRequest& req, std::vector<Spline>& out,
                   std::vector<std::string>& log, Progress* progress, float p0, float p1);

// Spline factory helpers used by the editor's spline tool.
Spline makeSplineForKind(SplineKind kind, const EraProfile& era);

}  // namespace zl
