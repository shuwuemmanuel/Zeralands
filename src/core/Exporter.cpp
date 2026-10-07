#include "core/Exporter.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace zl {

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string sanitize(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') out += c;
        else if (c == ' ' || c == '/' || c == '(' || c == ')') {
            if (!out.empty() && out.back() != '_') out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.substr(0, 80);
}

std::string defaultExportName(const Scene& scene) {
    const auto& env = environments()[std::clamp(scene.settings.envIndex, 0, int(environments().size()) - 1)];
    const auto& era = eras()[std::clamp(scene.settings.eraIndex, 0, int(eras().size()) - 1)];
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s_%s_%d_s%s_v%d", sanitize(env.id).c_str(), sanitize(era.id).c_str(), scene.terrain.res(),
                  sanitize(scene.settings.seedText).c_str(), scene.settings.variation);
    return buf;
}

static void shadeColor(const Scene& sc, int x, int y, float& r, float& g, float& b) {
    const Terrain& t = sc.terrain;
    const SplatMap& sp = sc.splat;
    r = g = b = 0;
    if (sp.res == t.res()) {
        for (int l = 0; l < sp.layerCount; ++l) {
            float w = sp.weight(l, x, y);
            if (w <= 0) continue;
            const auto& mi = materialInfo(sp.layers[l]);
            r += w * mi.albedo.r;
            g += w * mi.albedo.g;
            b += w * mi.albedo.b;
        }
    } else {
        r = g = b = t.height.at(x, y);
    }
}

void renderPreview(const Scene& sc, PreviewMode mode, int size, Image8& out) {
    out.w = out.h = size;
    out.c = 4;
    out.px.assign(size_t(size) * size * 4, 255);
    if (!sc.valid()) return;
    const Terrain& t = sc.terrain;
    const int res = t.res();
    const float k = float(res - 1) / float(std::max(1, size - 1));
    const Vec3 sun = Vec3(-0.6f, 0.75f, -0.35f).normalized();
    parallelFor(0, size, [&](int py) {
        for (int px = 0; px < size; ++px) {
            int x = std::min(res - 1, int(float(px) * k + 0.5f)), y = std::min(res - 1, int(float(py) * k + 0.5f));
            float h = t.height.at(x, y);
            float r = h, g = h, b = h;
            Vec3 n = t.normalAt(x, y);
            float light = clamp01(dot(n, sun)) * 0.85f + 0.25f;
            bool wet = !t.water.empty() && t.water.at(x, y) > h + 1e-6f;
            switch (mode) {
                case PreviewMode::Height: break;
                case PreviewMode::Slope: {
                    float s = clamp01(t.slopeAt(x, y) / 1.5f);
                    r = s; g = 1.f - s; b = 0.2f;
                    break;
                }
                case PreviewMode::Moisture: {
                    float m = t.moisture.empty() ? 0 : t.moisture.at(x, y);
                    r = 0.6f * (1 - m); g = 0.4f + 0.3f * m; b = 0.2f + 0.8f * m;
                    break;
                }
                case PreviewMode::Water: {
                    float f = t.flow.empty() ? 0 : t.flow.at(x, y);
                    r = g = b = h * 0.6f;
                    if (wet) { r = 0.1f; g = 0.35f; b = 0.85f; }
                    else b = std::max(b, smoothstepf(0.5f, 0.9f, f));
                    break;
                }
                case PreviewMode::Shaded:
                case PreviewMode::Materials: {
                    shadeColor(sc, x, y, r, g, b);
                    if (mode == PreviewMode::Shaded) { r *= light; g *= light; b *= light; }
                    if (!t.lava.empty() && t.lava.at(x, y) > 0.3f) { r = 1.f; g = 0.35f; b = 0.05f; }
                    if (!t.roadMask.empty() && t.roadMask.at(x, y) > 0.6f) { r = r * 0.4f + 0.18f; g = g * 0.4f + 0.16f; b = b * 0.4f + 0.14f; }
                    if (wet) {
                        float depth = clamp01((t.water.at(x, y) - h) * t.heightRange / 25.f);
                        Color3 wc = sc.atmosphere.water;
                        r = lerpf(r, wc.r, 0.6f + 0.4f * depth);
                        g = lerpf(g, wc.g, 0.6f + 0.4f * depth);
                        b = lerpf(b, wc.b, 0.6f + 0.4f * depth);
                    }
                    break;
                }
            }
            uint8_t* p = &out.px[(size_t(py) * size + px) * 4];
            p[0] = uint8_t(std::lround(clamp01(r) * 255.f));
            p[1] = uint8_t(std::lround(clamp01(g) * 255.f));
            p[2] = uint8_t(std::lround(clamp01(b) * 255.f));
            p[3] = 255;
        }
    });
}

static json splineToJson(const Spline& s) {
    json j;
    j["name"] = s.name;
    j["kind"] = splineKindName(s.kind);
    j["kindId"] = int(s.kind);
    j["material"] = materialInfo(s.material).name;
    j["materialId"] = int(s.material);
    j["closed"] = s.closed;
    j["width"] = s.width;
    j["shoulder"] = s.shoulder;
    j["maxBank"] = s.maxBank;
    j["markings"] = s.markings;
    j["elevated"] = s.elevated;
    j["waterFilled"] = s.waterFilled;
    j["kerbs"] = s.kerbs;
    j["conform"] = s.conform;
    j["prop"] = int(s.prop);
    j["propSpacing"] = s.propSpacing;
    json pts = json::array();
    for (const auto& p : s.points) pts.push_back({p.pos.x, p.pos.y, p.pos.z, p.width});
    j["points"] = pts;
    return j;
}

std::vector<std::string> exportScene(const Scene& sc, const ExportOptions& opt, std::vector<std::string>& log) {
    std::vector<std::string> written;
    if (!sc.valid()) {
        log.push_back("Nothing to export - generate or import a terrain first.");
        return written;
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(opt.directory), ec);
    const std::string base = (fs::u8path(opt.directory) / fs::u8path(opt.baseName.empty() ? defaultExportName(sc) : opt.baseName)).u8string();
    const Terrain& t = sc.terrain;
    const int res = t.res();
    auto note = [&](bool ok, const std::string& path) {
        if (ok) written.push_back(path);
        else log.push_back("Failed to write " + path);
    };

    // Heightmaps are exported normalized to their own min/max for maximum 16-bit precision.
    float mn, mx;
    t.height.minMax(mn, mx);
    Grid norm = t.height;
    for (auto& v : norm.vec()) v = (v - mn) / std::max(1e-9f, mx - mn);
    auto u16 = toU16(norm);
    if (opt.heightPng16) note(savePng16Gray(base + "_height16.png", res, res, u16.data()), base + "_height16.png");
    if (opt.heightRaw16) note(saveRaw16(base + "_height.r16", u16, true), base + "_height.r16");

    if (opt.splatmaps && sc.splat.res == res) {
        note(savePng8(base + "_splat_0-3.png", res, res, 4, sc.splat.weights0.data()), base + "_splat_0-3.png");
        if (sc.splat.layerCount > 4) note(savePng8(base + "_splat_4-7.png", res, res, 4, sc.splat.weights1.data()), base + "_splat_4-7.png");
        std::vector<uint8_t> layer(size_t(res) * res);
        for (int l = 0; l < sc.splat.layerCount; ++l) {
            const auto& src = l < 4 ? sc.splat.weights0 : sc.splat.weights1;
            for (size_t i = 0; i < layer.size(); ++i) layer[i] = src[i * 4 + (l & 3)];
            std::string p = base + "_layer" + std::to_string(l) + "_" + sanitize(materialInfo(sc.splat.layers[l]).name) + ".png";
            note(savePng8(p, res, res, 1, layer.data()), p);
        }
    }
    if (opt.normalMap) {
        std::vector<uint8_t> nm(size_t(res) * res * 3);
        parallelFor(0, res, [&](int y) {
            for (int x = 0; x < res; ++x) {
                Vec3 n = t.normalAt(x, y);
                size_t i = (size_t(y) * res + x) * 3;
                nm[i] = uint8_t(std::lround((n.x * 0.5f + 0.5f) * 255.f));
                nm[i + 1] = uint8_t(std::lround((n.z * 0.5f + 0.5f) * 255.f));
                nm[i + 2] = uint8_t(std::lround((n.y * 0.5f + 0.5f) * 255.f));
            }
        });
        note(savePng8(base + "_normal.png", res, res, 3, nm.data()), base + "_normal.png");
    }
    if (opt.colorPreview) {
        Image8 img;
        renderPreview(sc, PreviewMode::Shaded, std::min(res, 2048), img);
        note(savePng8(base + "_preview.png", img.w, img.h, 4, img.px.data()), base + "_preview.png");
    }
    if (opt.foliage && !sc.foliage.empty()) {
        std::ofstream csv(fs::u8path(base + "_foliage.csv"));
        csv << "kind,name,x_m,y_m,z_m,scale,rotation_rad,painted\n";
        for (const auto& f : sc.foliage)
            csv << f.kind << ',' << foliageInfo(FoliageKind(f.kind)).name << ',' << f.x << ',' << f.y << ',' << f.z << ',' << f.scale << ','
                << f.rotation << ',' << ((f.flags & 1) ? 1 : 0) << '\n';
        note(bool(csv), base + "_foliage.csv");
    }
    if (opt.splines) {
        json js = json::array();
        for (const auto& s : sc.splines) js.push_back(splineToJson(s));
        std::ofstream f(fs::u8path(base + "_splines.json"));
        f << js.dump(1);
        note(bool(f), base + "_splines.json");
    }
    if (opt.mesh) {
        int mres = std::min(res, std::max(17, opt.meshMaxRes));
        Grid m = t.height.resampled(mres, mres);
        std::ofstream f(fs::u8path(base + "_mesh.obj"));
        f << "# ZeraLands terrain mesh\n";
        float cell = t.worldSize / float(mres - 1);
        for (int y = 0; y < mres; ++y)
            for (int x = 0; x < mres; ++x) f << "v " << x * cell << ' ' << m.at(x, y) * t.heightRange << ' ' << y * cell << '\n';
        for (int y = 0; y < mres; ++y)
            for (int x = 0; x < mres; ++x) f << "vt " << float(x) / (mres - 1) << ' ' << 1.f - float(y) / (mres - 1) << '\n';
        for (int y = 0; y + 1 < mres; ++y)
            for (int x = 0; x + 1 < mres; ++x) {
                int a = y * mres + x + 1, b = a + 1, c = a + mres, d = c + 1;
                f << "f " << a << '/' << a << ' ' << c << '/' << c << ' ' << b << '/' << b << '\n';
                f << "f " << b << '/' << b << ' ' << c << '/' << c << ' ' << d << '/' << d << '\n';
            }
        note(bool(f), base + "_mesh.obj");
    }
    if (opt.metadata) {
        const auto& env = environments()[std::clamp(sc.settings.envIndex, 0, int(environments().size()) - 1)];
        const auto& era = eras()[std::clamp(sc.settings.eraIndex, 0, int(eras().size()) - 1)];
        json j;
        j["generator"] = "ZeraLands 2.0";
        j["environment"] = env.name;
        j["era"] = era.name;
        j["seed"] = sc.settings.seedText;
        j["variation"] = sc.settings.variation;
        j["prompt"] = sc.settings.prompt;
        j["resolution"] = res;
        j["worldSizeMeters"] = t.worldSize;
        j["heightRangeMeters"] = t.heightRange;
        j["exportedMinMeters"] = mn * t.heightRange;
        j["exportedMaxMeters"] = mx * t.heightRange;
        j["seaLevelNormalized"] = t.seaLevel;
        float zRange = (mx - mn) * t.heightRange;
        j["unreal"] = {{"scaleXYcm", t.cellSize() * 100.f}, {"scaleZ", zRange * 100.f / 512.f}, {"locationZcm", (mn * t.heightRange + zRange * 0.5f) * 100.f},
                       {"note", "Import _height.r16 / _height16.png in Landscape mode with these scales; use _layerN_*.png as weightmaps."}};
        j["unity"] = {{"terrainWidth", t.worldSize}, {"terrainHeight", zRange}, {"heightmapResolution", res}};
        json layers = json::array();
        for (int l = 0; l < sc.splat.layerCount; ++l) layers.push_back(materialInfo(sc.splat.layers[l]).name);
        j["materialLayers"] = layers;
        j["director"] = sc.report.summary;
        j["intent"] = sc.report.intent;
        j["log"] = sc.log;
        j["foliageCount"] = sc.foliage.size();
        j["splineCount"] = sc.splines.size();
        std::ofstream f(fs::u8path(base + "_metadata.json"));
        f << j.dump(2);
        note(bool(f), base + "_metadata.json");
    }
    log.push_back("Exported " + std::to_string(written.size()) + " files to " + opt.directory);
    return written;
}

}  // namespace zl
