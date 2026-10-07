#include "app/App.h"

#include "core/Networks.h"
#include "core/Project.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdio>
#include <filesystem>

namespace zl {

namespace fs = std::filesystem;

static void help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static const int kResolutions[] = {257, 505, 513, 1009, 1025, 2017, 2049, 4033};

// ------------------------------------------------------------------ menu / status
void App::drawMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Generate", "Ctrl+G", false, !busy_)) startGenerate();
        ImGui::Separator();
        if (ImGui::MenuItem("Import heightmap...", nullptr, false, !busy_))
            openFileDialog("Import heightmap", {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".webp", ".raw", ".r16"}, false, false,
                           [this](const std::string& p) { importPath_ = p; startImport(p); });
        if (ImGui::MenuItem("Open project...", nullptr, false, !busy_))
            openFileDialog("Open project", {".zlproj"}, false, false, [this](const std::string& p) {
                std::string err;
                if (loadProject(scene_, p, &err)) {
                    settings_ = scene_.settings;
                    bumpAll();
                    cam_.frame(scene_.terrain.worldSize, scene_.terrain.heightRange);
                    renderer_.requestMaterials(library_, scene_, texSize_);
                    firstScene_ = false;
                    log("Opened " + p);
                } else {
                    log("Open failed: " + err);
                }
            });
        if (ImGui::MenuItem("Save project...", nullptr, false, scene_.valid()))
            openFileDialog("Save project", {".zlproj"}, true, false, [this](const std::string& p) {
                std::string err;
                log(saveProject(scene_, p, &err) ? "Saved project " + p : "Save failed: " + err);
            });
        if (ImGui::MenuItem("Export now", nullptr, false, scene_.valid())) {
            std::vector<std::string> l;
            lastExport_ = exportScene(scene_, exportOpt_, l);
            for (auto& s : l) log(s);
            if (brainLearn_) brain_.record(scene_, "exported", 2.f);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("3D viewport", "Tab", viewTab_ == 0)) viewTab_ = 0;
        if (ImGui::MenuItem("2D heightmap", "Tab", viewTab_ == 1)) viewTab_ = 1;
        ImGui::MenuItem("Log", nullptr, &showLog_);
        ImGui::MenuItem("Grid overlay", nullptr, &rs_.showGrid);
        ImGui::SliderFloat("Vertical exaggeration", &rs_.exaggeration, 0.5f, 4.f);
        ImGui::SliderFloat("Foliage distance", &rs_.foliageDistance, 200.f, 8000.f, "%.0f m");
        ImGui::SliderInt("Viewport mesh", &rs_.meshRes, 256, 2048);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About ZeraLands")) showAbout_ = true;
        ImGui::MenuItem("ImGui demo", nullptr, &showDemo_);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawStatusBar(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    if (busy_) {
        std::string st;
        {
            std::lock_guard<std::mutex> lk(stageMu_);
            st = stage_;
        }
        ImGui::SetNextItemWidth(220);
        ImGui::ProgressBar(progress_.fraction, ImVec2(220, 16));
        ImGui::SameLine();
        ImGui::TextUnformatted(st.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) progress_.cancel = true;
    } else {
        ImGui::TextUnformatted(log_.empty() ? "" : log_.back().c_str());
        if (ImGui::IsItemClicked()) showLog_ = true;
    }
    ImGui::SameLine(std::max(0.f, w - 420.f));
    if (scene_.valid())
        ImGui::Text("%dx%d  |  %zu splines  |  %zu foliage  |  %.0f fps", scene_.terrain.res(), scene_.terrain.res(), scene_.splines.size(),
                    scene_.foliage.size(), fps_);
    else
        ImGui::Text("%.0f fps", fps_);
    ImGui::End();
}

// ------------------------------------------------------------------ left: generator
void App::drawGeneratorPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Generator", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    if (logoTex_) {
        float lw = std::min(w - 20.f, 220.f), lh = lw * float(logoH_) / float(std::max(1, logoW_));
        ImGui::SetCursorPosX((w - lw) * 0.5f);
        ImGui::Image(ImTextureID(uintptr_t(logoTex_)), ImVec2(lw, std::min(lh, 90.f)));
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.25f, 1.f), "ZERALANDS 2.0");
    }
    const auto& envs = environments();
    const auto& eraList = eras();

    if (ImGui::CollapsingHeader("World", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##env", envs[size_t(settings_.envIndex)].name.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (const auto& cat : environmentCategories()) {
                ImGui::SeparatorText(cat.c_str());
                for (size_t i = 0; i < envs.size(); ++i) {
                    if (envs[i].category != cat) continue;
                    if (ImGui::Selectable(envs[i].name.c_str(), int(i) == settings_.envIndex)) settings_.envIndex = int(i);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", envs[i].description.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", envs[size_t(settings_.envIndex)].description.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##era", eraList[size_t(settings_.eraIndex)].name.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (size_t i = 0; i < eraList.size(); ++i) {
                if (ImGui::Selectable(eraList[i].name.c_str(), int(i) == settings_.eraIndex)) settings_.eraIndex = int(i);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", eraList[i].blurb.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s", eraList[size_t(settings_.eraIndex)].blurb.c_str());
    }

    if (ImGui::CollapsingHeader("Seed & AI director", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(w - 150);
        ImGui::InputText("Seed", &settings_.seedText);
        ImGui::SameLine();
        if (ImGui::Button("Dice")) {
            settings_.seedText = std::to_string(Rng(uint64_t(time_ * 1e6) ^ 0xD1CEull).next() % 1000000);
            settings_.variation = 0;
            firstScene_ = true;
        }
        ImGui::SetNextItemWidth(w - 150);
        ImGui::InputInt("Variation", &settings_.variation);
        help("Same seed + a different variation = a close relative of the same landscape (same big layout, new details).\n"
             "With 'auto' on, every Generate breeds the next variation. Keep the number to reproduce a map exactly.");
        ImGui::Checkbox("Auto-advance variation", &autoVariation_);
        ImGui::TextUnformatted("Describe it (optional):");
        ImGui::InputTextMultiline("##prompt", &settings_.prompt, ImVec2(-1, 52));
        help("Natural-language intent, e.g. 'huge jagged snowy peaks with a few lakes and a river', 'very flat with no rivers',\n"
             "'an island with a volcano and canyons'. Intensifiers (very, slightly) and negation (no, without) are understood.");
        ImGui::SetNextItemWidth(w * 0.4f);
        ImGui::SliderInt("Candidates", &settings_.candidates, 1, 32);
        help("How many candidate layouts the director breeds and scores before refining the best one.");
        ImGui::SetNextItemWidth(w * 0.4f);
        ImGui::SliderInt("Refine steps", &settings_.refineSteps, 0, 12);
    }

    if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        char rbuf[16];
        std::snprintf(rbuf, sizeof(rbuf), "%d", settings_.resolution);
        ImGui::SetNextItemWidth(w * 0.45f);
        if (ImGui::BeginCombo("Resolution", rbuf)) {
            for (int r : kResolutions) {
                std::snprintf(rbuf, sizeof(rbuf), "%d%s", r, (r == 505 || r == 1009 || r == 2017 || r == 4033) ? "  (UE)" : "");
                if (ImGui::Selectable(rbuf, r == settings_.resolution)) settings_.resolution = r;
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::DragFloat("World size", &settings_.worldSize, 10.f, 250.f, 32000.f, "%.0f m");
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Height scale", &settings_.heightScale, 0.2f, 3.f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Roughness", &settings_.roughness, 0.3f, 2.f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Erosion", &settings_.erosion, 0.f, 2.f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Rivers", &settings_.riverAmount, 0.f, 2.5f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Water level", &settings_.waterLevel, -0.3f, 0.4f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Feature scale", &settings_.featureScale, 0.4f, 2.5f);
        help("< 1 = fewer, bigger landforms. > 1 = more, smaller landforms.");
    }

    if (ImGui::CollapsingHeader("Networks", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& era = eraList[size_t(settings_.eraIndex)];
        auto net = [&](const char* label, bool* v, const NetworkStyle& st) {
            if (!st.available) {
                ImGui::BeginDisabled();
                bool off = false;
                ImGui::Checkbox(label, &off);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", st.label.c_str());
                return;
            }
            ImGui::Checkbox(label, v);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", st.label.c_str());
        };
        net("Road network", &settings_.roads, era.road);
        net("Railway", &settings_.rail, era.rail);
        net("Small paths", &settings_.paths, era.path);
        ImGui::Checkbox("Racetrack", &settings_.racetrack);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", era.race.label.c_str());
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Road density", &settings_.roadDensity, 0.2f, 3.f);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Path density", &settings_.pathDensity, 0.2f, 3.f);
    }
    if (ImGui::CollapsingHeader("Foliage", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Procedural foliage", &settings_.foliage);
        ImGui::SameLine();
        ImGui::Checkbox("Era props", &settings_.props);
        ImGui::SetNextItemWidth(w * 0.45f);
        ImGui::SliderFloat("Density", &settings_.foliageDensity, 0.f, 3.f);
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(busy_);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.5f, 0.12f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.6f, 0.2f, 1.f));
    if (ImGui::Button(busy_ ? "Working..." : "GENERATE", ImVec2(-1, 42))) startGenerate();
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();
    if (busy_) ImGui::ProgressBar(progress_.fraction, ImVec2(-1, 0));
    if (!jobError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", jobError_.c_str());

    if (scene_.valid() && ImGui::CollapsingHeader("Director report")) {
        ImGui::TextWrapped("%s", scene_.report.summary.c_str());
        for (const auto& l : scene_.report.intent) ImGui::BulletText("%s", l.c_str());
        for (const auto& l : scene_.log) ImGui::BulletText("%s", l.c_str());
    }
    ImGui::End();
}

// ------------------------------------------------------------------ right: tools
void App::drawToolPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        auto fl = [&](int i) { return forceTab_ == i ? ImGuiTabItemFlags_SetSelected : 0; };
        if (ImGui::BeginTabItem("Tools", nullptr, fl(0))) { drawToolsTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Materials", nullptr, fl(1))) { drawMaterialsTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Import / GIS", nullptr, fl(2))) { drawImportTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Export", nullptr, fl(3))) { drawExportTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Brain", nullptr, fl(4))) { drawBrainTab(); ImGui::EndTabItem(); }
        forceTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::drawToolsTab() {
    const char* names[] = {"Navigate", "Sculpt", "Foliage paint", "Spline roads"};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(names[i], int(tool_) == i)) {
            endStroke();
            tool_ = Tool(i);
        }
    }
    ImGui::Separator();
    if (!scene_.valid()) {
        ImGui::TextWrapped("Generate, import or rip a terrain first.");
        return;
    }
    switch (tool_) {
        case Tool::Navigate:
            ImGui::TextWrapped("Fly: hold RMB + WASD (Q/E down/up, Shift faster, wheel while flying = speed). "
                               "Wheel = dolly, MMB = pan, Alt+LMB = orbit, F = frame terrain. Tab switches 2D/3D.");
            ImGui::SliderFloat("Fly speed", &cam_.speed, 2.f, 3000.f, "%.0f m/s", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Field of view", &cam_.fov, 0.3f, 1.8f);
            break;
        case Tool::Sculpt: {
            int t = int(sculpt_.tool);
            for (int i = 0; i < int(SculptTool::Count); ++i) {
                if (i % 4) ImGui::SameLine();
                if (ImGui::RadioButton(sculptToolName(SculptTool(i)), t == i)) sculpt_.tool = SculptTool(i);
            }
            ImGui::SliderFloat("Radius", &sculpt_.radius, 2.f, 1500.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Strength", &sculpt_.strength, 0.01f, 1.f);
            ImGui::SliderFloat("Falloff", &sculpt_.falloff, 0.f, 1.f);
            if (sculpt_.tool == SculptTool::Terrace) ImGui::SliderFloat("Step", &sculpt_.terraceStep, 1.f, 60.f, "%.0f m");
            ImGui::TextWrapped("LMB to sculpt. Flatten samples the height under the cursor when the stroke starts. "
                               "Water, splines and materials refresh when you release.");
            if (ImGui::Button("Re-run erosion & rebuild networks/foliage", ImVec2(-1, 0))) startRebuildFromCurrent();
            ImGui::SliderFloat("Rebuild erosion", &importOpt_.erosion, 0.f, 1.f);
            break;
        }
        case Tool::Paint: {
            ImGui::SliderFloat("Radius", &paint_.radius, 1.f, 600.f, "%.0f m", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Density", &paint_.density, 1.f, 600.f, "%.0f / ha", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Falloff", &paint_.falloff, 0.f, 1.f);
            ImGui::DragFloatRange2("Scale", &paint_.scaleMin, &paint_.scaleMax, 0.01f, 0.1f, 5.f);
            ImGui::SliderFloat("Max slope", &paint_.slopeMax, 0.05f, 3.f);
            ImGui::Checkbox("Erase (or hold Shift)", &paintErase_);
            ImGui::SameLine();
            ImGui::Checkbox("Avoid roads", &paint_.avoidRoads);
            ImGui::SeparatorText("Foliage types");
            if (ImGui::SmallButton("None")) std::fill(paintSelected_.begin(), paintSelected_.end(), 0);
            ImGui::BeginChild("##kinds", ImVec2(0, 260), ImGuiChildFlags_Borders);
            for (int k = 0; k < kFoliageCount; ++k) {
                const FoliageInfo& fi = foliageInfo(FoliageKind(k));
                bool sel = paintSelected_[size_t(k)] != 0;
                ImGui::ColorButton(fi.name, ImVec4(fi.primary.r, fi.primary.g, fi.primary.b, 1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
                ImGui::SameLine();
                if (ImGui::Checkbox(fi.name, &sel)) paintSelected_[size_t(k)] = sel ? 1 : 0;
                if (fi.isProp) { ImGui::SameLine(); ImGui::TextDisabled("prop"); }
            }
            ImGui::EndChild();
            if (ImGui::Button("Remove all selected types", ImVec2(-1, 0))) {
                pushUndo(false);
                auto& f = scene_.foliage;
                f.erase(std::remove_if(f.begin(), f.end(), [&](const FoliageInstance& fi) { return paintSelected_[fi.kind] != 0; }), f.end());
                scene_.foliageVersion = ++versionCounter_;
            }
            break;
        }
        case Tool::Spline: drawSplineEditor(); break;
    }
}

void App::drawSplineEditor() {
    ImGui::TextWrapped("LMB on terrain adds points to the active spline (or starts a new one). Drag points to move, Del removes, "
                       "Enter/Esc finishes. Heights follow the terrain with grade limits; the landscape is cut & filled to fit.");
    int nk = int(newSplineKind_);
    const char* kinds[] = {"Road", "Railway", "Path", "Racetrack", "Canal", "Aqueduct", "Maglev", "Lava channel", "Custom"};
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("New spline type", &nk, kinds, int(SplineKind::Count))) newSplineKind_ = SplineKind(nk);
    if (ImGui::Button("New spline")) {
        activeSpline_ = -1;
        selectedPoint_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete spline") && activeSpline()) {
        pushUndo(false);
        scene_.splines.erase(scene_.splines.begin() + activeSpline_);
        activeSpline_ = -1;
        selectedPoint_ = -1;
        restampSplines(scene_);
        scene_.splineVersion = ++versionCounter_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        pushUndo(false);
        scene_.splines.clear();
        activeSpline_ = -1;
        restampSplines(scene_);
        scene_.splineVersion = ++versionCounter_;
    }
    ImGui::BeginChild("##splines", ImVec2(0, 150), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < scene_.splines.size(); ++i) {
        const auto& s = scene_.splines[i];
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s  [%s, %zu pts]##%zu", s.name.c_str(), splineKindName(s.kind), s.points.size(), i);
        if (ImGui::Selectable(buf, int(i) == activeSpline_)) {
            activeSpline_ = int(i);
            selectedPoint_ = -1;
        }
    }
    ImGui::EndChild();
    Spline* s = activeSpline();
    if (!s) return;
    bool changed = false;
    ImGui::InputText("Name", &s->name);
    int k = int(s->kind);
    if (ImGui::Combo("Type", &k, kinds, int(SplineKind::Count))) { s->kind = SplineKind(k); changed = true; }
    int m = int(s->material);
    if (ImGui::BeginCombo("Surface", materialInfo(s->material).name)) {
        for (int i = 0; i < kMaterialCount; ++i)
            if (ImGui::Selectable(materialInfo(MaterialKind(i)).name, i == m)) { s->material = MaterialKind(i); changed = true; }
        ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Width", &s->width, 0.5f, 40.f, "%.1f m");
    changed |= ImGui::SliderFloat("Shoulder", &s->shoulder, 0.f, 40.f, "%.1f m");
    float bankDeg = s->maxBank * 57.2958f;
    if (ImGui::SliderFloat("Max banking", &bankDeg, 0.f, 45.f, "%.0f deg")) { s->maxBank = bankDeg / 57.2958f; changed = true; }
    changed |= ImGui::Checkbox("Closed loop", &s->closed);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Markings", &s->markings);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Kerbs", &s->kerbs);
    changed |= ImGui::Checkbox("Elevated", &s->elevated);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Water", &s->waterFilled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Conform", &s->conform);
    int prop = int(s->prop);
    if (ImGui::BeginCombo("Props", prop >= kFoliageCount ? "None" : foliageInfo(s->prop).name)) {
        if (ImGui::Selectable("None", prop >= kFoliageCount)) { s->prop = FoliageKind::Count; changed = true; }
        for (int i = 0; i < kFoliageCount; ++i)
            if (foliageInfo(FoliageKind(i)).isProp && ImGui::Selectable(foliageInfo(FoliageKind(i)).name, i == prop)) { s->prop = FoliageKind(i); changed = true; }
        ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Prop spacing", &s->propSpacing, 5.f, 500.f, "%.0f m");
    if (selectedPoint_ >= 0 && selectedPoint_ < int(s->points.size())) {
        auto& p = s->points[size_t(selectedPoint_)];
        ImGui::SeparatorText("Selected point");
        if (ImGui::DragFloat3("Position", &p.pos.x, 0.5f)) { s->conform = false; changed = true; }
        changed |= ImGui::SliderFloat("Point width", &p.width, 0.f, 40.f, p.width <= 0 ? "spline" : "%.1f m");
    }
    ImGui::Text("Length: %.2f km", s->length() / 1000.f);
    if (changed && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (s->conform) conformSplineHeights(*s, scene_.terrain, s->kind == SplineKind::Rail ? 0.03f : 0.12f, 40.f);
        restampSplines(scene_);
        scene_.splineVersion = ++versionCounter_;
    } else if (changed) {
        scene_.splineVersion = ++versionCounter_;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        restampSplines(scene_);
        scene_.splineVersion = ++versionCounter_;
    }
}

void App::drawMaterialsTab() {
    ImGui::TextWrapped("Point ZeraLands at your PBR texture folder (e.g. the 'pbr' folder from your Drive). Sets named like "
                       "Ground14_diffuse/_normal/_roughness/_displace/_occlusion are detected and matched to terrain layers.");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##lib", &libraryPath_);
    ImGui::SameLine();
    if (ImGui::Button("Browse")) openFileDialog("Texture library folder", {}, false, true, [this](const std::string& p) { libraryPath_ = p; scanLibrary(); });
    if (ImGui::Button("Scan library")) scanLibrary();
    ImGui::SameLine();
    if (ImGui::Button("Shuffle variants") && scene_.valid()) {
        library_.autoAssign(Rng(uint64_t(time_ * 1000)).next());
        renderer_.requestMaterials(library_, scene_, texSize_);
    }
    ImGui::Text("%zu texture sets found", library_.sets().size());
    const int sizes[] = {256, 512, 1024, 2048};
    int si = texSize_ == 256 ? 0 : texSize_ == 512 ? 1 : texSize_ == 1024 ? 2 : 3;
    const char* sizeNames[] = {"256", "512", "1024", "2048"};
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("Texture size", &si, sizeNames, 4)) {
        texSize_ = sizes[si];
        if (scene_.valid()) renderer_.requestMaterials(library_, scene_, texSize_);
    }
    ImGui::TextDisabled("%s", renderer_.materialStatus().c_str());
    ImGui::SeparatorText("Anti-tiling");
    ImGui::Checkbox("Stochastic tiling", &rs_.antiTiling);
    help("Each patch of ground picks a different virtual tile offset (blended by texture contrast) and far terrain mixes in a\n"
         "second, rotated scale - repetition disappears even on huge maps.");
    ImGui::Checkbox("Triplanar cliffs", &rs_.triplanar);
    help("Steep faces are projected from the side as well as from above, so rock never smears or stretches.");
    ImGui::Checkbox("Macro variation", &rs_.macroVariation);
    if (!scene_.valid()) return;
    ImGui::SeparatorText("Terrain layers");
    bool reload = false;
    for (int l = 0; l < scene_.splat.layerCount; ++l) {
        MaterialKind k = scene_.splat.layers[size_t(l)];
        ImGui::PushID(l);
        ImGui::Text("%d. %s", l, materialInfo(k).name);
        auto cands = library_.candidates(k);
        int cur = library_.assigned(k);
        const char* curName = cur >= 0 ? library_.sets()[size_t(cur)].name.c_str() : "Procedural";
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##set", curName)) {
            if (ImGui::Selectable("Procedural", cur < 0)) { library_.setAssigned(k, -1); reload = true; }
            for (int c : cands)
                if (ImGui::Selectable(library_.sets()[size_t(c)].name.c_str(), c == cur)) { library_.setAssigned(k, c); reload = true; }
            ImGui::Separator();
            for (int c = 0; c < int(library_.sets().size()); ++c)
                if (std::find(cands.begin(), cands.end(), c) == cands.end())
                    if (ImGui::Selectable((library_.sets()[size_t(c)].name + "  (other)").c_str(), c == cur)) { library_.setAssigned(k, c); reload = true; }
            ImGui::EndCombo();
        }
        float tm = renderer_.tileMeters(l);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##tile", &tm, 0.5f, 40.f, "tile %.1f m", ImGuiSliderFlags_Logarithmic)) renderer_.setTileMeters(l, tm);
        ImGui::PopID();
    }
    if (reload) renderer_.requestMaterials(library_, scene_, texSize_);
}

void App::drawImportTab() {
    ImGui::SeparatorText("Import heightmap image");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##imp", &importPath_);
    ImGui::SameLine();
    if (ImGui::Button("Browse##imp"))
        openFileDialog("Import heightmap", {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".webp", ".raw", ".r16"}, false, false,
                       [this](const std::string& p) { importPath_ = p; });
    ImGui::DragFloat("World size", &importOpt_.worldSize, 10.f, 0.f, 32000.f, importOpt_.worldSize <= 0 ? "use generator" : "%.0f m");
    ImGui::DragFloat("Height range", &importOpt_.heightRange, 5.f, 0.f, 9000.f, importOpt_.heightRange <= 0 ? "env default" : "%.0f m");
    ImGui::Checkbox("Sea level", &importUseSea_);
    if (importUseSea_) { ImGui::SameLine(); ImGui::SetNextItemWidth(120); ImGui::SliderFloat("##sea", &importSea_, 0.f, 0.9f); }
    ImGui::SliderFloat("AI erosion pass", &importOpt_.erosion, 0.f, 1.f);
    help("0 keeps the image untouched. Higher values run landscape evolution, droplet and thermal erosion tuned to the chosen environment.");
    ImGui::Checkbox("Keep source resolution", &importOpt_.keepResolution);
    ImGui::TextDisabled("Environment & era (left panel) drive materials, foliage and networks.");
    ImGui::BeginDisabled(busy_ || importPath_.empty());
    if (ImGui::Button("Import & build", ImVec2(-1, 0))) startImport(importPath_);
    ImGui::EndDisabled();

    ImGui::SeparatorText("GIS - rip a real landscape");
    float mapW = ImGui::GetContentRegionAvail().x;
    if (worldMap_.draw("world", mapW, mapW * 0.72f, gis_.lat, gis_.lon, gis_.sizeKm)) log("GIS location set");
    ImGui::Checkbox("Grayscale heights", &worldMap_.grayscale);
    ImGui::SameLine();
    ImGui::TextDisabled("wheel zoom, drag pan, click pick");
    const auto& places = famousPlaces();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##places", places[size_t(gisPlace_)].name)) {
        for (size_t i = 0; i < places.size(); ++i)
            if (ImGui::Selectable(places[i].name, int(i) == gisPlace_)) {
                gisPlace_ = int(i);
                gis_.lat = places[i].lat;
                gis_.lon = places[i].lon;
                gis_.sizeKm = places[i].sizeKm;
                int e = findEnvironment(places[i].hint);
                if (e >= 0) settings_.envIndex = e;
                worldMap_.centerOn(gis_.lat, gis_.lon, 9.f);
            }
        ImGui::EndCombo();
    }
    ImGui::InputDouble("Latitude", &gis_.lat, 0.01, 0.1, "%.5f");
    ImGui::InputDouble("Longitude", &gis_.lon, 0.01, 0.1, "%.5f");
    gis_.lat = std::clamp(gis_.lat, -84.0, 84.0);
    ImGui::SliderFloat("Area", &gis_.sizeKm, 0.5f, 80.f, "%.1f km", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("AI erosion##gis", &gisErosion_, 0.f, 1.f);
    ImGui::BeginDisabled(busy_);
    if (ImGui::Button("Rip landscape", ImVec2(-1, 0))) startGis();
    ImGui::EndDisabled();
    if (!worldMap_.lastError().empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", worldMap_.lastError().c_str());
    ImGui::TextDisabled("Elevation: AWS Terrain Tiles (Mapzen terrarium; SRTM, GMTED, ETOPO1 and others).");
}

void App::drawExportTab() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##dir", &exportOpt_.directory);
    ImGui::SameLine();
    if (ImGui::Button("Browse##exp")) openFileDialog("Export folder", {}, false, true, [this](const std::string& p) { exportOpt_.directory = p; });
    ImGui::InputTextWithHint("Name", scene_.valid() ? defaultExportName(scene_).c_str() : "auto", &exportOpt_.baseName);
    ImGui::Checkbox("Heightmap 16-bit PNG", &exportOpt_.heightPng16);
    ImGui::Checkbox("Heightmap RAW .r16 (Unreal)", &exportOpt_.heightRaw16);
    ImGui::Checkbox("Material weightmaps (splat)", &exportOpt_.splatmaps);
    ImGui::Checkbox("Normal map", &exportOpt_.normalMap);
    ImGui::Checkbox("Colour preview", &exportOpt_.colorPreview);
    ImGui::Checkbox("Foliage instances (CSV)", &exportOpt_.foliage);
    ImGui::Checkbox("Splines (JSON)", &exportOpt_.splines);
    ImGui::Checkbox("Mesh (OBJ)", &exportOpt_.mesh);
    if (exportOpt_.mesh) ImGui::SliderInt("Mesh resolution", &exportOpt_.meshMaxRes, 65, 2049);
    ImGui::Checkbox("Metadata + engine import scales", &exportOpt_.metadata);
    ImGui::BeginDisabled(!scene_.valid());
    if (ImGui::Button("Export", ImVec2(-1, 34))) {
        std::vector<std::string> l;
        lastExport_ = exportScene(scene_, exportOpt_, l);
        for (auto& s : l) log(s);
        if (brainLearn_) brain_.record(scene_, "exported", 2.f);
    }
    ImGui::EndDisabled();
    for (const auto& f : lastExport_) ImGui::TextDisabled("%s", fs::u8path(f).filename().u8string().c_str());
}

void App::drawBrainTab() {
    BrainStats st = brain_.stats();
    ImGui::TextWrapped("ZeraBrain learns from every map you generate, import, rip or export. It's building the dataset and models "
                       "for future prompt-to-map generation (see docs/ML.md).");
    ImGui::Checkbox("Learn from my maps", &brainLearn_);
    ImGui::Text("Maps learned: %d", st.samples);
    ImGui::Text("Training steps: %lld", st.trainSteps);
    ImGui::Text("Parameters: %zu", st.parameters);
    ImGui::Text("Loss  intent %.4f  shape %.4f  text->shape %.4f", st.intentLoss, st.shapeLoss, st.latentLoss);
    if (!st.shapeLossHistory.empty())
        ImGui::PlotLines("##shape", st.shapeLossHistory.data(), int(st.shapeLossHistory.size()), 0, "shape loss", 0.f, FLT_MAX, ImVec2(-1, 60));
    if (!st.intentLossHistory.empty())
        ImGui::PlotLines("##intent", st.intentLossHistory.data(), int(st.intentLossHistory.size()), 0, "intent loss", 0.f, FLT_MAX, ImVec2(-1, 60));
    ImGui::BeginDisabled(busy_ || st.samples == 0);
    if (ImGui::Button("Train 25 epochs on everything")) startTrain(25);
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", st.dir.c_str());
    ImGui::SeparatorText("What it has learned (experimental)");
    if (ImGui::Button("Sketch the current prompt")) {
        Grid g = brain_.sketch(settings_.prompt, settings_.envIndex, settings_.eraIndex);
        g.normalize(0.f, 1.f);
        std::vector<uint8_t> px(g.size() * 4);
        for (size_t i = 0; i < g.size(); ++i) {
            uint8_t v = uint8_t(g.vec()[i] * 255.f);
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
            px[i * 4 + 3] = 255;
        }
        brainSketchTex_ = renderer_.uploadRGBA(brainSketchTex_, g.width(), g.height(), px.data());
        brainSketchPrompt_ = settings_.prompt.empty() ? environments()[size_t(settings_.envIndex)].name : settings_.prompt;
    }
    if (brainSketchTex_) {
        ImGui::Image(ImTextureID(uintptr_t(brainSketchTex_)), ImVec2(160, 160));
        ImGui::SameLine();
        ImGui::TextWrapped("Its current idea of:\n\"%s\"", brainSketchPrompt_.c_str());
    }
    if (scene_.valid()) {
        float nov = brain_.reconstructionError(scene_);
        ImGui::Text("Novelty of this map: %.4f", nov);
        help("Reconstruction error of the shape autoencoder: high = unlike anything it has seen, low = familiar.");
    }
}

// ------------------------------------------------------------------ dialogs
void App::openFileDialog(const std::string& title, const std::vector<std::string>& exts, bool save, bool folder,
                         std::function<void(const std::string&)> onOk) {
    fd_.open = true;
    fd_.title = title;
    fd_.exts = exts;
    fd_.save = save;
    fd_.folder = folder;
    fd_.onOk = std::move(onOk);
    if (fd_.dir.empty()) fd_.dir = fs::current_path().u8string();
    fd_.name.clear();
    ImGui::OpenPopup("##filedialog");
}

void App::drawFileDialog() {
    if (fd_.open && !ImGui::IsPopupOpen("##filedialog")) ImGui::OpenPopup("##filedialog");
    ImGui::SetNextWindowSize(ImVec2(680, 480), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##filedialog", nullptr, ImGuiWindowFlags_NoTitleBar)) return;
    ImGui::TextUnformatted(fd_.title.c_str());
    ImGui::SetNextItemWidth(-60);
    ImGui::InputText("##dir", &fd_.dir);
    ImGui::SameLine();
    if (ImGui::Button("Up")) fd_.dir = fs::u8path(fd_.dir).parent_path().u8string();
#ifdef _WIN32
    for (char d = 'C'; d <= 'H'; ++d) {
        std::string root = std::string(1, d) + ":/";
        std::error_code ec;
        if (fs::exists(root, ec)) {
            ImGui::SameLine();
            if (ImGui::SmallButton(root.c_str())) fd_.dir = root;
        }
    }
#endif
    ImGui::BeginChild("##files", ImVec2(0, -70), ImGuiChildFlags_Borders);
    std::error_code ec;
    std::vector<fs::directory_entry> dirs, files;
    for (auto it = fs::directory_iterator(fs::u8path(fd_.dir), fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        if (it->is_directory(ec)) dirs.push_back(*it);
        else if (!fd_.folder) {
            std::string e = it->path().extension().u8string();
            for (auto& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (fd_.exts.empty() || std::find(fd_.exts.begin(), fd_.exts.end(), e) != fd_.exts.end()) files.push_back(*it);
        }
    }
    auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path().filename() < b.path().filename(); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    for (auto& d : dirs) {
        std::string n = "[" + d.path().filename().u8string() + "]";
        if (ImGui::Selectable(n.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) fd_.dir = d.path().u8string();
    }
    for (auto& f : files) {
        std::string n = f.path().filename().u8string();
        if (ImGui::Selectable(n.c_str(), n == fd_.name, ImGuiSelectableFlags_AllowDoubleClick)) {
            fd_.name = n;
            if (ImGui::IsMouseDoubleClicked(0)) {
                fd_.open = false;
                auto cb = fd_.onOk;
                ImGui::CloseCurrentPopup();
                if (cb) cb((fs::u8path(fd_.dir) / fs::u8path(n)).u8string());
            }
        }
    }
    ImGui::EndChild();
    if (!fd_.folder) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", &fd_.name);
    }
    if (ImGui::Button(fd_.folder ? "Use this folder" : (fd_.save ? "Save" : "Open"), ImVec2(140, 0))) {
        std::string path = fd_.folder ? fd_.dir : (fs::u8path(fd_.dir) / fs::u8path(fd_.name)).u8string();
        if (fd_.folder || !fd_.name.empty()) {
            fd_.open = false;
            auto cb = fd_.onOk;
            ImGui::CloseCurrentPopup();
            if (cb) cb(path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        fd_.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::drawAbout() {
    if (!showAbout_) return;
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("About ZeraLands", &showAbout_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.25f, 1), "ZeraLands 2.0");
        ImGui::TextWrapped("AI-directed landscape generator in C++: %zu environments x %zu eras, landscape-evolution erosion, "
                           "era-styled road/rail/path/racetrack networks, spline roads, foliage painting, PBR texturing with "
                           "anti-tiling, real-world GIS ripping and a self-training ML brain.",
                           environments().size(), eras().size());
        ImGui::Separator();
        ImGui::TextWrapped("Libraries: Dear ImGui, GLFW, GLEW, stb, lodepng, libwebp, nlohmann/json. Elevation data: AWS Terrain Tiles.");
    }
    ImGui::End();
}

}  // namespace zl
