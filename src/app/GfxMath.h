// ZeraLands - small matrix helpers and camera for the editor viewport.
#pragma once

#include "core/Common.h"

namespace zl {

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};   // column-major
    static Mat4 identity() { return Mat4(); }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    static Mat4 perspective(float fovy, float aspect, float zn, float zf) {
        Mat4 r;
        float f = 1.f / std::tan(fovy * 0.5f);
        for (float& v : r.m) v = 0;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf);
        r.m[11] = -1.f;
        r.m[14] = 2.f * zf * zn / (zn - zf);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 f = (at - eye).normalized();
        Vec3 s = cross(f, up).normalized();
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }
};

// Unreal-style viewport camera: RMB look + WASD/QE fly, MMB pan, wheel dolly, Alt+LMB orbit, F to frame.
struct Camera {
    Vec3 pos{0, 500, 0};
    float yaw = 0.8f, pitch = -0.35f;   // radians
    float fov = 1.0f;
    float speed = 120.f;                // m/s
    Vec3 forward() const { return Vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)); }
    Vec3 right() const { return cross(forward(), Vec3(0, 1, 0)).normalized(); }
    Mat4 view() const { return Mat4::lookAt(pos, pos + forward(), Vec3(0, 1, 0)); }
    void frame(float worldSize, float heightRange) {
        pos = Vec3(-worldSize * 0.15f, heightRange * 1.2f + worldSize * 0.35f, -worldSize * 0.15f);
        Vec3 target(worldSize * 0.5f, heightRange * 0.3f, worldSize * 0.5f);
        Vec3 d = (target - pos).normalized();
        yaw = std::atan2(d.x, d.z);
        pitch = std::asin(clampf(d.y, -1.f, 1.f));
        speed = std::max(20.f, worldSize * 0.06f);
    }
};

}  // namespace zl
