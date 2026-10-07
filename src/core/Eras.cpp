#include "core/Eras.h"

#include <cctype>

namespace zl {

namespace {

using M = MaterialKind;
using F = FoliageKind;
using S = SplineKind;

NetworkStyle N(const char* label, S kind, M mat, float width, float shoulder, float maxGrade, float slopeCost, float turnCost,
               float straight, float smooth) {
    NetworkStyle n;
    n.label = label; n.kind = kind; n.material = mat; n.width = width; n.shoulder = shoulder; n.maxGrade = maxGrade;
    n.slopeCost = slopeCost; n.turnCost = turnCost; n.straightness = straight; n.smoothing = smooth;
    return n;
}

NetworkStyle none(const char* why) {
    NetworkStyle n;
    n.available = false;
    n.label = why;
    return n;
}

RaceStyle R(const char* label, M mat, float width, bool oval, float banking, float follow, bool kerbs, float km) {
    RaceStyle r;
    r.label = label; r.material = mat; r.width = width; r.oval = oval; r.banking = banking; r.elevationFollow = follow;
    r.kerbs = kerbs; r.lengthKm = km;
    return r;
}

Atmosphere A(Color3 top, Color3 hor, Color3 sun, Color3 water, float fog) {
    Atmosphere a;
    a.skyTop = top; a.skyHorizon = hor; a.sun = sun; a.water = water; a.fogDensity = fog;
    return a;
}

FoliageRule P(F k, float density, float slopeMax, float hMin = 0.f, float hMax = 1.f, float mMin = 0.f, float mMax = 1.f,
              float cluster = 0.5f, float sMin = 0.8f, float sMax = 1.25f) {
    FoliageRule r;
    r.kind = k; r.density = density; r.slopeMax = slopeMax; r.hMin = hMin; r.hMax = hMax;
    r.moistMin = mMin; r.moistMax = mMax; r.cluster = cluster; r.scaleMin = sMin; r.scaleMax = sMax;
    return r;
}

std::vector<EraProfile> build() {
    std::vector<EraProfile> v;

    {
        EraProfile e;
        e.id = "primordial"; e.name = "Before the Beginning of Time";
        e.blurb = "A raw, newborn world: molten fissures, bare rock and no life at all.";
        e.road = N("Molten fissure network", S::LavaChannel, M::Lava, 5.f, 6.f, 0.5f, 5.f, 0.05f, 0.0f, 0.2f);
        e.road.waterCost = 0.f;
        e.rail = none("No railways before time began");
        e.path = N("Cooling cracks", S::LavaChannel, M::Lava, 1.5f, 2.f, 1.f, 2.f, 0.f, 0.f, 0.1f);
        e.race = R("Primeval obsidian rally loop", M::Basalt, 12.f, false, 0.05f, 0.8f, false, 3.5f);
        e.foliageDensity = 0.f;
        e.materialSwaps = {{M::Grass, M::Basalt}, {M::LushGrass, M::Basalt}, {M::DryGrass, M::Ash}, {M::ForestFloor, M::Ash},
                           {M::Moss, M::Basalt}, {M::Dirt, M::Ash}, {M::Mud, M::Lava}, {M::Sand, M::Ash}, {M::Snow, M::Ash}};
        e.extraFoliage = {P(F::Crystal, 0.5f, 1.f, 0, 1, 0, 1, 0.9f), P(F::Boulder, 3.f, 1.f)};
        e.volcanism = 0.8f; e.craters = 6.f; e.moistureMul = 0.1f; e.settlementDensity = 0.6f; e.scorch = 0.5f;
        e.atmosphere = A({0.20f, 0.06f, 0.05f}, {0.80f, 0.35f, 0.15f}, {1.f, 0.6f, 0.3f}, {0.15f, 0.08f, 0.05f}, 0.8f);
        e.atmosphereBlend = 0.85f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "dinosaur"; e.name = "Dinosaur Age (Mesozoic)";
        e.blurb = "Cycads, tree ferns and giant conifers; herds carve broad migration trails.";
        e.road = N("Migration trails", S::Path, M::PackedDirt, 9.f, 6.f, 0.35f, 10.f, 0.05f, 0.f, 0.3f);
        e.rail = none("No railways in the Mesozoic");
        e.path = N("Game trails", S::Path, M::PackedDirt, 2.f, 2.f, 0.6f, 6.f, 0.f, 0.f, 0.2f);
        e.race = R("Primeval dirt circuit", M::PackedDirt, 14.f, false, 0.08f, 0.7f, false, 3.f);
        e.foliageDensity = 1.4f;
        e.foliageSwaps = {{F::Broadleaf, F::TreeFern}, {F::Birch, F::Cycad}, {F::Palm, F::Cycad}, {F::GrassTuft, F::Fern},
                          {F::Flowers, F::Fern}, {F::Acacia, F::Cycad}, {F::Shrub, F::Fern}};
        e.extraFoliage = {P(F::Conifer, 10.f, 0.5f, 0, 0.7f, 0.4f, 1, 0.7f, 1.6f, 2.4f), P(F::GiantMushroom, 0.5f, 0.4f, 0, 0.6f, 0.6f, 1, 0.8f)};
        e.materialSwaps = {{M::Grass, M::LushGrass}};
        e.volcanism = 0.3f; e.moistureMul = 1.3f; e.settlementDensity = 0.8f;
        e.atmosphere = A({0.30f, 0.50f, 0.65f}, {0.80f, 0.82f, 0.70f}, {1.f, 0.92f, 0.75f}, {0.10f, 0.30f, 0.25f}, 0.55f);
        e.atmosphereBlend = 0.4f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "antediluvian"; e.name = "Before the Flood";
        e.blurb = "An overgrown, rain-fed antediluvian world of giant trees and ancient stone ways.";
        e.road = N("Ancient stone ways", S::Road, M::RomanStone, 5.f, 3.f, 0.15f, 30.f, 0.15f, 0.4f, 0.5f);
        e.rail = N("Great irrigation canals", S::Canal, M::Mud, 8.f, 5.f, 0.01f, 200.f, 0.3f, 0.3f, 0.7f);
        e.rail.waterFilled = true;
        e.path = N("Footpaths", S::Path, M::PackedDirt, 1.6f, 1.5f, 0.4f, 8.f, 0.02f, 0.f, 0.3f);
        e.race = R("Chariot field", M::PackedDirt, 16.f, true, 0.f, 0.f, false, 1.5f);
        e.foliageDensity = 1.5f; e.moistureMul = 1.4f; e.settlementDensity = 0.8f;
        e.extraFoliage = {P(F::Baobab, 1.f, 0.3f, 0, 0.7f, 0.4f, 1, 0.6f, 1.4f, 2.f)};
        e.foliageSwaps = {{F::Broadleaf, F::JungleTree}};
        e.atmosphere = A({0.35f, 0.55f, 0.75f}, {0.85f, 0.88f, 0.80f}, {1.f, 0.95f, 0.82f}, {0.10f, 0.32f, 0.30f}, 0.5f);
        e.atmosphereBlend = 0.3f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "ancient_middle_east"; e.name = "Ancient Middle East";
        e.blurb = "Caravan routes, qanat irrigation channels and date-palm terraces along the rivers.";
        e.road = N("Caravan routes", S::Road, M::PackedDirt, 6.f, 4.f, 0.18f, 18.f, 0.08f, 0.2f, 0.5f);
        e.rail = N("Qanats & irrigation canals", S::Canal, M::Mud, 4.f, 3.f, 0.01f, 250.f, 0.3f, 0.4f, 0.6f);
        e.rail.waterFilled = true;
        e.path = N("Shepherd tracks", S::Path, M::PackedDirt, 1.5f, 1.5f, 0.45f, 6.f, 0.02f, 0.f, 0.3f);
        e.road.prop = F::Milestone; e.road.propSpacing = 400.f;
        e.race = R("Camel racing oval", M::Sand, 18.f, true, 0.f, 0.f, false, 2.f);
        e.farmTerraces = 0.5f; e.foliageDensity = 0.9f; e.moistureMul = 0.8f;
        e.extraFoliage = {P(F::Palm, 6.f, 0.2f, 0, 0.6f, 0.55f, 1, 0.8f)};
        e.atmosphere = A({0.38f, 0.55f, 0.82f}, {0.92f, 0.84f, 0.68f}, {1.f, 0.9f, 0.72f}, {0.12f, 0.32f, 0.35f}, 0.45f);
        e.atmosphereBlend = 0.35f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "roman"; e.name = "Roman Empire";
        e.blurb = "Ruler-straight paved roads, stone aqueducts on arches and a chariot circus.";
        e.road = N("Roman roads (viae)", S::Road, M::RomanStone, 6.f, 3.f, 0.10f, 25.f, 1.2f, 0.95f, 0.15f);
        e.road.prop = F::Milestone; e.road.propSpacing = 300.f;
        e.rail = N("Aqueducts", S::Aqueduct, M::RomanStone, 3.f, 1.f, 0.004f, 300.f, 1.f, 0.9f, 0.3f);
        e.rail.elevated = true; e.rail.prop = F::Column; e.rail.propSpacing = 18.f;
        e.path = N("Shepherd paths", S::Path, M::PackedDirt, 1.6f, 1.5f, 0.4f, 8.f, 0.02f, 0.f, 0.3f);
        e.race = R("Circus chariot track", M::Sand, 22.f, true, 0.f, 0.f, false, 1.4f);
        e.farmTerraces = 0.35f; e.settlementDensity = 1.2f;
        e.extraFoliage = {P(F::Column, 0.05f, 0.2f, 0, 0.7f, 0, 1, 0.95f)};
        e.atmosphere = A({0.30f, 0.52f, 0.85f}, {0.85f, 0.85f, 0.80f}, {1.f, 0.94f, 0.80f}, {0.08f, 0.32f, 0.40f}, 0.35f);
        e.atmosphereBlend = 0.2f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "medieval"; e.name = "Medieval";
        e.blurb = "Winding cart tracks, cobbled market roads, canals with towpaths and a jousting ground.";
        e.road = N("Cart roads", S::Road, M::Cobblestone, 4.5f, 3.f, 0.16f, 22.f, 0.05f, 0.05f, 0.6f);
        e.road.prop = F::Torch; e.road.propSpacing = 120.f;
        e.rail = N("Canals & towpaths", S::Canal, M::Mud, 8.f, 4.f, 0.006f, 250.f, 0.4f, 0.4f, 0.6f);
        e.rail.waterFilled = true;
        e.path = N("Footpaths", S::Path, M::PackedDirt, 1.4f, 1.5f, 0.45f, 7.f, 0.f, 0.f, 0.3f);
        e.race = R("Jousting & horse-racing ground", M::PackedDirt, 16.f, true, 0.f, 0.1f, false, 1.8f);
        e.farmTerraces = 0.25f; e.foliageDensity = 1.15f; e.settlementDensity = 1.1f;
        e.atmosphere = A({0.36f, 0.50f, 0.72f}, {0.80f, 0.82f, 0.82f}, {1.f, 0.93f, 0.80f}, {0.10f, 0.25f, 0.30f}, 0.45f);
        e.atmosphereBlend = 0.25f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "steampunk"; e.name = "Steampunk (Victorian Industrial)";
        e.blurb = "Coal-smoke skies, cobbled turnpikes, steam railways with telegraph lines and open quarries.";
        e.road = N("Turnpike roads", S::Road, M::Cobblestone, 6.f, 3.f, 0.12f, 28.f, 0.2f, 0.4f, 0.5f);
        e.road.prop = F::StreetLamp; e.road.propSpacing = 90.f;
        e.rail = N("Steam railway", S::Rail, M::RailBed, 5.f, 6.f, 0.025f, 160.f, 0.9f, 0.6f, 0.8f);
        e.rail.prop = F::TelegraphPole; e.rail.propSpacing = 55.f;
        e.path = N("Miners' tracks", S::Path, M::Gravel, 2.f, 1.5f, 0.35f, 8.f, 0.02f, 0.f, 0.3f);
        e.race = R("Velocipede & steam-car circuit", M::Cobblestone, 12.f, false, 0.05f, 0.4f, true, 3.f);
        e.quarries = 2.f; e.foliageDensity = 0.75f; e.settlementDensity = 1.2f; e.scorch = 0.15f;
        e.atmosphere = A({0.42f, 0.42f, 0.42f}, {0.75f, 0.68f, 0.55f}, {1.f, 0.82f, 0.60f}, {0.12f, 0.18f, 0.18f}, 0.75f);
        e.atmosphereBlend = 0.6f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "1970s"; e.name = "1970s";
        e.blurb = "Two-lane blacktop, diesel railways, telegraph poles and a fast, dangerous road circuit.";
        e.road = N("Two-lane highways", S::Road, M::Asphalt, 8.f, 4.f, 0.08f, 35.f, 0.35f, 0.5f, 0.7f);
        e.road.markings = true; e.road.prop = F::TelegraphPole; e.road.propSpacing = 60.f;
        e.rail = N("Diesel railway", S::Rail, M::RailBed, 5.f, 6.f, 0.02f, 180.f, 1.f, 0.6f, 0.85f);
        e.path = N("Hiking trails", S::Path, M::PackedDirt, 1.5f, 1.5f, 0.4f, 6.f, 0.f, 0.f, 0.3f);
        e.race = R("Classic road circuit", M::Asphalt, 12.f, false, 0.06f, 0.6f, true, 6.f);
        e.atmosphere = A({0.32f, 0.50f, 0.78f}, {0.86f, 0.80f, 0.68f}, {1.f, 0.90f, 0.75f}, {0.10f, 0.28f, 0.33f}, 0.4f);
        e.atmosphereBlend = 0.25f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "present"; e.name = "Present Day";
        e.blurb = "Graded highways with lane markings and street lighting, electric rail and a Grand Prix circuit.";
        e.road = N("Highways & country roads", S::Road, M::Asphalt, 9.f, 5.f, 0.07f, 40.f, 0.4f, 0.55f, 0.8f);
        e.road.markings = true; e.road.prop = F::StreetLamp; e.road.propSpacing = 70.f;
        e.rail = N("Electric railway", S::Rail, M::RailBed, 6.f, 7.f, 0.02f, 200.f, 1.2f, 0.7f, 0.9f);
        e.rail.prop = F::TelegraphPole; e.rail.propSpacing = 60.f;
        e.path = N("Hiking trails", S::Path, M::Gravel, 1.6f, 1.5f, 0.4f, 6.f, 0.f, 0.f, 0.3f);
        e.race = R("Grand Prix circuit", M::Asphalt, 14.f, false, 0.08f, 0.35f, true, 5.f);
        e.atmosphere = A({0.28f, 0.48f, 0.82f}, {0.75f, 0.82f, 0.90f}, {1.f, 0.95f, 0.85f}, {0.10f, 0.28f, 0.35f}, 0.3f);
        e.atmosphereBlend = 0.f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "2026"; e.name = "2026";
        e.blurb = "Smart highways, high-speed rail, ridge-line wind farms and a modern circuit with runoff zones.";
        e.road = N("Smart highways", S::Road, M::Asphalt, 10.f, 6.f, 0.06f, 45.f, 0.5f, 0.6f, 0.85f);
        e.road.markings = true; e.road.prop = F::StreetLamp; e.road.propSpacing = 60.f;
        e.rail = N("High-speed rail", S::Rail, M::Concrete, 7.f, 8.f, 0.03f, 220.f, 2.f, 0.85f, 0.95f);
        e.path = N("Cycle & hiking trails", S::Path, M::Concrete, 2.5f, 1.5f, 0.25f, 8.f, 0.05f, 0.1f, 0.4f);
        e.race = R("Modern circuit with runoff", M::Asphalt, 15.f, false, 0.1f, 0.3f, true, 5.3f);
        e.extraFoliage = {P(F::WindTurbine, 0.04f, 0.15f, 0.45f, 1, 0, 1, 0.9f, 0.9f, 1.1f)};
        e.atmosphere = A({0.27f, 0.47f, 0.82f}, {0.74f, 0.82f, 0.90f}, {1.f, 0.95f, 0.86f}, {0.09f, 0.28f, 0.36f}, 0.28f);
        e.atmosphereBlend = 0.f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "futuristic"; e.name = "Futuristic";
        e.blurb = "Glowing guideways, elevated maglev lines on pylons and a banked anti-grav race circuit.";
        e.road = N("Glow guideways", S::Road, M::GlowPanel, 10.f, 6.f, 0.1f, 30.f, 0.6f, 0.7f, 0.95f);
        e.road.markings = true; e.road.prop = F::NeonPylon; e.road.propSpacing = 80.f;
        e.rail = N("Maglev lines", S::Maglev, M::Concrete, 6.f, 1.f, 0.08f, 20.f, 3.f, 0.95f, 0.95f);
        e.rail.elevated = true; e.rail.waterCost = 1.f; e.rail.prop = F::Column; e.rail.propSpacing = 60.f;
        e.path = N("Light trails", S::Path, M::GlowPanel, 2.f, 1.f, 0.3f, 6.f, 0.05f, 0.1f, 0.5f);
        e.race = R("Anti-grav circuit", M::GlowPanel, 18.f, false, 0.45f, 0.9f, true, 7.f);
        e.foliageDensity = 0.8f;
        e.atmosphere = A({0.12f, 0.16f, 0.35f}, {0.55f, 0.55f, 0.80f}, {0.90f, 0.92f, 1.f}, {0.05f, 0.25f, 0.40f}, 0.35f);
        e.atmosphereBlend = 0.45f;
        v.push_back(e);
    }
    {
        EraProfile e;
        e.id = "atomic"; e.name = "Atomic Wasteland";
        e.blurb = "A post-nuclear landscape: blast craters, scorched earth, dead forests, ruins and cracked roads.";
        e.road = N("Cracked highways", S::Road, M::Asphalt, 8.f, 4.f, 0.08f, 35.f, 0.35f, 0.5f, 0.7f);
        e.road.markings = true; e.road.prop = F::Ruin; e.road.propSpacing = 220.f;
        e.rail = N("Derelict railway", S::Rail, M::RailBed, 5.f, 5.f, 0.02f, 180.f, 1.f, 0.6f, 0.85f);
        e.rail.prop = F::TelegraphPole; e.rail.propSpacing = 90.f;
        e.path = N("Scavenger trails", S::Path, M::Ash, 1.6f, 1.5f, 0.45f, 6.f, 0.f, 0.f, 0.3f);
        e.race = R("Wasteland rally loop", M::PackedDirt, 14.f, false, 0.05f, 0.8f, false, 4.f);
        e.craters = 9.f; e.foliageDensity = 0.45f; e.moistureMul = 0.6f; e.scorch = 0.85f; e.settlementDensity = 0.9f;
        e.foliageSwaps = {{F::Broadleaf, F::DeadTree}, {F::Birch, F::DeadTree}, {F::Conifer, F::DeadTree}, {F::JungleTree, F::DeadTree},
                          {F::Palm, F::CharredStump}, {F::Flowers, F::SmallRocks}, {F::Fern, F::CharredStump}, {F::Acacia, F::DeadTree}};
        e.extraFoliage = {P(F::Ruin, 0.15f, 0.25f, 0, 0.8f, 0, 1, 0.9f), P(F::CharredStump, 4.f, 0.5f)};
        e.materialSwaps = {{M::Grass, M::DryGrass}, {M::LushGrass, M::DryGrass}, {M::ForestFloor, M::Ash}, {M::Moss, M::Ash}};
        e.atmosphere = A({0.38f, 0.36f, 0.28f}, {0.72f, 0.62f, 0.42f}, {1.f, 0.85f, 0.55f}, {0.20f, 0.25f, 0.12f}, 0.7f);
        e.atmosphereBlend = 0.75f;
        v.push_back(e);
    }
    return v;
}

std::string simplify(const std::string& s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

const std::vector<EraProfile>& eras() {
    static const std::vector<EraProfile> kEras = build();
    return kEras;
}

int findEra(const std::string& query) {
    const auto& list = eras();
    std::string q = simplify(query);
    if (q.empty()) return -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (simplify(list[i].id) == q || simplify(list[i].name) == q) return int(i);
    for (size_t i = 0; i < list.size(); ++i)
        if (simplify(list[i].name).find(q) != std::string::npos || simplify(list[i].id).find(q) != std::string::npos) return int(i);
    return -1;
}

}  // namespace zl
