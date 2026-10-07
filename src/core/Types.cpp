#include "core/Types.h"

namespace zl {

namespace {
constexpr Color3 C(float r, float g, float b) { return Color3{r, g, b}; }

const MaterialInfo kMaterials[kMaterialCount] = {
    {"Grass", C(0.27f, 0.42f, 0.16f), C(0.36f, 0.48f, 0.20f), 0.85f, 4.f, "grass meadow lawn turf"},
    {"Lush Grass", C(0.18f, 0.40f, 0.12f), C(0.26f, 0.50f, 0.15f), 0.80f, 4.f, "lush green grass jungle"},
    {"Dry Grass", C(0.55f, 0.50f, 0.28f), C(0.62f, 0.55f, 0.33f), 0.90f, 4.f, "dry savanna straw hay wheat"},
    {"Forest Floor", C(0.25f, 0.20f, 0.13f), C(0.32f, 0.27f, 0.15f), 0.90f, 4.f, "forest forrest leaves leaf litter needles"},
    {"Moss", C(0.22f, 0.33f, 0.12f), C(0.30f, 0.38f, 0.16f), 0.90f, 3.f, "moss mossy lichen"},
    {"Dirt", C(0.38f, 0.29f, 0.20f), C(0.45f, 0.35f, 0.24f), 0.92f, 4.f, "ground dirt soil earth"},
    {"Mud", C(0.25f, 0.20f, 0.15f), C(0.30f, 0.24f, 0.18f), 0.45f, 4.f, "mud wet swamp clay"},
    {"Sand", C(0.80f, 0.70f, 0.50f), C(0.86f, 0.77f, 0.57f), 0.95f, 4.f, "sand beach dune desert"},
    {"Red Sand", C(0.70f, 0.40f, 0.24f), C(0.78f, 0.48f, 0.30f), 0.95f, 4.f, "red sand mars ochre"},
    {"Pebbles", C(0.52f, 0.49f, 0.45f), C(0.60f, 0.57f, 0.52f), 0.80f, 2.f, "pebble pebbles shingle"},
    {"Gravel", C(0.47f, 0.45f, 0.42f), C(0.55f, 0.53f, 0.49f), 0.85f, 2.5f, "gravel scree rubble"},
    {"Rock", C(0.42f, 0.40f, 0.38f), C(0.50f, 0.48f, 0.45f), 0.80f, 8.f, "rock stone boulder granite"},
    {"Cliff", C(0.36f, 0.34f, 0.32f), C(0.46f, 0.43f, 0.40f), 0.80f, 12.f, "cliff rockface rock_face crag"},
    {"Sandstone", C(0.70f, 0.48f, 0.32f), C(0.78f, 0.58f, 0.40f), 0.85f, 10.f, "sandstone mesa canyon"},
    {"Basalt", C(0.17f, 0.16f, 0.16f), C(0.24f, 0.23f, 0.22f), 0.70f, 8.f, "basalt volcanic obsidian lavarock"},
    {"Snow", C(0.90f, 0.92f, 0.96f), C(0.96f, 0.97f, 1.00f), 0.60f, 6.f, "snow"},
    {"Ice", C(0.66f, 0.80f, 0.90f), C(0.78f, 0.88f, 0.95f), 0.15f, 8.f, "ice glacier frozen"},
    {"Lava", C(0.95f, 0.30f, 0.05f), C(0.15f, 0.05f, 0.03f), 0.60f, 10.f, "lava magma molten"},
    {"Ash", C(0.30f, 0.29f, 0.28f), C(0.38f, 0.36f, 0.34f), 0.95f, 4.f, "ash cinder burnt scorched"},
    {"Salt", C(0.92f, 0.90f, 0.86f), C(0.85f, 0.83f, 0.80f), 0.70f, 6.f, "salt crust flats"},
    {"Crystal", C(0.55f, 0.35f, 0.85f), C(0.30f, 0.75f, 0.90f), 0.10f, 6.f, "crystal amethyst quartz"},
    {"Asphalt", C(0.17f, 0.17f, 0.18f), C(0.22f, 0.22f, 0.23f), 0.75f, 6.f, "asphalt tarmac road"},
    {"Cobblestone", C(0.40f, 0.38f, 0.35f), C(0.30f, 0.28f, 0.26f), 0.80f, 3.f, "cobble cobblestone paving pavers"},
    {"Roman Stone", C(0.58f, 0.54f, 0.47f), C(0.45f, 0.42f, 0.37f), 0.80f, 4.f, "paving flagstone roman slab terrazzo"},
    {"Packed Dirt", C(0.48f, 0.38f, 0.26f), C(0.55f, 0.44f, 0.30f), 0.92f, 4.f, "path trail dirt_road packed"},
    {"Concrete", C(0.55f, 0.55f, 0.53f), C(0.62f, 0.62f, 0.60f), 0.85f, 4.f, "concrete cement"},
    {"Glow Panel", C(0.10f, 0.12f, 0.16f), C(0.10f, 0.85f, 1.00f), 0.30f, 6.f, "glow neon panel tech scifi metal_plates"},
    {"Rail Bed", C(0.35f, 0.32f, 0.30f), C(0.28f, 0.20f, 0.14f), 0.85f, 3.f, "ballast railbed rail"},
};

const FoliageInfo kFoliage[kFoliageCount] = {
    {"Conifer", ProxyShape::ConeTree, C(0.10f, 0.25f, 0.12f), C(0.28f, 0.18f, 0.10f), 18.f, 4.f, false},
    {"Broadleaf Tree", ProxyShape::RoundTree, C(0.20f, 0.38f, 0.12f), C(0.30f, 0.20f, 0.12f), 14.f, 5.f, false},
    {"Birch", ProxyShape::RoundTree, C(0.42f, 0.55f, 0.20f), C(0.85f, 0.83f, 0.78f), 12.f, 3.5f, false},
    {"Palm", ProxyShape::Palm, C(0.22f, 0.45f, 0.15f), C(0.45f, 0.35f, 0.22f), 11.f, 4.f, false},
    {"Jungle Tree", ProxyShape::RoundTree, C(0.08f, 0.32f, 0.10f), C(0.30f, 0.22f, 0.14f), 26.f, 6.f, false},
    {"Acacia", ProxyShape::RoundTree, C(0.38f, 0.45f, 0.18f), C(0.35f, 0.25f, 0.15f), 8.f, 9.f, false},
    {"Baobab", ProxyShape::RoundTree, C(0.35f, 0.42f, 0.20f), C(0.50f, 0.42f, 0.35f), 16.f, 14.f, false},
    {"Dead Tree", ProxyShape::Pole, C(0.30f, 0.26f, 0.22f), C(0.25f, 0.21f, 0.18f), 10.f, 5.f, false},
    {"Cactus", ProxyShape::Cactus, C(0.25f, 0.45f, 0.22f), C(0.22f, 0.40f, 0.20f), 5.f, 4.f, false},
    {"Shrub", ProxyShape::Bush, C(0.22f, 0.36f, 0.14f), C(0.25f, 0.20f, 0.12f), 1.6f, 1.8f, false},
    {"Desert Shrub", ProxyShape::Bush, C(0.48f, 0.47f, 0.30f), C(0.40f, 0.32f, 0.22f), 1.0f, 2.5f, false},
    {"Fern", ProxyShape::Fern, C(0.20f, 0.42f, 0.14f), C(0.18f, 0.30f, 0.12f), 1.2f, 1.2f, false},
    {"Cycad", ProxyShape::Palm, C(0.22f, 0.40f, 0.12f), C(0.40f, 0.30f, 0.18f), 4.f, 3.f, false},
    {"Tree Fern", ProxyShape::Palm, C(0.15f, 0.38f, 0.12f), C(0.30f, 0.22f, 0.15f), 8.f, 4.f, false},
    {"Grass Tuft", ProxyShape::Grass, C(0.35f, 0.50f, 0.18f), C(0.45f, 0.52f, 0.22f), 0.6f, 0.5f, false},
    {"Reeds", ProxyShape::Grass, C(0.42f, 0.48f, 0.22f), C(0.50f, 0.45f, 0.28f), 1.8f, 0.6f, false},
    {"Flowers", ProxyShape::Grass, C(0.85f, 0.65f, 0.20f), C(0.70f, 0.25f, 0.45f), 0.4f, 0.5f, false},
    {"Boulder", ProxyShape::Rock, C(0.45f, 0.43f, 0.40f), C(0.38f, 0.36f, 0.34f), 2.5f, 4.f, false},
    {"Small Rocks", ProxyShape::Rock, C(0.50f, 0.48f, 0.45f), C(0.42f, 0.40f, 0.38f), 0.6f, 1.f, false},
    {"Giant Mushroom", ProxyShape::Mushroom, C(0.75f, 0.30f, 0.25f), C(0.85f, 0.80f, 0.70f), 9.f, 6.f, false},
    {"Crystal Cluster", ProxyShape::Crystal, C(0.60f, 0.40f, 0.95f), C(0.35f, 0.85f, 0.95f), 4.f, 3.f, false},
    {"Charred Stump", ProxyShape::Stump, C(0.10f, 0.09f, 0.08f), C(0.20f, 0.17f, 0.14f), 2.f, 3.f, false},
    {"Mangrove", ProxyShape::RoundTree, C(0.15f, 0.35f, 0.15f), C(0.32f, 0.26f, 0.18f), 9.f, 5.f, false},
    {"Street Lamp", ProxyShape::Lamp, C(1.00f, 0.90f, 0.60f), C(0.25f, 0.25f, 0.27f), 8.f, 20.f, true},
    {"Telegraph Pole", ProxyShape::Pole, C(0.35f, 0.25f, 0.16f), C(0.30f, 0.22f, 0.14f), 9.f, 30.f, true},
    {"Torch Post", ProxyShape::Lamp, C(1.00f, 0.55f, 0.15f), C(0.30f, 0.20f, 0.12f), 2.5f, 25.f, true},
    {"Milestone", ProxyShape::Column, C(0.70f, 0.67f, 0.60f), C(0.60f, 0.57f, 0.50f), 1.4f, 60.f, true},
    {"Column", ProxyShape::Column, C(0.85f, 0.82f, 0.75f), C(0.70f, 0.67f, 0.60f), 9.f, 12.f, true},
    {"Neon Pylon", ProxyShape::Lamp, C(0.10f, 0.90f, 1.00f), C(0.15f, 0.15f, 0.20f), 12.f, 30.f, true},
    {"Wind Turbine", ProxyShape::Turbine, C(0.92f, 0.92f, 0.94f), C(0.80f, 0.80f, 0.82f), 90.f, 120.f, true},
    {"Ruin", ProxyShape::Column, C(0.45f, 0.42f, 0.38f), C(0.30f, 0.28f, 0.26f), 6.f, 15.f, true},
};

const char* kSplineNames[int(SplineKind::Count)] = {"Road", "Railway", "Path", "Racetrack", "Canal", "Aqueduct", "Maglev", "Lava Channel", "Custom"};
}  // namespace

const MaterialInfo& materialInfo(MaterialKind k) { return kMaterials[std::clamp(int(k), 0, kMaterialCount - 1)]; }
const FoliageInfo& foliageInfo(FoliageKind k) { return kFoliage[std::clamp(int(k), 0, kFoliageCount - 1)]; }
const char* splineKindName(SplineKind k) { return kSplineNames[std::clamp(int(k), 0, int(SplineKind::Count) - 1)]; }

}  // namespace zl
