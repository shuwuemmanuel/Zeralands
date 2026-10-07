#include "app/ProxyMeshes.h"

namespace zl {

namespace {

struct Builder {
    ProxyMesh m;
    void vert(Vec3 p, Vec3 n, Color3 c, float sway) {
        n = n.normalized();
        m.verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c.r, c.g, c.b, sway});
    }
    uint32_t base() const { return uint32_t(m.verts.size()); }
    void tri(uint32_t a, uint32_t b, uint32_t c) { m.idx.insert(m.idx.end(), {a, b, c}); }

    // frustum / cylinder / cone along +Y
    void cylinder(Vec3 c, float r0, float r1, float h, int seg, Color3 col, float s0, float s1, bool cap = true) {
        uint32_t b = base();
        float slope = (r0 - r1) / std::max(1e-3f, h);
        for (int i = 0; i <= seg; ++i) {
            float a = kTau * float(i) / float(seg);
            Vec3 dir(std::cos(a), 0, std::sin(a));
            Vec3 n = Vec3(dir.x, slope, dir.z);
            vert(c + dir * r0, n, col, s0);
            vert(c + Vec3(0, h, 0) + dir * r1, n, col, s1);
        }
        for (int i = 0; i < seg; ++i) {
            uint32_t a = b + uint32_t(i * 2);
            tri(a, a + 1, a + 2);
            tri(a + 1, a + 3, a + 2);
        }
        if (cap && r1 > 0.01f) {
            uint32_t cb = base();
            vert(c + Vec3(0, h, 0), Vec3(0, 1, 0), col, s1);
            for (int i = 0; i <= seg; ++i) {
                float a = kTau * float(i) / float(seg);
                vert(c + Vec3(std::cos(a) * r1, h, std::sin(a) * r1), Vec3(0, 1, 0), col, s1);
            }
            for (int i = 0; i < seg; ++i) tri(cb, cb + 2 + uint32_t(i), cb + 1 + uint32_t(i));
        }
    }
    void sphere(Vec3 c, Vec3 rad, int rings, int seg, Color3 col, float sway, float jitter = 0.f, uint32_t seed = 1) {
        uint32_t b = base();
        for (int r = 0; r <= rings; ++r) {
            float v = float(r) / float(rings);
            float phi = kPi * v;
            for (int s = 0; s <= seg; ++s) {
                float u = float(s) / float(seg);
                float th = kTau * u;
                Vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
                float j = 1.f;
                if (jitter > 0 && r > 0 && r < rings && s < seg) {
                    uint64_t h = splitmix64(uint64_t(r * 131 + s) ^ seed);
                    j = 1.f + jitter * (float(h & 1023) / 1023.f - 0.5f);
                }
                Color3 cc{col.r * (0.85f + 0.15f * n.y), col.g * (0.85f + 0.15f * n.y), col.b * (0.85f + 0.15f * n.y)};
                vert(c + Vec3(n.x * rad.x * j, n.y * rad.y * j, n.z * rad.z * j), n, cc, sway);
            }
        }
        // seam fix for jitter: copy first column to last
        for (int r = 0; r <= rings; ++r) {
            auto& first = m.verts[b + uint32_t(r * (seg + 1))];
            auto& last = m.verts[b + uint32_t(r * (seg + 1) + seg)];
            last.px = first.px; last.py = first.py; last.pz = first.pz;
        }
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < seg; ++s) {
                uint32_t a = b + uint32_t(r * (seg + 1) + s);
                uint32_t d = a + uint32_t(seg + 1);
                tri(a, d, a + 1);
                tri(a + 1, d, d + 1);
            }
    }
    void box(Vec3 c, Vec3 half, Color3 col, float sway) {
        static const Vec3 N[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const Vec3& n : N) {
            Vec3 u = std::fabs(n.y) > 0.5f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
            Vec3 v = cross(n, u);
            uint32_t b = base();
            Vec3 ctr = c + Vec3(n.x * half.x, n.y * half.y, n.z * half.z);
            Vec3 U(u.x * half.x, u.y * half.y, u.z * half.z), V(v.x * half.x, v.y * half.y, v.z * half.z);
            vert(ctr - U - V, n, col, sway);
            vert(ctr + U - V, n, col, sway);
            vert(ctr + U + V, n, col, sway);
            vert(ctr - U + V, n, col, sway);
            tri(b, b + 1, b + 2);
            tri(b, b + 2, b + 3);
        }
    }
    // crossed quads (rendered double sided)
    void cards(Vec3 c, float w, float h, int count, Color3 baseCol, Color3 tip, uint32_t seed) {
        for (int i = 0; i < count; ++i) {
            float a = kPi * float(i) / float(count) + float(splitmix64(seed + i) & 255) / 255.f * 0.4f;
            Vec3 d(std::cos(a) * w * 0.5f, 0, std::sin(a) * w * 0.5f);
            Vec3 n(-std::sin(a), 0.4f, std::cos(a));
            uint32_t b = base();
            vert(c - d, n, baseCol, 0.f);
            vert(c + d, n, baseCol, 0.f);
            vert(c + d * 0.6f + Vec3(0, h, 0), n, tip, 1.f);
            vert(c - d * 0.6f + Vec3(0, h, 0), n, tip, 1.f);
            tri(b, b + 1, b + 2);
            tri(b, b + 2, b + 3);
        }
    }
    void frond(Vec3 c, float len, float droop, float ang, Color3 col) {
        Vec3 d(std::cos(ang), 0, std::sin(ang));
        Vec3 side(-d.z, 0, d.x);
        uint32_t b = base();
        Vec3 mid = c + d * (len * 0.5f) + Vec3(0, len * 0.15f, 0);
        Vec3 tip = c + d * len - Vec3(0, droop, 0);
        vert(c, Vec3(0, 1, 0), col, 0.6f);
        vert(mid + side * (len * 0.18f), Vec3(0, 1, 0), col, 0.8f);
        vert(mid - side * (len * 0.18f), Vec3(0, 1, 0), col, 0.8f);
        vert(tip, Vec3(0, 1, 0), col, 1.f);
        tri(b, b + 1, b + 3);
        tri(b, b + 3, b + 2);
    }
};

Color3 mul(Color3 c, float k) { return {c.r * k, c.g * k, c.b * k}; }

}  // namespace

ProxyMesh buildProxyMesh(FoliageKind k) {
    const FoliageInfo& fi = foliageInfo(k);
    const float H = fi.baseHeight;
    Builder b;
    const Color3 p = fi.primary, s = fi.secondary;
    switch (k) {
        case FoliageKind::Conifer:
            b.cylinder({0, 0, 0}, H * 0.035f, H * 0.02f, H * 0.3f, 6, s, 0, 0.1f);
            for (int t = 0; t < 4; ++t) {
                float y0 = H * (0.18f + 0.19f * float(t));
                float r = H * (0.26f - 0.05f * float(t));
                b.cylinder({0, y0, 0}, r, 0.f, H * 0.36f, 8, mul(p, 0.85f + 0.1f * float(t)), 0.2f + 0.2f * float(t), 0.4f + 0.2f * float(t), false);
            }
            break;
        case FoliageKind::Broadleaf: case FoliageKind::Birch: case FoliageKind::JungleTree: case FoliageKind::Mangrove: {
            float trunkH = k == FoliageKind::JungleTree ? 0.6f : 0.45f;
            b.cylinder({0, 0, 0}, H * 0.04f, H * 0.025f, H * trunkH, 6, s, 0, 0.15f);
            if (k == FoliageKind::Mangrove)
                for (int r = 0; r < 4; ++r) {
                    float a = kTau * float(r) / 4.f;
                    b.cylinder({std::cos(a) * H * 0.12f, 0, std::sin(a) * H * 0.12f}, H * 0.015f, H * 0.012f, H * 0.25f, 4, s, 0, 0.05f, false);
                }
            b.sphere({0, H * (trunkH + 0.2f), 0}, {H * 0.32f, H * 0.3f, H * 0.32f}, 5, 8, p, 0.6f, 0.35f, uint32_t(k) * 7);
            b.sphere({H * 0.15f, H * (trunkH + 0.08f), H * 0.05f}, {H * 0.2f, H * 0.18f, H * 0.2f}, 4, 7, mul(p, 0.9f), 0.5f, 0.3f, 3);
            b.sphere({-H * 0.12f, H * (trunkH + 0.12f), -H * 0.1f}, {H * 0.2f, H * 0.18f, H * 0.2f}, 4, 7, mul(p, 1.08f), 0.5f, 0.3f, 5);
            break;
        }
        case FoliageKind::Acacia: case FoliageKind::Baobab: {
            float tr = k == FoliageKind::Baobab ? 0.14f : 0.03f;
            b.cylinder({0, 0, 0}, H * tr, H * tr * 0.7f, H * 0.65f, 7, s, 0, 0.1f);
            b.sphere({0, H * 0.75f, 0}, {H * 0.55f, H * 0.12f, H * 0.55f}, 4, 9, p, 0.5f, 0.3f, 9);
            break;
        }
        case FoliageKind::Palm: case FoliageKind::Cycad: case FoliageKind::TreeFern: {
            float lean = k == FoliageKind::Palm ? 0.12f : 0.f;
            b.cylinder({0, 0, 0}, H * 0.035f, H * 0.028f, H * 0.5f, 6, s, 0, 0.3f, false);
            b.cylinder({H * lean * 0.5f, H * 0.5f, 0}, H * 0.028f, H * 0.022f, H * 0.45f, 6, s, 0.3f, 0.6f, false);
            Vec3 top(H * lean, H * 0.95f, 0);
            int fronds = k == FoliageKind::Cycad ? 10 : 8;
            for (int f = 0; f < fronds; ++f) b.frond(top, H * (k == FoliageKind::Cycad ? 0.35f : 0.45f), H * 0.2f, kTau * float(f) / float(fronds), mul(p, 0.9f + 0.2f * float(f & 1)));
            break;
        }
        case FoliageKind::DeadTree:
            b.cylinder({0, 0, 0}, H * 0.04f, H * 0.012f, H, 5, p, 0, 0.1f, false);
            for (int br = 0; br < 4; ++br) {
                float a = kTau * float(br) / 4.f + 0.4f;
                b.cylinder({0, H * (0.45f + 0.12f * float(br)), 0}, H * 0.012f, H * 0.004f, H * 0.3f, 4, s, 0.1f, 0.2f, false);
                auto& v = b.m.verts;
                for (size_t i = v.size() - 10; i < v.size(); ++i)
                    if (v[i].py > H * (0.45f + 0.12f * float(br)) + 0.01f) { v[i].px += std::cos(a) * H * 0.2f; v[i].pz += std::sin(a) * H * 0.2f; }
            }
            break;
        case FoliageKind::Cactus:
            b.cylinder({0, 0, 0}, H * 0.08f, H * 0.07f, H, 8, p, 0, 0.05f);
            b.cylinder({H * 0.15f, H * 0.35f, 0}, H * 0.05f, H * 0.045f, H * 0.35f, 6, s, 0, 0.05f);
            b.cylinder({-H * 0.14f, H * 0.5f, 0}, H * 0.045f, H * 0.04f, H * 0.3f, 6, s, 0, 0.05f);
            b.box({H * 0.08f, H * 0.37f, 0}, {H * 0.07f, H * 0.03f, H * 0.03f}, s, 0);
            b.box({-H * 0.08f, H * 0.52f, 0}, {H * 0.07f, H * 0.03f, H * 0.03f}, s, 0);
            break;
        case FoliageKind::Shrub: case FoliageKind::DesertShrub:
            b.sphere({0, H * 0.45f, 0}, {H * 0.7f, H * 0.5f, H * 0.7f}, 4, 7, p, 0.4f, 0.45f, 11);
            b.sphere({H * 0.35f, H * 0.3f, 0}, {H * 0.4f, H * 0.35f, H * 0.4f}, 3, 6, mul(p, 0.9f), 0.4f, 0.4f, 13);
            break;
        case FoliageKind::Fern:
            for (int f = 0; f < 7; ++f) b.frond({0, H * 0.05f, 0}, H * 0.9f, -H * 0.3f, kTau * float(f) / 7.f, mul(p, 0.85f + 0.05f * float(f % 3)));
            break;
        case FoliageKind::GrassTuft: case FoliageKind::Reeds:
            b.cards({0, 0, 0}, H * (k == FoliageKind::Reeds ? 0.5f : 1.1f), H, 3, mul(p, 0.7f), s, 21);
            break;
        case FoliageKind::Flowers:
            b.cards({0, 0, 0}, H * 1.2f, H * 0.8f, 2, {0.25f, 0.45f, 0.15f}, {0.25f, 0.45f, 0.15f}, 23);
            b.sphere({0.1f, H * 0.85f, 0}, {H * 0.12f, H * 0.1f, H * 0.12f}, 2, 5, p, 1.f);
            b.sphere({-0.12f, H * 0.75f, 0.08f}, {H * 0.12f, H * 0.1f, H * 0.12f}, 2, 5, s, 1.f);
            break;
        case FoliageKind::Boulder: case FoliageKind::SmallRocks:
            b.sphere({0, H * 0.3f, 0}, {H * 0.8f, H * 0.55f, H * 0.65f}, 4, 7, p, 0.f, 0.5f, uint32_t(k) * 31);
            if (k == FoliageKind::SmallRocks) b.sphere({H * 0.9f, H * 0.15f, H * 0.3f}, {H * 0.4f, H * 0.3f, H * 0.35f}, 3, 5, s, 0.f, 0.5f, 3);
            break;
        case FoliageKind::GiantMushroom:
            b.cylinder({0, 0, 0}, H * 0.07f, H * 0.05f, H * 0.75f, 7, s, 0, 0.1f, false);
            b.sphere({0, H * 0.78f, 0}, {H * 0.42f, H * 0.2f, H * 0.42f}, 4, 10, p, 0.15f);
            break;
        case FoliageKind::Crystal:
            for (int c = 0; c < 5; ++c) {
                float a = kTau * float(c) / 5.f;
                float hh = H * (0.5f + 0.25f * float(c % 3));
                b.cylinder({std::cos(a) * H * 0.12f, 0, std::sin(a) * H * 0.12f}, H * 0.09f, 0.f, hh, 6, c & 1 ? p : s, 0, 0, false);
            }
            break;
        case FoliageKind::CharredStump:
            b.cylinder({0, 0, 0}, H * 0.25f, H * 0.18f, H, 6, p, 0, 0, true);
            break;
        case FoliageKind::StreetLamp: case FoliageKind::Torch: case FoliageKind::NeonPylon:
            b.cylinder({0, 0, 0}, H * 0.02f, H * 0.015f, H, 6, s, 0, 0, false);
            if (k == FoliageKind::StreetLamp) b.box({H * 0.08f, H * 0.98f, 0}, {H * 0.1f, H * 0.015f, H * 0.03f}, s, 0);
            b.sphere({k == FoliageKind::StreetLamp ? H * 0.16f : 0.f, H * 0.96f, 0}, {H * 0.04f, H * 0.03f, H * 0.04f}, 3, 6, p, 0);
            break;
        case FoliageKind::TelegraphPole:
            b.cylinder({0, 0, 0}, H * 0.018f, H * 0.014f, H, 6, p, 0, 0, false);
            b.box({0, H * 0.92f, 0}, {H * 0.12f, H * 0.012f, H * 0.012f}, s, 0);
            break;
        case FoliageKind::Milestone: case FoliageKind::Column: case FoliageKind::Ruin:
            if (k == FoliageKind::Ruin) {
                b.box({0, H * 0.35f, 0}, {H * 0.35f, H * 0.35f, H * 0.08f}, p, 0);
                b.box({H * 0.25f, H * 0.75f, 0}, {H * 0.1f, H * 0.25f, H * 0.08f}, s, 0);
            } else {
                b.cylinder({0, 0, 0}, H * 0.08f, H * 0.07f, H, 10, p, 0, 0, true);
                b.box({0, H, 0}, {H * 0.11f, H * 0.03f, H * 0.11f}, s, 0);
            }
            break;
        case FoliageKind::WindTurbine:
            b.cylinder({0, 0, 0}, H * 0.025f, H * 0.014f, H * 0.8f, 8, p, 0, 0, false);
            b.box({0, H * 0.8f, H * 0.02f}, {H * 0.02f, H * 0.02f, H * 0.05f}, s, 0);
            for (int bl = 0; bl < 3; ++bl) {
                float a = kTau * float(bl) / 3.f;
                Vec3 c(0, H * 0.8f, H * 0.07f);
                Vec3 d(std::cos(a), std::sin(a), 0);
                uint32_t bb = b.base();
                Vec3 side(-d.y, d.x, 0);
                b.vert(c + side * (H * 0.02f), {0, 0, 1}, p, 0);
                b.vert(c - side * (H * 0.02f), {0, 0, 1}, p, 0);
                b.vert(c + d * (H * 0.45f), {0, 0, 1}, p, 0);
                b.tri(bb, bb + 1, bb + 2);
            }
            break;
        default:
            b.box({0, H * 0.5f, 0}, {H * 0.3f, H * 0.5f, H * 0.3f}, p, 0);
            break;
    }
    return b.m;
}

}  // namespace zl
