#include "app/App.h"

#include "core/Hydrology.h"
#include "core/Networks.h"
#include "core/Project.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace zl {

namespace fs = std::filesystem;

static void applyTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 4.f;
    st.FrameRounding = 4.f;
    st.GrabRounding = 4.f;
    st.TabRounding = 4.f;
    st.ScrollbarRounding = 6.f;
    st.FramePadding = ImVec2(7, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.WindowBorderSize = 0.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.115f, 0.13f, 1.f);
    c[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.115f, 1.f);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.14f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.22f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.29f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.29f, 0.34f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.16f, 0.36f, 0.38f, 0.8f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.45f, 0.47f, 0.9f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.2f, 0.52f, 0.55f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.33f, 0.36f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.45f, 0.48f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.55f, 0.58f, 1.f);
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.7f, 0.25f, 1.f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.65f, 0.2f, 1.f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.75f, 0.3f, 1.f);
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.16f, 0.19f, 1.f);
    c[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.45f, 0.48f, 1.f);
    c[ImGuiCol_TabSelected] = ImVec4(0.18f, 0.38f, 0.41f, 1.f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.24f, 0.26f, 1.f);
    c[ImGuiCol_PlotLines] = ImVec4(0.95f, 0.65f, 0.2f, 1.f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.95f, 0.65f, 0.2f, 1.f);
    st.ScaleAllSizes(scale);
}

bool App::init(GLFWwindow* window, const std::string& exeDir, std::string& err) {
    window_ = window;
    exeDir_ = exeDir;
    float xs = 1.f, ys = 1.f;
    glfwGetWindowContentScale(window, &xs, &ys);
    const float scale = std::max(1.f, xs);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const char* fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/System/Library/Fonts/Supplemental/Arial.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    for (const char* f : fonts) {
        std::error_code ec;
        if (fs::exists(f, ec)) {
            io.Fonts->AddFontFromFileTTF(f, 16.f * scale);
            break;
        }
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    applyTheme(scale);

    if (!renderer_.init(err)) return false;
    brain_.open();
    paintSelected_.assign(size_t(kFoliageCount), 0);
    paintSelected_[size_t(FoliageKind::Broadleaf)] = 1;
    paintSelected_[size_t(FoliageKind::Shrub)] = 1;
    exportOpt_.directory = (fs::current_path() / "exports").u8string();
    worldMap_.centerOn(gis_.lat, gis_.lon, 2.f);

    // logo
    Image8 logo;
    for (const auto& p : {fs::u8path(exeDir_) / "assets" / "logo.png", fs::current_path() / "assets" / "logo.png"}) {
        if (loadImage8(p.u8string(), logo, 4)) {
            logoTex_ = renderer_.uploadRGBA(0, logo.w, logo.h, logo.px.data());
            logoW_ = logo.w;
            logoH_ = logo.h;
            break;
        }
    }
    // remember the texture library location between sessions
    {
        std::ifstream f(fs::u8path(userDataDir()) / "library_path.txt");
        std::getline(f, libraryPath_);
        if (!libraryPath_.empty()) scanLibrary();
    }
    log("Welcome to ZeraLands 2.0 - pick an environment and era, then Generate.");
    BrainStats bs = brain_.stats();
    log("ZeraBrain: " + std::to_string(bs.samples) + " maps learned so far (" + bs.dir + ")");
    return true;
}

void App::shutdown() {
    progress_.cancel = true;
    if (worker_.joinable()) worker_.join();
    renderer_.shutdown();
}

void App::log(const std::string& s) {
    log_.push_back(s);
    if (log_.size() > 400) log_.erase(log_.begin());
}

void App::scanLibrary() {
    int n = library_.scan(libraryPath_);
    log("Texture library: " + std::to_string(n) + " usable PBR sets in " + libraryPath_);
    std::ofstream f(fs::u8path(userDataDir()) / "library_path.txt");
    f << libraryPath_;
    if (scene_.valid()) renderer_.requestMaterials(library_, scene_, texSize_);
}

void App::bumpAll() {
    scene_.heightVersion = ++versionCounter_;
    scene_.splatVersion = ++versionCounter_;
    scene_.foliageVersion = ++versionCounter_;
    scene_.splineVersion = ++versionCounter_;
}

// ------------------------------------------------------------------ jobs
void App::runJob(Job j, std::function<bool(Scene&, Progress&)> work, const std::string& brainSource) {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    job_ = j;
    jobError_.clear();
    progress_.cancel = false;
    progress_.fraction = 0.f;
    progress_.onStage = [this](const std::string& s, float) {
        std::lock_guard<std::mutex> lk(stageMu_);
        stage_ = s;
    };
    worker_ = std::thread([this, work, brainSource]() {
        auto sc = std::make_unique<Scene>();
        bool ok = false;
        try {
            ok = work(*sc, progress_);
        } catch (const std::exception& e) {
            jobError_ = e.what();
        }
        if (ok && sc->valid()) {
            if (brainLearn_ && !brainSource.empty()) {
                progress_.set("ZeraBrain is learning from this map", 0.99f);
                brain_.record(*sc, brainSource);
            }
            pending_ = std::move(sc);
            pendingReady_ = true;
        }
        busy_ = false;
    });
}

void App::startGenerate() {
    if (busy_) return;
    if (autoVariation_ && !firstScene_) ++settings_.variation;
    GenSettings s = settings_;
    runJob(Job::Generate, [s](Scene& sc, Progress& p) { return generateScene(s, sc, &p); }, "generated");
}

void App::startImport(const std::string& path) {
    GenSettings s = settings_;
    ImportOptions io = importOpt_;
    io.seaLevel = importUseSea_ ? importSea_ : -1.f;
    io.sourceLabel = "Imported " + fs::u8path(path).filename().u8string();
    runJob(Job::Import, [s, io, path, this](Scene& sc, Progress& p) {
        Grid src;
        std::string err;
        p.set("Loading heightmap image", 0.02f);
        if (!loadHeightmap(path, src, &err)) { jobError_ = err; return false; }
        return buildSceneFromHeightmap(src, s, io, sc, &p);
    }, "imported");
}

void App::startGis() {
    GenSettings s = settings_;
    GisRequest req = gis_;
    req.resolution = s.resolution;
    float erosion = gisErosion_;
    runJob(Job::Gis, [s, req, erosion, this](Scene& sc, Progress& p) {
        TileCache cache;
        GisResult r;
        std::string err;
        if (!ripElevation(cache, req, r, &p, &err)) { jobError_ = err; return false; }
        Grid src;
        ImportOptions io;
        gisToHeightfield(r, src, io);
        io.erosion = erosion;
        return buildSceneFromHeightmap(src, s, io, sc, &p);
    }, "gis");
}

void App::startRebuildFromCurrent() {
    if (!scene_.valid()) return;
    GenSettings s = settings_;
    ImportOptions io = importOpt_;
    io.worldSize = scene_.terrain.worldSize;
    io.heightRange = scene_.terrain.heightRange;
    io.seaLevel = scene_.terrain.seaLevel;
    io.normalize = false;
    io.sourceLabel = "Rebuilt from edited terrain";
    Grid src = scene_.terrain.baseHeight;
    runJob(Job::Rebuild, [s, io, src](Scene& sc, Progress& p) { return buildSceneFromHeightmap(src, s, io, sc, &p); }, "edited");
}

void App::startTrain(int epochs) {
    if (busy_) return;
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    job_ = Job::Train;
    progress_.cancel = false;
    progress_.onStage = [this](const std::string& s, float) {
        std::lock_guard<std::mutex> lk(stageMu_);
        stage_ = s;
    };
    worker_ = std::thread([this, epochs]() {
        brain_.train(epochs, &progress_);
        busy_ = false;
    });
}

void App::acceptPendingScene() {
    if (!pendingReady_) return;
    if (worker_.joinable()) worker_.join();
    pendingReady_ = false;
    bool reframe = firstScene_ || std::fabs(pending_->terrain.worldSize - scene_.terrain.worldSize) > 1.f;
    scene_ = std::move(*pending_);
    pending_.reset();
    settings_.variation = scene_.settings.variation;
    firstScene_ = false;
    bumpAll();
    undo_.clear();
    redo_.clear();
    activeSpline_ = -1;
    selectedPoint_ = -1;
    if (reframe) cam_.frame(scene_.terrain.worldSize, scene_.terrain.heightRange);
    library_.autoAssign(hashCombine(scene_.genome.macroSeed, uint64_t(scene_.settings.variation)));
    renderer_.requestMaterials(library_, scene_, texSize_);
    for (const auto& l : scene_.report.intent) log("Intent: " + l);
    if (!scene_.report.summary.empty()) log(scene_.report.summary);
    for (const auto& l : scene_.log) log(l);
}

// ------------------------------------------------------------------ editing helpers
Spline* App::activeSpline() {
    if (activeSpline_ < 0 || activeSpline_ >= int(scene_.splines.size())) return nullptr;
    return &scene_.splines[size_t(activeSpline_)];
}

void App::pushUndo(bool withHeight) {
    Snapshot s;
    s.splines = scene_.splines;
    s.foliage = scene_.foliage;
    if (withHeight) {
        s.baseHeight = scene_.terrain.baseHeight;
        s.hasHeight = true;
    }
    undo_.push_back(std::move(s));
    if (undo_.size() > 24) undo_.erase(undo_.begin());
    redo_.clear();
}

void App::undo() {
    if (undo_.empty()) return;
    Snapshot cur{scene_.splines, scene_.foliage, undo_.back().hasHeight ? scene_.terrain.baseHeight : Grid(), undo_.back().hasHeight};
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(std::move(cur));
    scene_.splines = std::move(s.splines);
    scene_.foliage = std::move(s.foliage);
    if (s.hasHeight) {
        scene_.terrain.baseHeight = std::move(s.baseHeight);
        refreshAfterTerrainEdit();
    } else {
        restampSplines(scene_);
        bumpAll();
    }
    log("Undo");
}

void App::redo() {
    if (redo_.empty()) return;
    Snapshot cur{scene_.splines, scene_.foliage, redo_.back().hasHeight ? scene_.terrain.baseHeight : Grid(), redo_.back().hasHeight};
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(std::move(cur));
    scene_.splines = std::move(s.splines);
    scene_.foliage = std::move(s.foliage);
    if (s.hasHeight) {
        scene_.terrain.baseHeight = std::move(s.baseHeight);
        refreshAfterTerrainEdit();
    } else {
        restampSplines(scene_);
        bumpAll();
    }
    log("Redo");
}

void App::refreshAfterTerrainEdit() {
    Terrain& t = scene_.terrain;
    // water & moisture follow the new shape; rivers aren't re-carved (no cumulative trenching)
    HydrologyParams hp;
    hp.seaLevel = t.seaLevel;
    hp.lakes = scene_.genome.r.moisture >= 0.2f || !scene_.genome.basins.empty();
    hp.rivers = scene_.genome.r.rivers > 0.05f && scene_.genome.r.moisture > 0.08f;
    hp.riverDepth = 0.f;
    hp.riverThreshold = 0.0035f / std::max(0.3f, scene_.genome.r.rivers);
    hp.baseMoisture = scene_.genome.r.moisture;
    Grid keepLava = t.lava;
    t.height = t.baseHeight;
    computeHydrology(t, hp, scene_.genome.detailSeed, nullptr);
    t.lava = keepLava;
    t.baseHeight = t.height;
    for (auto& s : scene_.splines)
        if (s.conform) conformSplineHeights(s, t, s.kind == SplineKind::Rail ? 0.03f : 0.12f, 60.f);
    restampSplines(scene_);
    bumpAll();
}

// ------------------------------------------------------------------ picking
bool App::pickTerrain(float mx, float my, float vx, float vy, float vw, float vh, Vec3& hit) const {
    if (!scene_.valid()) return false;
    const Terrain& t = scene_.terrain;
    float ndcX = ((mx - vx) / vw) * 2.f - 1.f, ndcY = 1.f - ((my - vy) / vh) * 2.f;
    float th = std::tan(cam_.fov * 0.5f);
    Vec3 f = cam_.forward(), r = cam_.right(), u = cross(r, f);
    Vec3 dir = (f + r * (ndcX * th * (vw / vh)) + u * (ndcY * th)).normalized();
    const float ex = std::max(0.01f, rs_.exaggeration);
    // march in un-exaggerated space
    Vec3 o(cam_.pos.x, cam_.pos.y / ex, cam_.pos.z);
    Vec3 d = Vec3(dir.x, dir.y / ex, dir.z).normalized();
    float step = std::max(0.5f, t.cellSize());
    float maxT = t.worldSize * 4.f;
    float prevT = 0;
    auto above = [&](float tt) {
        Vec3 p = o + d * tt;
        if (p.x < 0 || p.z < 0 || p.x > t.worldSize || p.z > t.worldSize) return 1;   // outside: treat as above
        return p.y > t.heightAtWorld(p.x, p.z) ? 1 : 0;
    };
    for (float tt = 0; tt < maxT; tt += step) {
        Vec3 p = o + d * tt;
        bool inside = p.x >= 0 && p.z >= 0 && p.x <= t.worldSize && p.z <= t.worldSize;
        if (inside && !above(tt)) {
            float a = prevT, b = tt;
            for (int i = 0; i < 20; ++i) {
                float m = 0.5f * (a + b);
                if (above(m)) a = m;
                else b = m;
            }
            hit = o + d * b;
            return true;
        }
        prevT = tt;
        step *= 1.004f;
    }
    return false;
}

// ------------------------------------------------------------------ strokes
void App::beginStroke(Vec2 p) {
    stroking_ = true;
    strokeAccum_ = 1.f;
    if (tool_ == Tool::Sculpt) {
        pushUndo(true);
        if (sculpt_.tool == SculptTool::Flatten) sculpt_.flattenHeight = scene_.terrain.heightAtWorld(p.x, p.y);
    } else if (tool_ == Tool::Paint) {
        pushUndo(false);
    }
}

void App::strokeDab(Vec2 p, float dt) {
    if (tool_ == Tool::Sculpt) {
        int x0, y0, x1, y1;
        sculptDab(scene_.terrain, p, sculpt_, std::min(dt, 0.05f) * 3.f, toolRng_.next(), x0, y0, x1, y1);
        if (x1 >= x0) {
            renderer_.updateHeightRegion(scene_, x0, y0, x1, y1);
            dirtyX0_ = std::min(dirtyX0_, x0);
            dirtyY0_ = std::min(dirtyY0_, y0);
            dirtyX1_ = std::max(dirtyX1_, x1);
            dirtyY1_ = std::max(dirtyY1_, y1);
            previewStamp_ = -1;
        }
    } else if (tool_ == Tool::Paint) {
        strokeAccum_ += dt * 8.f;
        if (strokeAccum_ < 1.f) return;
        strokeAccum_ = 0.f;
        std::vector<FoliageKind> kinds;
        for (int k = 0; k < kFoliageCount; ++k)
            if (paintSelected_[size_t(k)]) kinds.push_back(FoliageKind(k));
        bool erase = paintErase_ || ImGui::GetIO().KeyShift;
        int n = 0;
        if (erase) n = eraseFoliage(scene_.foliage, p, paint_.radius, kinds, 0.5f, toolRng_);
        else {
            paint_.kinds = kinds;
            n = paintFoliage(scene_.terrain, scene_.foliage, p, paint_, toolRng_, 0.35f);
        }
        if (n) {
            scene_.foliageVersion = ++versionCounter_;
            previewStamp_ = -1;
        }
    }
}

void App::endStroke() {
    if (!stroking_) return;
    stroking_ = false;
    if (tool_ == Tool::Sculpt && dirtyX1_ >= 0) {
        refreshAfterTerrainEdit();
        dirtyX0_ = dirtyY0_ = 1 << 30;
        dirtyX1_ = dirtyY1_ = -1;
    }
}

void App::handleViewportInput(bool hovered, bool active, float vx, float vy, float vw, float vh, float dt, bool is2D, Vec2 world, bool hitValid) {
    ImGuiIO& io = ImGui::GetIO();
    cursorValid_ = hovered && hitValid;
    cursorWorld_ = world;

    // ---- camera (3D)
    if (!is2D && hovered) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cam_.yaw -= io.MouseDelta.x * 0.0035f;
            cam_.pitch = clampf(cam_.pitch - io.MouseDelta.y * 0.0035f, -1.5f, 1.5f);
            Vec3 move;
            if (ImGui::IsKeyDown(ImGuiKey_W)) move += cam_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_S)) move += -cam_.forward();
            if (ImGui::IsKeyDown(ImGuiKey_D)) move += cam_.right();
            if (ImGui::IsKeyDown(ImGuiKey_A)) move += -cam_.right();
            if (ImGui::IsKeyDown(ImGuiKey_E)) move += Vec3(0, 1, 0);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) move += Vec3(0, -1, 0);
            float boost = io.KeyShift ? 4.f : 1.f;
            cam_.pos += move * (cam_.speed * dt * boost);
            if (io.MouseWheel != 0) cam_.speed = clampf(cam_.speed * (io.MouseWheel > 0 ? 1.25f : 0.8f), 2.f, 5000.f);
        } else if (io.MouseWheel != 0) {
            cam_.pos += cam_.forward() * (io.MouseWheel * cam_.speed * 0.6f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.f)) {
            Vec3 up = cross(cam_.right(), cam_.forward());
            cam_.pos += cam_.right() * (-io.MouseDelta.x * cam_.speed * 0.004f) + up * (io.MouseDelta.y * cam_.speed * 0.004f);
        }
        if (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f)) {
            // orbit around the point under the screen centre
            Vec3 pivot;
            if (pickTerrain(vx + vw * 0.5f, vy + vh * 0.5f, vx, vy, vw, vh, pivot)) {
                Vec3 off = cam_.pos - Vec3(pivot.x, pivot.y * rs_.exaggeration, pivot.z);
                float yawD = -io.MouseDelta.x * 0.005f;
                float c = std::cos(yawD), s = std::sin(yawD);
                off = Vec3(c * off.x + s * off.z, off.y, -s * off.x + c * off.z);
                cam_.pos = Vec3(pivot.x, pivot.y * rs_.exaggeration, pivot.z) + off;
                cam_.yaw += yawD;
            }
        }
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && scene_.valid()) cam_.frame(scene_.terrain.worldSize, scene_.terrain.heightRange);

    // ---- brush size hotkeys
    if (hovered) {
        float* radius = tool_ == Tool::Sculpt ? &sculpt_.radius : &paint_.radius;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) *radius = std::max(1.f, *radius * 0.85f);
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) *radius = std::min(2000.f, *radius * 1.18f);
    }

    if (!scene_.valid() || io.KeyAlt) return;

    // ---- tools
    if (tool_ == Tool::Sculpt || tool_ == Tool::Paint) {
        if (hovered && hitValid && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) beginStroke(world);
        if (stroking_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && hitValid) strokeDab(world, dt);
        if (stroking_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) endStroke();
    } else if (tool_ == Tool::Spline) {
        Spline* s = activeSpline();
        const float pickRadius = is2D ? scene_.terrain.worldSize / (vw * zoom2d_) * 10.f : std::max(8.f, cam_.speed * 0.15f);
        if (hovered && hitValid && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = s ? pickControlPoint(*s, world, pickRadius) : -1;
            if (hit >= 0) {
                pushUndo(false);
                selectedPoint_ = hit;
                draggingPoint_ = true;
            } else {
                pushUndo(false);
                if (!s) {
                    const auto& era = eras()[size_t(std::clamp(settings_.eraIndex, 0, int(eras().size()) - 1))];
                    Spline ns = makeSplineForKind(newSplineKind_, era);
                    ns.name = std::string(splineKindName(newSplineKind_)) + " " + std::to_string(scene_.splines.size() + 1);
                    ns.closed = false;
                    scene_.splines.push_back(ns);
                    activeSpline_ = int(scene_.splines.size()) - 1;
                    s = activeSpline();
                }
                selectedPoint_ = insertControlPoint(*s, world, scene_.terrain);
                draggingPoint_ = true;
                scene_.splineVersion = ++versionCounter_;
            }
        }
        if (draggingPoint_ && s && selectedPoint_ >= 0 && selectedPoint_ < int(s->points.size())) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && hitValid) {
                auto& p = s->points[size_t(selectedPoint_)].pos;
                if (std::fabs(p.x - world.x) + std::fabs(p.z - world.y) > 0.01f) {
                    p.x = world.x;
                    p.z = world.y;
                    p.y = scene_.terrain.heightAtWorld(world.x, world.y);
                    scene_.splineVersion = ++versionCounter_;
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                draggingPoint_ = false;
                if (s->conform) conformSplineHeights(*s, scene_.terrain, s->kind == SplineKind::Rail ? 0.03f : 0.12f, 40.f);
                restampSplines(scene_);
                scene_.splineVersion = ++versionCounter_;
                previewStamp_ = -1;
            }
        }
        if (hovered && s && selectedPoint_ >= 0 && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            pushUndo(false);
            s->points.erase(s->points.begin() + selectedPoint_);
            selectedPoint_ = std::min(selectedPoint_, int(s->points.size()) - 1);
            restampSplines(scene_);
            previewStamp_ = -1;
        }
        if (hovered && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            activeSpline_ = -1;
            selectedPoint_ = -1;
        }
    }
}

// ------------------------------------------------------------------ viewport
void App::drawViewport(float x, float y, float w, float h, float dt) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##viewport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // toolbar
    ImGui::SetCursorPos(ImVec2(6, 4));
    if (ImGui::RadioButton("3D Viewport", viewTab_ == 0)) viewTab_ = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("2D Heightmap", viewTab_ == 1)) viewTab_ = 1;
    ImGui::SameLine(0, 24);
    if (viewTab_ == 1) {
        const char* modes[] = {"Height", "Shaded", "Materials", "Slope", "Water / flow", "Moisture"};
        int m = int(previewMode_);
        ImGui::SetNextItemWidth(140);
        if (ImGui::Combo("##mode", &m, modes, 6)) { previewMode_ = PreviewMode(m); previewStamp_ = -1; }
        ImGui::SameLine();
        if (ImGui::Button("Fit")) { zoom2d_ = 1.f; pan2d_ = {0, 0}; }
    } else {
        ImGui::Checkbox("Foliage", &rs_.foliage); ImGui::SameLine();
        ImGui::Checkbox("Water", &rs_.water); ImGui::SameLine();
        ImGui::Checkbox("Splines", &rs_.splines); ImGui::SameLine();
        ImGui::Checkbox("Shadows", &rs_.shadows); ImGui::SameLine();
        ImGui::Checkbox("Wire", &rs_.wireframe); ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("Sun", &rs_.sunAzimuth, 0.f, kTau, "az %.2f"); ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##elev", &rs_.sunElevation, 0.05f, 1.5f, "elev %.2f");
    }
    const float top = 34.f;
    const float vx = x, vy = y + top, vw = w, vh = h - top;
    ImGui::SetCursorPos(ImVec2(0, top));
    ImGuiIO& io = ImGui::GetIO();

    if (viewTab_ == 0) {
        Vec3 hit;
        bool hitValid = false;
        bool hovered = ImGui::IsWindowHovered() && io.MousePos.y > vy;
        if (hovered) hitValid = pickTerrain(io.MousePos.x, io.MousePos.y, vx, vy, vw, vh, hit);
        ViewportOverlay ov;
        if (tool_ == Tool::Sculpt || tool_ == Tool::Paint) {
            ov.brush = hitValid;
            ov.brushCenter = {hit.x, hit.z};
            ov.brushRadius = tool_ == Tool::Sculpt ? sculpt_.radius : paint_.radius;
            ov.brushColor = tool_ == Tool::Sculpt ? Color3{1.f, 0.65f, 0.15f} : (paintErase_ || io.KeyShift ? Color3{1.f, 0.25f, 0.2f} : Color3{0.3f, 1.f, 0.4f});
        }
        if (Spline* s = activeSpline()) {
            for (const auto& p : s->points) ov.points.push_back(p.pos);
            for (const auto& sm : s->sample(8.f)) ov.polyline.push_back(sm.pos);
            ov.selectedPoint = selectedPoint_;
        }
        renderer_.sync(scene_, rs_);
        renderer_.render(int(vw), int(vh), cam_, scene_, rs_, ov, time_);
        ImGui::Image(ImTextureID(uintptr_t(renderer_.colorTexture())), ImVec2(vw, vh), ImVec2(0, 1), ImVec2(1, 0));
        handleViewportInput(hovered, ImGui::IsItemActive(), vx, vy, vw, vh, dt, false, {hit.x, hit.z}, hitValid);
        if (!scene_.valid()) {
            const char* msg = "Choose an environment & era on the left, then press GENERATE (or import a heightmap / rip a real place).";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::GetWindowDrawList()->AddText(ImVec2(vx + (vw - ts.x) * 0.5f, vy + vh * 0.5f), IM_COL32(230, 230, 230, 255), msg);
        }
        ImGui::GetWindowDrawList()->AddText(ImVec2(vx + 8, vy + vh - 22), IM_COL32(255, 255, 255, 140),
                                            "RMB+WASD/QE fly  |  wheel dolly  |  MMB pan  |  Alt+LMB orbit  |  F frame  |  [ ] brush size  |  Ctrl+Z undo");
    } else {
        renderer_.sync(scene_, rs_);
        // 2D heightmap view
        int stamp = scene_.heightVersion * 131 + scene_.splatVersion * 7 + scene_.foliageVersion + int(previewMode_) * 100003;
        if (scene_.valid() && stamp != previewStamp_ && !stroking_) {
            Image8 img;
            renderPreview(scene_, previewMode_, std::min(scene_.terrain.res(), 2048), img);
            previewTex_ = renderer_.uploadRGBA(previewTex_, img.w, img.h, img.px.data());
            previewStamp_ = stamp;
        } else if (scene_.valid() && stroking_ && tool_ == Tool::Sculpt && previewStamp_ != -2) {
            // during sculpting the 2D view refreshes when the stroke ends
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(vx, vy), ImVec2(vx + vw, vy + vh), IM_COL32(20, 22, 26, 255));
        float side = std::min(vw, vh) * 0.96f * zoom2d_;
        ImVec2 o(vx + (vw - side) * 0.5f + pan2d_.x, vy + (vh - side) * 0.5f + pan2d_.y);
        ImGui::InvisibleButton("##2d", ImVec2(vw, vh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        bool hovered = ImGui::IsItemHovered();
        if (hovered && io.MouseWheel != 0) {
            float old = zoom2d_;
            zoom2d_ = clampf(zoom2d_ * (io.MouseWheel > 0 ? 1.15f : 0.87f), 0.2f, 40.f);
            ImVec2 m = io.MousePos;
            float k = zoom2d_ / old;
            ImVec2 c(vx + vw * 0.5f + pan2d_.x, vy + vh * 0.5f + pan2d_.y);
            pan2d_.x += (m.x - c.x) * (1.f - k);
            pan2d_.y += (m.y - c.y) * (1.f - k);
        }
        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.f))) {
            pan2d_.x += io.MouseDelta.x;
            pan2d_.y += io.MouseDelta.y;
        }
        side = std::min(vw, vh) * 0.96f * zoom2d_;
        o = ImVec2(vx + (vw - side) * 0.5f + pan2d_.x, vy + (vh - side) * 0.5f + pan2d_.y);
        if (scene_.valid() && previewTex_) {
            dl->PushClipRect(ImVec2(vx, vy), ImVec2(vx + vw, vy + vh), true);
            dl->AddImage(ImTextureID(uintptr_t(previewTex_)), o, ImVec2(o.x + side, o.y + side));
            const float W = scene_.terrain.worldSize;
            auto toScreen = [&](float wx, float wz) { return ImVec2(o.x + wx / W * side, o.y + wz / W * side); };
            if (rs_.splines)
                for (size_t si = 0; si < scene_.splines.size(); ++si) {
                    const auto& s = scene_.splines[si];
                    auto smp = s.sample(std::max(4.f, W / side * 3.f));
                    std::vector<ImVec2> pts;
                    for (auto& p : smp) pts.push_back(toScreen(p.pos.x, p.pos.z));
                    ImU32 col = int(si) == activeSpline_ ? IM_COL32(255, 210, 40, 255) : IM_COL32(40, 30, 25, 200);
                    if (pts.size() > 1) dl->AddPolyline(pts.data(), int(pts.size()), col, s.closed ? ImDrawFlags_Closed : 0, int(si) == activeSpline_ ? 2.5f : 1.5f);
                }
            if (Spline* s = activeSpline())
                for (size_t i = 0; i < s->points.size(); ++i)
                    dl->AddCircleFilled(toScreen(s->points[i].pos.x, s->points[i].pos.z), 5.f,
                                        int(i) == selectedPoint_ ? IM_COL32(255, 80, 60, 255) : IM_COL32(60, 200, 255, 255));
            Vec2 world{(io.MousePos.x - o.x) / side * W, (io.MousePos.y - o.y) / side * W};
            bool inside = world.x >= 0 && world.y >= 0 && world.x <= W && world.y <= W;
            if (hovered && inside && (tool_ == Tool::Sculpt || tool_ == Tool::Paint)) {
                float r = (tool_ == Tool::Sculpt ? sculpt_.radius : paint_.radius) / W * side;
                dl->AddCircle(io.MousePos, r, tool_ == Tool::Sculpt ? IM_COL32(255, 170, 40, 255) : IM_COL32(80, 255, 100, 255), 48, 2.f);
            }
            if (hovered && inside) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "x %.0f m  z %.0f m  h %.1f m", world.x, world.y, scene_.terrain.heightAtWorld(world.x, world.y));
                dl->AddText(ImVec2(vx + 8, vy + vh - 22), IM_COL32(255, 255, 255, 200), buf);
            }
            dl->PopClipRect();
            handleViewportInput(hovered, ImGui::IsItemActive(), vx, vy, vw, vh, dt, true, world, inside);
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------ frame
void App::frame(float dt) {
    time_ += dt;
    fps_ = fps_ * 0.95f + (dt > 0 ? 1.f / dt : 0.f) * 0.05f;
    acceptPendingScene();
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G)) startGenerate();
        if (ImGui::IsKeyPressed(ImGuiKey_Tab)) viewTab_ = 1 - viewTab_;
    }

    drawMenu();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuH = ImGui::GetFrameHeight();
    const float statusH = 26.f;
    const float leftW = std::min(380.f * io.FontGlobalScale + 20.f, vp->WorkSize.x * 0.3f);
    const float rightW = std::min(410.f, vp->WorkSize.x * 0.3f);
    const float x0 = vp->WorkPos.x, y0 = vp->Pos.y + menuH;
    const float H = vp->Size.y - menuH - statusH;
    drawGeneratorPanel(x0, y0, leftW, H);
    drawViewport(x0 + leftW, y0, vp->Size.x - leftW - rightW, H, dt);
    drawToolPanel(x0 + vp->Size.x - rightW, y0, rightW, H);
    drawStatusBar(x0, y0 + H, vp->Size.x, statusH);
    drawFileDialog();
    drawAbout();
    if (showLog_) {
        ImGui::SetNextWindowSize(ImVec2(640, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Log", &showLog_)) {
            for (const auto& l : log_) ImGui::TextWrapped("%s", l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
        }
        ImGui::End();
    }
    if (showDemo_) ImGui::ShowDemoWindow(&showDemo_);
}

}  // namespace zl
