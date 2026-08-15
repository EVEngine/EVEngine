#include "procgen/algorithms/SkyscraperMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 addScaled(Vec3 p, Vec3 n, float s) { return {p.x + n.x * s, p.y + n.y * s, p.z + n.z * s}; }

Vec3 normalize(Vec3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-8f) return {0.f, 1.f, 0.f};
    return {v.x / len, v.y / len, v.z / len};
}

// Bilinear interpolation of a rectangular face from its four corners.
// p0 bottom-left, p1 bottom-right, p2 top-right, p3 top-left.
Vec3 lerpFace(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float u, float v) {
    const float w = u;
    Vec3 base = {p0.x + (p1.x - p0.x) * w, p0.y + (p1.y - p0.y) * w, p0.z + (p1.z - p0.z) * w};
    Vec3 top  = {p3.x + (p2.x - p3.x) * w, p3.y + (p2.y - p3.y) * w, p3.z + (p2.z - p3.z) * w};
    return {base.x + (top.x - base.x) * v, base.y + (top.y - base.y) * v,
            base.z + (top.z - base.z) * v};
}

// Emit a quad given four corners and its desired outward normal. Winding is corrected so the
// geometric normal (cross of the first triangle) agrees with n.
void addQuad(MeshBuild &out, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n, float u, float v) {
    const Vec3 winding = cross(sub(p1, p0), sub(p3, p0));
    const float d = winding.x * n.x + winding.y * n.y + winding.z * n.z;
    if (d < 0.f) {
        const Vec3 tmp = p1;
        p1 = p3;
        p3 = tmp;
    }
    const uint32_t base = uint32_t(out.getVertexCount());
    out.addVertex(p0.x, p0.y, p0.z, n.x, n.y, n.z, u, v);
    out.addVertex(p1.x, p1.y, p1.z, n.x, n.y, n.z, u, v);
    out.addVertex(p2.x, p2.y, p2.z, n.x, n.y, n.z, u, v);
    out.addVertex(p3.x, p3.y, p3.z, n.x, n.y, n.z, u, v);
    out.addTriangle(base, base + 1, base + 2);
    out.addTriangle(base, base + 2, base + 3);
}

// Add one facade (a rectangular wall) plus a grid of raised window quads.
// Face corners are in CCW order from outside; n is the outward normal.
void addFacade(MeshBuild &out, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n, int cols, int rows,
               float windowDepth) {
    addQuad(out, p0, p1, p2, p3, n, 0.25f, 0.5f);
    const float margin = 0.12f;
    for (int r = 0; r < rows; ++r) {
        const float v0 = float(r) / float(rows);
        const float v1 = float(r + 1) / float(rows);
        const float vi0 = v0 + margin * (v1 - v0);
        const float vi1 = v1 - margin * (v1 - v0);
        for (int c = 0; c < cols; ++c) {
            const float u0 = float(c) / float(cols);
            const float u1 = float(c + 1) / float(cols);
            const float ui0 = u0 + margin * (u1 - u0);
            const float ui1 = u1 - margin * (u1 - u0);
            const Vec3 w0 = addScaled(lerpFace(p0, p1, p2, p3, ui0, vi0), n, windowDepth);
            const Vec3 w1 = addScaled(lerpFace(p0, p1, p2, p3, ui1, vi0), n, windowDepth);
            const Vec3 w2 = addScaled(lerpFace(p0, p1, p2, p3, ui1, vi1), n, windowDepth);
            const Vec3 w3 = addScaled(lerpFace(p0, p1, p2, p3, ui0, vi1), n, windowDepth);
            addQuad(out, w0, w1, w2, w3, n, 0.75f, 0.5f);
        }
    }
}

// A solid box (without window grid) centered on cx/cz: four sides + optional top cap.
void addBox(MeshBuild &out, float cx, float cz, float hw, float hd, float y0, float y1,
            bool top) {
    const Vec3 p000 = {cx - hw, y0, cz - hd};
    const Vec3 p100 = {cx + hw, y0, cz - hd};
    const Vec3 p110 = {cx + hw, y0, cz + hd};
    const Vec3 p010 = {cx - hw, y0, cz + hd};
    const Vec3 p001 = {cx - hw, y1, cz - hd};
    const Vec3 p101 = {cx + hw, y1, cz - hd};
    const Vec3 p111 = {cx + hw, y1, cz + hd};
    const Vec3 p011 = {cx - hw, y1, cz + hd};
    addQuad(out, p000, p100, p101, p001, {0, 0, 1}, 0.25f, 0.5f);    // +Z
    addQuad(out, p110, p010, p011, p111, {0, 0, -1}, 0.25f, 0.5f);   // -Z
    addQuad(out, p100, p110, p111, p101, {1, 0, 0}, 0.25f, 0.5f);    // +X
    addQuad(out, p010, p000, p001, p011, {-1, 0, 0}, 0.25f, 0.5f);   // -X
    if (top) addQuad(out, p001, p101, p111, p011, {0, 1, 0}, 0.25f, 0.5f);
}

}  // namespace

bool generateSkyscraperMesh(const Params &params, MeshBuild &out, std::string &error) {
    const int tiers = std::clamp(params.getInt("tiers", 5), 1, 24);
    const float baseWidth = std::max(0.5f, params.getFloat("baseWidth", 10.f));
    const float baseDepth = std::max(0.5f, params.getFloat("baseDepth", 10.f));
    const float tierHeight = std::max(0.5f, params.getFloat("tierHeight", 6.f));
    const float setback = std::clamp(params.getFloat("setback", 0.08f), 0.f, 0.6f);
    const int windowCols = std::clamp(params.getInt("windowCols", 6), 1, 24);
    const int windowRows = std::clamp(params.getInt("windowRows", 4), 1, 24);
    const float windowDepth = std::max(0.f, params.getFloat("windowDepth", 0.04f));
    const float spireHeight = std::max(0.f, params.getFloat("spireHeight", 0.f));

    out.clear();
    const float cx = 0.f, cz = 0.f;
    float yCursor = 0.f;

    for (int t = 0; t < tiers; ++t) {
        const float shrink = 1.f - setback * float(t);
        const float hw = baseWidth * 0.5f * shrink;
        const float hd = baseDepth * 0.5f * shrink;
        const float y0 = yCursor;
        const float y1 = yCursor + tierHeight;

        const Vec3 p000 = {cx - hw, y0, cz - hd};
        const Vec3 p100 = {cx + hw, y0, cz - hd};
        const Vec3 p110 = {cx + hw, y0, cz + hd};
        const Vec3 p010 = {cx - hw, y0, cz + hd};
        const Vec3 p001 = {cx - hw, y1, cz - hd};
        const Vec3 p101 = {cx + hw, y1, cz - hd};
        const Vec3 p111 = {cx + hw, y1, cz + hd};
        const Vec3 p011 = {cx - hw, y1, cz + hd};

        addFacade(out, p000, p100, p101, p001, {0, 0, 1}, windowCols, windowRows, windowDepth);
        addFacade(out, p110, p010, p011, p111, {0, 0, -1}, windowCols, windowRows, windowDepth);
        addFacade(out, p100, p110, p111, p101, {1, 0, 0}, windowCols, windowRows, windowDepth);
        addFacade(out, p010, p000, p001, p011, {-1, 0, 0}, windowCols, windowRows, windowDepth);
        yCursor = y1;
    }

    // Roof cap on the top tier.
    {
        const float shrink = 1.f - setback * float(tiers - 1);
        const float hw = baseWidth * 0.5f * shrink;
        const float hd = baseDepth * 0.5f * shrink;
        const float y1 = yCursor;
        addQuad(out, {cx - hw, y1, cz - hd}, {cx + hw, y1, cz - hd}, {cx + hw, y1, cz + hd},
                {cx - hw, y1, cz + hd}, {0, 1, 0}, 0.25f, 0.5f);
    }

    if (spireHeight > 0.f) {
        const float s = std::min(baseWidth, baseDepth) * 0.045f;
        addBox(out, cx, cz, s, s, yCursor, yCursor + spireHeight, true);
    }

    if (out.empty()) {
        error = "mesh.skyscraper: generated an empty mesh";
        return false;
    }
    out.setMeta("algorithm", "mesh.skyscraper");
    out.setMeta("tiers", std::to_string(tiers));
    out.setMeta("windowCols", std::to_string(windowCols));
    out.setMeta("windowRows", std::to_string(windowRows));
    out.setMeta("spireHeight", std::to_string(spireHeight));
    out.setMeta("height", std::to_string(float(tiers) * tierHeight + spireHeight));
    return true;
}

}  // namespace eve::procgen
