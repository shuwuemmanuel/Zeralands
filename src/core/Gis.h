// ZeraLands - GIS: rip real-world elevation (AWS Terrain Tiles / Mapzen "Terrarium" encoding, public, no key).
#pragma once

#include "core/Heightfield.h"
#include "core/ImageIO.h"

#include <mutex>
#include <string>
#include <vector>

namespace zl {

struct GeoPlace {
    const char* name;
    double lat, lon;
    float sizeKm;
    const char* hint;    // suggested ZeraLands environment id
};
const std::vector<GeoPlace>& famousPlaces();

// Web-mercator helpers (slippy-map tile coordinates, fractional).
double lonToTileX(double lon, int z);
double latToTileY(double lat, int z);
double tileXToLon(double x, int z);
double tileYToLat(double y, int z);
double metersPerPixel(double lat, int z);   // 256px tiles

class TileCache {
public:
    explicit TileCache(std::string dir = "");
    // Returns the decoded terrarium tile (RGB8, 256x256) from disk or network. Thread safe.
    bool get(int z, int x, int y, Image8& out, std::string* err = nullptr);
    bool cached(int z, int x, int y) const;
    const std::string& dir() const { return dir_; }
    static std::string tileUrl(int z, int x, int y);

private:
    std::string path(int z, int x, int y) const;
    std::string dir_;
    std::mutex mu_;
};

// Converts a terrarium tile to meters.
void decodeTerrarium(const Image8& tile, std::vector<float>& meters);
// Hypsometric "heightmap colour" rendering of a tile (RGBA8) for the world map picker.
void colorizeTerrarium(const Image8& tile, Image8& rgba, bool grayscale);

struct GisRequest {
    double lat = 46.0, lon = 7.75;
    float sizeKm = 8.f;      // square extent
    int resolution = 1009;
    int maxZoom = 14;
};

struct GisResult {
    Grid meters;             // elevation in meters (res x res)
    float minM = 0, maxM = 0;
    int zoom = 0, tiles = 0;
    float worldSize = 0;     // meters
    std::string label;
};

// Normalizes a rip to 0..1 and fills scene import options (world size, height range, sea level).
struct ImportOptions;
void gisToHeightfield(const GisResult& r, Grid& out, ImportOptions& io);

bool ripElevation(TileCache& cache, const GisRequest& req, GisResult& out, Progress* progress, std::string* err);

}  // namespace zl
