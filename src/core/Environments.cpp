#include "core/Environments.h"

#include <cctype>

namespace zl {

namespace {

using M = MaterialKind;
using F = FoliageKind;

LayerRule L(M k, float hMin, float hMax, float sMin, float sMax, float mMin = 0.f, float mMax = 1.f, float noise = 0.3f, float w = 1.f) {
    LayerRule r;
    r.kind = k;
    r.hMin = hMin; r.hMax = hMax; r.slopeMin = sMin; r.slopeMax = sMax;
    r.moistMin = mMin; r.moistMax = mMax; r.noise = noise; r.weight = w;
    return r;
}

FoliageRule P(F k, float density, float slopeMax, float hMin = 0.f, float hMax = 1.f, float mMin = 0.f, float mMax = 1.f,
              float cluster = 0.5f, float sMin = 0.8f, float sMax = 1.25f) {
    FoliageRule r;
    r.kind = k; r.density = density; r.slopeMax = slopeMax; r.hMin = hMin; r.hMax = hMax;
    r.moistMin = mMin; r.moistMax = mMax; r.cluster = cluster; r.scaleMin = sMin; r.scaleMax = sMax;
    return r;
}

Atmosphere A(Color3 top, Color3 hor, Color3 sun, Color3 water, float fog) {
    Atmosphere a;
    a.skyTop = top; a.skyHorizon = hor; a.sun = sun; a.water = water; a.fogDensity = fog;
    return a;
}

const Atmosphere kClear = A({0.28f, 0.48f, 0.82f}, {0.72f, 0.82f, 0.92f}, {1.f, 0.95f, 0.85f}, {0.10f, 0.28f, 0.35f}, 0.30f);
const Atmosphere kDesertSky = A({0.35f, 0.55f, 0.85f}, {0.92f, 0.85f, 0.72f}, {1.f, 0.92f, 0.78f}, {0.15f, 0.35f, 0.38f}, 0.40f);
const Atmosphere kColdSky = A({0.42f, 0.55f, 0.75f}, {0.85f, 0.88f, 0.92f}, {0.95f, 0.95f, 1.f}, {0.10f, 0.20f, 0.28f}, 0.45f);
const Atmosphere kTropicSky = A({0.20f, 0.50f, 0.88f}, {0.70f, 0.85f, 0.92f}, {1.f, 0.96f, 0.86f}, {0.05f, 0.45f, 0.50f}, 0.35f);
const Atmosphere kVolcanicSky = A({0.25f, 0.18f, 0.18f}, {0.70f, 0.42f, 0.30f}, {1.f, 0.70f, 0.45f}, {0.12f, 0.15f, 0.15f}, 0.65f);
const Atmosphere kAlienSky = A({0.18f, 0.12f, 0.35f}, {0.65f, 0.45f, 0.70f}, {0.90f, 0.85f, 1.f}, {0.20f, 0.10f, 0.35f}, 0.40f);
const Atmosphere kSwampSky = A({0.38f, 0.48f, 0.50f}, {0.68f, 0.72f, 0.66f}, {0.95f, 0.92f, 0.80f}, {0.14f, 0.18f, 0.10f}, 0.65f);

// Shared layer stacks --------------------------------------------------------
std::vector<LayerRule> temperateLayers(float snowLine = 0.86f) {
    return {
        L(M::Grass, 0.f, 0.80f, 0.f, 0.42f, 0.25f, 0.85f, 0.35f),
        L(M::ForestFloor, 0.f, 0.72f, 0.f, 0.55f, 0.6f, 1.f, 0.45f, 0.9f),
        L(M::Dirt, 0.f, 0.85f, 0.28f, 0.75f, 0.f, 1.f, 0.4f, 0.8f),
        L(M::Gravel, 0.f, 1.f, 0.0f, 0.6f, 0.0f, 0.25f, 0.5f, 0.6f),
        L(M::Rock, 0.f, 1.f, 0.55f, 1.2f, 0.f, 1.f, 0.3f),
        L(M::Cliff, 0.f, 1.f, 1.0f, 99.f, 0.f, 1.f, 0.2f, 1.2f),
        L(M::Snow, snowLine, 1.f, 0.f, 0.95f, 0.f, 1.f, 0.25f, 1.4f),
        L(M::Sand, 0.f, 0.02f, 0.f, 0.3f, 0.f, 1.f, 0.2f, 0.6f),
    };
}

std::vector<Environment> build() {
    std::vector<Environment> v;
    auto add = [&](const char* id, const char* name, const char* cat, const char* desc) -> Environment& {
        Environment e;
        e.id = id; e.name = name; e.category = cat; e.description = desc;
        e.atmosphere = kClear;
        e.layers = temperateLayers();
        v.push_back(e);
        return v.back();
    };

    // ===================================================================== ARID
    {
        auto& e = add("desert_erg", "Sand Dune Sea (Erg)", "Arid", "Endless wind-sculpted dunes with sharp slip faces.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 5; r.wFbm = 0.8f; r.wRidged = 0.f; r.wEroded = 0.2f; r.warp = 0.35f;
        r.relief = 0.35f; r.heightRange = 260.f; r.ranges = 0.f; r.dunes = 1.f; r.duneScale = 1.f;
        r.rivers = 0.f; r.hydraulic = 0.f; r.thermal = 0.15f; r.talus = 0.65f; r.wind = 0.6f; r.moisture = 0.03f; r.temperature = 0.95f;
        e.targets = {0.35f, 0.18f, 0.f, 0.35f, 0.4f};
        e.layers = {L(M::Sand, 0, 1, 0, 0.55f, 0, 1, 0.25f), L(M::RedSand, 0, 1, 0, 0.55f, 0, 1, 0.6f, 0.45f),
                    L(M::Rock, 0, 1, 0.7f, 99, 0, 1, 0.3f), L(M::Gravel, 0, 0.18f, 0, 0.3f, 0, 1, 0.6f, 0.35f)};
        e.foliage = {P(F::DesertShrub, 1.5f, 0.3f, 0, 0.35f, 0, 1, 0.8f), P(F::SmallRocks, 2.f, 0.6f, 0, 1, 0, 1, 0.7f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("desert_hamada", "Rocky Desert (Hamada)", "Arid", "Bare bedrock plateaus, gravel pans and wadis.");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 7; r.wFbm = 0.4f; r.wRidged = 0.3f; r.wEroded = 0.3f; r.warp = 0.2f;
        r.relief = 0.55f; r.heightRange = 380.f; r.ranges = 1.f; r.rangeStrength = 0.3f; r.mesas = 0.35f; r.terraces = 6;
        r.terraceStrength = 0.25f; r.rivers = 0.25f; r.hydraulic = 0.25f; r.thermal = 0.45f; r.talus = 0.75f; r.wind = 0.2f;
        r.moisture = 0.08f; r.temperature = 0.9f;
        e.targets = {0.5f, 0.3f, 0.f, 0.35f, 0.55f};
        e.layers = {L(M::Gravel, 0, 1, 0, 0.35f, 0, 1, 0.4f), L(M::Sand, 0, 0.45f, 0, 0.2f, 0, 1, 0.5f, 0.7f),
                    L(M::Rock, 0, 1, 0.3f, 99, 0, 1, 0.3f), L(M::Sandstone, 0, 1, 0.8f, 99, 0, 1, 0.4f),
                    L(M::RedSand, 0, 0.3f, 0, 0.2f, 0, 1, 0.6f, 0.4f), L(M::Pebbles, 0, 1, 0, 0.15f, 0.1f, 1, 0.5f, 0.4f)};
        e.foliage = {P(F::DesertShrub, 3.f, 0.35f, 0, 0.6f, 0.05f, 1, 0.6f), P(F::Boulder, 2.f, 1.f, 0, 1, 0, 1, 0.7f),
                     P(F::SmallRocks, 8.f, 0.8f, 0, 1, 0, 1, 0.5f), P(F::Cactus, 0.4f, 0.3f, 0, 0.5f, 0.1f, 1, 0.5f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("badlands", "Badlands", "Arid", "Densely gullied clay hills with striped sediment bands.");
        auto& r = e.recipe;
        r.baseFreq = 4.f; r.octaves = 8; r.wFbm = 0.2f; r.wRidged = 0.5f; r.wEroded = 0.3f; r.warp = 0.3f;
        r.relief = 0.6f; r.heightRange = 240.f; r.terraces = 14; r.terraceStrength = 0.35f; r.rivers = 0.8f; r.hydraulic = 0.9f;
        r.thermal = 0.2f; r.talus = 0.9f; r.moisture = 0.12f; r.temperature = 0.75f; r.ranges = 0.f;
        e.targets = {0.5f, 0.55f, 0.f, 0.15f, 0.8f};
        e.layers = {L(M::RedSand, 0, 1, 0, 0.4f, 0, 1, 0.5f), L(M::Sandstone, 0, 1, 0.3f, 99, 0, 1, 0.5f),
                    L(M::Dirt, 0, 0.4f, 0, 0.3f, 0, 1, 0.5f, 0.6f), L(M::DryGrass, 0, 0.5f, 0, 0.18f, 0.1f, 1, 0.6f, 0.5f),
                    L(M::Gravel, 0, 0.25f, 0, 0.2f, 0.2f, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::DesertShrub, 4.f, 0.3f, 0, 0.6f, 0, 1, 0.6f), P(F::GrassTuft, 10.f, 0.2f, 0, 0.5f, 0.1f, 1, 0.7f),
                     P(F::DeadTree, 0.3f, 0.3f, 0, 0.4f, 0.2f, 1, 0.5f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("mesa_canyonlands", "Mesas & Canyonlands", "Arid", "Flat-topped mesas, buttes and deep sandstone canyons.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.2f; r.wEroded = 0.3f; r.warp = 0.35f;
        r.relief = 0.8f; r.heightRange = 520.f; r.mesas = 0.9f; r.terraces = 5; r.terraceStrength = 0.5f; r.canyons = 0.6f;
        r.rivers = 0.5f; r.hydraulic = 0.35f; r.thermal = 0.2f; r.talus = 1.4f; r.moisture = 0.1f; r.temperature = 0.85f; r.ranges = 0.f;
        e.targets = {0.6f, 0.45f, 0.f, 0.3f, 0.7f};
        e.layers = {L(M::RedSand, 0, 1, 0, 0.25f, 0, 1, 0.4f), L(M::Sandstone, 0, 1, 0.45f, 99, 0, 1, 0.4f, 1.2f),
                    L(M::Gravel, 0, 0.4f, 0.1f, 0.5f, 0, 1, 0.5f, 0.6f), L(M::DryGrass, 0.5f, 1, 0, 0.15f, 0, 1, 0.6f, 0.4f),
                    L(M::Cliff, 0, 1, 1.4f, 99, 0, 1, 0.3f, 0.7f)};
        e.foliage = {P(F::DesertShrub, 3.f, 0.25f, 0, 1, 0, 1, 0.6f), P(F::Boulder, 1.5f, 0.6f, 0, 0.5f, 0, 1, 0.8f),
                     P(F::Cactus, 0.3f, 0.2f, 0, 0.5f, 0, 1, 0.4f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("salt_flats", "Salt Flats", "Arid", "Blinding white playa ringed by low eroded hills.");
        auto& r = e.recipe;
        r.baseFreq = 1.5f; r.octaves = 6; r.wFbm = 0.6f; r.wRidged = 0.2f; r.wEroded = 0.2f; r.relief = 0.5f; r.heightRange = 220.f;
        r.flatten = 0.75f; r.basins = 1.f; r.hydraulic = 0.3f; r.thermal = 0.3f; r.moisture = 0.05f; r.temperature = 0.85f; r.ranges = 1.f;
        r.rangeStrength = 0.3f; r.rivers = 0.1f;
        e.targets = {0.3f, 0.08f, 0.f, 0.75f, 0.3f};
        e.layers = {L(M::Salt, 0, 0.18f, 0, 0.08f, 0, 1, 0.3f, 1.5f), L(M::Sand, 0.1f, 0.5f, 0, 0.3f, 0, 1, 0.4f),
                    L(M::Gravel, 0.2f, 1, 0, 0.5f, 0, 1, 0.4f), L(M::Rock, 0, 1, 0.45f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::DesertShrub, 0.8f, 0.3f, 0.2f, 0.7f, 0, 1, 0.6f), P(F::SmallRocks, 2.f, 0.6f, 0.2f, 1, 0, 1, 0.6f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("desert_oasis", "Desert Oasis", "Arid", "Dunes and rock surrounding a spring-fed palm basin.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 6; r.wFbm = 0.7f; r.wRidged = 0.1f; r.wEroded = 0.2f; r.warp = 0.3f; r.relief = 0.45f;
        r.heightRange = 260.f; r.dunes = 0.6f; r.basins = 1.5f; r.rivers = 0.15f; r.hydraulic = 0.1f; r.thermal = 0.2f; r.wind = 0.4f;
        r.moisture = 0.15f; r.temperature = 0.9f; r.ranges = 0.f;
        e.targets = {0.4f, 0.2f, 0.f, 0.4f, 0.45f};
        e.layers = {L(M::Sand, 0, 1, 0, 0.55f, 0, 0.45f, 0.3f), L(M::LushGrass, 0, 1, 0, 0.3f, 0.5f, 1, 0.4f, 1.3f),
                    L(M::Mud, 0, 1, 0, 0.2f, 0.75f, 1, 0.4f, 1.0f), L(M::Rock, 0, 1, 0.6f, 99, 0, 1, 0.3f),
                    L(M::Sandstone, 0, 1, 0.9f, 99, 0, 1, 0.4f)};
        e.foliage = {P(F::Palm, 25.f, 0.3f, 0, 1, 0.5f, 1, 0.6f), P(F::Reeds, 60.f, 0.2f, 0, 1, 0.75f, 1, 0.7f),
                     P(F::DesertShrub, 1.5f, 0.3f, 0, 1, 0.1f, 0.5f, 0.7f), P(F::Shrub, 8.f, 0.3f, 0, 1, 0.45f, 1, 0.6f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("red_rock", "Red Rock Desert", "Arid", "Rust-red arches, hoodoos and fins under a hard sun.");
        auto& r = e.recipe;
        r.baseFreq = 2.6f; r.octaves = 7; r.wFbm = 0.2f; r.wRidged = 0.5f; r.wEroded = 0.3f; r.warp = 0.4f; r.relief = 0.7f;
        r.heightRange = 420.f; r.terraces = 9; r.terraceStrength = 0.45f; r.mesas = 0.4f; r.karst = 0.25f; r.rivers = 0.35f;
        r.hydraulic = 0.3f; r.thermal = 0.2f; r.talus = 1.2f; r.moisture = 0.08f; r.temperature = 0.85f;
        e.targets = {0.55f, 0.5f, 0.f, 0.25f, 0.75f};
        e.layers = {L(M::RedSand, 0, 1, 0, 0.3f, 0, 1, 0.4f), L(M::Sandstone, 0, 1, 0.3f, 99, 0, 1, 0.4f, 1.2f),
                    L(M::Gravel, 0, 1, 0, 0.25f, 0.15f, 1, 0.5f, 0.5f), L(M::Rock, 0, 1, 1.0f, 99, 0, 1, 0.4f, 0.4f)};
        e.foliage = {P(F::DesertShrub, 2.f, 0.3f), P(F::Boulder, 1.5f, 0.8f, 0, 1, 0, 1, 0.8f), P(F::Cactus, 0.3f, 0.2f)};
        e.atmosphere = kDesertSky;
    }

    // ================================================================= MOUNTAIN
    {
        auto& e = add("mountains_alpine", "Alpine Mountains", "Mountain", "Sharp glaciated peaks, snowfields and forested valleys.");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 9; r.wFbm = 0.15f; r.wRidged = 0.55f; r.wEroded = 0.3f; r.warp = 0.25f; r.relief = 1.f;
        r.heightRange = 2400.f; r.ranges = 2.f; r.rangeStrength = 0.6f; r.basins = 0.6f; r.glacial = 0.35f; r.rivers = 0.6f;
        r.hydraulic = 0.9f; r.thermal = 0.4f; r.talus = 1.1f; r.moisture = 0.6f; r.temperature = 0.3f;
        e.targets = {0.8f, 0.7f, 0.f, 0.12f, 0.75f};
        e.layers = temperateLayers(0.62f);
        e.foliage = {P(F::Conifer, 60.f, 0.6f, 0, 0.55f, 0.35f, 1, 0.7f, 0.7f, 1.4f), P(F::Birch, 6.f, 0.45f, 0, 0.35f, 0.4f, 1, 0.6f),
                     P(F::Shrub, 20.f, 0.7f, 0.2f, 0.65f, 0.2f, 1, 0.5f), P(F::Boulder, 4.f, 1.2f, 0.3f, 1, 0, 1, 0.7f),
                     P(F::GrassTuft, 80.f, 0.5f, 0.3f, 0.62f, 0.2f, 1, 0.5f), P(F::Flowers, 15.f, 0.35f, 0.35f, 0.6f, 0.3f, 1, 0.8f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("rolling_hills", "Rolling Hills", "Mountain", "Soft, rounded grassy hills with shallow valleys.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 6; r.wFbm = 0.6f; r.wRidged = 0.f; r.wBillow = 0.2f; r.wEroded = 0.2f; r.warp = 0.3f;
        r.relief = 0.55f; r.heightRange = 260.f; r.ranges = 0.f; r.basins = 0.5f; r.rivers = 0.4f; r.hydraulic = 0.4f;
        r.thermal = 0.6f; r.talus = 0.45f; r.moisture = 0.6f; r.temperature = 0.55f;
        e.targets = {0.45f, 0.2f, 0.f, 0.45f, 0.45f};
        e.foliage = {P(F::Broadleaf, 12.f, 0.4f, 0, 1, 0.5f, 1, 0.85f), P(F::Shrub, 15.f, 0.5f, 0, 1, 0.3f, 1, 0.7f),
                     P(F::GrassTuft, 150.f, 0.45f, 0, 1, 0.2f, 1, 0.4f), P(F::Flowers, 40.f, 0.3f, 0, 1, 0.3f, 1, 0.8f),
                     P(F::Boulder, 0.8f, 0.6f, 0, 1, 0, 1, 0.7f)};
    }
    {
        auto& e = add("highlands", "Highlands & Moors", "Mountain", "Windswept heather moorland, tarns and craggy outcrops.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 8; r.wFbm = 0.35f; r.wRidged = 0.3f; r.wEroded = 0.35f; r.warp = 0.25f; r.relief = 0.7f;
        r.heightRange = 700.f; r.ranges = 1.f; r.rangeStrength = 0.35f; r.basins = 2.f; r.glacial = 0.2f; r.rivers = 0.5f;
        r.hydraulic = 0.6f; r.thermal = 0.4f; r.moisture = 0.7f; r.temperature = 0.35f;
        e.targets = {0.55f, 0.35f, 0.f, 0.3f, 0.6f};
        e.layers = {L(M::DryGrass, 0, 1, 0, 0.45f, 0, 0.6f, 0.4f), L(M::Moss, 0, 1, 0, 0.4f, 0.5f, 1, 0.45f),
                    L(M::Mud, 0, 0.5f, 0, 0.1f, 0.75f, 1, 0.5f, 0.7f), L(M::Rock, 0, 1, 0.45f, 99, 0, 1, 0.3f),
                    L(M::Gravel, 0.6f, 1, 0.2f, 0.6f, 0, 1, 0.5f, 0.5f), L(M::Cliff, 0, 1, 1.1f, 99, 0, 1, 0.3f, 1.2f)};
        e.foliage = {P(F::GrassTuft, 120.f, 0.5f, 0, 1, 0, 1, 0.5f), P(F::Shrub, 25.f, 0.5f, 0, 0.8f, 0.3f, 1, 0.6f),
                     P(F::Boulder, 3.f, 1.f, 0, 1, 0, 1, 0.8f), P(F::Conifer, 3.f, 0.4f, 0, 0.4f, 0.5f, 1, 0.9f),
                     P(F::Reeds, 30.f, 0.15f, 0, 0.6f, 0.8f, 1, 0.7f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("plateau", "Plateau & Tablelands", "Mountain", "High tabletop plains broken by escarpments.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.relief = 0.75f; r.heightRange = 800.f;
        r.plateau = 0.62f; r.mesas = 0.5f; r.canyons = 0.3f; r.rivers = 0.4f; r.hydraulic = 0.4f; r.thermal = 0.3f; r.talus = 1.1f;
        r.moisture = 0.4f; r.temperature = 0.5f; r.ranges = 0.f;
        e.targets = {0.55f, 0.3f, 0.f, 0.5f, 0.5f};
        e.foliage = {P(F::GrassTuft, 100.f, 0.4f), P(F::Shrub, 10.f, 0.5f, 0, 1, 0.3f, 1, 0.7f), P(F::Broadleaf, 3.f, 0.35f, 0, 0.6f, 0.45f, 1, 0.9f),
                     P(F::Boulder, 2.f, 1.f, 0, 1, 0, 1, 0.7f)};
    }
    {
        auto& e = add("extreme_spikes", "Extreme Spikes", "Mountain", "Impossible needle peaks and razor ridges.");
        auto& r = e.recipe;
        r.baseFreq = 3.2f; r.octaves = 8; r.wFbm = 0.1f; r.wRidged = 0.8f; r.wEroded = 0.1f; r.warp = 0.15f; r.relief = 1.f;
        r.heightRange = 3200.f; r.spikes = 0.9f; r.ranges = 1.f; r.rangeStrength = 0.4f; r.rivers = 0.3f; r.hydraulic = 0.5f;
        r.thermal = 0.1f; r.talus = 2.0f; r.moisture = 0.35f; r.temperature = 0.3f;
        e.targets = {0.85f, 1.1f, 0.f, 0.05f, 0.9f};
        e.layers = temperateLayers(0.7f);
        e.foliage = {P(F::Conifer, 25.f, 0.7f, 0, 0.35f, 0.3f, 1, 0.8f), P(F::Boulder, 4.f, 1.5f, 0, 1, 0, 1, 0.7f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("rocky_crags", "Rocky Crags", "Mountain", "Granite tors, boulder fields and broken ridgelines.");
        auto& r = e.recipe;
        r.baseFreq = 3.f; r.octaves = 8; r.wFbm = 0.3f; r.wRidged = 0.4f; r.wEroded = 0.3f; r.warp = 0.2f; r.relief = 0.75f;
        r.heightRange = 900.f; r.ranges = 1.5f; r.rangeStrength = 0.45f; r.karst = 0.2f; r.rivers = 0.4f; r.hydraulic = 0.6f;
        r.thermal = 0.6f; r.talus = 0.85f; r.moisture = 0.35f; r.temperature = 0.45f;
        e.targets = {0.6f, 0.55f, 0.f, 0.15f, 0.8f};
        e.layers = {L(M::Rock, 0, 1, 0.25f, 99, 0, 1, 0.3f, 1.2f), L(M::Gravel, 0, 1, 0.1f, 0.6f, 0, 1, 0.4f),
                    L(M::DryGrass, 0, 0.75f, 0, 0.35f, 0.2f, 1, 0.4f), L(M::Dirt, 0, 0.7f, 0, 0.4f, 0, 1, 0.5f, 0.6f),
                    L(M::Cliff, 0, 1, 0.9f, 99, 0, 1, 0.3f, 1.2f), L(M::Moss, 0, 0.6f, 0, 0.6f, 0.6f, 1, 0.5f, 0.6f)};
        e.foliage = {P(F::Boulder, 15.f, 1.2f, 0, 1, 0, 1, 0.8f), P(F::SmallRocks, 30.f, 1.f), P(F::Shrub, 12.f, 0.6f, 0, 0.8f, 0.3f, 1, 0.6f),
                     P(F::Conifer, 6.f, 0.5f, 0, 0.6f, 0.4f, 1, 0.8f)};
    }

    // ================================================================= VOLCANIC
    {
        auto& e = add("volcano_active", "Active Volcano", "Volcanic", "A smoking stratovolcano with caldera lava and ash slopes.");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 8; r.wFbm = 0.3f; r.wRidged = 0.4f; r.wEroded = 0.3f; r.relief = 0.45f; r.heightRange = 1800.f;
        r.volcanoes = 1.f; r.lavaFill = 0.8f; r.ranges = 0.f; r.rivers = 0.5f; r.hydraulic = 0.6f; r.thermal = 0.3f; r.talus = 0.9f;
        r.moisture = 0.25f; r.temperature = 0.7f;
        e.targets = {0.65f, 0.4f, 0.f, 0.25f, 0.6f};
        e.layers = {L(M::Ash, 0, 1, 0, 0.6f, 0, 1, 0.35f), L(M::Basalt, 0, 1, 0.45f, 99, 0, 1, 0.3f, 1.2f),
                    L(M::Lava, 0, 1, 0, 99, 0, 1, 0.f, 0.f), L(M::Gravel, 0, 0.5f, 0, 0.4f, 0, 1, 0.5f, 0.5f),
                    L(M::Grass, 0, 0.35f, 0, 0.35f, 0.4f, 1, 0.5f, 0.7f), L(M::Rock, 0, 1, 0.8f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::Conifer, 8.f, 0.4f, 0, 0.3f, 0.4f, 1, 0.8f), P(F::DeadTree, 2.f, 0.5f, 0.2f, 0.5f, 0, 1, 0.6f),
                     P(F::Boulder, 3.f, 1.f, 0, 1, 0, 1, 0.7f), P(F::Shrub, 6.f, 0.4f, 0, 0.35f, 0.3f, 1, 0.6f)};
        e.atmosphere = kVolcanicSky;
    }
    {
        auto& e = add("magma_fields", "Magma Fields", "Volcanic", "Cracked basalt crust over rivers and lakes of molten rock.");
        auto& r = e.recipe;
        r.baseFreq = 2.6f; r.octaves = 7; r.wFbm = 0.4f; r.wRidged = 0.3f; r.wEroded = 0.3f; r.warp = 0.4f; r.relief = 0.45f;
        r.heightRange = 300.f; r.lavaFill = 1.f; r.basins = 3.f; r.volcanoes = 0.6f; r.ranges = 0.f; r.rivers = 0.4f; r.hydraulic = 0.2f;
        r.thermal = 0.4f; r.moisture = 0.f; r.temperature = 1.f;
        e.targets = {0.4f, 0.25f, 0.f, 0.35f, 0.65f};
        e.layers = {L(M::Basalt, 0, 1, 0, 99, 0, 1, 0.35f), L(M::Ash, 0, 1, 0, 0.3f, 0, 1, 0.5f, 0.6f),
                    L(M::Lava, 0, 1, 0, 99, 0, 1, 0.f, 0.f), L(M::Rock, 0, 1, 0.7f, 99, 0, 1, 0.4f, 0.5f)};
        e.foliage = {P(F::Boulder, 4.f, 1.f, 0, 1, 0, 1, 0.7f), P(F::CharredStump, 0.5f, 0.4f, 0.3f, 1, 0, 1, 0.6f)};
        e.atmosphere = kVolcanicSky;
    }
    {
        auto& e = add("volcanic_islands", "Volcanic Islands", "Volcanic", "Black-sand islands rising from the sea around old cones.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 8; r.wFbm = 0.4f; r.wRidged = 0.3f; r.wEroded = 0.3f; r.warp = 0.3f; r.relief = 0.75f;
        r.heightRange = 1100.f; r.seaLevel = 0.32f; r.islandMask = 0.7f; r.volcanoes = 2.f; r.lavaFill = 0.3f; r.rivers = 0.5f;
        r.hydraulic = 0.7f; r.thermal = 0.3f; r.moisture = 0.6f; r.temperature = 0.7f; r.ranges = 0.f;
        e.targets = {0.6f, 0.4f, 0.45f, 0.2f, 0.6f};
        e.layers = {L(M::Basalt, 0, 0.36f, 0, 0.4f, 0, 1, 0.3f), L(M::LushGrass, 0.34f, 0.8f, 0, 0.5f, 0.4f, 1, 0.4f),
                    L(M::Ash, 0.6f, 1, 0, 0.6f, 0, 1, 0.4f), L(M::Rock, 0, 1, 0.55f, 99, 0, 1, 0.3f),
                    L(M::Lava, 0, 1, 0, 99, 0, 1, 0.f, 0.f), L(M::ForestFloor, 0.34f, 0.7f, 0, 0.5f, 0.7f, 1, 0.4f)};
        e.foliage = {P(F::Palm, 10.f, 0.35f, 0.32f, 0.45f, 0.3f, 1, 0.6f), P(F::JungleTree, 30.f, 0.5f, 0.38f, 0.75f, 0.55f, 1, 0.7f),
                     P(F::Fern, 50.f, 0.6f, 0.35f, 0.75f, 0.5f, 1, 0.6f), P(F::Boulder, 2.f, 1.f, 0.3f, 1, 0, 1, 0.7f)};
        e.atmosphere = kTropicSky;
    }
    {
        auto& e = add("basalt_columns", "Basalt Columns & Ash Wastes", "Volcanic", "Hexagonal basalt plateaus, ash dunes and fumaroles.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.2f; r.wEroded = 0.3f; r.relief = 0.6f; r.heightRange = 450.f;
        r.terraces = 7; r.terraceStrength = 0.55f; r.mesas = 0.5f; r.karst = 0.35f; r.dunes = 0.3f; r.craters = 3.f; r.rivers = 0.2f;
        r.hydraulic = 0.3f; r.thermal = 0.2f; r.talus = 1.4f; r.moisture = 0.1f; r.temperature = 0.7f; r.ranges = 0.f;
        e.targets = {0.5f, 0.4f, 0.f, 0.3f, 0.7f};
        e.layers = {L(M::Ash, 0, 1, 0, 0.35f, 0, 1, 0.4f), L(M::Basalt, 0, 1, 0.3f, 99, 0, 1, 0.3f, 1.3f),
                    L(M::Gravel, 0, 1, 0, 0.3f, 0, 1, 0.5f, 0.5f), L(M::Sand, 0, 0.3f, 0, 0.2f, 0, 1, 0.5f, 0.4f)};
        e.foliage = {P(F::Boulder, 3.f, 1.f), P(F::SmallRocks, 10.f, 0.7f), P(F::DesertShrub, 0.6f, 0.3f)};
        e.atmosphere = kVolcanicSky;
    }

    // ===================================================================== COLD
    {
        auto& e = add("glacial_valley", "Glacial Valley", "Cold", "U-shaped valleys carved by glaciers, moraines and cirques.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 8; r.wFbm = 0.2f; r.wRidged = 0.5f; r.wEroded = 0.3f; r.warp = 0.2f; r.relief = 0.95f;
        r.heightRange = 1800.f; r.ranges = 2.f; r.rangeStrength = 0.5f; r.glacial = 0.9f; r.basins = 1.f; r.rivers = 0.4f;
        r.hydraulic = 0.5f; r.thermal = 0.5f; r.talus = 1.f; r.moisture = 0.55f; r.temperature = 0.15f;
        e.targets = {0.75f, 0.6f, 0.f, 0.15f, 0.7f};
        e.layers = temperateLayers(0.5f);
        e.layers.push_back(L(M::Ice, 0.7f, 1, 0, 0.5f, 0, 1, 0.3f, 1.2f));
        e.foliage = {P(F::Conifer, 35.f, 0.6f, 0, 0.45f, 0.3f, 1, 0.7f), P(F::Boulder, 6.f, 1.f, 0, 1, 0, 1, 0.8f),
                     P(F::GrassTuft, 50.f, 0.5f, 0, 0.5f, 0.3f, 1, 0.5f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("arctic_tundra", "Arctic Tundra", "Cold", "Permafrost flats, frost polygons and meltwater ponds.");
        auto& r = e.recipe;
        r.baseFreq = 2.5f; r.octaves = 6; r.wFbm = 0.6f; r.wBillow = 0.2f; r.wRidged = 0.f; r.wEroded = 0.2f; r.relief = 0.3f;
        r.heightRange = 180.f; r.basins = 6.f; r.flatten = 0.3f; r.rivers = 0.3f; r.hydraulic = 0.2f; r.thermal = 0.5f; r.talus = 0.4f;
        r.moisture = 0.6f; r.temperature = 0.1f; r.ranges = 0.f;
        e.targets = {0.3f, 0.1f, 0.f, 0.6f, 0.4f};
        e.layers = {L(M::Moss, 0, 1, 0, 0.3f, 0.3f, 1, 0.45f), L(M::DryGrass, 0, 1, 0, 0.35f, 0, 0.6f, 0.45f),
                    L(M::Snow, 0.45f, 1, 0, 0.5f, 0, 1, 0.5f, 0.9f), L(M::Gravel, 0, 1, 0.15f, 99, 0, 1, 0.4f),
                    L(M::Mud, 0, 0.4f, 0, 0.08f, 0.75f, 1, 0.5f, 0.7f)};
        e.foliage = {P(F::GrassTuft, 60.f, 0.4f), P(F::Shrub, 6.f, 0.3f, 0, 1, 0.4f, 1, 0.7f), P(F::SmallRocks, 6.f, 0.6f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("ice_sheet", "Ice Sheet & Frozen Wastes", "Cold", "Wind-scoured ice domes, crevasse fields and nunataks.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.2f; r.wBillow = 0.1f; r.wEroded = 0.2f; r.relief = 0.5f;
        r.heightRange = 600.f; r.dunes = 0.25f; r.duneScale = 2.f; r.spikes = 0.15f; r.rivers = 0.f; r.hydraulic = 0.f; r.thermal = 0.3f;
        r.wind = 0.5f; r.moisture = 0.3f; r.temperature = 0.f; r.ranges = 1.f; r.rangeStrength = 0.3f;
        e.targets = {0.45f, 0.2f, 0.f, 0.45f, 0.45f};
        e.layers = {L(M::Snow, 0, 1, 0, 0.5f, 0, 1, 0.3f, 1.2f), L(M::Ice, 0, 1, 0.3f, 1.f, 0, 1, 0.4f),
                    L(M::Rock, 0, 1, 0.8f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::SmallRocks, 1.f, 1.f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("fjords", "Fjords", "Cold", "Steep drowned glacial valleys winding inland from the sea.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 8; r.wFbm = 0.2f; r.wRidged = 0.5f; r.wEroded = 0.3f; r.warp = 0.3f; r.relief = 1.f;
        r.heightRange = 1400.f; r.seaLevel = 0.22f; r.glacial = 1.f; r.ranges = 2.f; r.rangeStrength = 0.4f; r.rivers = 0.5f;
        r.hydraulic = 0.6f; r.thermal = 0.4f; r.talus = 1.2f; r.moisture = 0.75f; r.temperature = 0.25f;
        e.targets = {0.75f, 0.7f, 0.25f, 0.1f, 0.7f};
        e.layers = temperateLayers(0.7f);
        e.layers[7] = L(M::Pebbles, 0, 0.25f, 0, 0.4f, 0, 1, 0.3f);
        e.foliage = {P(F::Conifer, 55.f, 0.75f, 0.22f, 0.6f, 0.3f, 1, 0.6f), P(F::Birch, 8.f, 0.5f, 0.22f, 0.45f, 0.4f, 1, 0.6f),
                     P(F::Boulder, 3.f, 1.2f), P(F::GrassTuft, 60.f, 0.5f, 0.22f, 0.65f, 0.3f, 1, 0.5f)};
        e.atmosphere = kColdSky;
    }
    {
        auto& e = add("taiga", "Taiga (Boreal Forest)", "Cold", "Endless spruce forest, bogs and glacial lakes.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.15f; r.wEroded = 0.35f; r.relief = 0.45f; r.heightRange = 350.f;
        r.basins = 5.f; r.glacial = 0.2f; r.rivers = 0.5f; r.hydraulic = 0.5f; r.thermal = 0.4f; r.moisture = 0.75f; r.temperature = 0.25f;
        r.ranges = 0.f;
        e.targets = {0.4f, 0.2f, 0.f, 0.45f, 0.5f};
        e.layers = {L(M::ForestFloor, 0, 1, 0, 0.5f, 0.3f, 1, 0.4f), L(M::Moss, 0, 1, 0, 0.4f, 0.6f, 1, 0.5f, 0.8f),
                    L(M::Grass, 0, 1, 0, 0.4f, 0, 0.5f, 0.4f, 0.6f), L(M::Rock, 0, 1, 0.45f, 99, 0, 1, 0.3f),
                    L(M::Snow, 0.85f, 1, 0, 0.8f, 0, 1, 0.3f), L(M::Mud, 0, 0.5f, 0, 0.06f, 0.85f, 1, 0.4f, 0.7f)};
        e.foliage = {P(F::Conifer, 140.f, 0.6f, 0, 0.9f, 0.3f, 1, 0.5f, 0.7f, 1.5f), P(F::Birch, 10.f, 0.4f, 0, 0.7f, 0.5f, 1, 0.7f),
                     P(F::Fern, 40.f, 0.4f, 0, 0.8f, 0.5f, 1, 0.6f), P(F::Boulder, 2.f, 0.8f), P(F::Reeds, 30.f, 0.1f, 0, 1, 0.85f, 1, 0.7f)};
        e.atmosphere = kColdSky;
    }

    // ================================================================ TEMPERATE
    {
        auto& e = add("temperate_forest", "Temperate Forest", "Temperate", "Deciduous woodland over gentle hills and brooks.");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.15f; r.wEroded = 0.35f; r.warp = 0.3f; r.relief = 0.6f;
        r.heightRange = 420.f; r.basins = 1.f; r.rivers = 0.6f; r.hydraulic = 0.6f; r.thermal = 0.4f; r.moisture = 0.7f;
        r.temperature = 0.55f; r.ranges = 0.5f; r.rangeStrength = 0.25f;
        e.targets = {0.5f, 0.28f, 0.f, 0.35f, 0.55f};
        e.foliage = {P(F::Broadleaf, 90.f, 0.55f, 0, 0.85f, 0.4f, 1, 0.55f, 0.7f, 1.4f), P(F::Birch, 15.f, 0.5f, 0, 0.8f, 0.4f, 1, 0.7f),
                     P(F::Conifer, 10.f, 0.6f, 0.4f, 1, 0.3f, 1, 0.7f), P(F::Shrub, 40.f, 0.6f, 0, 1, 0.3f, 1, 0.6f),
                     P(F::Fern, 60.f, 0.5f, 0, 0.8f, 0.6f, 1, 0.6f), P(F::GrassTuft, 60.f, 0.4f, 0, 1, 0.2f, 1, 0.5f),
                     P(F::Boulder, 1.5f, 0.8f)};
    }
    {
        auto& e = add("grassland_plains", "Grassland Plains (Prairie)", "Temperate", "Vast open grassland with meandering rivers.");
        auto& r = e.recipe;
        r.baseFreq = 1.5f; r.octaves = 6; r.wFbm = 0.7f; r.wRidged = 0.f; r.wEroded = 0.3f; r.warp = 0.35f; r.relief = 0.3f;
        r.heightRange = 120.f; r.flatten = 0.35f; r.rivers = 0.5f; r.hydraulic = 0.3f; r.thermal = 0.4f; r.moisture = 0.5f;
        r.temperature = 0.55f; r.ranges = 0.f; r.basins = 0.5f;
        e.targets = {0.3f, 0.08f, 0.f, 0.75f, 0.35f};
        e.layers = {L(M::Grass, 0, 1, 0, 0.4f, 0.3f, 1, 0.4f), L(M::DryGrass, 0, 1, 0, 0.4f, 0, 0.45f, 0.5f),
                    L(M::Dirt, 0, 1, 0.25f, 99, 0, 1, 0.4f), L(M::Mud, 0, 0.3f, 0, 0.05f, 0.85f, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::GrassTuft, 250.f, 0.4f, 0, 1, 0, 1, 0.3f), P(F::Flowers, 50.f, 0.3f, 0, 1, 0.3f, 1, 0.8f),
                     P(F::Broadleaf, 1.5f, 0.3f, 0, 1, 0.6f, 1, 0.9f), P(F::Shrub, 4.f, 0.4f, 0, 1, 0.5f, 1, 0.8f)};
    }
    {
        auto& e = add("steppe", "Steppe", "Temperate", "Dry, wind-blown grassland with low ridges.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 6; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.relief = 0.35f; r.heightRange = 200.f;
        r.flatten = 0.2f; r.rivers = 0.25f; r.hydraulic = 0.25f; r.thermal = 0.4f; r.wind = 0.2f; r.moisture = 0.25f; r.temperature = 0.5f;
        r.ranges = 0.5f; r.rangeStrength = 0.2f;
        e.targets = {0.35f, 0.12f, 0.f, 0.6f, 0.4f};
        e.layers = {L(M::DryGrass, 0, 1, 0, 0.4f, 0, 1, 0.4f), L(M::Grass, 0, 1, 0, 0.3f, 0.5f, 1, 0.5f, 0.6f),
                    L(M::Dirt, 0, 1, 0.2f, 99, 0, 1, 0.4f), L(M::Gravel, 0, 1, 0.4f, 99, 0, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::GrassTuft, 160.f, 0.4f), P(F::DesertShrub, 6.f, 0.4f, 0, 1, 0, 1, 0.7f), P(F::SmallRocks, 3.f, 0.6f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("river_valley", "River Valley", "Temperate", "A broad fertile valley with a wide meandering river.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.2f; r.wEroded = 0.3f; r.relief = 0.6f; r.heightRange = 500.f;
        r.canyons = 0.25f; r.rivers = 1.f; r.hydraulic = 0.7f; r.thermal = 0.4f; r.moisture = 0.7f; r.temperature = 0.55f; r.ranges = 1.f;
        r.rangeStrength = 0.35f;
        e.targets = {0.5f, 0.3f, 0.f, 0.35f, 0.55f};
        e.foliage = {P(F::Broadleaf, 40.f, 0.5f, 0, 0.8f, 0.5f, 1, 0.7f), P(F::Reeds, 50.f, 0.15f, 0, 0.4f, 0.85f, 1, 0.6f),
                     P(F::GrassTuft, 120.f, 0.4f), P(F::Shrub, 20.f, 0.5f, 0, 1, 0.4f, 1, 0.6f), P(F::Conifer, 10.f, 0.6f, 0.5f, 1, 0.3f, 1, 0.7f)};
    }
    {
        auto& e = add("canyon_river", "River Canyons", "Temperate", "Rivers incised deep into layered rock.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.relief = 0.6f; r.heightRange = 700.f;
        r.canyons = 1.f; r.terraces = 8; r.terraceStrength = 0.35f; r.rivers = 0.9f; r.hydraulic = 0.4f; r.thermal = 0.2f; r.talus = 1.3f;
        r.moisture = 0.35f; r.temperature = 0.65f; r.ranges = 0.f;
        e.targets = {0.55f, 0.4f, 0.f, 0.35f, 0.6f};
        e.layers = {L(M::DryGrass, 0, 1, 0, 0.3f, 0, 1, 0.4f), L(M::Sandstone, 0, 1, 0.45f, 99, 0, 1, 0.4f, 1.2f),
                    L(M::Gravel, 0, 0.3f, 0, 0.3f, 0.5f, 1, 0.4f, 0.7f), L(M::Rock, 0, 1, 1.1f, 99, 0, 1, 0.3f, 0.7f),
                    L(M::Dirt, 0, 1, 0.2f, 0.5f, 0, 1, 0.4f, 0.6f), L(M::Grass, 0, 0.35f, 0, 0.3f, 0.6f, 1, 0.4f, 0.8f)};
        e.foliage = {P(F::Shrub, 12.f, 0.4f), P(F::Conifer, 6.f, 0.5f, 0.4f, 1, 0, 1, 0.8f), P(F::Broadleaf, 20.f, 0.3f, 0, 0.3f, 0.6f, 1, 0.7f),
                     P(F::Boulder, 2.f, 1.f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("karst", "Karst Tower Forest", "Temperate", "Limestone pinnacles rising from flat jungle-green floors.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 6; r.wFbm = 0.5f; r.wRidged = 0.2f; r.wEroded = 0.3f; r.relief = 0.25f; r.heightRange = 500.f;
        r.karst = 1.f; r.flatten = 0.3f; r.rivers = 0.5f; r.hydraulic = 0.3f; r.thermal = 0.1f; r.talus = 2.f; r.moisture = 0.85f;
        r.temperature = 0.75f; r.ranges = 0.f; r.basins = 1.f;
        e.targets = {0.5f, 0.45f, 0.f, 0.45f, 0.8f};
        e.layers = {L(M::LushGrass, 0, 1, 0, 0.4f, 0, 1, 0.4f), L(M::Rock, 0, 1, 0.6f, 99, 0, 1, 0.4f, 1.2f),
                    L(M::Moss, 0, 1, 0.3f, 1.2f, 0.4f, 1, 0.5f, 0.8f), L(M::Mud, 0, 0.3f, 0, 0.08f, 0.85f, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::JungleTree, 50.f, 0.9f, 0, 1, 0.3f, 1, 0.6f), P(F::Fern, 80.f, 0.7f, 0, 1, 0.3f, 1, 0.5f),
                     P(F::Shrub, 40.f, 1.f, 0, 1, 0.3f, 1, 0.6f), P(F::Reeds, 20.f, 0.1f, 0, 0.3f, 0.85f, 1, 0.6f)};
        e.atmosphere = kTropicSky;
    }

    // ================================================================= TROPICAL
    {
        auto& e = add("rainforest", "Tropical Rainforest (Jungle)", "Tropical", "Steamy jungle hills cut by muddy rivers and waterfalls.");
        auto& r = e.recipe;
        r.baseFreq = 2.5f; r.octaves = 8; r.wFbm = 0.4f; r.wRidged = 0.25f; r.wEroded = 0.35f; r.warp = 0.3f; r.relief = 0.65f;
        r.heightRange = 600.f; r.rivers = 0.9f; r.hydraulic = 0.9f; r.thermal = 0.3f; r.moisture = 0.95f; r.temperature = 0.9f;
        r.ranges = 1.f; r.rangeStrength = 0.3f; r.basins = 0.5f;
        e.targets = {0.55f, 0.4f, 0.f, 0.2f, 0.7f};
        e.layers = {L(M::LushGrass, 0, 1, 0, 0.45f, 0, 1, 0.4f), L(M::ForestFloor, 0, 1, 0, 0.6f, 0.5f, 1, 0.45f, 1.1f),
                    L(M::Mud, 0, 0.4f, 0, 0.12f, 0.85f, 1, 0.4f, 0.8f), L(M::Moss, 0, 1, 0.4f, 1.2f, 0, 1, 0.5f),
                    L(M::Rock, 0, 1, 0.9f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::JungleTree, 160.f, 0.8f, 0, 1, 0.3f, 1, 0.45f, 0.7f, 1.4f), P(F::Palm, 15.f, 0.5f, 0, 0.6f, 0.3f, 1, 0.6f),
                     P(F::TreeFern, 30.f, 0.7f, 0, 1, 0.5f, 1, 0.6f), P(F::Fern, 150.f, 0.8f), P(F::Shrub, 60.f, 0.8f)};
        e.atmosphere = kTropicSky;
    }
    {
        auto& e = add("savanna", "Savanna", "Tropical", "Golden grassland dotted with acacias, baobabs and kopjes.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 6; r.wFbm = 0.65f; r.wRidged = 0.1f; r.wEroded = 0.25f; r.warp = 0.3f; r.relief = 0.3f;
        r.heightRange = 180.f; r.karst = 0.15f; r.flatten = 0.25f; r.rivers = 0.3f; r.hydraulic = 0.3f; r.thermal = 0.4f;
        r.moisture = 0.3f; r.temperature = 0.85f; r.ranges = 0.f; r.basins = 0.7f;
        e.targets = {0.3f, 0.1f, 0.f, 0.65f, 0.4f};
        e.layers = {L(M::DryGrass, 0, 1, 0, 0.4f, 0, 0.6f, 0.4f), L(M::Grass, 0, 1, 0, 0.4f, 0.5f, 1, 0.5f, 0.7f),
                    L(M::RedSand, 0, 1, 0, 0.3f, 0, 0.3f, 0.6f, 0.5f), L(M::Rock, 0, 1, 0.45f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::Acacia, 4.f, 0.3f, 0, 1, 0, 1, 0.75f), P(F::Baobab, 0.3f, 0.2f, 0, 1, 0, 1, 0.4f),
                     P(F::GrassTuft, 200.f, 0.4f), P(F::DesertShrub, 8.f, 0.4f, 0, 1, 0, 1, 0.7f), P(F::Boulder, 0.6f, 1.f, 0, 1, 0, 1, 0.9f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("archipelago", "Tropical Archipelago (Atolls)", "Tropical", "Turquoise lagoons, reef rings and palm islands.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.warp = 0.35f; r.relief = 0.5f;
        r.heightRange = 160.f; r.seaLevel = 0.42f; r.islandMask = 0.5f; r.rivers = 0.1f; r.hydraulic = 0.2f; r.thermal = 0.5f;
        r.talus = 0.4f; r.moisture = 0.7f; r.temperature = 0.95f; r.ranges = 0.f;
        e.targets = {0.35f, 0.12f, 0.65f, 0.25f, 0.45f};
        e.layers = {L(M::Sand, 0, 0.47f, 0, 0.5f, 0, 1, 0.25f, 1.2f), L(M::LushGrass, 0.45f, 1, 0, 0.5f, 0, 1, 0.4f),
                    L(M::Rock, 0, 1, 0.6f, 99, 0, 1, 0.3f), L(M::Pebbles, 0.40f, 0.46f, 0, 0.3f, 0, 1, 0.6f, 0.4f)};
        e.foliage = {P(F::Palm, 30.f, 0.4f, 0.43f, 0.6f, 0, 1, 0.6f), P(F::JungleTree, 20.f, 0.5f, 0.5f, 1, 0, 1, 0.7f),
                     P(F::Shrub, 30.f, 0.5f, 0.45f, 1, 0, 1, 0.5f)};
        e.atmosphere = kTropicSky;
    }
    {
        auto& e = add("mangrove_coast", "Mangrove Coast", "Tropical", "Tidal channels braided through mangrove thickets.");
        auto& r = e.recipe;
        r.baseFreq = 3.f; r.octaves = 6; r.wFbm = 0.6f; r.wRidged = 0.f; r.wEroded = 0.4f; r.warp = 0.5f; r.relief = 0.3f;
        r.heightRange = 60.f; r.seaLevel = 0.4f; r.flatten = 0.2f; r.rivers = 0.6f; r.hydraulic = 0.4f; r.thermal = 0.3f;
        r.moisture = 1.f; r.temperature = 0.9f; r.ranges = 0.f;
        e.targets = {0.25f, 0.06f, 0.45f, 0.4f, 0.5f};
        e.layers = {L(M::Mud, 0, 0.46f, 0, 99, 0, 1, 0.35f, 1.1f), L(M::LushGrass, 0.44f, 1, 0, 99, 0, 1, 0.4f),
                    L(M::Sand, 0.38f, 0.43f, 0, 0.3f, 0, 1, 0.5f, 0.6f)};
        e.foliage = {P(F::Mangrove, 120.f, 0.5f, 0.38f, 0.5f, 0, 1, 0.5f), P(F::Palm, 15.f, 0.4f, 0.46f, 1, 0, 1, 0.6f),
                     P(F::Reeds, 80.f, 0.3f, 0.4f, 0.52f, 0, 1, 0.5f), P(F::JungleTree, 30.f, 0.5f, 0.5f, 1, 0, 1, 0.6f)};
        e.atmosphere = kSwampSky;
    }

    // ================================================================== WETLAND
    {
        auto& e = add("swamp", "Swamp & Bayou", "Wetland", "Black-water channels, hummocks and drowned forest.");
        auto& r = e.recipe;
        r.baseFreq = 3.f; r.octaves = 6; r.wFbm = 0.5f; r.wBillow = 0.3f; r.wRidged = 0.f; r.wEroded = 0.2f; r.warp = 0.5f;
        r.relief = 0.2f; r.heightRange = 40.f; r.basins = 8.f; r.flatten = 0.3f; r.rivers = 0.3f; r.hydraulic = 0.1f; r.thermal = 0.3f;
        r.moisture = 1.f; r.temperature = 0.7f; r.ranges = 0.f;
        e.targets = {0.25f, 0.05f, 0.f, 0.5f, 0.45f};
        e.layers = {L(M::Mud, 0, 1, 0, 99, 0.6f, 1, 0.4f, 1.1f), L(M::Moss, 0, 1, 0, 99, 0, 1, 0.5f),
                    L(M::LushGrass, 0, 1, 0, 0.3f, 0, 0.75f, 0.5f, 0.7f), L(M::ForestFloor, 0, 1, 0, 0.5f, 0, 1, 0.5f, 0.5f)};
        e.foliage = {P(F::Mangrove, 40.f, 0.5f, 0, 1, 0.5f, 1, 0.6f), P(F::DeadTree, 6.f, 0.5f, 0, 1, 0.8f, 1, 0.6f),
                     P(F::Reeds, 200.f, 0.3f, 0, 1, 0.6f, 1, 0.5f), P(F::Broadleaf, 20.f, 0.4f, 0, 1, 0.4f, 0.85f, 0.7f),
                     P(F::Fern, 60.f, 0.5f, 0, 1, 0.5f, 1, 0.6f)};
        e.atmosphere = kSwampSky;
    }
    {
        auto& e = add("river_delta", "Marshland & River Delta", "Wetland", "Distributary channels fanning into the sea through reed beds.");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 6; r.wFbm = 0.7f; r.wRidged = 0.f; r.wEroded = 0.3f; r.warp = 0.5f; r.relief = 0.3f;
        r.heightRange = 50.f; r.seaLevel = 0.3f; r.rivers = 1.f; r.hydraulic = 0.4f; r.thermal = 0.3f; r.flatten = 0.25f;
        r.moisture = 0.9f; r.temperature = 0.6f; r.ranges = 0.f;
        e.targets = {0.3f, 0.05f, 0.3f, 0.55f, 0.45f};
        e.layers = {L(M::Mud, 0, 0.36f, 0, 99, 0, 1, 0.4f), L(M::Grass, 0.33f, 1, 0, 0.4f, 0, 0.85f, 0.4f),
                    L(M::LushGrass, 0.33f, 1, 0, 0.4f, 0.7f, 1, 0.5f), L(M::Sand, 0.28f, 0.34f, 0, 0.3f, 0, 1, 0.4f, 0.7f)};
        e.foliage = {P(F::Reeds, 220.f, 0.3f, 0.29f, 0.45f, 0, 1, 0.5f), P(F::GrassTuft, 120.f, 0.4f, 0.33f, 1),
                     P(F::Broadleaf, 6.f, 0.3f, 0.36f, 1, 0.5f, 1, 0.8f), P(F::Shrub, 15.f, 0.3f, 0.33f, 1, 0.5f, 1, 0.7f)};
        e.atmosphere = kSwampSky;
    }

    // ================================================================== COASTAL
    {
        auto& e = add("coastal_cliffs", "Coastal Cliffs", "Coastal", "Grassy headlands dropping in sheer cliffs to a rocky sea.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 8; r.wFbm = 0.6f; r.wRidged = 0.15f; r.wEroded = 0.25f; r.warp = 0.3f; r.relief = 0.7f;
        r.heightRange = 260.f; r.seaLevel = 0.3f; r.islandMask = 0.35f; r.plateau = 0.75f; r.terraces = 3; r.terraceStrength = 0.4f;
        r.rivers = 0.3f; r.hydraulic = 0.3f; r.thermal = 0.2f; r.talus = 1.6f; r.moisture = 0.6f; r.temperature = 0.45f; r.ranges = 0.f;
        e.targets = {0.55f, 0.35f, 0.35f, 0.4f, 0.55f};
        e.layers = {L(M::Grass, 0.3f, 1, 0, 0.45f, 0, 1, 0.4f), L(M::Cliff, 0, 1, 0.6f, 99, 0, 1, 0.3f, 1.3f),
                    L(M::Pebbles, 0, 0.34f, 0, 0.5f, 0, 1, 0.4f), L(M::Sand, 0, 0.32f, 0, 0.2f, 0, 1, 0.5f, 0.8f),
                    L(M::Rock, 0, 1, 0.4f, 0.8f, 0, 1, 0.3f), L(M::DryGrass, 0.5f, 1, 0, 0.3f, 0, 0.5f, 0.6f, 0.5f)};
        e.foliage = {P(F::GrassTuft, 180.f, 0.45f, 0.31f, 1), P(F::Shrub, 15.f, 0.5f, 0.32f, 1, 0, 1, 0.7f),
                     P(F::Flowers, 30.f, 0.3f, 0.32f, 1, 0, 1, 0.8f), P(F::Boulder, 2.f, 1.5f, 0.25f, 0.4f, 0, 1, 0.7f)};
    }
    {
        auto& e = add("beach_coast", "Sandy Beach Coast", "Coastal", "Long beaches, dune grass and gentle coastal hills.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 7; r.wFbm = 0.7f; r.wRidged = 0.05f; r.wEroded = 0.25f; r.warp = 0.3f; r.relief = 0.5f;
        r.heightRange = 150.f; r.seaLevel = 0.33f; r.islandMask = 0.25f; r.dunes = 0.2f; r.rivers = 0.4f; r.hydraulic = 0.3f;
        r.thermal = 0.4f; r.moisture = 0.55f; r.temperature = 0.7f; r.ranges = 0.f;
        e.targets = {0.4f, 0.12f, 0.35f, 0.5f, 0.45f};
        e.layers = {L(M::Sand, 0, 0.37f, 0, 0.5f, 0, 1, 0.3f, 1.2f), L(M::Grass, 0.35f, 1, 0, 0.45f, 0, 1, 0.4f),
                    L(M::DryGrass, 0.34f, 0.5f, 0, 0.4f, 0, 0.6f, 0.5f, 0.8f), L(M::Rock, 0, 1, 0.55f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::GrassTuft, 120.f, 0.4f, 0.34f, 0.5f, 0, 1, 0.5f), P(F::Palm, 6.f, 0.3f, 0.34f, 0.45f, 0, 1, 0.6f),
                     P(F::Broadleaf, 15.f, 0.4f, 0.42f, 1, 0.4f, 1, 0.7f), P(F::Shrub, 20.f, 0.5f, 0.37f, 1, 0, 1, 0.6f)};
        e.atmosphere = kTropicSky;
    }

    // =================================================================== EXOTIC
    {
        auto& e = add("lunar_craters", "Lunar Crater Field", "Exotic", "Airless regolith plains pocked with impact craters.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 6; r.wFbm = 0.7f; r.wRidged = 0.1f; r.wEroded = 0.2f; r.relief = 0.25f; r.heightRange = 600.f;
        r.craters = 30.f; r.rivers = 0.f; r.hydraulic = 0.f; r.thermal = 0.25f; r.moisture = 0.f; r.temperature = 0.4f; r.ranges = 0.f;
        e.targets = {0.4f, 0.25f, 0.f, 0.45f, 0.7f};
        e.layers = {L(M::Ash, 0, 1, 0, 0.5f, 0, 1, 0.3f), L(M::Gravel, 0, 1, 0.3f, 99, 0, 1, 0.4f), L(M::Basalt, 0, 0.3f, 0, 0.2f, 0, 1, 0.5f, 0.6f),
                    L(M::Rock, 0, 1, 0.8f, 99, 0, 1, 0.3f)};
        e.foliage = {P(F::Boulder, 1.f, 1.f), P(F::SmallRocks, 6.f, 1.f)};
        e.atmosphere = A({0.02f, 0.02f, 0.04f}, {0.12f, 0.12f, 0.15f}, {1.f, 1.f, 1.f}, {0.1f, 0.1f, 0.1f}, 0.05f);
    }
    {
        auto& e = add("crystal_fields", "Crystal Fields", "Exotic", "Glowing crystal spires erupting from violet bedrock.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 7; r.wFbm = 0.4f; r.wRidged = 0.4f; r.wEroded = 0.2f; r.warp = 0.4f; r.relief = 0.5f;
        r.heightRange = 500.f; r.crystals = 1.f; r.karst = 0.2f; r.rivers = 0.2f; r.hydraulic = 0.2f; r.thermal = 0.2f; r.moisture = 0.3f;
        r.temperature = 0.5f; r.ranges = 0.5f; r.rangeStrength = 0.3f;
        e.targets = {0.5f, 0.45f, 0.f, 0.3f, 0.85f};
        e.layers = {L(M::Basalt, 0, 1, 0, 0.5f, 0, 1, 0.4f), L(M::Crystal, 0, 1, 0.7f, 99, 0, 1, 0.4f, 1.2f),
                    L(M::Gravel, 0, 1, 0, 0.4f, 0.3f, 1, 0.5f, 0.6f), L(M::Rock, 0, 1, 0.4f, 0.9f, 0, 1, 0.4f)};
        e.foliage = {P(F::Crystal, 12.f, 1.2f, 0, 1, 0, 1, 0.8f), P(F::GiantMushroom, 3.f, 0.4f, 0, 0.6f, 0.4f, 1, 0.8f),
                     P(F::SmallRocks, 6.f, 1.f)};
        e.atmosphere = kAlienSky;
    }
    {
        auto& e = add("alien_spires", "Alien Spires", "Exotic", "Warped otherworldly hoodoos under a violet sky.");
        auto& r = e.recipe;
        r.baseFreq = 2.8f; r.octaves = 8; r.wFbm = 0.2f; r.wRidged = 0.5f; r.wBillow = 0.2f; r.wEroded = 0.1f; r.warp = 0.9f;
        r.relief = 0.7f; r.heightRange = 900.f; r.spikes = 0.5f; r.karst = 0.5f; r.terraces = 10; r.terraceStrength = 0.3f;
        r.rivers = 0.2f; r.hydraulic = 0.3f; r.thermal = 0.1f; r.talus = 2.f; r.moisture = 0.4f; r.temperature = 0.6f; r.ranges = 0.f;
        e.targets = {0.6f, 0.7f, 0.f, 0.2f, 0.9f};
        e.layers = {L(M::RedSand, 0, 1, 0, 0.4f, 0, 1, 0.4f), L(M::Crystal, 0, 1, 1.2f, 99, 0, 1, 0.5f, 0.6f),
                    L(M::Sandstone, 0, 1, 0.4f, 99, 0, 1, 0.4f), L(M::Moss, 0, 0.5f, 0, 0.3f, 0.5f, 1, 0.5f, 0.6f)};
        e.foliage = {P(F::GiantMushroom, 6.f, 0.5f, 0, 0.6f, 0.3f, 1, 0.8f), P(F::Crystal, 4.f, 1.f), P(F::Fern, 20.f, 0.4f, 0, 0.5f, 0.5f, 1, 0.7f)};
        e.atmosphere = kAlienSky;
    }

    // ================================================================== MORE
    {
        auto& e = add("mediterranean", "Mediterranean Hills", "Temperate", "Sun-baked limestone hills, olive groves, cypress and scrub (maquis).");
        auto& r = e.recipe;
        r.baseFreq = 2.2f; r.octaves = 7; r.wFbm = 0.45f; r.wRidged = 0.2f; r.wEroded = 0.35f; r.warp = 0.3f; r.relief = 0.6f;
        r.heightRange = 450.f; r.seaLevel = 0.18f; r.islandMask = 0.15f; r.terraces = 0; r.rivers = 0.4f; r.hydraulic = 0.5f;
        r.thermal = 0.4f; r.moisture = 0.35f; r.temperature = 0.75f; r.ranges = 1.f; r.rangeStrength = 0.3f;
        e.targets = {0.5f, 0.3f, 0.15f, 0.3f, 0.55f};
        e.layers = {L(M::DryGrass, 0.18f, 1, 0, 0.4f, 0, 0.6f, 0.45f), L(M::Grass, 0.18f, 1, 0, 0.35f, 0.55f, 1, 0.45f, 0.8f),
                    L(M::Dirt, 0.18f, 1, 0.25f, 0.7f, 0, 1, 0.4f, 0.8f), L(M::Rock, 0, 1, 0.55f, 99, 0, 1, 0.3f),
                    L(M::Pebbles, 0, 0.22f, 0, 0.4f, 0, 1, 0.4f), L(M::Sand, 0, 0.21f, 0, 0.2f, 0, 1, 0.4f, 0.8f)};
        e.foliage = {P(F::Shrub, 60.f, 0.6f, 0.19f, 1, 0, 1, 0.6f), P(F::Conifer, 8.f, 0.5f, 0.2f, 0.9f, 0.2f, 1, 0.8f, 0.6f, 1.f),
                     P(F::Broadleaf, 12.f, 0.4f, 0.2f, 0.7f, 0.3f, 1, 0.7f, 0.5f, 0.8f), P(F::GrassTuft, 100.f, 0.4f, 0.19f, 1),
                     P(F::Boulder, 2.f, 1.f, 0.19f, 1)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("tepui", "Tepui Table Mountains", "Tropical", "Sheer-walled sandstone tabletops rising above cloud-forest jungle.");
        auto& r = e.recipe;
        r.baseFreq = 1.4f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.warp = 0.35f; r.relief = 0.8f;
        r.heightRange = 1200.f; r.mesas = 1.f; r.plateau = 0.8f; r.rivers = 0.6f; r.hydraulic = 0.4f; r.thermal = 0.1f; r.talus = 2.5f;
        r.moisture = 0.95f; r.temperature = 0.8f; r.ranges = 0.f;
        e.targets = {0.65f, 0.4f, 0.f, 0.45f, 0.7f};
        e.layers = {L(M::LushGrass, 0, 1, 0, 0.45f, 0, 1, 0.4f), L(M::Sandstone, 0, 1, 0.6f, 99, 0, 1, 0.4f, 1.3f),
                    L(M::Moss, 0.6f, 1, 0, 0.6f, 0, 1, 0.5f, 0.8f), L(M::ForestFloor, 0, 0.5f, 0, 0.6f, 0.5f, 1, 0.4f),
                    L(M::Mud, 0, 0.3f, 0, 0.1f, 0.85f, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::JungleTree, 120.f, 0.7f, 0, 0.55f, 0, 1, 0.5f), P(F::TreeFern, 30.f, 0.6f, 0, 1, 0, 1, 0.6f),
                     P(F::Shrub, 40.f, 0.6f, 0.55f, 1, 0, 1, 0.6f), P(F::Fern, 100.f, 0.8f)};
        e.atmosphere = kTropicSky;
    }
    {
        auto& e = add("rift_valley", "Rift Valley", "Mountain", "A great tectonic trough between fault-block escarpments, with soda lakes.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 7; r.wFbm = 0.5f; r.wRidged = 0.25f; r.wEroded = 0.25f; r.warp = 0.2f; r.relief = 0.8f;
        r.heightRange = 1100.f; r.ranges = 2.f; r.rangeStrength = 0.55f; r.basins = 2.f; r.volcanoes = 0.6f; r.lavaFill = 0.f;
        r.rivers = 0.4f; r.hydraulic = 0.5f; r.thermal = 0.3f; r.talus = 1.1f; r.moisture = 0.35f; r.temperature = 0.75f;
        e.targets = {0.6f, 0.35f, 0.f, 0.35f, 0.6f};
        e.layers = {L(M::DryGrass, 0, 1, 0, 0.4f, 0, 0.6f, 0.4f), L(M::Grass, 0, 1, 0, 0.35f, 0.55f, 1, 0.4f, 0.8f),
                    L(M::Basalt, 0, 1, 0.5f, 99, 0, 1, 0.3f), L(M::Salt, 0, 0.2f, 0, 0.05f, 0.7f, 1, 0.4f, 0.7f),
                    L(M::RedSand, 0, 1, 0, 0.3f, 0, 0.3f, 0.5f, 0.6f)};
        e.foliage = {P(F::Acacia, 5.f, 0.35f), P(F::GrassTuft, 140.f, 0.4f), P(F::DesertShrub, 6.f, 0.5f), P(F::Boulder, 1.5f, 1.f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("loess_plateau", "Loess Plateau", "Arid", "Wind-laid silt hills dissected into countless gullies and terraced fields.");
        auto& r = e.recipe;
        r.baseFreq = 2.4f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.1f; r.wEroded = 0.3f; r.relief = 0.6f; r.heightRange = 300.f;
        r.plateau = 0.7f; r.terraces = 18; r.terraceStrength = 0.25f; r.rivers = 1.f; r.hydraulic = 1.f; r.thermal = 0.2f; r.talus = 1.2f;
        r.moisture = 0.25f; r.temperature = 0.6f; r.ranges = 0.f;
        e.targets = {0.5f, 0.45f, 0.f, 0.25f, 0.8f};
        e.layers = {L(M::Dirt, 0, 1, 0, 0.5f, 0, 1, 0.4f), L(M::DryGrass, 0, 1, 0, 0.25f, 0.2f, 1, 0.5f, 0.7f),
                    L(M::Sandstone, 0, 1, 0.6f, 99, 0, 1, 0.4f, 0.8f), L(M::Grass, 0, 1, 0, 0.15f, 0.5f, 1, 0.5f, 0.5f)};
        e.foliage = {P(F::GrassTuft, 80.f, 0.4f), P(F::Shrub, 10.f, 0.5f), P(F::Broadleaf, 2.f, 0.3f, 0, 1, 0.4f, 1, 0.8f)};
        e.atmosphere = kDesertSky;
    }
    {
        auto& e = add("cenote_plains", "Cenote Sinkhole Plains", "Tropical", "Flat limestone jungle pocked with collapsed water-filled sinkholes.");
        auto& r = e.recipe;
        r.baseFreq = 1.8f; r.octaves = 6; r.wFbm = 0.7f; r.wRidged = 0.f; r.wEroded = 0.3f; r.relief = 0.25f; r.heightRange = 80.f;
        r.flatten = 0.4f; r.craters = 16.f; r.basins = 2.f; r.rivers = 0.f; r.hydraulic = 0.1f; r.thermal = 0.1f; r.talus = 2.f;
        r.moisture = 0.85f; r.temperature = 0.85f; r.ranges = 0.f;
        e.targets = {0.3f, 0.1f, 0.f, 0.6f, 0.5f};
        e.layers = {L(M::ForestFloor, 0, 1, 0, 0.5f, 0, 1, 0.4f), L(M::LushGrass, 0, 1, 0, 0.3f, 0, 1, 0.5f, 0.7f),
                    L(M::Rock, 0, 1, 0.5f, 99, 0, 1, 0.3f, 1.2f), L(M::Moss, 0, 1, 0.3f, 1.f, 0.6f, 1, 0.5f, 0.6f)};
        e.foliage = {P(F::JungleTree, 100.f, 0.6f), P(F::Palm, 10.f, 0.4f), P(F::Fern, 80.f, 0.7f), P(F::Shrub, 40.f, 0.7f)};
        e.atmosphere = kTropicSky;
    }
    {
        auto& e = add("caldera_lake", "Caldera Lake", "Volcanic", "A collapsed volcano cradling a deep blue crater lake and island cone.");
        auto& r = e.recipe;
        r.baseFreq = 2.f; r.octaves = 8; r.wFbm = 0.4f; r.wRidged = 0.3f; r.wEroded = 0.3f; r.relief = 0.4f; r.heightRange = 1200.f;
        r.craters = 1.f; r.volcanoes = 1.f; r.lavaFill = 0.f; r.basins = 1.f; r.rivers = 0.4f; r.hydraulic = 0.6f; r.thermal = 0.3f;
        r.moisture = 0.6f; r.temperature = 0.45f; r.ranges = 0.f;
        e.targets = {0.6f, 0.35f, 0.f, 0.25f, 0.6f};
        e.layers = {L(M::Grass, 0, 0.7f, 0, 0.4f, 0.3f, 1, 0.4f), L(M::Ash, 0.4f, 1, 0, 0.6f, 0, 1, 0.4f, 0.8f),
                    L(M::Basalt, 0, 1, 0.5f, 99, 0, 1, 0.3f), L(M::ForestFloor, 0, 0.6f, 0, 0.5f, 0.6f, 1, 0.4f),
                    L(M::Pebbles, 0, 1, 0, 0.3f, 0.85f, 1, 0.4f, 0.6f)};
        e.foliage = {P(F::Conifer, 50.f, 0.6f, 0, 0.6f, 0.3f, 1, 0.6f), P(F::Shrub, 20.f, 0.6f), P(F::GrassTuft, 60.f, 0.5f), P(F::Boulder, 2.f, 1.f)};
        e.atmosphere = kClear;
    }
    {
        auto& e = add("martian_plains", "Martian Plains", "Exotic", "Rust-red dust plains, wind streaks, mesas and ancient dry channels.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 7; r.wFbm = 0.6f; r.wRidged = 0.15f; r.wEroded = 0.25f; r.warp = 0.3f; r.relief = 0.5f;
        r.heightRange = 900.f; r.craters = 10.f; r.mesas = 0.35f; r.canyons = 0.4f; r.dunes = 0.15f; r.rivers = 0.f; r.hydraulic = 0.f;
        r.thermal = 0.3f; r.wind = 0.4f; r.moisture = 0.f; r.temperature = 0.2f; r.ranges = 0.f;
        e.targets = {0.5f, 0.25f, 0.f, 0.4f, 0.6f};
        e.layers = {L(M::RedSand, 0, 1, 0, 0.4f, 0, 1, 0.35f), L(M::Sandstone, 0, 1, 0.4f, 99, 0, 1, 0.4f), L(M::Gravel, 0, 1, 0.2f, 0.6f, 0, 1, 0.5f, 0.6f)};
        e.foliage = {P(F::Boulder, 1.5f, 1.f), P(F::SmallRocks, 8.f, 1.f)};
        e.atmosphere = A({0.45f, 0.30f, 0.22f}, {0.85f, 0.62f, 0.45f}, {1.f, 0.88f, 0.75f}, {0.3f, 0.2f, 0.15f}, 0.55f);
    }
    {
        auto& e = add("chalk_downs", "Chalk Downs & White Cliffs", "Coastal", "Smooth grassy downland ending in brilliant white chalk cliffs.");
        auto& r = e.recipe;
        r.baseFreq = 1.6f; r.octaves = 6; r.wFbm = 0.6f; r.wBillow = 0.2f; r.wRidged = 0.f; r.wEroded = 0.2f; r.relief = 0.55f;
        r.heightRange = 160.f; r.seaLevel = 0.2f; r.islandMask = 0.2f; r.plateau = 0.85f; r.rivers = 0.3f; r.hydraulic = 0.2f;
        r.thermal = 0.6f; r.talus = 2.2f; r.moisture = 0.55f; r.temperature = 0.45f; r.ranges = 0.f;
        e.targets = {0.45f, 0.15f, 0.25f, 0.55f, 0.4f};
        e.layers = {L(M::Grass, 0.2f, 1, 0, 0.5f, 0, 1, 0.4f), L(M::Salt, 0, 1, 0.7f, 99, 0, 1, 0.3f, 1.3f),
                    L(M::Pebbles, 0, 0.23f, 0, 0.5f, 0, 1, 0.4f), L(M::Dirt, 0.2f, 1, 0.35f, 0.8f, 0, 1, 0.5f, 0.5f)};
        e.foliage = {P(F::GrassTuft, 200.f, 0.45f, 0.21f, 1), P(F::Flowers, 30.f, 0.3f, 0.21f, 1, 0, 1, 0.8f),
                     P(F::Broadleaf, 3.f, 0.3f, 0.25f, 1, 0.4f, 1, 0.9f), P(F::Shrub, 10.f, 0.4f, 0.21f, 1, 0, 1, 0.8f)};
    }

    // Names used by ZeraLands 1.x presets keep working.
    auto alias = [&](const char* id, std::vector<std::string> names) {
        for (auto& e : v)
            if (e.id == id) e.aliases.insert(e.aliases.end(), names.begin(), names.end());
    };
    alias("desert_erg", {"Desert (Erg)", "erg", "sand dunes", "dunes"});
    alias("desert_hamada", {"Desert (Hamada)", "hamada", "rocky desert", "bedrock desert"});
    alias("mountains_alpine", {"Mountains (Alpine)", "alpine", "alpine mountains"});
    alias("rolling_hills", {"mountains_rounded", "Mountains (Rounded Hills)", "rounded hills", "soft hills", "eroded hills"});
    alias("volcano_active", {"volcanic_active", "Volcanic (Active Volcano)", "volcano", "volcanic crater"});
    alias("archipelago", {"coastal_atolls", "Coastal (Atolls)", "atolls", "atoll"});
    alias("savanna", {"vegetated_savanna", "Vegetated (Savanna)"});
    alias("glacial_valley", {"snow_glacial_valley", "Snow (Glacial Valley)", "glacier"});
    alias("canyon_river", {"river_canyons", "River (Canyons)", "canyons"});
    alias("extreme_spikes", {"Extreme (Spikes)", "spikes"});
    alias("magma_fields", {"magma", "lava fields", "lava plains"});
    alias("rocky_crags", {"rocky", "crags"});

    return v;
}

std::string simplify(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

const std::vector<Environment>& environments() {
    static const std::vector<Environment> kEnvs = build();
    return kEnvs;
}

int findEnvironment(const std::string& query) {
    const auto& envs = environments();
    std::string q = simplify(query);
    if (q.empty()) return -1;
    for (size_t i = 0; i < envs.size(); ++i) {
        if (simplify(envs[i].id) == q || simplify(envs[i].name) == q) return int(i);
        for (const auto& a : envs[i].aliases)
            if (simplify(a) == q) return int(i);
    }
    for (size_t i = 0; i < envs.size(); ++i)
        if (simplify(envs[i].name).find(q) != std::string::npos || simplify(envs[i].id).find(q) != std::string::npos) return int(i);
    return -1;
}

std::vector<std::string> environmentCategories() {
    std::vector<std::string> cats;
    for (const auto& e : environments())
        if (std::find(cats.begin(), cats.end(), e.category) == cats.end()) cats.push_back(e.category);
    return cats;
}

}  // namespace zl
