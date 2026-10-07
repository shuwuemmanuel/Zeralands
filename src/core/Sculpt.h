// ZeraLands - landscape sculpt brushes (works on generated, imported and GIS terrains alike).
#pragma once

#include "core/Heightfield.h"

namespace zl {

enum class SculptTool : int { Raise, Lower, Smooth, Flatten, Noise, Erode, Terrace, Count };
const char* sculptToolName(SculptTool t);

struct SculptBrush {
    SculptTool tool = SculptTool::Raise;
    float radius = 60.f;          // meters
    float strength = 0.5f;        // 0..1
    float falloff = 0.6f;         // 0 hard .. 1 soft
    float flattenHeight = -1.f;   // meters; <0 = sampled at stroke start by the caller
    float terraceStep = 8.f;      // meters
};

// Applies one dab (dt seconds of brush time). Edits baseHeight and mirrors the delta into height.
// Returns the touched grid rectangle (x0,y0,x1,y1) via out params.
void sculptDab(Terrain& t, Vec2 centerXZ, const SculptBrush& b, float dt, uint64_t seed, int& x0, int& y0, int& x1, int& y1);

}  // namespace zl
