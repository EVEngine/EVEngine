#include "procgen/algorithms/LinearStructure.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace eve::procgen {
namespace {

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-8f ? V3{a.x / l, a.y / l, a.z / l} : V3{0.f, 1.f, 0.f};
}

// Per-kind geometry defaults.
struct Dims {
    float height    = 1.f;
    float depth     = 1.f;
    float thickness = 0.1f;
};

// Face corner tables for an oriented box (local axes u,v,w). Corner indices
// reference local coords: 0=(-U,-V,-W) 1=(+U,-V,-W) 2=(+U,+V,-W) 3=(-U,+V,-W)
// 4=(-U,-V,+W) 5=(+U,-V,+W) 6=(+U,+V,+W) 7=(-U,+V,+W).
const int kFaceCorners[6][4] = {
    {1, 2, 6, 5},  // +U
    {4, 7, 3, 0},  // -U
    {3, 7, 6, 2},  // +V
    {0, 1, 5, 4},  // -V
    {4, 5, 6, 7},  // +W
    {1, 0, 3, 2},  // -W
};
const int kFaceNormalAxis[6] = {0, 0, 1, 1, 2, 2};

// UV axis priority so the long/horizontal axis maps to texture U: U(0) > W(2) > V(1).
int axisPriority(int axis) { return axis == 0 ? 2 : (axis == 2 ? 1 : 0); }

void emitQuad(MeshBuild &out, const V3 corner[4], const V3 &normal, const V3 &t1, const V3 &t2,
              float uvScale) {
    for (int i = 0; i < 4; ++i) {
        const V3 &c = corner[i];
        out.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, dot(c, t1) * uvScale,
                      dot(c, t2) * uvScale);
    }
    const uint32_t base = uint32_t(out.getVertexCount()) - 4u;
    out.addTriangle(base, base + 1, base + 2);
    out.addTriangle(base, base + 2, base + 3);
}

void emitOrientedBox(MeshBuild &out, const V3 &center, const V3 &u, const V3 &v, const V3 &w,
                     float ex, float ey, float ez, float uvScale) {
    const float lu[8] = {-ex, ex, ex, -ex, -ex, ex, ex, -ex};
    const float lv[8] = {-ey, -ey, ey, ey, -ey, -ey, ey, ey};
    const float lw[8] = {-ez, -ez, -ez, -ez, ez, ez, ez, ez};
    V3 world[8];
    for (int i = 0; i < 8; ++i) world[i] = center + u * lu[i] + v * lv[i] + w * lw[i];

    for (int f = 0; f < 6; ++f) {
        const int a  = kFaceNormalAxis[f];
        V3        n  = a == 0 ? u : (a == 1 ? v : w);
        if (f % 2 == 1) n = -n;
        // Two tangent axes are the two axes != normal axis.
        int others[2];
        int oi = 0;
        for (int ax = 0; ax < 3; ++ax)
            if (ax != a) others[oi++] = ax;
        V3 t1, t2;
        if (axisPriority(others[0]) >= axisPriority(others[1])) {
            t1 = others[0] == 0 ? u : (others[0] == 1 ? v : w);
            t2 = others[1] == 0 ? u : (others[1] == 1 ? v : w);
        } else {
            t1 = others[1] == 0 ? u : (others[1] == 1 ? v : w);
            t2 = others[0] == 0 ? u : (others[0] == 1 ? v : w);
        }
        V3 corner[4];
        for (int c = 0; c < 4; ++c) corner[c] = world[kFaceCorners[f][c]];
        emitQuad(out, corner, n, t1, t2, uvScale);
    }
}

// Axis-aligned box; `x0..x1` along X, `y0..y1` along Y, `z0..z1` along Z.
void addAABB(MeshBuild &out, float x0, float y0, float z0, float x1, float y1, float z1,
             float uvScale) {
    const V3 center{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f};
    emitOrientedBox(out, center, V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}, (x1 - x0) * 0.5f,
                    (y1 - y0) * 0.5f, (z1 - z0) * 0.5f, uvScale);
}

// Oriented square beam from a to b with half-thickness `radius`.
void addBeam(MeshBuild &out, V3 a, V3 b, float radius, float uvScale) {
    const V3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    if (len < 1e-6f) return;
    const V3 n = d * (1.f / len);
    V3 t = std::fabs(n.x) < 0.9f ? V3{0, 1, 0} : V3{0, 0, 1};
    const V3 v = normalize(t - n * dot(t, n));
    const V3 w = cross(n, v);
    emitOrientedBox(out, (a + b) * 0.5f, n, v, w, len * 0.5f, radius, radius, uvScale);
}

// --- Wooden fence: posts at each unit boundary + three horizontal rails. ---
void buildFence(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    const float postW = dm.thickness;
    const float railTh = dm.thickness * 0.55f;
    const float railD = dm.depth * 0.5f;
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, 0.f, -dm.depth * 0.5f, x0 + postW, dm.height, dm.depth * 0.5f, uvScale);
        for (int r = 0; r < 3; ++r) {
            const float y = dm.height * (0.22f + 0.28f * float(r));
            addAABB(out, x0 + postW, y, -railD, x0 + L, y + railTh, railD, uvScale);
        }
    }
}

// --- Stone wall: rubble body + wider top cap. ---
void buildStoneWall(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, 0.f, -dm.depth * 0.5f, x0 + L, dm.height, dm.depth * 0.5f, uvScale);
        addAABB(out, x0, dm.height, -dm.depth * 0.55f, x0 + L, dm.height + dm.thickness,
                dm.depth * 0.55f, uvScale);
    }
}

// --- Bridge: deck + handrails + cross beams. ---
void buildBridge(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    const float deckTh = dm.thickness;
    const float railInset = 0.18f;
    const float railTh = 0.08f;
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, 0.f, -dm.depth * 0.5f, x0 + L, deckTh, dm.depth * 0.5f, uvScale);
        for (int s = 0; s < 2; ++s) {
            const float z = (s == 0 ? -1.f : 1.f) * (dm.depth * 0.5f - railInset);
            addAABB(out, x0, deckTh, z - railTh * 0.5f, x0 + L, deckTh + dm.height,
                    z + railTh * 0.5f, uvScale);
        }
        const float beamTh = 0.10f;
        addAABB(out, x0 + L / 3.f, 0.f, -dm.depth * 0.5f, x0 + L / 3.f + beamTh, deckTh,
                dm.depth * 0.5f, uvScale);
        addAABB(out, x0 + 2.f * L / 3.f, 0.f, -dm.depth * 0.5f, x0 + 2.f * L / 3.f + beamTh,
                deckTh, dm.depth * 0.5f, uvScale);
    }
}

// --- Great Wall: body, walkway, outer merlons + inner guard rail. ---
void buildGreatWall(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    const int n = std::max(2, int(std::lround(L / 0.5f)));
    const float period = L / float(n);
    const float mw = period * 0.5f;
    const float merlonH = dm.thickness * 1.2f;
    const float mDepth = dm.depth * 0.45f;
    const float innerTh = 0.12f;
    const float innerH = 0.25f;
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, 0.f, -dm.depth * 0.5f, x0 + L, dm.height, dm.depth * 0.5f, uvScale);
        for (int i = 0; i < n; ++i) {
            const float mx = x0 + float(i) * period;
            addAABB(out, mx, dm.height, dm.depth * 0.5f - mDepth, mx + mw, dm.height + merlonH,
                    dm.depth * 0.5f, uvScale);
        }
        addAABB(out, x0, dm.height, -dm.depth * 0.5f, x0 + L, dm.height + innerH,
                -dm.depth * 0.5f + innerTh, uvScale);
    }
}

// --- Hedge: leafy base mass + overlapping rounded bush blobs. ---
void buildHedge(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    const int n = std::max(2, int(std::lround(L / 0.6f)));
    const float period = L / float(n);
    const float bw = period * 0.7f;
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, 0.f, -dm.depth * 0.5f, x0 + L, dm.height * 0.55f, dm.depth * 0.5f,
                uvScale);
        for (int i = 0; i < n; ++i) {
            const float cx = x0 + float(i) * period + period * 0.5f;
            addAABB(out, cx - bw * 0.5f, dm.height * 0.45f, -dm.depth * 0.52f,
                    cx + bw * 0.5f, dm.height * 0.95f, dm.depth * 0.52f, uvScale);
        }
    }
}

// --- Cheval de frise: top rail + crossed X legs + feet. ---
void buildChevalDeFrise(MeshBuild &out, int segments, float L, const Dims &dm, float uvScale) {
    const float railTh = dm.thickness * 1.4f;
    const float bw = L * 0.35f;
    const float dz = dm.depth * 0.5f;
    for (int k = 0; k < segments; ++k) {
        const float x0 = float(k) * L;
        addAABB(out, x0, dm.height - railTh, -dm.depth * 0.5f, x0 + L, dm.height,
                dm.depth * 0.5f, uvScale);
        const float cx = x0 + L * 0.5f;
        const float legRadius = dm.thickness * 0.5f;
        addBeam(out, V3{cx - bw, 0.f, dz}, V3{cx + bw, dm.height - railTh * 0.5f, -dz},
                legRadius, uvScale);
        addBeam(out, V3{cx - bw, 0.f, -dz}, V3{cx + bw, dm.height - railTh * 0.5f, dz},
                legRadius, uvScale);
        addAABB(out, cx - bw - 0.06f, 0.f, -dm.depth * 0.55f, cx - bw + 0.10f, 0.06f,
                dm.depth * 0.55f, uvScale);
        addAABB(out, cx + bw - 0.06f, 0.f, -dm.depth * 0.55f, cx + bw + 0.10f, 0.06f,
                dm.depth * 0.55f, uvScale);
    }
}

}  // namespace

bool generateLinearStructure(const std::string &kind, const Params &params, MeshBuild &out,
                             std::string &error) {
    const int segments = std::clamp(params.getInt("segments", 6), 1, 256);
    const float L = std::max(0.1f, params.getFloat("segLength", 1.f));
    const float uvScale = std::max(0.f, params.getFloat("uvRepeat", 2.f));

    Dims dm;
    if (kind == "mesh.fence") {
        dm = {1.1f, 0.09f, 0.08f};
    } else if (kind == "mesh.stonewall") {
        dm = {1.0f, 0.5f, 0.12f};
    } else if (kind == "mesh.bridge") {
        dm = {1.2f, 2.4f, 0.12f};
    } else if (kind == "mesh.greatwall") {
        dm = {1.6f, 1.0f, 0.18f};
    } else if (kind == "mesh.hedge") {
        dm = {0.9f, 0.8f, 0.06f};
    } else if (kind == "mesh.chevaldefrise") {
        dm = {0.9f, 0.7f, 0.08f};
    } else {
        error = "unknown linear structure '" + kind +
                "' (use mesh.fence|mesh.stonewall|mesh.bridge|mesh.greatwall|mesh.hedge|"
                "mesh.chevaldefrise)";
        return false;
    }
    dm.height = std::max(0.05f, params.getFloat("height", dm.height));
    dm.depth = std::max(0.02f, params.getFloat("depth", dm.depth));
    dm.thickness = std::max(0.005f, params.getFloat("thickness", dm.thickness));

    out.clear();
    out.reserve(segments * 400, segments * 1200);

    if (kind == "mesh.fence") {
        buildFence(out, segments, L, dm, uvScale);
    } else if (kind == "mesh.stonewall") {
        buildStoneWall(out, segments, L, dm, uvScale);
    } else if (kind == "mesh.bridge") {
        buildBridge(out, segments, L, dm, uvScale);
    } else if (kind == "mesh.greatwall") {
        buildGreatWall(out, segments, L, dm, uvScale);
    } else if (kind == "mesh.hedge") {
        buildHedge(out, segments, L, dm, uvScale);
    } else if (kind == "mesh.chevaldefrise") {
        buildChevalDeFrise(out, segments, L, dm, uvScale);
    }

    const float scale = std::max(0.01f, params.getFloat("scale", 1.f));
    if (scale != 1.f) {
        for (float &p : out.positions()) p *= scale;
    }

    if (out.empty()) {
        error = kind + ": empty mesh (check segments/segLength/scale)";
        return false;
    }
    out.setMeta("algorithm", kind);
    out.setMeta("segments", std::to_string(segments));
    return true;
}

void registerLinearStructureRecipes(MeshRecipeRegistry &registry) {
    registry.registerRecipe(
        "mesh.fence", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.fence", p, o, e);
        });
    registry.registerRecipe(
        "mesh.stonewall", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.stonewall", p, o, e);
        });
    registry.registerRecipe(
        "mesh.bridge", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.bridge", p, o, e);
        });
    registry.registerRecipe(
        "mesh.greatwall", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.greatwall", p, o, e);
        });
    registry.registerRecipe(
        "mesh.hedge", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.hedge", p, o, e);
        });
    registry.registerRecipe(
        "mesh.chevaldefrise", [](const Params &p, MeshBuild &o, std::string &e) {
            return generateLinearStructure("mesh.chevaldefrise", p, o, e);
        });
}

}  // namespace eve::procgen
