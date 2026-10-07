#include "core/Project.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace zl {

namespace fs = std::filesystem;
using json = nlohmann::json;

json settingsToJson(const GenSettings& s) {
    return json{{"environment", environments()[std::clamp(s.envIndex, 0, int(environments().size()) - 1)].id},
                {"era", eras()[std::clamp(s.eraIndex, 0, int(eras().size()) - 1)].id},
                {"seed", s.seedText},
                {"variation", s.variation},
                {"prompt", s.prompt},
                {"resolution", s.resolution},
                {"worldSize", s.worldSize},
                {"heightScale", s.heightScale},
                {"roughness", s.roughness},
                {"erosion", s.erosion},
                {"waterLevel", s.waterLevel},
                {"featureScale", s.featureScale},
                {"riverAmount", s.riverAmount},
                {"roads", s.roads},
                {"rail", s.rail},
                {"paths", s.paths},
                {"racetrack", s.racetrack},
                {"roadDensity", s.roadDensity},
                {"pathDensity", s.pathDensity},
                {"foliage", s.foliage},
                {"props", s.props},
                {"foliageDensity", s.foliageDensity},
                {"candidates", s.candidates},
                {"refineSteps", s.refineSteps}};
}

GenSettings settingsFromJson(const json& j) {
    GenSettings s;
    auto get = [&](const char* k, auto& v) {
        if (j.contains(k)) j.at(k).get_to(v);
    };
    if (j.contains("environment")) s.envIndex = std::max(0, findEnvironment(j["environment"].get<std::string>()));
    if (j.contains("era")) s.eraIndex = std::max(0, findEra(j["era"].get<std::string>()));
    get("seed", s.seedText);
    get("variation", s.variation);
    get("prompt", s.prompt);
    get("resolution", s.resolution);
    get("worldSize", s.worldSize);
    get("heightScale", s.heightScale);
    get("roughness", s.roughness);
    get("erosion", s.erosion);
    get("waterLevel", s.waterLevel);
    get("featureScale", s.featureScale);
    get("riverAmount", s.riverAmount);
    get("roads", s.roads);
    get("rail", s.rail);
    get("paths", s.paths);
    get("racetrack", s.racetrack);
    get("roadDensity", s.roadDensity);
    get("pathDensity", s.pathDensity);
    get("foliage", s.foliage);
    get("props", s.props);
    get("foliageDensity", s.foliageDensity);
    get("candidates", s.candidates);
    get("refineSteps", s.refineSteps);
    return s;
}

json splineToJsonFull(const Spline& s) {
    json pts = json::array();
    for (const auto& p : s.points) pts.push_back({p.pos.x, p.pos.y, p.pos.z, p.width});
    return json{{"name", s.name},         {"kind", int(s.kind)},        {"material", int(s.material)}, {"closed", s.closed},
                {"width", s.width},       {"shoulder", s.shoulder},     {"maxBank", s.maxBank},        {"markings", s.markings},
                {"elevated", s.elevated}, {"waterFilled", s.waterFilled}, {"kerbs", s.kerbs},          {"conform", s.conform},
                {"prop", int(s.prop)},    {"propSpacing", s.propSpacing}, {"points", pts}};
}

Spline splineFromJson(const json& j) {
    Spline s;
    s.name = j.value("name", std::string("Spline"));
    s.kind = SplineKind(j.value("kind", 0));
    s.material = MaterialKind(j.value("material", int(MaterialKind::Asphalt)));
    s.closed = j.value("closed", false);
    s.width = j.value("width", 6.f);
    s.shoulder = j.value("shoulder", 4.f);
    s.maxBank = j.value("maxBank", 0.f);
    s.markings = j.value("markings", false);
    s.elevated = j.value("elevated", false);
    s.waterFilled = j.value("waterFilled", false);
    s.kerbs = j.value("kerbs", false);
    s.conform = j.value("conform", true);
    s.prop = FoliageKind(j.value("prop", int(FoliageKind::Count)));
    s.propSpacing = j.value("propSpacing", 50.f);
    if (j.contains("points"))
        for (const auto& p : j["points"]) {
            SplinePoint sp;
            sp.pos = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
            sp.width = p.size() > 3 ? p[3].get<float>() : 0.f;
            s.points.push_back(sp);
        }
    return s;
}

// ---- binary sidecar: magic, version, then tagged float grids + foliage
namespace {
const char kMagic[8] = {'Z', 'L', 'B', 'I', 'N', '0', '0', '2'};

void writeGrid(std::ofstream& f, const Grid& g) {
    int32_t w = g.width(), h = g.height();
    f.write(reinterpret_cast<const char*>(&w), 4);
    f.write(reinterpret_cast<const char*>(&h), 4);
    if (w * h > 0) f.write(reinterpret_cast<const char*>(g.data()), std::streamsize(sizeof(float) * size_t(w) * h));
}
bool readGrid(std::ifstream& f, Grid& g) {
    int32_t w = 0, h = 0;
    f.read(reinterpret_cast<char*>(&w), 4);
    f.read(reinterpret_cast<char*>(&h), 4);
    if (!f || w < 0 || h < 0 || w > 16384 || h > 16384) return false;
    g.resize(w, h);
    if (w * h > 0) f.read(reinterpret_cast<char*>(g.data()), std::streamsize(sizeof(float) * size_t(w) * h));
    return bool(f);
}
}  // namespace

bool saveProject(const Scene& sc, const std::string& path, std::string* err) {
    if (!sc.valid()) {
        if (err) *err = "no terrain";
        return false;
    }
    fs::path p = fs::u8path(path);
    if (p.extension() != ".zlproj") p += ".zlproj";
    fs::path bin = p;
    bin.replace_extension(".zlbin");
    json j;
    j["format"] = "zeralands-project";
    j["version"] = 2;
    j["settings"] = settingsToJson(sc.settings);
    j["worldSize"] = sc.terrain.worldSize;
    j["heightRange"] = sc.terrain.heightRange;
    j["seaLevel"] = sc.terrain.seaLevel;
    j["genome"] = {{"macroSeed", sc.genome.macroSeed}, {"detailSeed", sc.genome.detailSeed}};
    j["report"] = {{"summary", sc.report.summary}, {"intent", sc.report.intent}};
    json sp = json::array();
    for (const auto& s : sc.splines) sp.push_back(splineToJsonFull(s));
    j["splines"] = sp;
    j["binary"] = bin.filename().u8string();
    {
        std::ofstream f(p);
        if (!f) {
            if (err) *err = "cannot write " + p.u8string();
            return false;
        }
        f << j.dump(1);
    }
    std::ofstream f(bin, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot write " + bin.u8string();
        return false;
    }
    f.write(kMagic, 8);
    writeGrid(f, sc.terrain.baseHeight);
    writeGrid(f, sc.terrain.water);
    writeGrid(f, sc.terrain.flow);
    writeGrid(f, sc.terrain.moisture);
    writeGrid(f, sc.terrain.lava);
    uint64_t n = sc.foliage.size();
    f.write(reinterpret_cast<const char*>(&n), 8);
    if (n) f.write(reinterpret_cast<const char*>(sc.foliage.data()), std::streamsize(sizeof(FoliageInstance) * n));
    return bool(f);
}

bool loadProject(Scene& out, const std::string& path, std::string* err) {
    fs::path p = fs::u8path(path);
    std::ifstream jf(p);
    if (!jf) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    json j;
    try {
        jf >> j;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
    Scene sc;
    sc.settings = settingsFromJson(j["settings"]);
    sc.terrain.worldSize = j.value("worldSize", sc.settings.worldSize);
    sc.terrain.heightRange = j.value("heightRange", 600.f);
    sc.terrain.seaLevel = j.value("seaLevel", -1.f);
    if (j.contains("genome")) {
        sc.genome.macroSeed = j["genome"].value("macroSeed", uint64_t(1));
        sc.genome.detailSeed = j["genome"].value("detailSeed", uint64_t(1));
    }
    if (j.contains("report")) {
        sc.report.summary = j["report"].value("summary", std::string());
        if (j["report"].contains("intent")) sc.report.intent = j["report"]["intent"].get<std::vector<std::string>>();
    }
    for (const auto& s : j["splines"]) sc.splines.push_back(splineFromJson(s));
    fs::path bin = p.parent_path() / fs::u8path(j.value("binary", p.stem().u8string() + ".zlbin"));
    std::ifstream f(bin, std::ios::binary);
    char magic[8];
    f.read(magic, 8);
    if (!f || std::memcmp(magic, kMagic, 8) != 0) {
        if (err) *err = "missing or invalid " + bin.u8string();
        return false;
    }
    Terrain& t = sc.terrain;
    if (!readGrid(f, t.baseHeight) || !readGrid(f, t.water) || !readGrid(f, t.flow) || !readGrid(f, t.moisture) || !readGrid(f, t.lava)) {
        if (err) *err = "corrupt terrain data";
        return false;
    }
    uint64_t n = 0;
    f.read(reinterpret_cast<char*>(&n), 8);
    if (n > 50000000ull) n = 0;
    sc.foliage.resize(size_t(n));
    if (n) f.read(reinterpret_cast<char*>(sc.foliage.data()), std::streamsize(sizeof(FoliageInstance) * n));
    t.height = t.baseHeight;
    const auto& env = environments()[std::clamp(sc.settings.envIndex, 0, int(environments().size()) - 1)];
    const auto& era = eras()[std::clamp(sc.settings.eraIndex, 0, int(eras().size()) - 1)];
    sc.atmosphere = blendAtmosphere(env, era);
    int hv = out.heightVersion;
    out = std::move(sc);
    out.heightVersion = hv;
    restampSplines(out);
    out.log.push_back("Loaded project " + p.u8string());
    return true;
}

std::string userDataDir() {
    fs::path base;
#ifdef _WIN32
    if (const char* a = std::getenv("APPDATA")) base = fs::u8path(a) / "ZeraLands";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) base = fs::u8path(h) / "Library" / "Application Support" / "ZeraLands";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME")) base = fs::u8path(x) / "zeralands";
    else if (const char* h = std::getenv("HOME")) base = fs::u8path(h) / ".local" / "share" / "zeralands";
#endif
    if (base.empty()) base = fs::current_path() / "zeralands_data";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.u8string();
}

}  // namespace zl
