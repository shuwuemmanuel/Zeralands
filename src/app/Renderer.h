// ZeraLands - OpenGL 3.3 viewport renderer: terrain (stochastic anti-tiling, triplanar, height-blended PBR layers,
// soft terrain shadows), water, spline ribbons (roads/rail/race/canal/lava), instanced foliage, sky and fog.
#pragma once

#include "app/GfxMath.h"
#include "core/Generator.h"
#include "core/TextureLibrary.h"

#include <array>
#include <atomic>
#include <future>
#include <string>
#include <vector>

namespace zl {

struct RenderSettings {
    bool foliage = true, water = true, splines = true, shadows = true, wireframe = false;
    bool antiTiling = true, triplanar = true, macroVariation = true;
    float sunAzimuth = 2.3f, sunElevation = 0.55f;
    float foliageDistance = 1800.f;
    float exaggeration = 1.f;
    int meshRes = 1024;
    bool showGrid = false;
};

struct ViewportOverlay {
    bool brush = false;
    Vec2 brushCenter;
    float brushRadius = 0;
    Color3 brushColor{1, 1, 1};
    std::vector<Vec3> points;      // spline control points (world)
    std::vector<Vec3> polyline;    // active spline centre line
    int selectedPoint = -1;
};

class Renderer {
public:
    bool init(std::string& err);
    void shutdown();

    // Uploads whatever changed in the scene (compares version counters).
    void sync(const Scene& scene, const RenderSettings& rs);
    // Fast partial height upload during sculpting.
    void updateHeightRegion(const Scene& scene, int x0, int y0, int x1, int y1);
    void invalidateFoliage() { foliageVersion_ = -1; }
    void invalidateSplines() { splineVersion_ = -1; }

    // Material textures: decoded on a worker thread, uploaded when ready.
    void requestMaterials(const TextureLibrary& lib, const Scene& scene, int texSize);
    bool materialsLoading() const { return pending_.valid(); }
    const std::string& materialStatus() const { return materialStatus_; }
    float tileMeters(int layer) const { return tile_[size_t(layer)]; }
    void setTileMeters(int layer, float m) { tile_[size_t(layer)] = m; }

    void render(int width, int height, const Camera& cam, const Scene& scene, const RenderSettings& rs, const ViewportOverlay& ov, float time);
    unsigned colorTexture() const { return resolveTex_; }
    Mat4 viewProj() const { return vp_; }

    // 2D view texture (RGBA8) helpers
    unsigned uploadRGBA(unsigned tex, int w, int h, const uint8_t* data);

private:
    struct MeshGL { unsigned vao = 0, vbo = 0, ibo = 0, inst = 0; int indexCount = 0; int instanceCount = 0; };
    struct MaterialBatch {
        std::vector<MaterialKind> kinds;
        std::vector<MaterialPixels> pixels;
        int size = 0;
        bool network = false;
    };
    void ensureTargets(int w, int h);
    void buildGrid(int n);
    void uploadTerrain(const Scene& s);
    void uploadSplat(const Scene& s);
    void uploadFoliage(const Scene& s);
    void uploadSplines(const Scene& s);
    void uploadMaterialArray(unsigned& albTex, unsigned& nrmTex, const MaterialBatch& b);

    unsigned progTerrain_ = 0, progWater_ = 0, progRibbon_ = 0, progFoliage_ = 0, progSky_ = 0, progGizmo_ = 0;
    unsigned gridVao_ = 0, gridVbo_ = 0, gridIbo_ = 0;
    int gridN_ = 0, gridIndexCount_ = 0;
    unsigned texHeight_ = 0, texSplat0_ = 0, texSplat1_ = 0, texWater_ = 0;
    unsigned texAlb_ = 0, texNrm_ = 0, texNetAlb_ = 0, texNetNrm_ = 0;
    unsigned fboMs_ = 0, rboColorMs_ = 0, rboDepthMs_ = 0, fboResolve_ = 0, resolveTex_ = 0;
    int fbW_ = 0, fbH_ = 0;
    unsigned skyVao_ = 0, gizmoVao_ = 0, gizmoVbo_ = 0;
    unsigned ribbonVao_ = 0, ribbonVbo_ = 0;
    int ribbonVerts_ = 0;
    std::vector<MeshGL> foliageMeshes_;   // per FoliageKind
    int heightVersion_ = -1, splatVersion_ = -1, foliageVersion_ = -1, splineVersion_ = -1;
    int texRes_ = 0;
    std::array<float, 8> tile_{};
    std::array<float, 8> emissive_{};
    std::array<MaterialKind, 8> netKinds_{};
    int netCount_ = 0;
    std::vector<MaterialKind> layerKinds_;
    std::future<std::pair<MaterialBatch, MaterialBatch>> pending_;
    std::string materialStatus_ = "procedural materials";
    Mat4 vp_;
    float scaledHeight_ = 1.f;
};

}  // namespace zl
