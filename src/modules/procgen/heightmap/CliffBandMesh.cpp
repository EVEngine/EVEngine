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

float edgeSlope(float run, float relief) {
    const float hypot = std::sqrt(run * run + relief * relief);
    if (!(hypot > 1e-8f)) return 0.f;
    return 1.f - (run / hypot);
}

float sampleFitness(const Heightmap& fitness, int x0, int z0, int x1, int z1) {
    return 0.5f * (fitness.height(x0, z0) + fitness.height(x1, z1));
}

Vec3 displaceWall(Vec3 p, Vec3 outward, float topY, float bottomY, const NoiseField& noise,
                  const CliffBandMeshSettings& s) {
    float y = p.y;
    if (s.ledgeHeight > 0.f) {
        const float q = std::floor(y / s.ledgeHeight);
        y             = std::clamp(q * s.ledgeHeight, bottomY, topY);
    }
    const float strata =
        noise.ridged(p.x * s.strataScale, y * s.strataVertical + p.z * s.strataScale * 0.35f, 4) * 2.f - 1.f;
    const float pits   = noise.fbm(p.x * s.strataScale * 2.1f, y * s.strataVertical * 1.7f, 3);
    const float amount = s.strataAmplitude * (0.70f * strata - 0.30f * pits);
    const Vec3  dir    = normalize(outward * std::max(0.05f, s.outwardBias) + Vec3{0.f, 0.05f, 0.f});
    return {p.x + dir.x * amount, y + dir.y * amount * 0.2f, p.z + dir.z * amount};
}

void emitWallQuad(MeshBuild& out, Vec3 top0, Vec3 top1, Vec3 bot1, Vec3 bot0, Vec3 outward, const NoiseField& noise,
                  const CliffBandMeshSettings& s, int& triangleCount) {
    const int   divU   = std::max(1, 1 + s.subdivisions);
    const int   divV   = std::max(1, 1 + s.subdivisions);
    const float topY   = std::max(std::max(top0.y, top1.y), std::max(bot0.y, bot1.y));
    const float botY   = std::min(std::min(top0.y, top1.y), std::min(bot0.y, bot1.y));
    auto        corner = [&](float u, float v) {
        const Vec3 top = top0 * (1.f - u) + top1 * u;
        const Vec3 bot = bot0 * (1.f - u) + bot1 * u;
        return top * (1.f - v) + bot * v;
    };

    for (int iu = 0; iu < divU; ++iu) {
        const float u0 = float(iu) / float(divU);
        const float u1 = float(iu + 1) / float(divU);
        for (int iv = 0; iv < divV; ++iv) {
            const float v0  = float(iv) / float(divV);
            const float v1  = float(iv + 1) / float(divV);
            Vec3        p00 = corner(u0, v0);
            Vec3        p10 = corner(u1, v0);
            Vec3        p11 = corner(u1, v1);
            Vec3        p01 = corner(u0, v1);
            p00             = displaceWall(p00, outward, topY, botY, noise, s);
            p10             = displaceWall(p10, outward, topY, botY, noise, s);
            p11             = displaceWall(p11, outward, topY, botY, noise, s);
            p01             = displaceWall(p01, outward, topY, botY, noise, s);
            Vec3 n          = normalize(cross(p10 - p00, p01 - p00));
            if (n.x * outward.x + n.z * outward.z < 0.f) n = n * -1.f;
            const uint32_t base = uint32_t(out.getVertexCount());
            const float    uA   = p00.x * 0.18f + p00.z * 0.09f;
            const float    uB   = p10.x * 0.18f + p10.z * 0.09f;
            out.addVertex(p00.x, p00.y, p00.z, n.x, n.y, n.z, uA, p00.y * 0.14f);
            out.addVertex(p10.x, p10.y, p10.z, n.x, n.y, n.z, uB, p10.y * 0.14f);
            out.addVertex(p11.x, p11.y, p11.z, n.x, n.y, n.z, uB, p11.y * 0.14f);
            out.addVertex(p01.x, p01.y, p01.z, n.x, n.y, n.z, uA, p01.y * 0.14f);
            out.addTriangle(base, base + 1u, base + 2u);
            out.addTriangle(base, base + 2u, base + 3u);
            triangleCount += 2;
        }
    }
}

// Wall sits on the dual edge (midpoint between samples) and faces the low side —
// same idea as hex addCliff, so a terrace along X yields a face whose normal is ±X.
bool considerEdge(MeshBuild& out, const Heightmap& heights, const Heightmap& fitness, int x0, int z0, int x1, int z1,
                  float run, const CliffBandMeshSettings& s, NoiseField& noise, int& triangles) {
    const float h0     = heights.height(x0, z0) * s.heightScale;
    const float h1     = heights.height(x1, z1) * s.heightScale;
    const float relief = std::abs(h0 - h1);
    if (!(relief > 1e-4f) || relief < s.minRelief) return true;
    const float slope = edgeSlope(run, relief);
    if (slope < s.slopeMin || slope > s.slopeMax) return true;
    const float fit = sampleFitness(fitness, x0, z0, x1, z1);
    if (fit < 0.02f) return true;

    const float topY = std::max(h0, h1);
    const float botY = std::min(h0, h1);
    const float ax   = s.originX + float(x0) * s.spacingX;
    const float az   = s.originZ + float(z0) * s.spacingZ;
    const float bx   = s.originX + float(x1) * s.spacingX;
    const float bz   = s.originZ + float(z1) * s.spacingZ;
    const float mx   = 0.5f * (ax + bx);
    const float mz   = 0.5f * (az + bz);

    Vec3 outward;
    Vec3 topA, topB, botA, botB;
    // Shell the wall in front of the heightfield face (front + back + thickness).
    const float spacing = std::max(s.spacingX, s.spacingZ);
    const float push    = 0.12f * spacing;
    const float depth   = 0.22f * spacing + 0.35f * s.strataAmplitude;
    if (x0 != x1) {
        const float half = 0.5f * s.spacingZ;
        outward          = h0 >= h1 ? Vec3{bx - ax, 0.f, 0.f} : Vec3{ax - bx, 0.f, 0.f};
        outward          = normalize(outward);
        const float wf   = mx + outward.x * push;
        const float wb   = mx + outward.x * (push + depth);
        topA             = {wf, topY, mz - half};
        topB             = {wf, topY, mz + half};
        botB             = {wf, botY, mz + half};
        botA             = {wf, botY, mz - half};
        emitWallQuad(out, topA, topB, botB, botA, outward, noise, s, triangles);
        // Back face + caps keep the shell readable when the camera skims the face.
        emitWallQuad(out, {wb, topY, mz + half}, {wb, topY, mz - half}, {wb, botY, mz - half}, {wb, botY, mz + half},
                     outward * -1.f, noise, s, triangles);
        emitWallQuad(out, {wf, topY, mz - half}, {wb, topY, mz - half}, {wb, topY, mz + half}, {wf, topY, mz + half},
                     {0.f, 1.f, 0.f}, noise, s, triangles);
    } else {
        const float half = 0.5f * s.spacingX;
        outward          = h0 >= h1 ? Vec3{0.f, 0.f, bz - az} : Vec3{0.f, 0.f, az - bz};
        outward          = normalize(outward);
        const float zf   = mz + outward.z * push;
        const float zb   = mz + outward.z * (push + depth);
        topA             = {mx - half, topY, zf};
        topB             = {mx + half, topY, zf};
        botB             = {mx + half, botY, zf};
        botA             = {mx - half, botY, zf};
        emitWallQuad(out, topA, topB, botB, botA, outward, noise, s, triangles);
        emitWallQuad(out, {mx + half, topY, zb}, {mx - half, topY, zb}, {mx - half, botY, zb}, {mx + half, botY, zb},
                     outward * -1.f, noise, s, triangles);
        emitWallQuad(out, {mx - half, topY, zf}, {mx + half, topY, zf}, {mx + half, topY, zb}, {mx - half, topY, zb},
                     {0.f, 1.f, 0.f}, noise, s, triangles);
    }
    if (s.maxTriangles > 0 && triangles > s.maxTriangles) return false;
    return true;
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
        !std::isfinite(settings.minRelief) || settings.minRelief < 0.f || settings.subdivisions < 0 ||
        settings.subdivisions > 4 || !std::isfinite(settings.strataAmplitude) || settings.strataAmplitude < 0.f ||
        !std::isfinite(settings.strataScale) || settings.strataScale <= 0.f ||
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

    const int segs = std::max(1, 1 + settings.subdivisions);
    candidate.reserve((w * h) * 4 * segs * segs, (w * h) * 6 * segs * segs);

    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            if (!considerEdge(candidate, heights, *fitness, x, z, x + 1, z, settings.spacingX, settings, noise,
                              triangles)) {
                return Result<int>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "cliff.band: triangle budget exceeded", "maxTriangles"));
            }
        }
    }
    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w; ++x) {
            if (!considerEdge(candidate, heights, *fitness, x, z, x, z + 1, settings.spacingZ, settings, noise,
                              triangles)) {
                return Result<int>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "cliff.band: triangle budget exceeded", "maxTriangles"));
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
