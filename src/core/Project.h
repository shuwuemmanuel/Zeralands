// ZeraLands - project files: <name>.zlproj (JSON: settings, splines, report) + <name>.zlbin (terrain & foliage).
#pragma once

#include "core/Generator.h"

#include <nlohmann/json.hpp>

#include <string>

namespace zl {

nlohmann::json settingsToJson(const GenSettings& s);
GenSettings settingsFromJson(const nlohmann::json& j);
nlohmann::json splineToJsonFull(const Spline& s);
Spline splineFromJson(const nlohmann::json& j);

bool saveProject(const Scene& scene, const std::string& path, std::string* err);
bool loadProject(Scene& scene, const std::string& path, std::string* err);

// Per-user data directory (brain, GIS tile cache, preferences).
std::string userDataDir();

}  // namespace zl
