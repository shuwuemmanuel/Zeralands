#include "core/TextureLibrary.h"

#include "core/ImageIO.h"
#include "core/Noise.h"

#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace zl {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> splitTokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (!std::isalnum(ch)) { flush(); continue; }
        bool isDigit = std::isdigit(ch) != 0;
        if (!cur.empty()) {
            bool prevDigit = std::isdigit(static_cast<unsigned char>(cur.back())) != 0;
            bool camel = std::isupper(ch) && std::islower(static_cast<unsigned char>(s[i - 1]));
            if (prevDigit != isDigit || camel) {
                // keep "2k"/"4k" together
                if (!(prevDigit && (ch == 'k' || ch == 'K') && cur.size() <= 2)) flush();
            }
        }
        cur += char(std::tolower(ch));
    }
    flush();
    return out;
}

enum class MapType { None, Albedo, Normal, NormalDX, Rough, AO, Height, Spec, Gloss };

MapType mapTypeOf(const std::vector<std::string>& toks, size_t& tokIndex) {
    // scan from the end: "Ground14_normal_xtm" -> normal
    for (size_t k = toks.size(); k-- > 0;) {
        const std::string& t = toks[k];
        tokIndex = k;
        if (t == "diffuse" || t == "albedo" || t == "basecolor" || t == "color" || t == "colour" || t == "col" || t == "diff" ||
            t == "base")
            return MapType::Albedo;
        if (t == "normal" || t == "nor" || t == "nrm" || t == "normalgl" || t == "norm") {
            if (k + 1 < toks.size() && toks[k + 1] == "dx") return MapType::NormalDX;
            return MapType::Normal;
        }
        if (t == "normaldx") return MapType::NormalDX;
        if (t == "roughness" || t == "rough" || t == "rgh") return MapType::Rough;
        if (t == "occlusion" || t == "ao" || t == "ambientocclusion") return MapType::AO;
        if (t == "displace" || t == "displacement" || t == "height" || t == "disp" || t == "bump") return MapType::Height;
        if (t == "specular" || t == "spec") return MapType::Spec;
        if (t == "gloss" || t == "glossiness") return MapType::Gloss;
    }
    return MapType::None;
}

bool isImage(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp" || e == ".webp";
}

bool tokenIn(const char* list, const std::string& tok) {
    std::istringstream ss(list);
    std::string w;
    while (ss >> w)
        if (w == tok) return true;
    return false;
}

}  // namespace

int TextureLibrary::scan(const std::string& root) {
    root_ = root;
    sets_.clear();
    assign_.fill(-1);
    std::error_code ec;
    if (root.empty() || !fs::is_directory(fs::u8path(root), ec)) return 0;

    std::map<std::string, TextureSet> groups;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(root), fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || !isImage(it->path())) continue;
        std::string stem = it->path().stem().u8string();
        auto toks = splitTokens(stem);
        size_t mi = 0;
        MapType mt = mapTypeOf(toks, mi);
        if (mt == MapType::None) continue;
        std::string key;
        for (size_t k = 0; k < mi; ++k) key += toks[k] + "_";
        std::string folder = it->path().parent_path().u8string();
        std::string gkey = folder + "|" + key;
        TextureSet& s = groups[gkey];
        if (s.name.empty()) {
            // readable name: original stem up to the map token
            // readable name: the original stem up to the map token ("Ground14_diffuse_xtm" -> "Ground14")
            std::string low = stem;
            for (auto& ch : low) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            size_t cut = low.rfind(toks[mi]);
            std::string nm = cut != std::string::npos ? stem.substr(0, cut) : stem;
            while (!nm.empty() && (nm.back() == '_' || nm.back() == '-' || nm.back() == ' ' || nm.back() == '.')) nm.pop_back();
            s.name = nm.empty() ? stem : nm;
            s.folder = folder;
        }
        std::string file = it->path().u8string();
        switch (mt) {
            case MapType::Albedo: s.albedo = file; break;
            case MapType::Normal: if (s.normal.empty() || s.normalDirectX) { s.normal = file; s.normalDirectX = false; } break;
            case MapType::NormalDX: if (s.normal.empty()) { s.normal = file; s.normalDirectX = true; } break;
            case MapType::Rough: s.roughness = file; break;
            case MapType::AO: s.ao = file; break;
            case MapType::Height:
                // prefer true displacement over bump
                if (s.height.empty() || toks[mi] != "bump") s.height = file;
                break;
            case MapType::Spec: s.specular = file; break;
            case MapType::Gloss: s.gloss = file; break;
            default: break;
        }
    }

    for (auto& [k, s] : groups) {
        if (s.albedo.empty()) continue;
        // classify by tokens from the set name and its folder chain (relative to root)
        std::vector<std::string> toks = splitTokens(s.name);
        fs::path rel = fs::path(fs::u8path(s.folder)).lexically_relative(fs::u8path(root_));
        for (const auto& part : rel) {
            auto t2 = splitTokens(part.u8string());
            toks.insert(toks.end(), t2.begin(), t2.end());
        }
        float bestScore = 0;
        for (int m = 0; m < kMaterialCount; ++m) {
            float sc = 0;
            for (size_t i = 0; i < toks.size(); ++i)
                if (tokenIn(materialInfo(MaterialKind(m)).keywords, toks[i])) sc += i < 3 ? 2.f : 1.f;   // name beats folder
            s.score[size_t(m)] = sc;
            if (sc > bestScore) { bestScore = sc; s.best = MaterialKind(m); }
        }
        // completeness bonus so full PBR sets win ties
        float bonus = (s.normal.empty() ? 0.f : 0.3f) + (s.roughness.empty() ? 0.f : 0.2f) + (s.height.empty() ? 0.f : 0.2f);
        for (auto& v : s.score)
            if (v > 0) v += bonus;
        sets_.push_back(s);
    }
    std::sort(sets_.begin(), sets_.end(), [](const TextureSet& a, const TextureSet& b) { return a.name < b.name; });
    autoAssign(1);
    int usable = 0;
    for (auto& s : sets_) usable += s.best != MaterialKind::Count ? 1 : 0;
    return usable;
}

// Materials that can borrow a related texture (colour-shifted on load) when the library has no exact match.
static MaterialKind fallbackOf(MaterialKind k) {
    using M = MaterialKind;
    switch (k) {
        case M::LushGrass: case M::DryGrass: case M::Moss: return M::Grass;
        case M::ForestFloor: case M::Mud: case M::PackedDirt: case M::Ash: return M::Dirt;
        case M::Cliff: case M::Sandstone: case M::Basalt: case M::Crystal: return M::Rock;
        case M::RedSand: case M::Salt: return M::Sand;
        case M::Pebbles: case M::RailBed: return M::Gravel;
        case M::Ice: return M::Snow;
        case M::RomanStone: case M::Cobblestone: return M::Concrete;
        case M::GlowPanel: return M::Asphalt;
        default: return M::Count;
    }
}

std::vector<int> TextureLibrary::candidates(MaterialKind k) const {
    std::vector<int> idx;
    for (size_t i = 0; i < sets_.size(); ++i)
        if (sets_[i].score[size_t(k)] > 0) idx.push_back(int(i));
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return sets_[a].score[size_t(k)] > sets_[b].score[size_t(k)]; });
    // borrow from the related material (and its own fallback) when nothing matches exactly
    for (MaterialKind f = fallbackOf(k); idx.empty() && f != MaterialKind::Count; f = fallbackOf(f)) {
        for (size_t i = 0; i < sets_.size(); ++i)
            if (sets_[i].score[size_t(f)] > 0) idx.push_back(int(i));
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return sets_[a].score[size_t(f)] > sets_[b].score[size_t(f)]; });
        if (f == fallbackOf(f)) break;
    }
    return idx;
}

void TextureLibrary::autoAssign(uint64_t seed) {
    Rng rng(seed);
    for (int m = 0; m < kMaterialCount; ++m) {
        auto c = candidates(MaterialKind(m));
        if (c.empty()) { assign_[size_t(m)] = -1; continue; }
        float top = sets_[c[0]].score[size_t(m)];
        std::vector<int> best;
        for (int i : c)
            if (sets_[i].score[size_t(m)] >= top - 0.25f) best.push_back(i);
        assign_[size_t(m)] = best[size_t(rng.next() % best.size())];
    }
}

MaterialPixels TextureLibrary::load(MaterialKind k, int size) const {
    int si = assign_[size_t(k)];
    if (si < 0 || si >= int(sets_.size())) return proceduralMaterial(k, size);
    const TextureSet& s = sets_[size_t(si)];
    Image8 alb;
    if (!loadImage8(s.albedo, alb, 3)) return proceduralMaterial(k, size);
    alb = resizeImage8(alb, size, size);
    auto loadGray = [&](const std::string& path, uint8_t def) {
        Image8 g;
        if (!path.empty() && loadImage8(path, g, 1)) return resizeImage8(g, size, size);
        g.w = g.h = size;
        g.c = 1;
        g.px.assign(size_t(size) * size, def);
        return g;
    };
    Image8 height = loadGray(s.height, 128), ao = loadGray(s.ao, 255);
    Image8 rough;
    if (!s.roughness.empty()) rough = loadGray(s.roughness, 200);
    else if (!s.gloss.empty()) { rough = loadGray(s.gloss, 55); for (auto& v : rough.px) v = uint8_t(255 - v); }
    else if (!s.specular.empty()) { rough = loadGray(s.specular, 55); for (auto& v : rough.px) v = uint8_t(255 - v / 2); }
    else rough = loadGray("", uint8_t(materialInfo(k).roughness * 255.f));
    Image8 nrm;
    bool haveN = !s.normal.empty() && loadImage8(s.normal, nrm, 3);
    if (haveN) nrm = resizeImage8(nrm, size, size);

    // borrowed set: shift its average colour toward this material's palette (keeps detail, fixes hue)
    if (s.score[size_t(k)] <= 0.f) {
        double mr = 0, mg = 0, mb = 0;
        const size_t np = size_t(size) * size;
        for (size_t i = 0; i < np; ++i) { mr += alb.px[i * 3]; mg += alb.px[i * 3 + 1]; mb += alb.px[i * 3 + 2]; }
        const Color3 want = materialInfo(k).albedo;
        float fr = float(want.r * 255.0 / std::max(1.0, mr / np)), fg = float(want.g * 255.0 / std::max(1.0, mg / np)),
              fb = float(want.b * 255.0 / std::max(1.0, mb / np));
        for (size_t i = 0; i < np; ++i) {
            alb.px[i * 3] = uint8_t(std::clamp(alb.px[i * 3] * lerpf(1.f, fr, 0.75f), 0.f, 255.f));
            alb.px[i * 3 + 1] = uint8_t(std::clamp(alb.px[i * 3 + 1] * lerpf(1.f, fg, 0.75f), 0.f, 255.f));
            alb.px[i * 3 + 2] = uint8_t(std::clamp(alb.px[i * 3 + 2] * lerpf(1.f, fb, 0.75f), 0.f, 255.f));
        }
    }
    MaterialPixels mp;
    mp.size = size;
    mp.source = s.name;
    const size_t n = size_t(size) * size;
    mp.albedoHeight.resize(n * 4);
    mp.normalRough.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        mp.albedoHeight[i * 4 + 0] = alb.px[i * 3 + 0];
        mp.albedoHeight[i * 4 + 1] = alb.px[i * 3 + 1];
        mp.albedoHeight[i * 4 + 2] = alb.px[i * 3 + 2];
        mp.albedoHeight[i * 4 + 3] = height.px[i];
        uint8_t nx = 128, ny = 128;
        if (haveN) {
            nx = nrm.px[i * 3 + 0];
            ny = s.normalDirectX ? uint8_t(255 - nrm.px[i * 3 + 1]) : nrm.px[i * 3 + 1];
        }
        mp.normalRough[i * 4 + 0] = nx;
        mp.normalRough[i * 4 + 1] = ny;
        mp.normalRough[i * 4 + 2] = ao.px[i];
        mp.normalRough[i * 4 + 3] = rough.px[i];
    }
    if (!haveN) {
        // derive normals from height so the surface still has relief
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                auto H = [&](int xx, int yy) { return float(height.px[size_t((yy + size) % size) * size + size_t((xx + size) % size)]) / 255.f; };
                float dx = (H(x + 1, y) - H(x - 1, y)) * 2.f, dy = (H(x, y + 1) - H(x, y - 1)) * 2.f;
                Vec3 nn = Vec3(-dx, -dy, 1.f).normalized();
                size_t i = size_t(y) * size + x;
                mp.normalRough[i * 4 + 0] = uint8_t(std::lround((nn.x * 0.5f + 0.5f) * 255.f));
                mp.normalRough[i * 4 + 1] = uint8_t(std::lround((nn.y * 0.5f + 0.5f) * 255.f));
            }
    }
    return mp;
}

// ------------------------------------------------------------------ procedural fallback
MaterialPixels proceduralMaterial(MaterialKind k, int size, uint64_t seed) {
    const MaterialInfo& mi = materialInfo(k);
    Noise n(hashCombine(seed, uint64_t(k) * 977));
    MaterialPixels mp;
    mp.size = size;
    mp.source = "procedural";
    const size_t N = size_t(size) * size;
    std::vector<float> H(N), V(N);   // height, colour variation
    const int P = 8;                  // base period (cells across the tile)
    auto cellular = [&](float u, float v, int period, float& edge, uint32_t& id) {
        // periodic worley by wrapping the lattice manually
        float x = u * period, y = v * period;
        int ix = int(std::floor(x)), iy = int(std::floor(y));
        float f1 = 9, f2 = 9;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                int cx = ix + ox, cy = iy + oy;
                int wx = ((cx % period) + period) % period, wy = ((cy % period) + period) % period;
                uint64_t h = hashCombine(uint64_t(wx) * 73856093ull ^ uint64_t(wy) * 19349663ull, seed + uint64_t(k));
                float px = float(cx) + 0.15f + 0.7f * float(h & 0xFFFF) / 65535.f;
                float py = float(cy) + 0.15f + 0.7f * float((h >> 16) & 0xFFFF) / 65535.f;
                float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
                if (d < f1) { f2 = f1; f1 = d; id = uint32_t(h >> 32); }
                else if (d < f2) f2 = d;
            }
        edge = f2 - f1;
    };
    parallelFor(0, size, [&](int y) {
        for (int x = 0; x < size; ++x) {
            float u = float(x) / float(size), v = float(y) / float(size);
            float base = n.periodicFbm(u * P, v * P, P, 6, 0.55f);
            float fine = n.periodicFbm(u * P * 8 + 3.f, v * P * 8, P * 8, 3, 0.5f);
            float h = 0.5f + 0.35f * base + 0.15f * fine;
            float var = 0.5f + 0.5f * n.periodicFbm(u * 4 + 9.f, v * 4, 4, 4, 0.5f);
            float edge;
            uint32_t id = 0;
            switch (k) {
                case MaterialKind::Cobblestone: case MaterialKind::Pebbles: case MaterialKind::Gravel: case MaterialKind::RailBed: {
                    int per = k == MaterialKind::Cobblestone ? 10 : (k == MaterialKind::Pebbles ? 14 : 24);
                    cellular(u, v, per, edge, id);
                    float stone = smoothstepf(0.02f, 0.18f, edge);
                    h = stone * (0.6f + 0.4f * float(id & 255) / 255.f) + 0.1f * fine;
                    var = float((id >> 8) & 255) / 255.f;
                    break;
                }
                case MaterialKind::RomanStone: case MaterialKind::Concrete: {
                    int rows = k == MaterialKind::RomanStone ? 6 : 2;
                    float ry = v * rows;
                    int row = int(ry);
                    float rx = u * rows * 1.5f + (row & 1) * 0.5f + float(hashCombine(row, 3) % 100) / 300.f;
                    float jx = rx - std::floor(rx), jy = ry - std::floor(ry);
                    float joint = smoothstepf(0.0f, 0.05f, std::min(std::min(jx, 1 - jx), std::min(jy, 1 - jy)));
                    h = joint * (0.75f + 0.1f * base) + 0.08f * fine;
                    var = float(hashCombine(uint64_t(int(rx)) * 31 + row, 9) % 255) / 255.f;
                    break;
                }
                case MaterialKind::Crystal: case MaterialKind::Basalt: {
                    cellular(u, v, k == MaterialKind::Crystal ? 6 : 9, edge, id);
                    h = smoothstepf(0.0f, 0.3f, edge) * 0.8f + 0.2f * fine;
                    var = float(id & 255) / 255.f;
                    break;
                }
                case MaterialKind::Lava: {
                    cellular(u, v, 7, edge, id);
                    float crack = 1.f - smoothstepf(0.0f, 0.12f, edge + 0.05f * base);
                    h = 1.f - crack;
                    var = crack;   // var==1 -> glowing
                    break;
                }
                case MaterialKind::GlowPanel: {
                    float gx = std::fabs(u * 4 - std::round(u * 4)), gy = std::fabs(v * 4 - std::round(v * 4));
                    float line = 1.f - smoothstepf(0.0f, 0.03f, std::min(gx, gy));
                    h = 0.6f - line * 0.2f + 0.05f * fine;
                    var = line;
                    break;
                }
                case MaterialKind::Grass: case MaterialKind::LushGrass: case MaterialKind::DryGrass: {
                    float blades = n.periodicFbm(u * P * 16, v * P * 4, P * 16, 2, 0.5f);
                    h = 0.5f + 0.3f * blades + 0.2f * base;
                    break;
                }
                case MaterialKind::Sand: case MaterialKind::RedSand: {
                    float ripples = std::sin((v * 24.f + base * 2.f) * kTau) * 0.5f + 0.5f;
                    h = 0.45f + 0.25f * ripples + 0.2f * base + 0.1f * fine;
                    break;
                }
                case MaterialKind::Asphalt: h = 0.5f + 0.1f * base + 0.4f * fine; var = 0.5f + 0.5f * fine; break;
                default: break;
            }
            size_t i = size_t(y) * size + x;
            H[i] = clamp01(h);
            V[i] = clamp01(var);
        }
    });
    mp.albedoHeight.resize(N * 4);
    mp.normalRough.resize(N * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            size_t i = size_t(y) * size + x;
            float t = V[i];
            float shade = 0.75f + 0.5f * (H[i] - 0.5f);
            Color3 c{lerpf(mi.albedo.r, mi.albedo2.r, t), lerpf(mi.albedo.g, mi.albedo2.g, t), lerpf(mi.albedo.b, mi.albedo2.b, t)};
            if (k == MaterialKind::Lava || k == MaterialKind::GlowPanel) {
                c = t > 0.5f ? mi.albedo2 : mi.albedo;
                if (k == MaterialKind::Lava) c = Color3{lerpf(0.12f, 1.f, t), lerpf(0.05f, 0.45f, t * t), lerpf(0.03f, 0.08f, t)};
                shade = 1.f;
            }
            mp.albedoHeight[i * 4 + 0] = uint8_t(std::lround(clamp01(c.r * shade) * 255.f));
            mp.albedoHeight[i * 4 + 1] = uint8_t(std::lround(clamp01(c.g * shade) * 255.f));
            mp.albedoHeight[i * 4 + 2] = uint8_t(std::lround(clamp01(c.b * shade) * 255.f));
            mp.albedoHeight[i * 4 + 3] = uint8_t(std::lround(H[i] * 255.f));
            auto Hs = [&](int xx, int yy) { return H[size_t((yy + size) % size) * size + size_t((xx + size) % size)]; };
            float dx = (Hs(x + 1, y) - Hs(x - 1, y)) * float(size) / 64.f, dy = (Hs(x, y + 1) - Hs(x, y - 1)) * float(size) / 64.f;
            Vec3 nn = Vec3(-dx, -dy, 1.f).normalized();
            mp.normalRough[i * 4 + 0] = uint8_t(std::lround((nn.x * 0.5f + 0.5f) * 255.f));
            mp.normalRough[i * 4 + 1] = uint8_t(std::lround((nn.y * 0.5f + 0.5f) * 255.f));
            mp.normalRough[i * 4 + 2] = uint8_t(std::lround((0.6f + 0.4f * H[i]) * 255.f));
            mp.normalRough[i * 4 + 3] = uint8_t(std::lround(clamp01(mi.roughness + 0.1f * (0.5f - H[i])) * 255.f));
        }
    return mp;
}

}  // namespace zl
