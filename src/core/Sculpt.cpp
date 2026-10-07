#include "core/Sculpt.h"

#include "core/Erosion.h"
#include "core/Noise.h"

namespace zl {

const char* sculptToolName(SculptTool t) {
    static const char* names[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Erode", "Terrace"};
    return names[std::clamp(int(t), 0, int(SculptTool::Count) - 1)];
}

void sculptDab(Terrain& t, Vec2 c, const SculptBrush& b, float dt, uint64_t seed, int& x0, int& y0, int& x1, int& y1) {
    const int res = t.baseHeight.width();
    x0 = y0 = 0;
    x1 = y1 = -1;
    if (res == 0) return;
    const float cell = t.cellSize();
    const float rc = b.radius / cell;
    const float cx = c.x / cell, cy = c.y / cell;
    x0 = std::max(0, int(cx - rc - 1));
    y0 = std::max(0, int(cy - rc - 1));
    x1 = std::min(res - 1, int(cx + rc + 1));
    y1 = std::min(res - 1, int(cy + rc + 1));
    if (x1 < x0 || y1 < y0) return;
    const float amount = b.strength * dt;
    Grid& H = t.baseHeight;
    Grid before;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    before.resize(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) before.at(x, y) = H.at(x0 + x, y0 + y);

    auto weightAt = [&](int x, int y) {
        float d = std::sqrt((float(x) - cx) * (float(x) - cx) + (float(y) - cy) * (float(y) - cy)) / std::max(0.5f, rc);
        if (d >= 1.f) return 0.f;
        return 1.f - smoothstepf(1.f - std::max(0.05f, b.falloff), 1.f, d);
    };

    // Erosion works on a local copy with a few hundred droplets.
    Grid eroded;
    if (b.tool == SculptTool::Erode) {
        eroded = before;
        HydraulicParams hp;
        hp.droplets = std::max(50, int(float(w * h) * 0.6f * amount));
        hp.heightScale = t.heightRange / cell;
        hp.maxLifetime = 30;
        hp.radius = 2;
        hydraulicErosion(eroded, hp, seed);
        thermalErosion(eroded, 2, 0.9f / hp.heightScale, 0.5f);
    }
    Noise nz(seed);
    const float metersToNorm = 1.f / t.heightRange;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float wgt = weightAt(x, y);
            if (wgt <= 0) continue;
            float v = before.at(x - x0, y - y0);
            float nv = v;
            switch (b.tool) {
                case SculptTool::Raise: nv = v + amount * wgt * 25.f * metersToNorm * (b.radius / 50.f); break;
                case SculptTool::Lower: nv = v - amount * wgt * 25.f * metersToNorm * (b.radius / 50.f); break;
                case SculptTool::Smooth: {
                    float s = 0;
                    int n = 0;
                    for (int oy = -2; oy <= 2; ++oy)
                        for (int ox = -2; ox <= 2; ++ox) {
                            s += H.atClamped(x + ox, y + oy);
                            ++n;
                        }
                    nv = lerpf(v, s / float(n), clamp01(amount * wgt * 6.f));
                    break;
                }
                case SculptTool::Flatten: {
                    float target = b.flattenHeight * metersToNorm;
                    nv = lerpf(v, target, clamp01(amount * wgt * 5.f));
                    break;
                }
                case SculptTool::Noise: {
                    float n = nz.fbm(float(x) * 0.08f, float(y) * 0.08f, 4);
                    nv = v + n * amount * wgt * 10.f * metersToNorm;
                    break;
                }
                case SculptTool::Erode: nv = lerpf(v, eroded.at(x - x0, y - y0), wgt); break;
                case SculptTool::Terrace: {
                    float step = std::max(0.5f, b.terraceStep) * metersToNorm;
                    float q = std::floor(v / step) * step + step * smoothstepf(0.7f, 1.f, v / step - std::floor(v / step));
                    nv = lerpf(v, q, clamp01(amount * wgt * 4.f));
                    break;
                }
                default: break;
            }
            H.at(x, y) = nv;
            if (!t.height.empty()) t.height.at(x, y) += nv - v;
        }
    }
}

}  // namespace zl
