// ZeraLands - exporting scenes for game engines (Unreal, Unity, Godot, Blender) and CPU preview rendering.
#pragma once

#include "core/Generator.h"
#include "core/ImageIO.h"

#include <string>
#include <vector>

namespace zl {

struct ExportOptions {
    std::string directory = "exports";
    std::string baseName;          // empty = auto from env/seed/variation
    bool heightPng16 = true;
    bool heightRaw16 = true;       // UE-ready .r16 (little endian)
    bool splatmaps = true;         // per-layer grayscale weightmaps + RGBA packs
    bool normalMap = true;
    bool colorPreview = true;
    bool foliage = true;           // CSV + JSON instance lists
    bool splines = true;           // JSON
    bool mesh = false;             // OBJ (decimated)
    int meshMaxRes = 513;
    bool metadata = true;
};

// Writes the requested files; returns written paths (and pushes errors into `log`).
std::vector<std::string> exportScene(const Scene& scene, const ExportOptions& opt, std::vector<std::string>& log);
std::string defaultExportName(const Scene& scene);

enum class PreviewMode { Height, Shaded, Materials, Slope, Water, Moisture };
// CPU render of the terrain (used by the 2D view and preview export). out is RGBA8, size x size.
void renderPreview(const Scene& scene, PreviewMode mode, int size, Image8& out);

}  // namespace zl
