// ZeraLands - interactive world elevation map (GIS picker) drawn with Dear ImGui.
#pragma once

#include "core/Gis.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

namespace zl {

class WorldMap {
public:
    WorldMap();
    ~WorldMap();
    // Draws the map; returns true when the user clicked a new location.
    bool draw(const char* id, float width, float height, double& lat, double& lon, float sizeKm);
    void centerOn(double lat, double lon, float zoom);
    bool grayscale = false;
    int pending() const;
    const std::string& lastError() const { return lastError_; }

private:
    using Key = std::tuple<int, int, int, bool>;   // z, x, y, grayscale
    void worker();
    void uploadReady();
    unsigned texFor(const Key& k);

    double cx_ = 0.5, cy_ = 0.36;   // view centre in mercator [0,1]
    float zoom_ = 1.6f;
    std::map<Key, unsigned> textures_;
    std::set<Key> requested_;
    std::deque<Key> queue_;
    std::deque<std::pair<Key, Image8>> ready_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    bool quit_ = false;
    TileCache cache_;
    std::string lastError_;
    bool dragging_ = false;
};

}  // namespace zl
