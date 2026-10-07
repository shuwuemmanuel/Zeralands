#include "app/WorldMap.h"

#include <GL/glew.h>
#include <imgui.h>

#include <cstdio>

namespace zl {

WorldMap::WorldMap() { thread_ = std::thread([this] { worker(); }); }

WorldMap::~WorldMap() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

int WorldMap::pending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return int(queue_.size());
}

void WorldMap::worker() {
    for (;;) {
        Key k;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            k = queue_.back();   // newest request first: what the user is looking at now
            queue_.pop_back();
        }
        Image8 tile, rgba;
        std::string err;
        if (cache_.get(std::get<0>(k), std::get<1>(k), std::get<2>(k), tile, &err)) {
            colorizeTerrarium(tile, rgba, std::get<3>(k));
        } else {
            rgba.w = rgba.h = 1;
            rgba.c = 4;
            rgba.px = {40, 40, 48, 255};
            std::lock_guard<std::mutex> lk(mu_);
            lastError_ = err;
        }
        std::lock_guard<std::mutex> lk(mu_);
        ready_.push_back({k, std::move(rgba)});
    }
}

void WorldMap::uploadReady() {
    std::deque<std::pair<Key, Image8>> got;
    {
        std::lock_guard<std::mutex> lk(mu_);
        got.swap(ready_);
    }
    for (auto& [k, img] : got) {
        unsigned tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
        textures_[k] = tex;
    }
    // keep GPU memory bounded
    if (textures_.size() > 400) {
        for (auto it = textures_.begin(); it != textures_.end() && textures_.size() > 300;) {
            if (std::get<0>(it->first) > 3) {
                glDeleteTextures(1, &it->second);
                requested_.erase(it->first);
                it = textures_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

unsigned WorldMap::texFor(const Key& k) {
    auto it = textures_.find(k);
    if (it != textures_.end()) return it->second;
    if (!requested_.count(k)) {
        requested_.insert(k);
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push_back(k);
        if (queue_.size() > 64) queue_.pop_front();
        cv_.notify_one();
    }
    return 0;
}

void WorldMap::centerOn(double lat, double lon, float zoom) {
    cx_ = lonToTileX(lon, 0);
    cy_ = latToTileY(lat, 0);
    zoom_ = zoom;
}

bool WorldMap::draw(const char* id, float width, float height, double& lat, double& lon, float sizeKm) {
    uploadReady();
    bool changed = false;
    ImGui::PushID(id);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size(width, height);
    ImGui::InvisibleButton("map", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + width, p0.y + height), true);
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), IM_COL32(18, 22, 30, 255));

    const int z = std::clamp(int(std::floor(zoom_)), 0, 14);
    const double worldPx = 256.0 * std::pow(2.0, double(zoom_));   // whole world width in screen px
    auto toScreen = [&](double mx, double my) {
        return ImVec2(float(p0.x + width * 0.5 + (mx - cx_) * worldPx), float(p0.y + height * 0.5 + (my - cy_) * worldPx));
    };
    auto toMerc = [&](ImVec2 s) {
        return std::make_pair(cx_ + (double(s.x) - p0.x - width * 0.5) / worldPx, cy_ + (double(s.y) - p0.y - height * 0.5) / worldPx);
    };

    // interaction
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0.f) {
        auto before = toMerc(io.MousePos);
        zoom_ = clampf(zoom_ + io.MouseWheel * 0.35f, 0.f, 14.5f);
        const double wp = 256.0 * std::pow(2.0, double(zoom_));
        cx_ = before.first - (double(io.MousePos.x) - p0.x - width * 0.5) / wp;
        cy_ = before.second - (double(io.MousePos.y) - p0.y - height * 0.5) / wp;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f)) {
        dragging_ = true;
        cx_ -= io.MouseDelta.x / worldPx;
        cy_ -= io.MouseDelta.y / worldPx;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.f)) {
        cx_ -= io.MouseDelta.x / worldPx;
        cy_ -= io.MouseDelta.y / worldPx;
    }
    if (ImGui::IsItemDeactivated() && !dragging_ && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        auto m = toMerc(io.MousePos);
        double n = 1.0;
        lon = tileXToLon(std::fmod(m.first + 10.0, 1.0) * n, 0);
        lat = tileYToLat(std::clamp(m.second, 0.0, 1.0), 0);
        changed = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_ = false;
    cy_ = std::clamp(cy_, 0.0, 1.0);

    // tiles (fallback to coarser parents while the sharp ones stream in)
    const int n = 1 << z;
    const double tilePx = worldPx / double(n);
    auto tl = toMerc(p0);
    auto br = toMerc(ImVec2(p0.x + width, p0.y + height));
    int tx0 = int(std::floor(tl.first * n)), tx1 = int(std::floor(br.first * n));
    int ty0 = std::max(0, int(std::floor(tl.second * n))), ty1 = std::min(n - 1, int(std::floor(br.second * n)));
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            int wx = ((tx % n) + n) % n;
            ImVec2 a = toScreen(double(tx) / n, double(ty) / n);
            ImVec2 b(float(a.x + tilePx), float(a.y + tilePx));
            unsigned tex = texFor({z, wx, ty, grayscale});
            if (tex) {
                dl->AddImage(ImTextureID(uintptr_t(tex)), a, b);
                continue;
            }
            for (int up = 1; up <= z; ++up) {
                int pz = z - up, px = wx >> up, py = ty >> up;
                auto it = textures_.find({pz, px, py, grayscale});
                if (it == textures_.end()) continue;
                float span = float(1 << up);
                float u0 = float(wx - (px << up)) / span, v0 = float(ty - (py << up)) / span;
                dl->AddImage(ImTextureID(uintptr_t(it->second)), a, b, ImVec2(u0, v0), ImVec2(u0 + 1.f / span, v0 + 1.f / span));
                break;
            }
        }

    // selection marker + rip extent
    double mx = lonToTileX(lon, 0), my = latToTileY(lat, 0);
    ImVec2 c = toScreen(mx, my);
    double mpp = metersPerPixel(lat, 0) * 256.0 / worldPx;   // meters per screen pixel
    float half = float(sizeKm * 1000.0 / mpp * 0.5);
    dl->AddRect(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), IM_COL32(255, 210, 40, 255), 0.f, 0, 2.f);
    dl->AddCircleFilled(c, 4.f, IM_COL32(255, 80, 40, 255));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4f, %.4f   zoom %.1f", lat, lon, zoom_);
    dl->AddText(ImVec2(p0.x + 6, p0.y + height - 18), IM_COL32(255, 255, 255, 220), buf);
    if (pending() > 0) dl->AddText(ImVec2(p0.x + 6, p0.y + 4), IM_COL32(255, 255, 255, 200), "loading tiles...");
    dl->AddText(ImVec2(p0.x + width - 205, p0.y + 4), IM_COL32(220, 220, 220, 160), "Terrain Tiles: Mapzen/AWS");
    dl->PopClipRect();
    ImGui::PopID();
    return changed;
}

}  // namespace zl
