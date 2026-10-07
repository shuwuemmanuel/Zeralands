// ZeraLands headless generator: batch generation, heightmap import, GIS ripping, export and brain training.
#include "core/Brain.h"
#include "core/Exporter.h"
#include "core/Gis.h"
#include "core/Project.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace zl;

static void usage() {
    std::puts(
        "ZeraLands CLI 2.0\n"
        "  zeralands_cli [options]\n\n"
        "Generation:\n"
        "  --env NAME            environment (id or name, see --list)\n"
        "  --era NAME            era (id or name, see --list)\n"
        "  --seed TEXT           seed (number or any text)\n"
        "  --variation N         variation of the seed (same seed + new variation = close but different)\n"
        "  --prompt \"TEXT\"       natural-language intent, e.g. \"huge snowy peaks with a few lakes\"\n"
        "  --res N               resolution (UE friendly: 505, 1009, 2017, 4033)\n"
        "  --world M             world size in meters\n"
        "  --height-scale X      multiply the environment height range\n"
        "  --erosion X           erosion multiplier\n"
        "  --roads / --no-roads  --rail  --paths / --no-paths  --race  --no-foliage  --no-props\n"
        "Sources:\n"
        "  --import FILE         start from a heightmap image (png/jpg/tga/webp/raw/r16)\n"
        "  --import-erosion X    erosion applied to the imported map (0..1, default 0)\n"
        "  --height-range M      meters represented by the imported/GIS map\n"
        "  --gis LAT LON KM      rip real-world elevation around a coordinate\n"
        "  --gis-place N         rip famous place #N (see --list)\n"
        "Output:\n"
        "  --out DIR             export directory (default exports)\n"
        "  --name NAME           export base name\n"
        "  --mesh                also write an OBJ mesh\n"
        "  --previews            write preview PNGs for every view mode\n"
        "  --save-project FILE   write a .zlproj project\n"
        "Brain:\n"
        "  --no-brain            don't record this map in ZeraBrain\n"
        "  --brain-train N       run N training epochs over all recorded maps\n"
        "  --brain-stats         print ZeraBrain statistics\n"
        "  --list                list environments, eras and GIS places\n");
}

int main(int argc, char** argv) {
    GenSettings s;
    s.resolution = 1009;
    std::string importPath, outDir = "exports", name, projectPath;
    float importErosion = 0.f, heightRange = 0.f;
    bool gis = false, mesh = false, previews = false, useBrain = true, brainStats = false, doGenerate = true;
    int brainEpochs = 0;
    GisRequest gr;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--list") {
            std::puts("Environments:");
            for (const auto& e : environments()) std::printf("  %-20s %-34s [%s]\n", e.id.c_str(), e.name.c_str(), e.category.c_str());
            std::puts("Eras:");
            for (const auto& e : eras()) std::printf("  %-20s %s\n", e.id.c_str(), e.name.c_str());
            std::puts("GIS places:");
            int k = 0;
            for (const auto& p : famousPlaces()) std::printf("  %2d %-34s %.4f, %.4f (%.0f km)\n", k++, p.name, p.lat, p.lon, p.sizeKm);
            return 0;
        }
        else if (a == "--env") { s.envIndex = findEnvironment(next()); if (s.envIndex < 0) { std::fputs("unknown environment\n", stderr); return 2; } }
        else if (a == "--era") { s.eraIndex = findEra(next()); if (s.eraIndex < 0) { std::fputs("unknown era\n", stderr); return 2; } }
        else if (a == "--seed") s.seedText = next();
        else if (a == "--variation") s.variation = std::stoi(next());
        else if (a == "--prompt") s.prompt = next();
        else if (a == "--res") s.resolution = std::stoi(next());
        else if (a == "--world") s.worldSize = std::stof(next());
        else if (a == "--height-scale") s.heightScale = std::stof(next());
        else if (a == "--erosion") s.erosion = std::stof(next());
        else if (a == "--roads") s.roads = true;
        else if (a == "--no-roads") s.roads = false;
        else if (a == "--rail") s.rail = true;
        else if (a == "--paths") s.paths = true;
        else if (a == "--no-paths") s.paths = false;
        else if (a == "--race") s.racetrack = true;
        else if (a == "--no-foliage") s.foliage = false;
        else if (a == "--no-props") s.props = false;
        else if (a == "--import") importPath = next();
        else if (a == "--import-erosion") importErosion = std::stof(next());
        else if (a == "--height-range") heightRange = std::stof(next());
        else if (a == "--gis") { gis = true; gr.lat = std::stod(next()); gr.lon = std::stod(next()); gr.sizeKm = std::stof(next()); }
        else if (a == "--gis-place") {
            int k = std::stoi(next());
            if (k < 0 || k >= int(famousPlaces().size())) { std::fputs("bad place index\n", stderr); return 2; }
            const auto& p = famousPlaces()[size_t(k)];
            gis = true; gr.lat = p.lat; gr.lon = p.lon; gr.sizeKm = p.sizeKm;
            int e = findEnvironment(p.hint);
            if (e >= 0) s.envIndex = e;
        }
        else if (a == "--out") outDir = next();
        else if (a == "--name") name = next();
        else if (a == "--mesh") mesh = true;
        else if (a == "--previews") previews = true;
        else if (a == "--save-project") projectPath = next();
        else if (a == "--no-brain") useBrain = false;
        else if (a == "--brain-train") { brainEpochs = std::stoi(next()); doGenerate = false; }
        else if (a == "--brain-stats") { brainStats = true; doGenerate = false; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }

    Brain brain;
    if (useBrain || brainEpochs > 0 || brainStats) brain.open();
    if (brainEpochs > 0) {
        Progress p;
        p.onStage = [](const std::string& st, float f) { std::printf("\r%-40s %5.1f%%", st.c_str(), f * 100.f); std::fflush(stdout); };
        brain.train(brainEpochs, &p);
        std::puts("");
    }
    if (brainStats || brainEpochs > 0) {
        BrainStats st = brain.stats();
        std::printf("ZeraBrain at %s\n  samples: %d\n  train steps: %lld\n  parameters: %zu\n  losses: intent %.4f  shape %.4f  latent %.4f\n",
                    st.dir.c_str(), st.samples, st.trainSteps, st.parameters, st.intentLoss, st.shapeLoss, st.latentLoss);
    }
    if (!doGenerate && importPath.empty() && !gis) return 0;

    Progress progress;
    std::string lastStage;
    progress.onStage = [&](const std::string& st, float f) {
        if (st != lastStage) { std::printf("\n[%5.1f%%] %s", f * 100.f, st.c_str()); lastStage = st; }
        std::fflush(stdout);
    };

    Scene scene;
    std::string source = "generated";
    if (!importPath.empty() || gis) {
        Grid src;
        ImportOptions io;
        io.erosion = importErosion;
        io.heightRange = heightRange;
        if (gis) {
            TileCache cache;
            GisResult res;
            gr.resolution = s.resolution;
            std::string err;
            if (!ripElevation(cache, gr, res, &progress, &err)) { std::fprintf(stderr, "\nGIS failed: %s\n", err.c_str()); return 1; }
            float hr = io.heightRange;
            gisToHeightfield(res, src, io);
            if (hr > 0) io.heightRange = hr;
            source = "gis";
        } else {
            std::string err;
            if (!loadHeightmap(importPath, src, &err)) { std::fprintf(stderr, "import failed: %s\n", err.c_str()); return 1; }
            io.sourceLabel = "Imported " + importPath;
            source = "imported";
        }
        if (!buildSceneFromHeightmap(src, s, io, scene, &progress)) { std::fputs("\nbuild failed\n", stderr); return 1; }
    } else {
        if (!generateScene(s, scene, &progress)) { std::fputs("\ngeneration failed\n", stderr); return 1; }
    }
    std::puts("");
    for (const auto& l : scene.report.intent) std::printf("  intent: %s\n", l.c_str());
    if (!scene.report.summary.empty()) std::printf("  director: %s\n", scene.report.summary.c_str());
    for (const auto& l : scene.log) std::printf("  %s\n", l.c_str());

    ExportOptions eo;
    eo.directory = outDir;
    eo.baseName = name;
    eo.mesh = mesh;
    std::vector<std::string> log;
    auto files = exportScene(scene, eo, log);
    for (const auto& f : files) std::printf("  wrote %s\n", f.c_str());
    for (const auto& l : log) std::printf("  %s\n", l.c_str());
    if (previews) {
        const char* names[] = {"height", "shaded", "materials", "slope", "water", "moisture"};
        for (int m = 0; m < 6; ++m) {
            Image8 img;
            renderPreview(scene, PreviewMode(m), std::min(1024, scene.terrain.res()), img);
            std::string p = outDir + "/" + (name.empty() ? defaultExportName(scene) : name) + "_view_" + names[m] + ".png";
            savePng8(p, img.w, img.h, 4, img.px.data());
            std::printf("  wrote %s\n", p.c_str());
        }
    }
    if (!projectPath.empty()) {
        std::string err;
        if (saveProject(scene, projectPath, &err)) std::printf("  saved project %s\n", projectPath.c_str());
        else std::fprintf(stderr, "  project save failed: %s\n", err.c_str());
    }
    if (useBrain && brain.record(scene, source)) {
        BrainStats st = brain.stats();
        std::printf("  ZeraBrain learned this map (%d maps, %lld steps, shape loss %.4f)\n", st.samples, st.trainSteps, st.shapeLoss);
    }
    return 0;
}
