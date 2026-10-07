#include "app/Renderer.h"

#include "app/ProxyMeshes.h"

#include <GL/glew.h>

#include <cstdio>

namespace zl {

namespace {

// ------------------------------------------------------------------ shaders
const char* kCommon = R"(
#version 330 core
uniform vec3 uSunDir, uSunCol, uSkyTop, uSkyHor, uCam;
uniform float uFog;
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 applyFog(vec3 col, vec3 wpos) {
    float d = length(uCam - wpos);
    float f = 1.0 - exp(-pow(d * uFog * 0.00022, 1.35));
    vec3 fogCol = toLinear(uSkyHor) + toLinear(uSunCol) * 0.15 * pow(max(dot(normalize(wpos - uCam), uSunDir), 0.0), 8.0);
    return mix(col, fogCol, clamp(f, 0.0, 1.0));
}
vec3 toneMap(vec3 c) { c = c / (1.0 + c * 0.15); return pow(c, vec3(1.0 / 2.2)); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm3(vec2 p) { return vnoise(p) * 0.55 + vnoise(p * 2.03 + 7.1) * 0.3 + vnoise(p * 4.1 - 3.3) * 0.15; }
)";

const char* kTerrainVS = R"(
layout(location = 0) in vec2 aUV;
uniform sampler2D uHeight;
uniform mat4 uVP;
uniform float uWorld, uHR;
uniform vec2 uTexel;
out vec3 vWorld;
out vec2 vUV;
void main() {
    vec2 tuv = aUV * (1.0 - uTexel) + 0.5 * uTexel;
    float h = textureLod(uHeight, tuv, 0.0).r;
    vec3 p = vec3(aUV.x * uWorld, h * uHR, aUV.y * uWorld);
    vWorld = p; vUV = aUV;
    gl_Position = uVP * vec4(p, 1.0);
}
)";

const char* kTerrainFS = R"(
in vec3 vWorld;
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uHeight, uSplat0, uSplat1;
uniform sampler2DArray uAlb, uNrm;
uniform int uLayers, uAntiTile, uTriplanar, uShadows, uMacro, uGrid;
uniform float uTile[8];
uniform float uEmissive[8];
uniform vec2 uTexel;
uniform float uWorld, uHR, uScorch, uTime;
uniform vec4 uBrush;      // center xz, radius, active
uniform vec3 uBrushColor;

vec2 tuv(vec2 uv) { return uv * (1.0 - uTexel) + 0.5 * uTexel; }
float Hm(vec2 uv) { return texture(uHeight, tuv(uv)).r * uHR; }

// Stochastic, orientation-preserving anti-tiling (after I. Quilez): each region of the plane picks one of
// eight virtual tile offsets via low-frequency noise and blends between two, guided by texture contrast.
void sampleNoTile(vec2 uv, float layer, float vari, out vec4 alb, out vec4 nrm) {
    if (uAntiTile == 0) { alb = texture(uAlb, vec3(uv, layer)); nrm = texture(uNrm, vec3(uv, layer)); return; }
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    float l = vari * 8.0;
    float i = floor(l), f = fract(l);
    vec2 oa = sin(vec2(3.0, 7.0) * i + layer * 1.37);
    vec2 ob = sin(vec2(3.0, 7.0) * (i + 1.0) + layer * 1.37);
    vec4 a = textureGrad(uAlb, vec3(uv + oa, layer), dx, dy);
    vec4 b = textureGrad(uAlb, vec3(uv + ob, layer), dx, dy);
    float t = smoothstep(0.2, 0.8, f - 0.1 * dot(a.rgb - b.rgb, vec3(1.0)));
    alb = mix(a, b, t);
    vec4 na = textureGrad(uNrm, vec3(uv + oa, layer), dx, dy);
    vec4 nb = textureGrad(uNrm, vec3(uv + ob, layer), dx, dy);
    nrm = mix(na, nb, t);
}

void main() {
    vec2 e = uTexel;
    float hl = Hm(vUV - vec2(e.x, 0)), hr = Hm(vUV + vec2(e.x, 0)), hd = Hm(vUV - vec2(0, e.y)), hu = Hm(vUV + vec2(0, e.y));
    float cell = uWorld * e.x;
    vec3 N = normalize(vec3(hl - hr, 2.0 * cell, hd - hu));

    vec4 s0 = texture(uSplat0, tuv(vUV)), s1 = texture(uSplat1, tuv(vUV));
    float w[8] = float[8](s0.r, s0.g, s0.b, s0.a, s1.r, s1.g, s1.b, s1.a);
    float dist = length(uCam - vWorld);
    vec3 bw = pow(abs(N), vec3(4.0)); bw /= (bw.x + bw.y + bw.z);
    bool tri = uTriplanar == 1 && N.y < 0.88;
    float vari = vnoise(vWorld.xz * 0.0045);
    float farBlend = uAntiTile == 1 ? smoothstep(60.0, 450.0, dist) * 0.5 : 0.0;

    vec3 albs[8]; vec3 nrms[8]; vec2 ars[8]; float hts[8];
    float ma = 0.0;
    for (int i = 0; i < 8; ++i) {
        hts[i] = 0.0; albs[i] = vec3(0.0); nrms[i] = N; ars[i] = vec2(1.0, 0.8);
        if (i >= uLayers || w[i] < 0.01) continue;
        float L = float(i);
        float s = 1.0 / uTile[i];
        vec4 a, n;
        sampleNoTile(vWorld.xz * s, L, vari, a, n);
        vec2 tn = n.xy * 2.0 - 1.0;
        vec3 nw = normalize(vec3(tn.x + N.x, N.y, tn.y + N.z));
        if (farBlend > 0.01) {
            vec4 a2, n2;
            sampleNoTile(vWorld.xz * s * 0.23 + 0.37, L, 1.0 - vari, a2, n2);
            a = mix(a, a2, farBlend);
            n.ba = mix(n.ba, n2.ba, farBlend);
        }
        if (tri) {
            // side projections remove stretching on cliffs
            vec4 ax, nx, az, nz;
            sampleNoTile(vWorld.zy * s, L, vari, ax, nx);
            sampleNoTile(vWorld.xy * s, L, vari, az, nz);
            vec2 tx = nx.xy * 2.0 - 1.0, tz = nz.xy * 2.0 - 1.0;
            vec3 nwx = normalize(vec3(N.x, tx.y + N.y, tx.x + N.z));
            vec3 nwz = normalize(vec3(tz.x + N.x, tz.y + N.y, N.z));
            a = a * bw.y + ax * bw.x + az * bw.z;
            n.ba = n.ba * bw.y + nx.ba * bw.x + nz.ba * bw.z;
            nw = normalize(nw * bw.y + nwx * bw.x + nwz * bw.z);
        }
        albs[i] = toLinear(a.rgb);
        hts[i] = a.a;
        nrms[i] = nw;
        ars[i] = n.ba;
        ma = max(ma, w[i] + a.a * 0.45);
    }
    // height-based blending: pebbles poke through sand, grass fills cracks
    ma -= 0.18;
    vec3 alb = vec3(0.0), nrm = vec3(0.0); vec2 ar = vec2(0.0); float emis = 0.0, tot = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= uLayers || w[i] < 0.01) continue;
        float b = max(w[i] + hts[i] * 0.45 - ma, 0.0);
        alb += albs[i] * b; nrm += nrms[i] * b; ar += ars[i] * b; emis += uEmissive[i] * b; tot += b;
    }
    if (tot < 1e-4) { alb = vec3(0.3); nrm = N; ar = vec2(1.0, 0.8); tot = 1.0; }
    alb /= tot; nrm = normalize(nrm); ar /= tot; emis /= tot;
    if (uMacro == 1) alb *= 0.82 + 0.36 * fbm3(vWorld.xz * 0.0018);
    float lum = dot(alb, vec3(0.3, 0.59, 0.11));
    alb = mix(alb, vec3(lum) * vec3(0.55, 0.5, 0.45), uScorch * 0.6);

    // soft terrain self-shadowing by marching the heightfield toward the sun
    float sh = 1.0;
    if (uShadows == 1 && uSunDir.y > 0.0) {
        vec3 p = vWorld + N * 0.5;
        float stepLen = max(cell * 1.5, 2.0);
        for (int k = 0; k < 28; ++k) {
            p += uSunDir * stepLen;
            stepLen *= 1.25;
            vec2 uv = p.xz / uWorld;
            if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0 || p.y > uHR * 1.05) break;
            float d = p.y - Hm(uv);
            sh = min(sh, clamp(d / (stepLen * 0.6) + 0.5, 0.0, 1.0));
            if (sh <= 0.0) break;
        }
    }
    vec3 V = normalize(uCam - vWorld);
    float ndl = max(dot(nrm, uSunDir), 0.0);
    float rough = clamp(ar.y, 0.05, 1.0);
    vec3 Hh = normalize(uSunDir + V);
    float spec = pow(max(dot(nrm, Hh), 0.0), mix(64.0, 6.0, rough)) * (1.0 - rough) * 0.35;
    vec3 sky = mix(toLinear(uSkyHor), toLinear(uSkyTop), nrm.y * 0.5 + 0.5);
    vec3 sun = toLinear(uSunCol) * 2.6;
    vec3 col = alb * (sun * ndl * sh + sky * 0.55 * ar.x) + sun * spec * sh;
    col += alb * emis * (2.2 + 0.6 * sin(uTime * 1.3 + vWorld.x * 0.05));
    col = applyFog(col, vWorld);

    if (uGrid == 1) {
        vec2 g = abs(fract(vWorld.xz / 100.0 - 0.5) - 0.5) / fwidth(vWorld.xz / 100.0);
        col = mix(col, vec3(1.0), (1.0 - min(min(g.x, g.y), 1.0)) * 0.25);
    }
    if (uBrush.w > 0.5) {
        float d = length(vWorld.xz - uBrush.xy);
        float px = fwidth(d) * 1.5;
        float ring = 1.0 - smoothstep(0.0, px, abs(d - uBrush.z));
        float inner = (1.0 - smoothstep(uBrush.z * 0.97, uBrush.z, d)) * 0.12;
        col = mix(col, toLinear(uBrushColor), clamp(ring + inner, 0.0, 1.0));
    }
    oColor = vec4(toneMap(col), 1.0);
}
)";

const char* kWaterVS = R"(
layout(location = 0) in vec2 aUV;
uniform sampler2D uHeight, uWater;
uniform mat4 uVP;
uniform float uWorld, uHR;
uniform vec2 uTexel;
out vec3 vWorld;
out float vDepth;
void main() {
    vec2 tuv = aUV * (1.0 - uTexel) + 0.5 * uTexel;
    float h = textureLod(uHeight, tuv, 0.0).r;
    float w = textureLod(uWater, tuv, 0.0).r;
    float wy = w;
    vDepth = (wy - h) * uHR;
    vec3 p = vec3(aUV.x * uWorld, wy * uHR, aUV.y * uWorld);
    vWorld = p;
    gl_Position = uVP * vec4(p, 1.0);
}
)";

const char* kWaterFS = R"(
in vec3 vWorld;
in float vDepth;
out vec4 oColor;
uniform vec3 uWaterCol;
uniform float uTime;
void main() {
    if (vDepth <= 0.03) discard;
    vec2 p = vWorld.xz * 0.06;
    float t = uTime * 0.6;
    float nx = fbm3(p + vec2(t, t * 0.7)) - fbm3(p + vec2(0.37, 0.0) + vec2(t, t * 0.7));
    float nz = fbm3(p.yx + vec2(-t * 0.8, t)) - fbm3(p.yx + vec2(0.0, 0.37) + vec2(-t * 0.8, t));
    vec3 N = normalize(vec3(nx * 0.6, 1.0, nz * 0.6));
    vec3 V = normalize(uCam - vWorld);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 R = reflect(-V, N);
    vec3 sky = mix(toLinear(uSkyHor), toLinear(uSkyTop), clamp(R.y, 0.0, 1.0));
    vec3 deep = toLinear(uWaterCol) * 0.6, shallow = toLinear(uWaterCol) * 1.6 + vec3(0.02, 0.05, 0.04);
    vec3 body = mix(shallow, deep, clamp(vDepth / 12.0, 0.0, 1.0));
    float spec = pow(max(dot(R, uSunDir), 0.0), 220.0) * 6.0;
    vec3 col = mix(body, sky, fres) + toLinear(uSunCol) * spec;
    float foam = (1.0 - smoothstep(0.0, 0.6, vDepth)) * (0.5 + 0.5 * vnoise(vWorld.xz * 0.5 + uTime));
    col = mix(col, vec3(0.9), foam * 0.35);
    col = applyFog(col, vWorld);
    float alpha = clamp(0.35 + vDepth / 3.0, 0.0, 0.92) + fres * 0.08;
    oColor = vec4(toneMap(col), clamp(alpha, 0.0, 1.0));
}
)";

const char* kRibbonVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;      // lateral meters, along meters
layout(location = 2) in vec4 aInfo;    // kind, network layer, width, curvature
layout(location = 3) in vec4 aFlags;   // markings, kerbs, side(0 top,1 skirt), unused
layout(location = 4) in vec3 aNormal;
uniform mat4 uVP;
out vec3 vWorld; out vec2 vUV; flat out vec4 vInfo; flat out vec4 vFlags; out vec3 vN; out float vCurv;
void main() {
    vWorld = aPos; vUV = aUV; vInfo = aInfo; vFlags = aFlags; vN = aNormal; vCurv = aInfo.w;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

const char* kRibbonFS = R"(
in vec3 vWorld; in vec2 vUV; flat in vec4 vInfo; flat in vec4 vFlags; in vec3 vN; in float vCurv;
out vec4 oColor;
uniform sampler2DArray uNetAlb, uNetNrm;
uniform float uNetTile[8];
uniform float uTime;
uniform vec3 uWaterCol;
void main() {
    int kind = int(vInfo.x + 0.5);
    int layer = int(vInfo.y + 0.5);
    float halfW = vInfo.z * 0.5;
    float lat = vUV.x, along = vUV.y;
    vec2 uv = vec2(lat, along) / uNetTile[layer];
    vec4 a = texture(uNetAlb, vec3(uv, float(layer)));
    vec4 nr = texture(uNetNrm, vec3(uv, float(layer)));
    vec3 alb = toLinear(a.rgb) * (0.85 + 0.3 * vnoise(vWorld.xz * 0.05));
    float emis = 0.0, alpha = 1.0, rough = nr.a;
    float edge = abs(lat) / max(halfW, 0.01);
    if (vFlags.z > 0.5) alb *= 0.55;   // skirts / deck sides
    if (kind == 0 || kind == 8) {   // road
        if (vFlags.x > 0.5 && vFlags.z < 0.5) {
            float dash = step(fract(along / 12.0), 0.5);
            if (abs(lat) < 0.09 && dash > 0.5) alb = vec3(0.85, 0.65, 0.1);
            if (abs(abs(lat) - (halfW - 0.35)) < 0.08) alb = vec3(0.85);
        }
        alpha = 1.0 - smoothstep(0.92, 1.0, edge);
    } else if (kind == 1) {          // railway: ballast, sleepers, rails
        float sleeper = step(fract(along / 0.65), 0.38) * step(abs(lat), 1.3);
        alb = mix(alb, vec3(0.22, 0.14, 0.08), sleeper * 0.9);
        float rail = 1.0 - smoothstep(0.03, 0.06, abs(abs(lat) - 0.72));
        alb = mix(alb, vec3(0.55, 0.55, 0.58), rail);
        rough = mix(rough, 0.2, rail);
        alpha = 1.0 - smoothstep(0.85, 1.0, edge);
    } else if (kind == 2) {          // path: soft dirt edges
        alpha = 1.0 - smoothstep(0.45, 1.0, edge + 0.25 * (vnoise(vWorld.xz * 0.7) - 0.5));
    } else if (kind == 3) {          // racetrack
        float kerbZone = step(halfW - 1.3, abs(lat)) * step(1.0 / 300.0, abs(vCurv));
        if (vFlags.y > 0.5 && kerbZone > 0.5) alb = mod(floor(along / 2.5), 2.0) < 1.0 ? vec3(0.8, 0.05, 0.03) : vec3(0.9);
        if (abs(abs(lat) - (halfW - 0.25)) < 0.1) alb = vec3(0.9);
        if (along < 3.0) alb = mod(floor(lat / 0.75) + floor(along / 0.75), 2.0) < 1.0 ? vec3(0.02) : vec3(0.95);
        if (layer >= 0 && a.r < 0.2 && a.b > 0.6) emis = 1.0;
    } else if (kind == 4) {          // canal water
        vec3 V = normalize(uCam - vWorld);
        float fres = 0.04 + 0.96 * pow(1.0 - max(V.y, 0.0), 5.0);
        alb = mix(toLinear(uWaterCol) * 1.2, toLinear(uSkyHor), fres);
        rough = 0.05;
        if (edge > 0.82) alb = toLinear(vec3(0.45, 0.42, 0.38));
    } else if (kind == 6) {          // maglev: concrete beam with a light strip
        float strip = 1.0 - smoothstep(0.1, 0.2, abs(lat));
        emis = strip;
        alb = mix(alb, vec3(0.2, 0.9, 1.0), strip);
    } else if (kind == 7) {          // lava channel
        float flow = fbm3(vec2(lat * 0.4, along * 0.08 - uTime * 0.6));
        alb = mix(vec3(0.9, 0.18, 0.02), vec3(1.0, 0.75, 0.2), flow);
        emis = 1.6 - edge;
        alpha = 1.0 - smoothstep(0.7, 1.0, edge);
    }
    vec3 N = normalize(vN + vec3(nr.x * 2.0 - 1.0, 0.0, nr.y * 2.0 - 1.0) * 0.4);
    float ndl = max(dot(N, uSunDir), 0.0);
    vec3 V = normalize(uCam - vWorld);
    float spec = pow(max(dot(N, normalize(uSunDir + V)), 0.0), mix(80.0, 8.0, rough)) * (1.0 - rough) * 0.6;
    vec3 sky = mix(toLinear(uSkyHor), toLinear(uSkyTop), 0.6);
    vec3 col = alb * (toLinear(uSunCol) * 2.6 * ndl + sky * 0.55) + toLinear(uSunCol) * spec + alb * emis * 2.5;
    col = applyFog(col, vWorld);
    if (alpha < 0.02) discard;
    oColor = vec4(toneMap(col), alpha);
}
)";

const char* kFoliageVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aSway;
layout(location = 4) in vec4 iPosScale;   // xyz, scale
layout(location = 5) in float iRot;
uniform mat4 uVP;
uniform float uTime, uMaxDist;
out vec3 vWorld; out vec3 vN; out vec3 vColor;
void main() {
    float d = length(iPosScale.xyz - uCam);
    if (d > uMaxDist) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    float c = cos(iRot), s = sin(iRot);
    vec3 p = aPos * iPosScale.w;
    p = vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
    float wind = sin(uTime * 1.6 + iPosScale.x * 0.05 + iPosScale.z * 0.07) + 0.4 * sin(uTime * 3.7 + iPosScale.x * 0.21);
    p.x += aSway * wind * 0.035 * max(1.0, aPos.y * iPosScale.w * 0.08);
    vec3 n = vec3(c * aNormal.x - s * aNormal.z, aNormal.y, s * aNormal.x + c * aNormal.z);
    float shrink = 1.0 - smoothstep(uMaxDist * 0.8, uMaxDist, d);
    vWorld = iPosScale.xyz + p * shrink;
    vN = n;
    float tint = 0.9 + 0.2 * fract(sin(dot(iPosScale.xz, vec2(12.9898, 78.233))) * 43758.5453);
    vColor = aColor * tint;
    gl_Position = uVP * vec4(vWorld, 1.0);
}
)";

const char* kFoliageFS = R"(
in vec3 vWorld; in vec3 vN; in vec3 vColor;
out vec4 oColor;
void main() {
    vec3 N = normalize(vN);
    if (!gl_FrontFacing) N = -N;
    float ndl = max(dot(N, uSunDir), 0.0) * 0.8 + 0.2 * max(dot(-N, uSunDir), 0.0);
    vec3 sky = mix(toLinear(uSkyHor), toLinear(uSkyTop), N.y * 0.5 + 0.5);
    vec3 alb = toLinear(vColor);
    vec3 col = alb * (toLinear(uSunCol) * 2.4 * ndl + sky * 0.6);
    col = applyFog(col, vWorld);
    oColor = vec4(toneMap(col), 1.0);
}
)";

const char* kSkyVS = R"(
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
)";

const char* kSkyFS = R"(
in vec2 vNdc;
out vec4 oColor;
uniform vec3 uFwd, uRight, uUp;
uniform float uTanHalf, uAspect;
void main() {
    vec3 d = normalize(uFwd + uRight * vNdc.x * uTanHalf * uAspect + uUp * vNdc.y * uTanHalf);
    float t = pow(clamp(d.y, 0.0, 1.0), 0.45);
    vec3 col = mix(toLinear(uSkyHor), toLinear(uSkyTop), t);
    float sd = max(dot(d, uSunDir), 0.0);
    col += toLinear(uSunCol) * (pow(sd, 900.0) * 8.0 + pow(sd, 12.0) * 0.25);
    if (d.y < 0.0) col = mix(col, toLinear(uSkyHor) * 0.8, clamp(-d.y * 4.0, 0.0, 1.0));
    oColor = vec4(toneMap(col), 1.0);
}
)";

const char* kGizmoVS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uVP;
uniform float uPointSize;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uVP * vec4(aPos, 1.0); gl_PointSize = uPointSize; }
)";

const char* kGizmoFS = R"(
in vec3 vColor;
out vec4 oColor;
void main() { oColor = vec4(vColor, 1.0); }
)";

unsigned compile(const char* vs, const char* fs, std::string& err) {
    auto stage = [&](GLenum type, const char* src) -> unsigned {
        unsigned s = glCreateShader(type);
        std::string full = std::string(kCommon) + src;
        const char* p = full.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            err += std::string(type == GL_VERTEX_SHADER ? "VS: " : "FS: ") + log;
        }
        return s;
    };
    unsigned v = stage(GL_VERTEX_SHADER, vs), f = stage(GL_FRAGMENT_SHADER, fs);
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        err += std::string("link: ") + log;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

int U(unsigned p, const char* n) { return glGetUniformLocation(p, n); }
void set3(unsigned p, const char* n, Color3 c) { glUniform3f(U(p, n), c.r, c.g, c.b); }
void set3(unsigned p, const char* n, Vec3 v) { glUniform3f(U(p, n), v.x, v.y, v.z); }

int networkLayerOf(const std::array<MaterialKind, 8>& kinds, int count, MaterialKind k) {
    for (int i = 0; i < count; ++i)
        if (kinds[size_t(i)] == k) return i;
    return 0;
}

}  // namespace

// ------------------------------------------------------------------ setup
bool Renderer::init(std::string& err) {
    progTerrain_ = compile(kTerrainVS, kTerrainFS, err);
    progWater_ = compile(kWaterVS, kWaterFS, err);
    progRibbon_ = compile(kRibbonVS, kRibbonFS, err);
    progFoliage_ = compile(kFoliageVS, kFoliageFS, err);
    progSky_ = compile(kSkyVS, kSkyFS, err);
    progGizmo_ = compile(kGizmoVS, kGizmoFS, err);
    if (!err.empty()) return false;
    glGenVertexArrays(1, &skyVao_);
    glGenVertexArrays(1, &gizmoVao_);
    glGenBuffers(1, &gizmoVbo_);
    glBindVertexArray(gizmoVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    glGenVertexArrays(1, &ribbonVao_);
    glGenBuffers(1, &ribbonVbo_);
    glBindVertexArray(ribbonVao_);
    glBindBuffer(GL_ARRAY_BUFFER, ribbonVbo_);
    const int stride = 16 * 4;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(20));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(36));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(52));

    // foliage proxy meshes
    foliageMeshes_.resize(size_t(kFoliageCount));
    for (int k = 0; k < kFoliageCount; ++k) {
        ProxyMesh pm = buildProxyMesh(FoliageKind(k));
        MeshGL& m = foliageMeshes_[size_t(k)];
        glGenVertexArrays(1, &m.vao);
        glGenBuffers(1, &m.vbo);
        glGenBuffers(1, &m.ibo);
        glGenBuffers(1, &m.inst);
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(pm.verts.size() * sizeof(ProxyVertex)), pm.verts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ProxyVertex), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ProxyVertex), reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ProxyVertex), reinterpret_cast<void*>(24));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ProxyVertex), reinterpret_cast<void*>(36));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(pm.idx.size() * 4), pm.idx.data(), GL_STATIC_DRAW);
        m.indexCount = int(pm.idx.size());
        glBindBuffer(GL_ARRAY_BUFFER, m.inst);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, 20, nullptr);
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, 20, reinterpret_cast<void*>(16));
        glVertexAttribDivisor(5, 1);
    }
    glBindVertexArray(0);

    // placeholder textures so the first frame is valid
    auto make2D = [](unsigned& t, GLenum ifmt, GLenum fmt, GLenum type, const void* px) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GLint(ifmt), 1, 1, 0, fmt, type, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    float zero = 0.f, minus = -1.f;
    uint8_t s0[4] = {255, 0, 0, 0}, s1[4] = {0, 0, 0, 0};
    make2D(texHeight_, GL_R32F, GL_RED, GL_FLOAT, &zero);
    make2D(texWater_, GL_R32F, GL_RED, GL_FLOAT, &minus);
    make2D(texSplat0_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, s0);
    make2D(texSplat1_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, s1);
    tile_.fill(4.f);
    emissive_.fill(0.f);
    MaterialBatch b;
    b.size = 64;
    b.kinds = {MaterialKind::Grass};
    b.pixels = {proceduralMaterial(MaterialKind::Grass, 64)};
    uploadMaterialArray(texAlb_, texNrm_, b);
    b.kinds = {MaterialKind::Asphalt};
    b.pixels = {proceduralMaterial(MaterialKind::Asphalt, 64)};
    uploadMaterialArray(texNetAlb_, texNetNrm_, b);
    return true;
}

void Renderer::shutdown() {
    if (pending_.valid()) pending_.wait();
}

void Renderer::ensureTargets(int w, int h) {
    if (w == fbW_ && h == fbH_ && fboMs_) return;
    fbW_ = w;
    fbH_ = h;
    if (!fboMs_) {
        glGenFramebuffers(1, &fboMs_);
        glGenRenderbuffers(1, &rboColorMs_);
        glGenRenderbuffers(1, &rboDepthMs_);
        glGenFramebuffers(1, &fboResolve_);
        glGenTextures(1, &resolveTex_);
    }
    int samples = 4;
    glBindRenderbuffer(GL_RENDERBUFFER, rboColorMs_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, rboDepthMs_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fboMs_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rboColorMs_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepthMs_);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, fboResolve_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::buildGrid(int n) {
    if (n == gridN_) return;
    gridN_ = n;
    std::vector<float> uv;
    uv.reserve(size_t(n) * n * 2);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            uv.push_back(float(x) / float(n - 1));
            uv.push_back(float(y) / float(n - 1));
        }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(n - 1) * (n - 1) * 6);
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            uint32_t a = uint32_t(y * n + x), b = a + 1, c = a + uint32_t(n), d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    if (!gridVao_) {
        glGenVertexArrays(1, &gridVao_);
        glGenBuffers(1, &gridVbo_);
        glGenBuffers(1, &gridIbo_);
    }
    glBindVertexArray(gridVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(uv.size() * 4), uv.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    gridIndexCount_ = int(idx.size());
    glBindVertexArray(0);
}

void Renderer::uploadTerrain(const Scene& s) {
    const Terrain& t = s.terrain;
    texRes_ = t.res();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texHeight_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texRes_, texRes_, 0, GL_RED, GL_FLOAT, t.height.data());
    glBindTexture(GL_TEXTURE_2D, texWater_);
    if (!t.water.empty()) {
        // dry cells sit just below the ground so filtering between wet and dry texels gives clean shorelines
        std::vector<float> w(t.water.vec());
        const float below = 0.4f / std::max(1.f, t.heightRange);
        for (size_t i = 0; i < w.size(); ++i)
            if (w[i] < t.height.vec()[i]) w[i] = t.height.vec()[i] - below;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texRes_, texRes_, 0, GL_RED, GL_FLOAT, w.data());
    }
}

void Renderer::updateHeightRegion(const Scene& s, int x0, int y0, int x1, int y1) {
    const Terrain& t = s.terrain;
    if (t.res() != texRes_ || x1 < x0 || y1 < y0) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, texRes_);
    glBindTexture(GL_TEXTURE_2D, texHeight_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0 + 1, y1 - y0 + 1, GL_RED, GL_FLOAT, t.height.data() + size_t(y0) * texRes_ + x0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void Renderer::uploadSplat(const Scene& s) {
    const SplatMap& sp = s.splat;
    if (sp.res == 0) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, texSplat0_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sp.res, sp.res, 0, GL_RGBA, GL_UNSIGNED_BYTE, sp.weights0.data());
    glBindTexture(GL_TEXTURE_2D, texSplat1_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sp.res, sp.res, 0, GL_RGBA, GL_UNSIGNED_BYTE, sp.weights1.data());
}

void Renderer::uploadFoliage(const Scene& s) {
    std::vector<std::vector<float>> per(static_cast<size_t>(kFoliageCount));
    for (const auto& f : s.foliage) {
        if (f.kind >= kFoliageCount) continue;
        auto& v = per[f.kind];
        v.insert(v.end(), {f.x, f.y, f.z, f.scale, f.rotation});
    }
    for (int k = 0; k < kFoliageCount; ++k) {
        MeshGL& m = foliageMeshes_[size_t(k)];
        glBindBuffer(GL_ARRAY_BUFFER, m.inst);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(per[size_t(k)].size() * 4), per[size_t(k)].empty() ? nullptr : per[size_t(k)].data(), GL_DYNAMIC_DRAW);
        m.instanceCount = int(per[size_t(k)].size() / 5);
    }
}

void Renderer::uploadSplines(const Scene& sc) {
    const Terrain& t = sc.terrain;
    std::vector<float> v;
    auto push = [&](Vec3 p, float lat, float along, float kind, float layer, float width, float curv, float mark, float kerb, float side, Vec3 n) {
        v.insert(v.end(), {p.x, p.y, p.z, lat, along, kind, layer, width, curv, mark, kerb, side, 0.f, n.x, n.y, n.z});
    };
    for (const auto& s : sc.splines) {
        auto smp = s.sample(std::max(1.5f, t.cellSize() * 0.75f));
        if (smp.size() < 2) continue;
        if (s.closed) smp.push_back(smp.front());
        const float kind = float(int(s.kind));
        const float layer = float(networkLayerOf(netKinds_, netCount_, s.material));
        const float lift = s.kind == SplineKind::Path ? 0.08f : 0.18f;
        std::vector<Vec3> L(smp.size()), R(smp.size());
        for (size_t i = 0; i < smp.size(); ++i) {
            const SplineSample& p = smp[i];
            Vec2 side = p.dir.perp();
            float half = p.width * 0.5f;
            Vec3 c = p.pos;
            Vec3 off(side.x * half, 0, side.y * half);
            float bankDrop = std::tan(p.bank) * half;
            L[i] = c - off + Vec3(0, bankDrop, 0);
            R[i] = c + off - Vec3(0, bankDrop, 0);
            if (!s.elevated) {
                L[i].y = std::max(L[i].y, t.heightAtWorld(L[i].x, L[i].z)) + lift;
                R[i].y = std::max(R[i].y, t.heightAtWorld(R[i].x, R[i].z)) + lift;
                if (s.waterFilled) { L[i].y -= 1.2f; R[i].y -= 1.2f; }
            }
        }
        for (size_t i = 0; i + 1 < smp.size(); ++i) {
            float d0 = smp[i].distance, d1 = i + 1 == smp.size() - 1 && s.closed ? smp[i].distance + (Vec2(smp[i + 1].pos.x, smp[i + 1].pos.z) - Vec2(smp[i].pos.x, smp[i].pos.z)).length() : smp[i + 1].distance;
            float w0 = smp[i].width, w1 = smp[i + 1].width, c0 = smp[i].curvature, c1 = smp[i + 1].curvature;
            Vec3 n = cross(R[i] - L[i], L[i + 1] - L[i]).normalized();
            if (n.y < 0) n = -n;
            float mk = s.markings ? 1.f : 0.f, kb = s.kerbs ? 1.f : 0.f;
            push(L[i], -w0 * 0.5f, d0, kind, layer, w0, c0, mk, kb, 0, n);
            push(R[i], w0 * 0.5f, d0, kind, layer, w0, c0, mk, kb, 0, n);
            push(L[i + 1], -w1 * 0.5f, d1, kind, layer, w1, c1, mk, kb, 0, n);
            push(R[i], w0 * 0.5f, d0, kind, layer, w0, c0, mk, kb, 0, n);
            push(R[i + 1], w1 * 0.5f, d1, kind, layer, w1, c1, mk, kb, 0, n);
            push(L[i + 1], -w1 * 0.5f, d1, kind, layer, w1, c1, mk, kb, 0, n);
            if (s.elevated) {
                // deck sides so aqueducts / maglev beams read as solid structures
                for (int sideI = 0; sideI < 2; ++sideI) {
                    Vec3 a = sideI ? R[i] : L[i], b = sideI ? R[i + 1] : L[i + 1];
                    Vec3 dn(0, -1.6f, 0);
                    Vec3 sn = (sideI ? (R[i] - L[i]) : (L[i] - R[i])).normalized();
                    push(a, 0, d0, kind, layer, w0, 0, 0, 0, 1, sn);
                    push(b, 0, d1, kind, layer, w1, 0, 0, 0, 1, sn);
                    push(a + dn, 0, d0, kind, layer, w0, 0, 0, 0, 1, sn);
                    push(b, 0, d1, kind, layer, w1, 0, 0, 0, 1, sn);
                    push(b + dn, 0, d1, kind, layer, w1, 0, 0, 0, 1, sn);
                    push(a + dn, 0, d0, kind, layer, w0, 0, 0, 0, 1, sn);
                }
            }
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, ribbonVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * 4), v.empty() ? nullptr : v.data(), GL_DYNAMIC_DRAW);
    ribbonVerts_ = int(v.size() / 16);
}

void Renderer::uploadMaterialArray(unsigned& albTex, unsigned& nrmTex, const MaterialBatch& b) {
    if (b.pixels.empty()) return;
    const int n = int(b.pixels.size()), sz = b.size;
    for (int pass = 0; pass < 2; ++pass) {
        unsigned& tex = pass == 0 ? albTex : nrmTex;
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, sz, sz, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        for (int i = 0; i < n; ++i) {
            const auto& px = pass == 0 ? b.pixels[size_t(i)].albedoHeight : b.pixels[size_t(i)].normalRough;
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, sz, sz, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        }
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (GLEW_EXT_texture_filter_anisotropic) {
            float maxAniso = 1.f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.f, maxAniso));
        }
    }
}

void Renderer::requestMaterials(const TextureLibrary& lib, const Scene& scene, int texSize) {
    if (pending_.valid()) pending_.wait();
    std::vector<MaterialKind> terrainKinds;
    for (int i = 0; i < scene.splat.layerCount; ++i) terrainKinds.push_back(scene.splat.layers[size_t(i)]);
    if (terrainKinds.empty()) terrainKinds.push_back(MaterialKind::Grass);
    std::vector<MaterialKind> netKinds;
    for (const auto& s : scene.splines)
        if (std::find(netKinds.begin(), netKinds.end(), s.material) == netKinds.end() && netKinds.size() < 8) netKinds.push_back(s.material);
    for (MaterialKind k : {MaterialKind::Asphalt, MaterialKind::PackedDirt, MaterialKind::RailBed, MaterialKind::Cobblestone})
        if (std::find(netKinds.begin(), netKinds.end(), k) == netKinds.end() && netKinds.size() < 8) netKinds.push_back(k);
    TextureLibrary libCopy = lib;
    materialStatus_ = "loading materials...";
    pending_ = std::async(std::launch::async, [libCopy, terrainKinds, netKinds, texSize]() {
        MaterialBatch a, b;
        a.size = b.size = texSize;
        b.network = true;
        a.kinds = terrainKinds;
        b.kinds = netKinds;
        a.pixels.resize(terrainKinds.size());
        b.pixels.resize(netKinds.size());
        parallelFor(0, int(terrainKinds.size()), [&](int i) { a.pixels[size_t(i)] = libCopy.load(terrainKinds[size_t(i)], texSize); });
        parallelFor(0, int(netKinds.size()), [&](int i) { b.pixels[size_t(i)] = libCopy.load(netKinds[size_t(i)], texSize / 2); });
        b.size = texSize / 2;
        return std::make_pair(std::move(a), std::move(b));
    });
}

void Renderer::sync(const Scene& s, const RenderSettings& rs) {
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto batches = pending_.get();
        uploadMaterialArray(texAlb_, texNrm_, batches.first);
        uploadMaterialArray(texNetAlb_, texNetNrm_, batches.second);
        layerKinds_ = batches.first.kinds;
        int lib = 0;
        for (size_t i = 0; i < batches.first.kinds.size() && i < 8; ++i) {
            tile_[i] = materialInfo(batches.first.kinds[i]).tileMeters;
            emissive_[i] = (batches.first.kinds[i] == MaterialKind::Lava || batches.first.kinds[i] == MaterialKind::Crystal) ? 1.f : 0.f;
            lib += batches.first.pixels[i].source != "procedural" ? 1 : 0;
        }
        netCount_ = 0;
        for (size_t i = 0; i < batches.second.kinds.size() && i < 8; ++i) netKinds_[i] = batches.second.kinds[i], ++netCount_;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d/%zu terrain layers from texture library, %d px", lib, batches.first.kinds.size(), batches.first.size);
        materialStatus_ = buf;
        splineVersion_ = -1;   // network layer indices may have changed
    }
    if (!s.valid()) return;
    buildGrid(std::clamp(std::min(s.terrain.res(), rs.meshRes), 65, 2049));
    if (s.heightVersion != heightVersion_) { uploadTerrain(s); heightVersion_ = s.heightVersion; }
    if (s.splatVersion != splatVersion_) { uploadSplat(s); splatVersion_ = s.splatVersion; }
    if (s.foliageVersion != foliageVersion_) { uploadFoliage(s); foliageVersion_ = s.foliageVersion; }
    if (s.splineVersion != splineVersion_) { uploadSplines(s); splineVersion_ = s.splineVersion; }
}

unsigned Renderer::uploadRGBA(unsigned tex, int w, int h, const uint8_t* data) {
    if (!tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return tex;
}

// ------------------------------------------------------------------ frame
void Renderer::render(int width, int height, const Camera& cam, const Scene& scene, const RenderSettings& rs, const ViewportOverlay& ov, float time) {
    if (width < 8 || height < 8) return;
    ensureTargets(width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, fboMs_);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Terrain& t = scene.terrain;
    const float world = scene.valid() ? t.worldSize : 1000.f;
    const float hr = scene.valid() ? t.heightRange * rs.exaggeration : 100.f;
    const float aspect = float(width) / float(height);
    Mat4 proj = Mat4::perspective(cam.fov, aspect, std::max(0.5f, world * 0.0002f), world * 6.f);
    Mat4 view = cam.view();
    // vertical exaggeration is applied as a world Y scale
    Mat4 ex;
    ex.m[5] = rs.exaggeration;
    vp_ = proj * view * ex;
    Vec3 sunDir(std::cos(rs.sunElevation) * std::sin(rs.sunAzimuth), std::sin(rs.sunElevation), std::cos(rs.sunElevation) * std::cos(rs.sunAzimuth));
    const Atmosphere& at = scene.atmosphere;
    Vec3 camScaled = cam.pos;
    camScaled.y /= std::max(0.01f, rs.exaggeration);
    auto common = [&](unsigned p) {
        glUseProgram(p);
        glUniformMatrix4fv(U(p, "uVP"), 1, GL_FALSE, vp_.m);
        set3(p, "uSunDir", sunDir);
        set3(p, "uSunCol", at.sun);
        set3(p, "uSkyTop", at.skyTop);
        set3(p, "uSkyHor", at.skyHorizon);
        set3(p, "uCam", camScaled);
        glUniform1f(U(p, "uFog"), at.fogDensity * 4033.f / std::max(500.f, world));
        glUniform1f(U(p, "uTime"), time);
    };

    // sky
    glDisable(GL_DEPTH_TEST);
    common(progSky_);
    Vec3 f = cam.forward(), r = cam.right(), u = cross(r, f);
    set3(progSky_, "uFwd", f);
    set3(progSky_, "uRight", r);
    set3(progSky_, "uUp", u);
    glUniform1f(U(progSky_, "uTanHalf"), std::tan(cam.fov * 0.5f));
    glUniform1f(U(progSky_, "uAspect"), aspect);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (scene.valid() && gridIndexCount_ > 0) {
        const float texel = 1.f / float(std::max(1, texRes_));
        // terrain
        common(progTerrain_);
        glUniform1f(U(progTerrain_, "uWorld"), world);
        glUniform1f(U(progTerrain_, "uHR"), t.heightRange);
        glUniform2f(U(progTerrain_, "uTexel"), texel, texel);
        glUniform1i(U(progTerrain_, "uLayers"), std::min(scene.splat.layerCount, int(layerKinds_.size())));
        glUniform1fv(U(progTerrain_, "uTile"), 8, tile_.data());
        glUniform1fv(U(progTerrain_, "uEmissive"), 8, emissive_.data());
        glUniform1i(U(progTerrain_, "uAntiTile"), rs.antiTiling ? 1 : 0);
        glUniform1i(U(progTerrain_, "uTriplanar"), rs.triplanar ? 1 : 0);
        glUniform1i(U(progTerrain_, "uShadows"), rs.shadows ? 1 : 0);
        glUniform1i(U(progTerrain_, "uMacro"), rs.macroVariation ? 1 : 0);
        glUniform1i(U(progTerrain_, "uGrid"), rs.showGrid ? 1 : 0);
        const auto& era = eras()[size_t(std::clamp(scene.settings.eraIndex, 0, int(eras().size()) - 1))];
        glUniform1f(U(progTerrain_, "uScorch"), era.scorch);
        glUniform4f(U(progTerrain_, "uBrush"), ov.brushCenter.x, ov.brushCenter.y, ov.brushRadius, ov.brush ? 1.f : 0.f);
        set3(progTerrain_, "uBrushColor", ov.brushColor);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHeight_); glUniform1i(U(progTerrain_, "uHeight"), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texSplat0_); glUniform1i(U(progTerrain_, "uSplat0"), 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texSplat1_); glUniform1i(U(progTerrain_, "uSplat1"), 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb_); glUniform1i(U(progTerrain_, "uAlb"), 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm_); glUniform1i(U(progTerrain_, "uNrm"), 4);
        if (rs.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glBindVertexArray(gridVao_);
        glDrawElements(GL_TRIANGLES, gridIndexCount_, GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // spline ribbons
        if (rs.splines && ribbonVerts_ > 0) {
            common(progRibbon_);
            std::array<float, 8> netTile{};
            for (int i = 0; i < 8; ++i) netTile[size_t(i)] = i < netCount_ ? materialInfo(netKinds_[size_t(i)]).tileMeters : 4.f;
            glUniform1fv(U(progRibbon_, "uNetTile"), 8, netTile.data());
            set3(progRibbon_, "uWaterCol", at.water);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texNetAlb_); glUniform1i(U(progRibbon_, "uNetAlb"), 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY, texNetNrm_); glUniform1i(U(progRibbon_, "uNetNrm"), 1);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.f, -2.f);
            glBindVertexArray(ribbonVao_);
            glDrawArrays(GL_TRIANGLES, 0, ribbonVerts_);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_BLEND);
        }

        // foliage
        if (rs.foliage) {
            common(progFoliage_);
            glDisable(GL_CULL_FACE);
            for (int k = 0; k < kFoliageCount; ++k) {
                const MeshGL& m = foliageMeshes_[size_t(k)];
                if (m.instanceCount == 0) continue;
                const FoliageInfo& fi = foliageInfo(FoliageKind(k));
                float maxD = rs.foliageDistance * clampf(fi.baseHeight / 8.f, 0.18f, 2.5f);
                if (fi.isProp) maxD = std::max(maxD, rs.foliageDistance);
                glUniform1f(U(progFoliage_, "uMaxDist"), maxD);
                glBindVertexArray(m.vao);
                glDrawElementsInstanced(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, nullptr, m.instanceCount);
            }
        }

        // water
        if (rs.water && !t.water.empty()) {
            common(progWater_);
            glUniform1f(U(progWater_, "uWorld"), world);
            glUniform1f(U(progWater_, "uHR"), t.heightRange);
            glUniform2f(U(progWater_, "uTexel"), texel, texel);
            set3(progWater_, "uWaterCol", at.water);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHeight_); glUniform1i(U(progWater_, "uHeight"), 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texWater_); glUniform1i(U(progWater_, "uWater"), 1);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glBindVertexArray(gridVao_);
            glDrawElements(GL_TRIANGLES, gridIndexCount_, GL_UNSIGNED_INT, nullptr);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // gizmos: spline centre line + control points
        if (!ov.points.empty() || !ov.polyline.empty()) {
            std::vector<float> g;
            for (const auto& p : ov.polyline) g.insert(g.end(), {p.x, p.y + 0.6f, p.z, 1.f, 0.85f, 0.1f});
            int lineCount = int(ov.polyline.size());
            for (size_t i = 0; i < ov.points.size(); ++i) {
                bool sel = int(i) == ov.selectedPoint;
                g.insert(g.end(), {ov.points[i].x, ov.points[i].y + 1.f, ov.points[i].z, sel ? 1.f : 0.2f, sel ? 0.3f : 0.8f, sel ? 0.2f : 1.f});
            }
            common(progGizmo_);
            glBindVertexArray(gizmoVao_);
            glBindBuffer(GL_ARRAY_BUFFER, gizmoVbo_);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(g.size() * 4), g.data(), GL_STREAM_DRAW);
            glDisable(GL_DEPTH_TEST);
            if (lineCount > 1) glDrawArrays(GL_LINE_STRIP, 0, lineCount);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUniform1f(U(progGizmo_, "uPointSize"), 11.f);
            glDrawArrays(GL_POINTS, lineCount, GLsizei(ov.points.size()));
            glDisable(GL_PROGRAM_POINT_SIZE);
            glEnable(GL_DEPTH_TEST);
        }
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboMs_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboResolve_);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}  // namespace zl
