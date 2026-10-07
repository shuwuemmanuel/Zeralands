// ZeraLands - landscape splines (roads, rails, paths, racetracks, canals...) and terrain stamping.
#pragma once

#include "core/Heightfield.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace zl {

struct SplinePoint {
    Vec3 pos;            // world meters (x, height, z)
    float width = 0.f;   // 0 = use spline width
};

struct SplineSample {
    Vec3 pos;
    Vec2 dir;            // horizontal tangent
    float width = 6.f;
    float bank = 0.f;    // radians, positive tilts the right side down
    float curvature = 0.f;   // 1/m, signed
    float distance = 0.f;    // along-curve meters
};

struct Spline {
    std::string name;
    SplineKind kind = SplineKind::Road;
    MaterialKind material = MaterialKind::Asphalt;
    bool closed = false;
    float width = 6.f;
    float shoulder = 4.f;
    float maxBank = 0.f;          // racetracks: max banking angle
    bool markings = false;
    bool elevated = false;        // bridges/maglev/aqueduct decks; no terrain stamping
    bool waterFilled = false;     // canals
    bool kerbs = false;
    bool conform = true;          // true: heights follow (smoothed) terrain when edited
    FoliageKind prop = FoliageKind::Count;
    float propSpacing = 50.f;
    std::vector<SplinePoint> points;

    // Centripetal Catmull-Rom evaluation at roughly `step` meter spacing.
    std::vector<SplineSample> sample(float step) const;
    float length() const;
};

// Re-derives control point heights from the terrain with grade limiting (used when conform is on).
void conformSplineHeights(Spline& s, const Terrain& t, float maxGrade, float smoothMeters);

// Stamps all splines into terrain.height from terrain.baseHeight; fills terrain.roadMask.
void stampSplines(Terrain& t, const std::vector<Spline>& splines);

// Nearest control point (index) to a world xz position within maxDist, or -1.
int pickControlPoint(const Spline& s, Vec2 worldXZ, float maxDist);
// Inserts a point on the closest segment; returns new index.
int insertControlPoint(Spline& s, Vec2 worldXZ, const Terrain& t);

}  // namespace zl
