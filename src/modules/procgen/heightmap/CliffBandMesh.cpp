#include "procgen/heightmap/CliffBandMesh.h"

#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/texture/NoiseField.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

using namespace raster_detail;

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

float length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (!(len > 1e-8f)) return {0.f, 1.f, 0.f};
    return v * (1.f / len);
}

float sampleFitness(const Heightmap& fitness, int x0, int z0, int x1, int z1, int x2, int z2) {
    return (fitness.height(x0, z0) + fitness.height(x1, z1) + fitness.height(x2, z2)) * (1.f / 3.f);
}

float faceSlope(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 n = normalize(cross(b - a, c - a));
    // Prefer the upward-facing orientation so 1-n.y matches deriveTerrainSlope.
    const float ny = n.y >= 0.f ? n.y : -n.y;
    return 1.f - ny;
}

Vec3 displace(Vec3 p, Vec3 normal, const NoiseField& noise, const CliffBandMeshSettings& s) {
    Vec3 outward = normalize({normal.x, 0.f, normal.z});
    if (length({normal.x, 0.f, normal.z}) < 1e-4f) outward = {1.f, 0.f, 0.f};
    const Vec3 dir = normalize(normal * (1.f - s.outwardBias) + outward * s.outwardBias);
    float      y   = p.y;
    if (s.ledgeHeight > 0.f) {
        const float q = std::floor(y / s.ledgeHeight);
        y             = q * s.ledgeHeight;
    }
    const float strata =
        noise.ridged(p.x * s.strataScale, y * s.strataVertical + p.z * s.strataScale * 0.35f, 4) * 2.f - 1.f;
    const float pits   = noise.fbm(p.x * s.strataScale * 2.1f, y * s.strataVertical * 1.7f, 3);
    const float amount = s.strataAmplitude * (0.65f * strata - 0.35f * pits);
    return {p.x + dir.x * amount, y + dir.y * amount * 0.35f, p.z + dir.z * amount};
}

void emitSubdivided(MeshBuild& out, Vec3 a, Vec3 b, Vec3 c, const NoiseField& noise, const CliffBandMeshSettings& s,
                    int& triangleCount) {
    const int         div = std::max(1, 1 + s.subdivisions);
    std::vector<Vec3> points(size_t((div + 1) * (div + 2) / 2));
    int               index = 0;
    for (int i = 0; i <= div; ++i) {
        const float fi = float(i) / float(div);
        for (int j = 0; j <= div - i; ++j) {
            const float fj          = float(j) / float(div);
            const float fk          = 1.f - fi - fj;
            points[size_t(index++)] = a * fk + b * fi + c * fj;
        }
    }
    auto at = [div](int i, int j) {
        // Row i has (div - i + 1) samples; index of (i,j) in lower-left packing.
        int base = 0;
        for (int r = 0; r < i; ++r) base += div - r + 1;
        return base + j;
    };
    for (int i = 0; i < div; ++i) {
        for (int j = 0; j < div - i; ++j) {
            Vec3 p0             = points[size_t(at(i, j))];
            Vec3 p1             = points[size_t(at(i + 1, j))];
            Vec3 p2             = points[size_t(at(i, j + 1))];
            Vec3 n              = normalize(cross(p1 - p0, p2 - p0));
            p0                  = displace(p0, n, noise, s);
            p1                  = displace(p1, n, noise, s);
            p2                  = displace(p2, n, noise, s);
            n                   = normalize(cross(p1 - p0, p2 - p0));
            const float    u0   = p0.x * 0.15f + p0.z * 0.07f;
            const float    u1   = p1.x * 0.15f + p1.z * 0.07f;
            const float    u2   = p2.x * 0.15f + p2.z * 0.07f;
            const uint32_t base = uint32_t(out.getVertexCount());
            out.addVertex(p0.x, p0.y, p0.z, n.x, n.y, n.z, u0, p0.y * 0.12f);
            out.addVertex(p1.x, p1.y, p1.z, n.x, n.y, n.z, u1, p1.y * 0.12f);
            out.addVertex(p2.x, p2.y, p2.z, n.x, n.y, n.z, u2, p2.y * 0.12f);
            out.addTriangle(base, base + 1u, base + 2u);
            ++triangleCount;
            if (j + 1 < div - i) {
                Vec3 q0           = points[size_t(at(i + 1, j))];
                Vec3 q1           = points[size_t(at(i + 1, j + 1))];
                Vec3 q2           = points[size_t(at(i, j + 1))];
                Vec3 qn           = normalize(cross(q1 - q0, q2 - q0));
                q0                = displace(q0, qn, noise, s);
                q1                = displace(q1, qn, noise, s);
                q2                = displace(q2, qn, noise, s);
                qn                = normalize(cross(q1 - q0, q2 - q0));
                const uint32_t qb = uint32_t(out.getVertexCount());
                out.addVertex(q0.x, q0.y, q0.z, qn.x, qn.y, qn.z, q0.x * 0.15f + q0.z * 0.07f, q0.y * 0.12f);
                out.addVertex(q1.x, q1.y, q1.z, qn.x, qn.y, qn.z, q1.x * 0.15f + q1.z * 0.07f, q1.y * 0.12f);
                out.addVertex(q2.x, q2.y, q2.z, qn.x, qn.y, qn.z, q2.x * 0.15f + q2.z * 0.07f, q2.y * 0.12f);
                out.addTriangle(qb, qb + 1u, qb + 2u);
                ++triangleCount;
            }
        }
    }
}

}  // namespace

Result<int> generateCliffFitnessMask(Heightmap& target, const Heightmap& heights, float spacingX, float spacingZ,
                                     float heightScale, float slopeMin, float slopeMax) {
    auto slopeResult = deriveTerrainSlope(target, heights, spacingX, spacingZ, heightScale);
    if (!slopeResult.ok()) return slopeResult;
    if (!std::isfinite(slopeMin) || !std::isfinite(slopeMax) || slopeMin > slopeMax || slopeMin < 0.f || slopeMax > 1.f)
        return invalid("cliff.fitness: slope range must be finite, ordered, and inside [0,1]");
    std::vector<float> out(target.data().size());
    const float        span = slopeMax - slopeMin;
    for (size_t i = 0; i < out.size(); ++i) {
        const float s = target.data()[i];
        if (!(span > 0.f)) {
            out[i] = s >= slopeMin ? 1.f : 0.f;
            continue;
        }
        const float t = std::clamp((s - slopeMin) / span, 0.f, 1.f);
        out[i]        = t * t * (3.f - 2.f * t);
    }
    return publish(target, std::move(out));
}

Result<int> generateCliffBandMesh(MeshBuild& out, const Heightmap& heights, const CliffBandMeshSettings& settings,
                                  const Heightmap* cliffFitness) {
    if (!validRaster(heights) || heights.getWidth() < 2 || heights.getHeight() < 2)
        return invalid("cliff.band: heightmap must be at least 2x2 with finite samples");
    if (!std::isfinite(settings.spacingX) || settings.spacingX <= 0.f || !std::isfinite(settings.spacingZ) ||
        settings.spacingZ <= 0.f || !std::isfinite(settings.heightScale) || !std::isfinite(settings.originX) ||
        !std::isfinite(settings.originZ) || !std::isfinite(settings.slopeMin) || !std::isfinite(settings.slopeMax) ||
        settings.slopeMin > settings.slopeMax || settings.slopeMin < 0.f || settings.slopeMax > 1.f ||
        settings.subdivisions < 0 || settings.subdivisions > 4 || !std::isfinite(settings.strataAmplitude) ||
        settings.strataAmplitude < 0.f || !std::isfinite(settings.strataScale) || settings.strataScale <= 0.f ||
        !std::isfinite(settings.strataVertical) || settings.strataVertical <= 0.f ||
        !std::isfinite(settings.ledgeHeight) || settings.ledgeHeight < 0.f || !std::isfinite(settings.outwardBias) ||
        settings.outwardBias < 0.f || settings.outwardBias > 1.f || settings.maxTriangles < 0)
        return invalid("cliff.band: invalid settings");
    if (cliffFitness && (cliffFitness->getWidth() != heights.getWidth() ||
                         cliffFitness->getHeight() != heights.getHeight() || !validRaster(*cliffFitness)))
        return invalid("cliff.band: cliffFitness must match heightmap size and be finite");

    Heightmap        localSlope;
    const Heightmap* fitness = cliffFitness;
    if (!fitness) {
        localSlope.resize(heights.getWidth(), heights.getHeight());
        auto fitnessResult = generateCliffFitnessMask(localSlope, heights, settings.spacingX, settings.spacingZ,
                                                      settings.heightScale, settings.slopeMin, settings.slopeMax);
        if (!fitnessResult.ok()) return fitnessResult;
        fitness = &localSlope;
    }

    MeshBuild  candidate;
    NoiseField noise;
    noise.seed          = settings.seed;
    int       triangles = 0;
    const int w         = heights.getWidth();
    const int h         = heights.getHeight();
    auto      world     = [&](int x, int z) -> Vec3 {
        return {settings.originX + float(x) * settings.spacingX, heights.height(x, z) * settings.heightScale,
                settings.originZ + float(z) * settings.spacingZ};
    };

    candidate.reserve((w - 1) * (h - 1) * 6 * (1 + settings.subdivisions) * (1 + settings.subdivisions),
                      (w - 1) * (h - 1) * 6 * (1 + settings.subdivisions) * (1 + settings.subdivisions));

    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            const Vec3 p00 = world(x, z);
            const Vec3 p10 = world(x + 1, z);
            const Vec3 p01 = world(x, z + 1);
            const Vec3 p11 = world(x + 1, z + 1);
            struct Tri {
                Vec3 a, b, c;
                int  ax, az, bx, bz, cx, cz;
            };
            const Tri tris[2] = {{p00, p10, p11, x, z, x + 1, z, x + 1, z + 1},
                                 {p00, p11, p01, x, z, x + 1, z + 1, x, z + 1}};
            for (const Tri& tri : tris) {
                const float slope = faceSlope(tri.a, tri.b, tri.c);
                const float fit   = sampleFitness(*fitness, tri.ax, tri.az, tri.bx, tri.bz, tri.cx, tri.cz);
                if (slope < settings.slopeMin || slope > settings.slopeMax || fit < settings.slopeMin) continue;
                const int before = triangles;
                emitSubdivided(candidate, tri.a, tri.b, tri.c, noise, settings, triangles);
                if (settings.maxTriangles > 0 && triangles > settings.maxTriangles) {
                    return Result<int>::failure(Diagnostic::error(
                        DiagnosticCode::Failed, "cliff.band: triangle budget exceeded", "maxTriangles"));
                }
                (void)before;
            }
        }
    }

    out.clear();
    out = std::move(candidate);
    out.setMeta("algorithm", "mesh.cliffband");
    out.setMeta("triangles", std::to_string(triangles));
    out.setMeta("seed", std::to_string(settings.seed));
    return Result<int>::success(triangles);
}

}  // namespace eve::procgen
