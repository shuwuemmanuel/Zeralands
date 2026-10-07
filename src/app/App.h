// ZeraLands - editor application: UI, tools (sculpt, foliage paint, spline roads), async jobs, views.
#pragma once

#include "app/Renderer.h"
#include "app/WorldMap.h"
#include "core/Brain.h"
#include "core/Exporter.h"
#include "core/Gis.h"
#include "core/Sculpt.h"
#include "core/TextureLibrary.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;

namespace zl {

class App {
public:
    bool init(GLFWwindow* window, const std::string& exeDir, std::string& err);
    void frame(float dt);
    void shutdown();
    bool quitRequested() const { return quit_; }
    // test hooks (used by --screenshot)
    void startGenerate();
    bool busy() const { return busy_; }
    void setView(int tab) { viewTab_ = tab; }
    GenSettings& settings() { return settings_; }
    Scene& scene() { return scene_; }
    RenderSettings& renderSettings() { return rs_; }
    void setTool(int t) { tool_ = Tool(t); }
    Camera& camera() { return cam_; }
    void setTab(int t) { forceTab_ = t; }
    std::string& libraryPath() { return libraryPath_; }
    void scanLibrary();

private:
    enum class Tool { Navigate, Sculpt, Paint, Spline };
    enum class Job { None, Generate, Import, Gis, Rebuild, Train };
    struct Snapshot {
        std::vector<Spline> splines;
        std::vector<FoliageInstance> foliage;
        Grid baseHeight;
        bool hasHeight = false;
    };

    // UI
    void drawMenu();
    void drawGeneratorPanel(float x, float y, float w, float h);
    void drawToolPanel(float x, float y, float w, float h);
    void drawViewport(float x, float y, float w, float h, float dt);
    void drawStatusBar(float x, float y, float w, float h);
    void drawToolsTab();
    void drawMaterialsTab();
    void drawImportTab();
    void drawExportTab();
    void drawBrainTab();
    void drawSplineEditor();
    void drawFileDialog();
    void drawAbout();
    void openFileDialog(const std::string& title, const std::vector<std::string>& exts, bool save, bool folder,
                        std::function<void(const std::string&)> onOk);

    // jobs
    void runJob(Job j, std::function<bool(Scene&, Progress&)> work, const std::string& brainSource);
    void startImport(const std::string& path);
    void startGis();
    void startRebuildFromCurrent();
    void startTrain(int epochs);
    void acceptPendingScene();

    // tools
    bool pickTerrain(float mx, float my, float vx, float vy, float vw, float vh, Vec3& hit) const;
    void handleViewportInput(bool hovered, bool active, float vx, float vy, float vw, float vh, float dt, bool is2D, Vec2 worldUnderMouse,
                             bool hitValid);
    void beginStroke(Vec2 p);
    void strokeDab(Vec2 p, float dt);
    void endStroke();
    void pushUndo(bool withHeight);
    void undo();
    void redo();
    void refreshAfterTerrainEdit();
    void bumpAll();
    void log(const std::string& s);
    Spline* activeSpline();

    GLFWwindow* window_ = nullptr;
    std::string exeDir_;
    bool quit_ = false;

    GenSettings settings_;
    Scene scene_;
    int versionCounter_ = 10;
    bool autoVariation_ = true;
    bool firstScene_ = true;

    // async job
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> pendingReady_{false};
    Job job_ = Job::None;
    std::unique_ptr<Scene> pending_;
    Progress progress_;
    std::mutex stageMu_;
    std::string stage_;
    std::string jobError_;

    Renderer renderer_;
    Camera cam_;
    RenderSettings rs_;
    TextureLibrary library_;
    std::string libraryPath_;
    int texSize_ = 1024;
    Brain brain_;
    bool brainLearn_ = true;
    unsigned brainSketchTex_ = 0;
    std::string brainSketchPrompt_;

    WorldMap worldMap_;
    GisRequest gis_;
    float gisErosion_ = 0.f;
    int gisPlace_ = 0;

    std::string importPath_;
    ImportOptions importOpt_;
    bool importUseSea_ = false;
    float importSea_ = 0.2f;

    ExportOptions exportOpt_;
    std::vector<std::string> lastExport_;

    // tools state
    Tool tool_ = Tool::Navigate;
    SculptBrush sculpt_;
    PaintBrush paint_;
    std::vector<uint8_t> paintSelected_;
    bool paintErase_ = false;
    int activeSpline_ = -1;
    int selectedPoint_ = -1;
    bool draggingPoint_ = false;
    bool stroking_ = false;
    float strokeAccum_ = 0.f;
    int dirtyX0_ = 1 << 30, dirtyY0_ = 1 << 30, dirtyX1_ = -1, dirtyY1_ = -1;
    SplineKind newSplineKind_ = SplineKind::Road;
    Rng toolRng_{12345};
    Vec2 cursorWorld_;
    bool cursorValid_ = false;
    std::vector<Snapshot> undo_, redo_;

    // views
    int viewTab_ = 0;   // 0 = 3D, 1 = 2D
    int forceTab_ = -1;
    PreviewMode previewMode_ = PreviewMode::Shaded;
    unsigned previewTex_ = 0;
    int previewStamp_ = -1;
    float zoom2d_ = 1.f;
    Vec2 pan2d_{0, 0};
    bool showLog_ = false, showAbout_ = false, showDemo_ = false;
    std::vector<std::string> log_;
    unsigned logoTex_ = 0;
    int logoW_ = 0, logoH_ = 0;
    float time_ = 0.f;
    float fps_ = 0.f;

    // file dialog
    struct FileDialogState {
        bool open = false;
        bool save = false, folder = false;
        std::string title, dir, name;
        std::vector<std::string> exts;
        std::function<void(const std::string&)> onOk;
    } fd_;
};

}  // namespace zl
