#include "procgen/algorithms/LSystem.h"
#include "procgen/algorithms/LSystemMesh.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/ParamSchema.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace eve::procgen {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3    norm(V3 a) {
    const float n = std::sqrt(std::max(1e-12f, dot(a, a)));
    return mul(a, 1.f / n);
}

void basisFor(V3 axis, V3& right, V3& forward) {
    axis            = norm(axis);
    const V3 helper = std::fabs(axis.y) < 0.92f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
    right           = norm(cross(helper, axis));
    forward         = norm(cross(axis, right));
}

void addTaperedCylinder(MeshBuild& out, V3 a, V3 b, float r0, float r1, int sides) {
    const V3 axis = norm(sub(b, a));
    V3       right, forward;
    basisFor(axis, right, forward);
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int ring = 0; ring < 2; ++ring) {
        const V3    center = ring ? b : a;
        const float radius = ring ? r1 : r0;
        for (int i = 0; i < sides; ++i) {
            const float t     = float(i) / float(sides);
            const float angle = t * 2.f * kPi;
            const V3    radial = add(mul(right, std::cos(angle)), mul(forward, std::sin(angle)));
            const V3    p      = add(center, mul(radial, radius));
            // Bark occupies the left side of the texture atlas; foliage the right.
            out.addVertex(p.x, p.y, p.z, radial.x, radial.y, radial.z, t * 0.45f, float(ring));
        }
    }
    for (int i = 0; i < sides; ++i) {
        const uint32_t n  = uint32_t((i + 1) % sides);
        const uint32_t i0 = base + uint32_t(i), i1 = base + n;
        const uint32_t i2 = base + uint32_t(sides) + uint32_t(i);
        const uint32_t i3 = base + uint32_t(sides) + n;
        out.addTriangle(i0, i2, i1);
        out.addTriangle(i1, i2, i3);
    }
}

void addLeafCard(MeshBuild& out, V3 c, V3 direction, float size, float twist) {
    V3 right, up;
    basisFor(norm(direction), right, up);
    right = add(mul(right, std::cos(twist)), mul(up, std::sin(twist)));
    up    = norm(cross(norm(direction), right));
    const V3       r = mul(right, size * 0.5f), u = mul(up, size);
    const V3       normal    = norm(cross(right, up));
    const uint32_t base      = uint32_t(out.getVertexCount());
    const V3       points[4] = {sub(sub(c, r), u), add(sub(c, u), r), add(add(c, r), u), add(sub(c, r), u)};
    const float    uv[4][2]  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i)
        out.addVertex(points[i].x, points[i].y, points[i].z, normal.x, normal.y, normal.z, 0.55f + uv[i][0] * 0.45f,
                      uv[i][1]);
    out.addTriangle(base, base + 1, base + 2);
    out.addTriangle(base, base + 2, base + 3);
    out.addTriangle(base + 2, base + 1, base);
    out.addTriangle(base + 3, base + 2, base);
}

void configurePreset(LSystem& ls, const std::string& style) {
    ls.setLeafSymbols("L");
    if (style == "fern") {
        ls.setAxiom("X");
        ls.clearRules();
        ls.addRule('X', "F-[[X]+X]+F[+FX]-X");
        ls.addRule('F', "FF");
        ls.setAngle(25.f);
        ls.setInitialHeading(0.f, 1.f, 0.f);
        ls.setBranchRadius(0.05f);
        ls.setBranchRadiusFalloff(0.8f);
    } else if (style == "plant") {
        ls.setAxiom("F");
        ls.clearRules();
        ls.addRule('F', "F[+F]F[-F]F");
        ls.setAngle(26.f);
        ls.setInitialHeading(0.f, 1.f, 0.f);
        ls.setBranchRadius(0.09f);
        ls.setBranchRadiusFalloff(0.72f);
    } else if (style == "weed") {
        ls.setAxiom("X");
        ls.clearRules();
        ls.addRule('X', "F[-X][+X]FX");
        ls.addRule('X', "F[-X]FX");
        ls.addRule('X', "F[&X]F^X");
        ls.setAngle(28.f);
        ls.setInitialHeading(0.f, 1.f, 0.f);
        ls.setBranchRadius(0.06f);
        ls.setBranchRadiusFalloff(0.7f);
    } else {  // tree (default)
        ls.setAxiom("A");
        ls.clearRules();
        ls.addRules('A', {"F[&L A]F[&L A]^F[&L A]F L A", "F[&L A]F L A", "F L A"},
                    {2.f, 1.f, 1.f});
        ls.setAngle(22.f);
        ls.setInitialHeading(0.f, 1.f, 0.f);
        ls.setBranchRadius(0.16f);
        ls.setBranchRadiusFalloff(0.62f);
    }
}

}  // namespace

bool generateLSystemMesh(const Params& params, MeshBuild& out, std::string& error) {
    const std::string style = params.getString("style", "tree");
    if (style != "tree" && style != "fern" && style != "plant" && style != "weed") {
        error = "mesh.lsystem: unknown style '" + style + "'";
        return false;
    }
    const std::string leafMode = params.getString("leafMode", "cards");
    if (leafMode != "cards" && leafMode != "none") {
        error = "mesh.lsystem: unknown leafMode '" + leafMode + "'";
        return false;
    }

    LSystem ls;
    configurePreset(ls, style);
    ls.setSeed(params.getSeed());
    ls.setIterations(params.getInt("iterations", 5));
    ls.setAngle(params.getFloat("angle", 22.f));
    ls.setBranchRadius(params.getFloat("branchRadius", 0.16f));
    ls.setBranchRadiusFalloff(params.getFloat("radiusFalloff", 0.62f));
    ls.setLeafSize(params.getFloat("leafSize", 0.4f));
    ls.setTropism(0.f, params.getFloat("tropism", 0.f), 0.f);
    const int    branchSegments = std::clamp(params.getInt("branchSegments", 6), 3, 24);
    const int    leavesPerMarker = std::max(1, params.getInt("leavesPerMarker", 1));

    LSystemResult result;
    ls.generate(result);
    if (result.segments.empty()) {
        error = "mesh.lsystem: generated no geometry";
        return false;
    }

    out.reserve(int(result.segments.size() * 12), int(result.segments.size() * 24));
    for (const LSystemSegment& seg : result.segments) {
        if (seg.leaf) {
            if (leafMode == "none") continue;
            const V3 c   = {seg.ex, seg.ey, seg.ez};
            const V3 dir = {seg.dx, seg.dy, seg.dz};
            for (int i = 0; i < leavesPerMarker; ++i) {
                const float twist = float(i) * 2.39996323f;  // golden angle
                addLeafCard(out, c, dir, seg.leafSize, twist);
            }
            continue;
        }
        const V3 a = {seg.sx, seg.sy, seg.sz};
        const V3 b = {seg.ex, seg.ey, seg.ez};
        addTaperedCylinder(out, a, b, std::max(1e-4f, seg.r0), std::max(1e-4f, seg.r1), branchSegments);
    }

    out.setMeta("recipe", "mesh.lsystem");
    out.setMeta("style", style);
    out.setMeta("leafMode", leafMode);
    out.setMeta("seed", std::to_string(params.getSeed()));
    if (out.empty()) {
        error = "mesh.lsystem: generated an empty mesh";
        return false;
    }
    return true;
}

void registerLSystemRecipes(MeshRecipeRegistry& registry) {
    RecipeDescriptor schema{std::string("mesh.lsystem"), "L-System Plant", "Mesh", {}};
    schema.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    schema.params.push_back(
        ParamDescriptor::choice("style", "Style", "tree", {"tree", "fern", "plant", "weed"}));
    schema.params.push_back(ParamDescriptor::choice("leafMode", "Leaf Mode", "cards", {"cards", "none"}));
    schema.params.push_back(ParamDescriptor::integer("iterations", "Iterations", 5, 1, 8));
    schema.params.push_back(ParamDescriptor::floating("angle", "Turn Angle", 22.f, 5.f, 90.f, 1.f));
    schema.params.push_back(ParamDescriptor::floating("branchRadius", "Branch Radius", 0.16f, 0.005f, 5.f, 0.005f));
    schema.params.push_back(ParamDescriptor::floating("radiusFalloff", "Radius Falloff", 0.62f, 0.05f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("leafSize", "Leaf Size", 0.4f, 0.02f, 10.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("tropism", "Tropism", 0.f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("branchSegments", "Branch Segments", 6, 3, 24));
    schema.params.push_back(ParamDescriptor::integer("leavesPerMarker", "Leaves Per Marker", 1, 1, 8));
    registry.registerRecipe(std::move(schema), generateLSystemMesh);
}

}  // namespace eve::procgen