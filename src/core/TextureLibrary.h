// ZeraLands - PBR texture library: folder scanning, map detection, material classification and
// procedural (tileable) fallback textures.
#pragma once

#include "core/Types.h"

#include <array>
#include <string>
#include <vector>

namespace zl {

struct TextureSet {
    std::string name;        // e.g. "Ground14"
    std::string folder;      // containing folder
    std::string albedo, normal, roughness, ao, height, specular, gloss;
    bool normalDirectX = false;
    std::array<float, kMaterialCount> score{};   // keyword match score per material kind
    MaterialKind best = MaterialKind::Count;
};

// Packed GPU-ready texels for one material: albedoHeight (rgb + height in a), normalRough (nx, ny, ao, roughness).
struct MaterialPixels {
    int size = 0;
    std::vector<uint8_t> albedoHeight;
    std::vector<uint8_t> normalRough;
    std::string source;      // texture set name or "procedural"
};

class TextureLibrary {
public:
    // Recursively scans `root`; returns number of usable sets.
    int scan(const std::string& root);
    const std::vector<TextureSet>& sets() const { return sets_; }
    const std::string& root() const { return root_; }

    // Candidate sets (indices, best first) for a material kind.
    std::vector<int> candidates(MaterialKind k) const;
    // Picks a set per material (variety from seed among equally good candidates). -1 = procedural.
    void autoAssign(uint64_t seed);
    int assigned(MaterialKind k) const { return assign_[size_t(k)]; }
    void setAssigned(MaterialKind k, int setIndex) { assign_[size_t(k)] = setIndex; }

    // Decodes + resizes a set into packed texels (falls back to procedural if decoding fails).
    MaterialPixels load(MaterialKind k, int size) const;

private:
    std::string root_;
    std::vector<TextureSet> sets_;
    std::array<int, kMaterialCount> assign_{};
};

// Seamlessly tiling procedural material, used when no library texture is assigned.
MaterialPixels proceduralMaterial(MaterialKind k, int size, uint64_t seed = 7);

}  // namespace zl
