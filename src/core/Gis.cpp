#include "core/Gis.h"

#include "core/Generator.h"
#include "core/Project.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#endif

namespace zl {

namespace fs = std::filesystem;

const std::vector<GeoPlace>& famousPlaces() {
    static const std::vector<GeoPlace> places = {
        {"Matterhorn, Swiss Alps", 45.9763, 7.6586, 10.f, "mountains_alpine"},
        {"Mount Everest, Himalaya", 27.9881, 86.9250, 16.f, "mountains_alpine"},
        {"Grand Canyon, Arizona", 36.0970, -112.1130, 16.f, "mesa_canyonlands"},
        {"Monument Valley, Utah", 36.9980, -110.0985, 14.f, "mesa_canyonlands"},
        {"Yosemite Valley, California", 37.7456, -119.5936, 12.f, "glacial_valley"},
        {"Mount Fuji, Japan", 35.3606, 138.7274, 20.f, "volcano_active"},
        {"Mount St. Helens, Washington", 46.1912, -122.1944, 12.f, "volcano_active"},
        {"Kilauea, Hawaii", 19.4069, -155.2834, 20.f, "magma_fields"},
        {"Kilimanjaro, Tanzania", -3.0674, 37.3556, 30.f, "volcano_active"},
        {"Geirangerfjord, Norway", 62.1049, 7.0940, 16.f, "fjords"},
        {"Dolomites (Tre Cime), Italy", 46.6187, 12.3020, 10.f, "rocky_crags"},
        {"Torres del Paine, Chile", -50.9423, -73.4068, 20.f, "extreme_spikes"},
        {"Death Valley, California", 36.5054, -117.0794, 30.f, "salt_flats"},
        {"Namib Sand Sea, Namibia", -24.7300, 15.3400, 20.f, "desert_erg"},
        {"Guilin Karst, China", 24.7700, 110.4900, 12.f, "karst"},
        {"Scottish Highlands (Glencoe)", 56.6820, -5.1020, 12.f, "highlands"},
        {"Badlands, South Dakota", 43.8554, -102.3397, 12.f, "badlands"},
        {"Iceland (Landmannalaugar)", 63.9830, -19.0600, 14.f, "basalt_columns"},
        {"Bora Bora, French Polynesia", -16.5004, -151.7415, 14.f, "archipelago"},
        {"Cliffs of Moher, Ireland", 52.9715, -9.4309, 8.f, "coastal_cliffs"},
        {"Mississippi Delta, Louisiana", 29.1500, -89.2500, 30.f, "river_delta"},
        {"Serengeti Kopjes, Tanzania", -2.3333, 34.8333, 16.f, "savanna"},
        {"Meteor Crater, Arizona", 35.0275, -111.0225, 4.f, "lunar_craters"},
        {"Rolling Tuscany, Italy", 43.0700, 11.6800, 12.f, "rolling_hills"},
    };
    return places;
}

double lonToTileX(double lon, int z) { return (lon + 180.0) / 360.0 * double(1 << z); }
double latToTileY(double lat, int z) {
    double r = lat * 3.14159265358979323846 / 180.0;
    return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / 3.14159265358979323846) / 2.0 * double(1 << z);
}
double tileXToLon(double x, int z) { return x / double(1 << z) * 360.0 - 180.0; }
double tileYToLat(double y, int z) {
    double n = 3.14159265358979323846 - 2.0 * 3.14159265358979323846 * y / double(1 << z);
    return 180.0 / 3.14159265358979323846 * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}
double metersPerPixel(double lat, int z) { return 156543.03392 * std::cos(lat * 3.14159265358979323846 / 180.0) / double(1 << z); }

TileCache::TileCache(std::string dir) : dir_(std::move(dir)) {
    if (dir_.empty()) dir_ = (fs::u8path(userDataDir()) / "gis_cache").u8string();
    std::error_code ec;
    fs::create_directories(fs::u8path(dir_), ec);
}

std::string TileCache::tileUrl(int z, int x, int y) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png", z, x, y);
    return buf;
}

std::string TileCache::path(int z, int x, int y) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d_%d_%d.png", z, x, y);
    return (fs::u8path(dir_) / buf).u8string();
}

bool TileCache::cached(int z, int x, int y) const {
    std::error_code ec;
    return fs::exists(fs::u8path(path(z, x, y)), ec);
}

static bool downloadFile(const std::string& url, const std::string& dest) {
    std::string tmp = dest + ".part";
#ifdef _WIN32
    std::wstring wurl(url.begin(), url.end());
    std::wstring wdst = fs::u8path(tmp).wstring();
    if (URLDownloadToFileW(nullptr, wurl.c_str(), wdst.c_str(), 0, nullptr) != S_OK) return false;
#else
    std::string cmd = "curl -sSfL --max-time 40 -o \"" + tmp + "\" \"" + url + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) return false;
#endif
    std::error_code ec;
    fs::rename(fs::u8path(tmp), fs::u8path(dest), ec);
    return !ec;
}

bool TileCache::get(int z, int x, int y, Image8& out, std::string* err) {
    const int n = 1 << z;
    x = ((x % n) + n) % n;
    if (y < 0 || y >= n) {
        if (err) *err = "tile out of range";
        return false;
    }
    std::string p = path(z, x, y);
    if (!cached(z, x, y)) {
        if (!downloadFile(tileUrl(z, x, y), p)) {
            if (err) *err = "download failed: " + tileUrl(z, x, y);
            return false;
        }
    }
    if (!loadImage8(p, out, 3, err)) {
        std::error_code ec;
        fs::remove(fs::u8path(p), ec);   // corrupt cache entry
        return false;
    }
    return true;
}

void decodeTerrarium(const Image8& tile, std::vector<float>& m) {
    m.resize(size_t(tile.w) * tile.h);
    for (size_t i = 0; i < m.size(); ++i) {
        const uint8_t* p = &tile.px[i * size_t(tile.c)];
        m[i] = float(p[0]) * 256.f + float(p[1]) + float(p[2]) / 256.f - 32768.f;
    }
}

void colorizeTerrarium(const Image8& tile, Image8& rgba, bool gray) {
    std::vector<float> m;
    decodeTerrarium(tile, m);
    rgba.w = tile.w;
    rgba.h = tile.h;
    rgba.c = 4;
    rgba.px.resize(size_t(tile.w) * tile.h * 4);
    struct Stop { float h; float r, g, b; };
    static const Stop stops[] = {{-8000, 0.02f, 0.05f, 0.20f}, {-200, 0.10f, 0.30f, 0.55f}, {0, 0.35f, 0.60f, 0.75f}, {1, 0.20f, 0.45f, 0.20f},
                                 {400, 0.55f, 0.65f, 0.35f}, {1200, 0.65f, 0.50f, 0.30f}, {2600, 0.55f, 0.45f, 0.40f}, {4000, 0.95f, 0.95f, 0.97f},
                                 {9000, 1.f, 1.f, 1.f}};
    for (int y = 0; y < tile.h; ++y)
        for (int x = 0; x < tile.w; ++x) {
            size_t i = size_t(y) * tile.w + x;
            float h = m[i];
            float r, g, b;
            if (gray) {
                r = g = b = clamp01((h + 200.f) / 6000.f);
            } else {
                int s = 0;
                while (s < 7 && h > stops[s + 1].h) ++s;
                float t = clamp01((h - stops[s].h) / (stops[s + 1].h - stops[s].h));
                r = lerpf(stops[s].r, stops[s + 1].r, t);
                g = lerpf(stops[s].g, stops[s + 1].g, t);
                b = lerpf(stops[s].b, stops[s + 1].b, t);
            }
            // hillshade
            float hx = m[size_t(y) * tile.w + std::min(x + 1, tile.w - 1)] - m[size_t(y) * tile.w + std::max(x - 1, 0)];
            float hy = m[size_t(std::min(y + 1, tile.h - 1)) * tile.w + x] - m[size_t(std::max(y - 1, 0)) * tile.w + x];
            float shade = clampf(1.f - (hx * 0.6f + hy * 0.4f) / 600.f, 0.55f, 1.25f);
            if (h <= 0.f) shade = 1.f;
            uint8_t* o = &rgba.px[i * 4];
            o[0] = uint8_t(std::lround(clamp01(r * shade) * 255.f));
            o[1] = uint8_t(std::lround(clamp01(g * shade) * 255.f));
            o[2] = uint8_t(std::lround(clamp01(b * shade) * 255.f));
            o[3] = 255;
        }
}

bool ripElevation(TileCache& cache, const GisRequest& req, GisResult& out, Progress* progress, std::string* err) {
    const int res = std::clamp(req.resolution, 65, 8193);
    const double extentM = double(std::max(0.2f, req.sizeKm)) * 1000.0;
    const double wantMpp = extentM / double(res);
    int z = 1;
    while (z < req.maxZoom && metersPerPixel(req.lat, z + 1) >= wantMpp * 0.75) ++z;
    // keep the download reasonable (<= ~100 tiles)
    auto tilesFor = [&](int zz) {
        double half = extentM * 0.5 / metersPerPixel(req.lat, zz) / 256.0;
        int span = int(std::ceil(half * 2.0)) + 1;
        return span * span;
    };
    while (z > 1 && tilesFor(z) > 100) --z;

    const double cx = lonToTileX(req.lon, z) * 256.0, cy = latToTileY(req.lat, z) * 256.0;
    const double halfPx = extentM * 0.5 / metersPerPixel(req.lat, z);
    const int tx0 = int(std::floor((cx - halfPx) / 256.0)), tx1 = int(std::floor((cx + halfPx) / 256.0));
    const int ty0 = int(std::floor((cy - halfPx) / 256.0)), ty1 = int(std::floor((cy + halfPx) / 256.0));
    std::map<std::pair<int, int>, std::vector<float>> tiles;
    int total = (tx1 - tx0 + 1) * (ty1 - ty0 + 1), done = 0;
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (progress) {
                if (progress->cancel) return false;
                progress->set("Downloading elevation tiles", 0.05f + 0.4f * float(done) / float(total));
            }
            Image8 img;
            if (!cache.get(z, tx, ty, img, err)) return false;
            decodeTerrarium(img, tiles[{tx, ty}]);
            ++done;
        }
    auto sampleGlobal = [&](double gx, double gy) {
        int ix = int(std::floor(gx)), iy = int(std::floor(gy));
        float fx = float(gx - ix), fy = float(gy - iy);
        auto at = [&](int px, int py) {
            int tx = int(std::floor(px / 256.0)), ty = int(std::floor(py / 256.0));
            auto it = tiles.find({tx, ty});
            if (it == tiles.end()) return 0.f;
            int lx = px - tx * 256, ly = py - ty * 256;
            return it->second[size_t(ly) * 256 + size_t(lx)];
        };
        float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
        return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
    };
    out.meters.resize(res, res);
    const double x0 = cx - halfPx, y0 = cy - halfPx, step = halfPx * 2.0 / double(res - 1);
    parallelFor(0, res, [&](int y) {
        for (int x = 0; x < res; ++x) {
            double gx = std::clamp(x0 + step * x, double(tx0 * 256), double(tx1 * 256 + 254));
            double gy = std::clamp(y0 + step * y, double(ty0 * 256), double(ty1 * 256 + 254));
            out.meters.at(x, y) = sampleGlobal(gx, gy);
        }
    });
    out.meters.minMax(out.minM, out.maxM);
    out.zoom = z;
    out.tiles = total;
    out.worldSize = float(extentM);
    char buf[200];
    std::snprintf(buf, sizeof(buf), "GIS rip %.4f, %.4f (%.1f km, zoom %d, %d tiles): %.0f m .. %.0f m", req.lat, req.lon, req.sizeKm, z, total,
                  out.minM, out.maxM);
    out.label = buf;
    if (progress) progress->set("Elevation stitched", 0.45f);
    return true;
}

void gisToHeightfield(const GisResult& r, Grid& out, ImportOptions& io) {
    // Anchor to real sea level only when the area actually reaches the sea; otherwise use the full range.
    const bool coastal = r.minM < 0.f;
    const float base = r.minM;
    const float range = std::max(1.f, r.maxM - base);
    out = r.meters;
    for (auto& v : out.vec()) v = (v - base) / range;
    io.worldSize = r.worldSize;
    io.heightRange = range;
    io.normalize = false;
    io.seaLevel = coastal ? (0.f - base) / range : -1.f;
    io.sourceLabel = r.label;
}

}  // namespace zl
