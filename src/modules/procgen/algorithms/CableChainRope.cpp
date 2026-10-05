#include "procgen/algorithms/CableChainRope.h"

#include "procgen/ParamSchema.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-8f ? V3{a.x / l, a.y / l, a.z / l} : V3{0.f, 1.f, 0.f};
}

/** @brief Circular tube along a polyline; open ends (no caps) for seamless tiling. */
void emitOpenTube(MeshBuild& out, const std::vector<V3>& path, float radius, int radialSegs, float uvScale,
                  float uOffset) {
    if (path.size() < 2 || radius <= 0.f) return;
    radialSegs = std::clamp(radialSegs, 3, 48);
    const int rings = int(path.size());

    std::vector<V3> tangents(size_t(rings));
    for (int i = 0; i < rings; ++i) {
        if (i == 0)
            tangents[size_t(i)] = normalize(path[1] - path[0]);
        else if (i + 1 == rings)
            tangents[size_t(i)] = normalize(path[size_t(i)] - path[size_t(i - 1)]);
        else
            tangents[size_t(i)] = normalize(path[size_t(i + 1)] - path[size_t(i - 1)]);
    }

    // Parallel-transport frames to avoid sudden flips.
    std::vector<V3> normals(size_t(rings));
    std::vector<V3> binormals(size_t(rings));
    V3               n0 = std::fabs(tangents[0].y) < 0.9f ? V3{0, 1, 0} : V3{0, 0, 1};
    normals[0]          = normalize(n0 - tangents[0] * dot(n0, tangents[0]));
    binormals[0]        = cross(tangents[0], normals[0]);
    for (int i = 1; i < rings; ++i) {
        const V3 axis = cross(tangents[size_t(i - 1)], tangents[size_t(i)]);
        const float axisLen = std::sqrt(dot(axis, axis));
        if (axisLen < 1e-6f) {
            normals[size_t(i)]   = normals[size_t(i - 1)];
            binormals[size_t(i)] = binormals[size_t(i - 1)];
        } else {
            const V3    a   = axis * (1.f / axisLen);
            const float ang = std::atan2(axisLen, dot(tangents[size_t(i - 1)], tangents[size_t(i)]));
            const float ca  = std::cos(ang);
            const float sa  = std::sin(ang);
            auto rotate = [&](V3 v) {
                return v * ca + cross(a, v) * sa + a * (dot(a, v) * (1.f - ca));
            };
            normals[size_t(i)]   = normalize(rotate(normals[size_t(i - 1)]));
            binormals[size_t(i)] = cross(tangents[size_t(i)], normals[size_t(i)]);
        }
    }

    std::vector<float> arc(size_t(rings), 0.f);
    for (int i = 1; i < rings; ++i) {
        const V3  d = path[size_t(i)] - path[size_t(i - 1)];
        arc[size_t(i)] = arc[size_t(i - 1)] + std::sqrt(dot(d, d));
    }

    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j <= radialSegs; ++j) {
            const float a  = 2.f * kPi * float(j) / float(radialSegs);
            const float ca = std::cos(a);
            const float sa = std::sin(a);
            const V3    n  = normals[size_t(i)] * ca + binormals[size_t(i)] * sa;
            const V3    p  = path[size_t(i)] + n * radius;
            const float u  = (a * radius + uOffset) * uvScale;
            const float v  = arc[size_t(i)] * uvScale;
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, u, v);
        }
    }
    const int stride = radialSegs + 1;
    for (int i = 0; i + 1 < rings; ++i) {
        for (int j = 0; j < radialSegs; ++j) {
            const uint32_t a = base + uint32_t(i * stride + j);
            const uint32_t b = a + uint32_t(stride);
            out.addTriangle(a, b, a + 1);
            out.addTriangle(a + 1, b, b + 1);
        }
    }
}

/**
 * @brief Oval torus link. `vertical=false` → major ellipse in XZ; `true` → XY.
 * Elongation along X makes classic chain links that interlock when spaced by pitch.
 */
void emitOvalLink(MeshBuild& out, V3 center, float majorX, float majorY, float minorR, int majorSegs, int minorSegs,
                  bool vertical, float uvScale) {
    majorSegs = std::clamp(majorSegs, 8, 64);
    minorSegs = std::clamp(minorSegs, 4, 24);
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i <= majorSegs; ++i) {
        const float u  = float(i) / float(majorSegs);
        const float a  = 2.f * kPi * u;
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        // Tangent of the major ellipse and the plane normal of the link.
        V3 majorPos, majorT, planeN;
        if (vertical) {
            majorPos = {center.x + majorX * ca, center.y + majorY * sa, center.z};
            majorT   = normalize(V3{-majorX * sa, majorY * ca, 0.f});
            planeN   = {0.f, 0.f, 1.f};
        } else {
            majorPos = {center.x + majorX * ca, center.y, center.z + majorY * sa};
            majorT   = normalize(V3{-majorX * sa, 0.f, majorY * ca});
            planeN   = {0.f, 1.f, 0.f};
        }
        const V3 radial = normalize(cross(planeN, majorT));
        for (int j = 0; j <= minorSegs; ++j) {
            const float v  = float(j) / float(minorSegs);
            const float b  = 2.f * kPi * v;
            const float cb = std::cos(b);
            const float sb = std::sin(b);
            const V3    n  = radial * cb + planeN * sb;
            const V3    p  = majorPos + n * minorR;
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, a * ((majorX + majorY) * 0.5f) * uvScale,
                          b * minorR * uvScale);
        }
    }
    const int stride = minorSegs + 1;
    for (int i = 0; i < majorSegs; ++i) {
        for (int j = 0; j < minorSegs; ++j) {
            const uint32_t a = base + uint32_t(i * stride + j);
            const uint32_t b = a + uint32_t(stride);
            out.addTriangle(a, a + 1, b);
            out.addTriangle(a + 1, b + 1, b);
        }
    }
}

void buildTwistedStrands(MeshBuild& out, int segments, float L, float radius, float strandRadius, int strands,
                         int twists, int lengthSegs, int radialSegs, float uvScale, bool hempBulge) {
    strands     = std::clamp(strands, 2, 12);
    twists      = std::max(1, twists);
    lengthSegs  = std::clamp(lengthSegs, 4, 128);
    radialSegs  = std::clamp(radialSegs, 4, 24);
    const float coreR = std::max(0.f, radius - strandRadius * 1.15f);

    for (int s = 0; s < strands; ++s) {
        const float phase = 2.f * kPi * float(s) / float(strands);
        for (int seg = 0; seg < segments; ++seg) {
            const float x0 = float(seg) * L;
            std::vector<V3> path;
            path.reserve(size_t(lengthSegs + 1));
            for (int i = 0; i <= lengthSegs; ++i) {
                const float t = float(i) / float(lengthSegs);
                const float x = x0 + t * L;
                // Integer twists per segment ⇒ phase at x=L matches x=0 for tiling.
                const float a = phase + 2.f * kPi * float(twists) * t;
                float       r = coreR;
                if (hempBulge) {
                    // Mild axial bulge so strands read as soft fiber bundles.
                    r *= 0.92f + 0.08f * std::sin(a * 3.f + float(s));
                }
                path.push_back({x, r * std::cos(a), r * std::sin(a)});
            }
            float sr = strandRadius;
            if (hempBulge) sr *= 1.05f;
            emitOpenTube(out, path, sr, radialSegs, uvScale, phase * radius);
        }
    }
}

void buildChain(MeshBuild& out, int segments, float L, float radius, float thickness, int majorSegs, int minorSegs,
                float uvScale) {
    // One tileable unit = two interlocking links (horizontal + vertical).
    // Pitch between link centers is L/2; majorX sized so rings pass through each other.
    const float pitch  = L * 0.5f;
    const float majorX = std::max(thickness * 2.5f, pitch * 0.72f);
    const float majorY = std::max(thickness * 1.6f, radius);
    const float minorR = std::clamp(thickness, 0.005f, majorY * 0.45f);

    for (int seg = 0; seg < segments; ++seg) {
        const float baseX = float(seg) * L + pitch * 0.5f;
        // Horizontal link (major ellipse in XZ).
        emitOvalLink(out, {baseX, 0.f, 0.f}, majorX, majorY, minorR, majorSegs, minorSegs, false, uvScale);
        // Vertical link (major ellipse in XY), centered a half-pitch ahead.
        emitOvalLink(out, {baseX + pitch, 0.f, 0.f}, majorX, majorY, minorR, majorSegs, minorSegs, true, uvScale);
    }
}

RecipeDescriptor makeSchema(std::string id, std::string name, float radius, float thickness, int strands,
                            int twists) {
    RecipeDescriptor descriptor{std::move(id), std::move(name), "Cable", {}};
    descriptor.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    descriptor.params.push_back(ParamDescriptor::integer("segments", "Segments", 6, 1, 256));
    descriptor.params.push_back(ParamDescriptor::floating("segLength", "Segment Length", 1.f, 0.1f, 100.f, 0.05f));
    descriptor.params.push_back(ParamDescriptor::floating("radius", "Radius", radius, 0.01f, 10.f, 0.01f));
    descriptor.params.push_back(
        ParamDescriptor::floating("thickness", "Strand / Bar Thickness", thickness, 0.005f, 5.f, 0.005f));
    descriptor.params.push_back(ParamDescriptor::integer("strands", "Strand Count", strands, 2, 12));
    descriptor.params.push_back(ParamDescriptor::integer("twists", "Twists Per Segment", twists, 1, 16));
    descriptor.params.push_back(ParamDescriptor::integer("lengthSegs", "Length Segments", 16, 4, 128));
    descriptor.params.push_back(ParamDescriptor::integer("radialSegs", "Radial Segments", 8, 3, 48));
    descriptor.params.push_back(ParamDescriptor::integer("majorSegs", "Link Major Segments", 24, 8, 64));
    descriptor.params.push_back(ParamDescriptor::integer("minorSegs", "Link Minor Segments", 8, 4, 24));
    descriptor.params.push_back(ParamDescriptor::floating("uvRepeat", "UV Repeat", 2.f, 0.f, 100.f, 0.1f));
    descriptor.params.push_back(ParamDescriptor::floating("scale", "Scale", 1.f, 0.01f, 100.f, 0.01f));
    return descriptor;
}

}  // namespace

bool generateCableChainRope(const std::string& kind, const Params& params, MeshBuild& out, std::string& error) {
    const int   segments   = std::clamp(params.getInt("segments", 6), 1, 256);
    const float L          = std::max(0.1f, params.getFloat("segLength", 1.f));
    const float uvScale    = std::max(0.f, params.getFloat("uvRepeat", 2.f));
    const float scale      = std::max(0.01f, params.getFloat("scale", 1.f));
    const int   lengthSegs = std::clamp(params.getInt("lengthSegs", 16), 4, 128);
    const int   radialSegs = std::clamp(params.getInt("radialSegs", 8), 3, 48);
    const int   majorSegs  = std::clamp(params.getInt("majorSegs", 24), 8, 64);
    const int   minorSegs  = std::clamp(params.getInt("minorSegs", 8), 4, 24);

    out.clear();

    if (kind == "mesh.cable") {
        const float radius       = std::max(0.01f, params.getFloat("radius", 0.08f));
        const float thickness    = std::max(0.005f, params.getFloat("thickness", 0.022f));
        const int   strands      = std::clamp(params.getInt("strands", 6), 2, 12);
        const int   twists       = std::max(1, params.getInt("twists", 1));
        out.reserve(segments * strands * (lengthSegs + 1) * (radialSegs + 1),
                    segments * strands * lengthSegs * radialSegs * 6);
        buildTwistedStrands(out, segments, L, radius, thickness, strands, twists, lengthSegs, radialSegs, uvScale,
                            false);
    } else if (kind == "mesh.rope") {
        const float radius       = std::max(0.01f, params.getFloat("radius", 0.10f));
        const float thickness    = std::max(0.005f, params.getFloat("thickness", 0.045f));
        const int   strands      = std::clamp(params.getInt("strands", 3), 2, 12);
        const int   twists       = std::max(1, params.getInt("twists", 1));
        out.reserve(segments * strands * (lengthSegs + 1) * (radialSegs + 1),
                    segments * strands * lengthSegs * radialSegs * 6);
        buildTwistedStrands(out, segments, L, radius, thickness, strands, twists, lengthSegs, radialSegs, uvScale,
                            true);
    } else if (kind == "mesh.chain") {
        const float radius    = std::max(0.01f, params.getFloat("radius", 0.12f));
        const float thickness = std::max(0.005f, params.getFloat("thickness", 0.035f));
        out.reserve(segments * 2 * (majorSegs + 1) * (minorSegs + 1),
                    segments * 2 * majorSegs * minorSegs * 6);
        buildChain(out, segments, L, radius, thickness, majorSegs, minorSegs, uvScale);
    } else {
        error = "unknown cable/chain/rope kind '" + kind + "' (use mesh.cable|mesh.chain|mesh.rope)";
        return false;
    }

    if (scale != 1.f) {
        for (float& p : out.positions()) p *= scale;
    }
    if (out.empty()) {
        error = kind + ": empty mesh (check segments/radius/thickness)";
        return false;
    }
    out.setMeta("algorithm", kind);
    out.setMeta("segments", std::to_string(segments));
    return true;
}

void registerCableChainRopeRecipes(MeshRecipeRegistry& registry) {
    registry.registerRecipe(makeSchema("mesh.cable", "Steel Cable", 0.08f, 0.022f, 6, 1),
                            [](const Params& p, MeshBuild& o, std::string& e) {
                                return generateCableChainRope("mesh.cable", p, o, e);
                            });
    registry.registerRecipe(makeSchema("mesh.chain", "Iron Chain", 0.12f, 0.035f, 2, 1),
                            [](const Params& p, MeshBuild& o, std::string& e) {
                                return generateCableChainRope("mesh.chain", p, o, e);
                            });
    registry.registerRecipe(makeSchema("mesh.rope", "Hemp Rope", 0.10f, 0.045f, 3, 1),
                            [](const Params& p, MeshBuild& o, std::string& e) {
                                return generateCableChainRope("mesh.rope", p, o, e);
                            });
}

}  // namespace eve::procgen
