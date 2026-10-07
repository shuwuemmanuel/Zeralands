// ZeraLands - historical / speculative eras that restyle networks, foliage, materials and atmosphere.
#pragma once

#include "core/Environments.h"

#include <string>
#include <utility>
#include <vector>

namespace zl {

// How one network layer (roads, rail, paths) is built in a given era.
struct NetworkStyle {
    bool available = true;
    std::string label;                 // e.g. "Roman roads", "Steam railway", "Caravan routes"
    SplineKind kind = SplineKind::Road;
    MaterialKind material = MaterialKind::PackedDirt;
    float width = 6.f;                 // meters
    float shoulder = 4.f;              // meters of blended falloff each side
    float maxGrade = 0.12f;            // rise/run allowed before cost explodes
    float slopeCost = 40.f;            // A* slope penalty multiplier
    float turnCost = 0.2f;             // A* turning penalty (higher -> straighter)
    float straightness = 0.3f;         // 0 winding .. 1 ruler straight (Roman)
    float smoothing = 0.5f;            // spline relaxation
    float waterCost = 30.f;            // crossing water (bridges) penalty
    bool markings = false;             // painted lane lines
    bool elevated = false;             // maglev / aqueduct decks above terrain
    bool waterFilled = false;          // canals
    FoliageKind prop = FoliageKind::Count;   // props placed along the spline (Count = none)
    float propSpacing = 40.f;
};

struct RaceStyle {
    std::string label;                 // "Grand Prix circuit", "Circus Maximus chariot track"...
    MaterialKind material = MaterialKind::Asphalt;
    float width = 14.f;
    bool oval = false;                 // stadium / hippodrome shape
    float banking = 0.f;               // max bank angle (radians) on tight corners
    float elevationFollow = 0.3f;      // 0 flat track .. 1 follows terrain
    bool kerbs = true;
    float lengthKm = 4.5f;
};

struct EraProfile {
    std::string id, name, blurb;
    NetworkStyle road, rail, path;
    RaceStyle race;
    float settlementDensity = 1.f;     // number of road network nodes multiplier
    float foliageDensity = 1.f;
    std::vector<std::pair<FoliageKind, FoliageKind>> foliageSwaps;
    std::vector<FoliageRule> extraFoliage;
    std::vector<std::pair<MaterialKind, MaterialKind>> materialSwaps;
    float craters = 0.f;               // bomb / impact craters added
    float farmTerraces = 0.f;          // agricultural terracing on gentle hills
    float quarries = 0.f;              // open-pit mines
    float volcanism = 0.f;             // extra lava / volcanic activity
    float moistureMul = 1.f;
    float scorch = 0.f;                // 0..1 burn / irradiation tint
    Atmosphere atmosphere;             // used as a tint over the environment's own sky
    float atmosphereBlend = 0.f;       // 0 keep environment sky .. 1 use era sky
};

const std::vector<EraProfile>& eras();
int findEra(const std::string& nameOrId);

}  // namespace zl
