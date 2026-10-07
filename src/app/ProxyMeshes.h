// ZeraLands - low-poly procedural proxy meshes for every foliage / prop kind (instanced in the viewport).
#pragma once

#include "core/Types.h"

#include <vector>

namespace zl {

struct ProxyVertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
    float sway;   // 0 = rigid (trunk base) .. 1 = moves with wind (leaf tips)
};

struct ProxyMesh {
    std::vector<ProxyVertex> verts;
    std::vector<uint32_t> idx;
};

ProxyMesh buildProxyMesh(FoliageKind k);

}  // namespace zl
