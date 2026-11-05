#include "procgen/heightmap/CliffBandMesh.h"

#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/texture/NoiseField.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

Vec3& operator+=(Vec3& a, Vec3 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

float length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (!(len > 1e-8f)) return {0.f, 1.f, 0.f};
    return v * (1.f / len);
}

uint32_t hashu(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float hash01(uint32_t x) { return float(hashu(x) & 0x00ffffffu) / float(0x01000000u); }

float worldHeight(const Heightmap& heights, int x, int z, float heightScale) {
    return heights.height(x, z) * heightScale;
}

float sampleBilinearHeight(const Heightmap& heights, float fx, float fz, float heightScale) {
    const int   w   = heights.getWidth();
    const int   h   = heights.getHeight();
    const float x   = std::clamp(fx, 0.f, float(w - 1));
    const float z   = std::clamp(fz, 0.f, float(h - 1));
    const int   x0  = int(std::floor(x));
    const int   z0  = int(std::floor(z));
    const int   x1  = std::min(x0 + 1, w - 1);
    const int   z1  = std::min(z0 + 1, h - 1);
    const float tx  = x - float(x0);
    const float tz  = z - float(z0);
    const float h00 = worldHeight(heights, x0, z0, heightScale);
    const float h10 = worldHeight(heights, x1, z0, heightScale);
    const float h01 = worldHeight(heights, x0, z1, heightScale);
    const float h11 = worldHeight(heights, x1, z1, heightScale);
    const float hx0 = h00 * (1.f - tx) + h10 * tx;
    const float hx1 = h01 * (1.f - tx) + h11 * tx;
    return hx0 * (1.f - tz) + hx1 * tz;
}

float sampleFitnessBilinear(const Heightmap& fitness, float fx, float fz) {
    const int   w   = fitness.getWidth();
    const int   h   = fitness.getHeight();
    const float x   = std::clamp(fx, 0.f, float(w - 1));
    const float z   = std::clamp(fz, 0.f, float(h - 1));
    const int   x0  = int(std::floor(x));
    const int   z0  = int(std::floor(z));
    const int   x1  = std::min(x0 + 1, w - 1);
    const int   z1  = std::min(z0 + 1, h - 1);
    const float tx  = x - float(x0);
    const float tz  = z - float(z0);
    const float f00 = fitness.height(x0, z0);
    const float f10 = fitness.height(x1, z0);
    const float f01 = fitness.height(x0, z1);
    const float f11 = fitness.height(x1, z1);
    const float fx0 = f00 * (1.f - tx) + f10 * tx;
    const float fx1 = f01 * (1.f - tx) + f11 * tx;
    return fx0 * (1.f - tz) + fx1 * tz;
}

/** Horizontal outward for a steep patch: -∇h in XZ, pointing toward the lower side. */
Vec3 outwardAt(const Heightmap& heights, float fx, float fz, float heightScale, float spacingX, float spacingZ) {
    const float eps = 0.35f;
    const float hL  = sampleBilinearHeight(heights, fx - eps, fz, heightScale);
    const float hR  = sampleBilinearHeight(heights, fx + eps, fz, heightScale);
    const float hD  = sampleBilinearHeight(heights, fx, fz - eps, heightScale);
    const float hU  = sampleBilinearHeight(heights, fx, fz + eps, heightScale);
    // Descent direction = downslope = toward lower elevation.
    Vec3 out = {hL - hR, 0.f, hD - hU};
    if (length(out) < 1e-5f) {
        // Fallback: prefer +X when degenerate.
        out = {1.f, 0.f, 0.f};
    }
    (void)spacingX;
    (void)spacingZ;
    return normalize(out);
}

/** Face slope from a triangle normal: 1-|n.y| (matches deriveTerrainSlope). */
float triSlope(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3 n = normalize(cross(b - a, c - a));
    return 1.f - std::abs(n.y);
}

float triRelief(Vec3 a, Vec3 b, Vec3 c) {
    return std::max(std::abs(a.y - b.y), std::max(std::abs(a.y - c.y), std::abs(b.y - c.y)));
}

Vec3 cellCorner(const Heightmap& heights, int x, int z, const CliffBandMeshSettings& s) {
    return {s.originX + float(x) * s.spacingX, worldHeight(heights, x, z, s.heightScale),
            s.originZ + float(z) * s.spacingZ};
}

/**
 * Mark which of the two cell triangles are steep enough.
 * bit0 = tri (00,10,01), bit1 = tri (10,11,01). Avoids sucking flat plateau
 * corners into the cliff mesh (the "beige shelf" failure mode).
 */
uint8_t steepTriMask(const Heightmap& heights, const Heightmap& fitness, int x, int z, const CliffBandMeshSettings& s) {
    const Vec3  p00 = cellCorner(heights, x, z, s);
    const Vec3  p10 = cellCorner(heights, x + 1, z, s);
    const Vec3  p01 = cellCorner(heights, x, z + 1, s);
    const Vec3  p11 = cellCorner(heights, x + 1, z + 1, s);
    const float fit = 0.25f * (fitness.height(x, z) + fitness.height(x + 1, z) + fitness.height(x, z + 1) +
                               fitness.height(x + 1, z + 1));
    if (fit < 0.02f) return 0;
    uint8_t mask = 0;
    if (triRelief(p00, p10, p01) >= s.minRelief) {
        const float sl = triSlope(p00, p10, p01);
        if (sl >= s.slopeMin && sl <= s.slopeMax) mask |= 1u;
    }
    if (triRelief(p10, p11, p01) >= s.minRelief) {
        const float sl = triSlope(p10, p11, p01);
        if (sl >= s.slopeMin && sl <= s.slopeMax) mask |= 2u;
    }
    return mask;
}

struct CliffVert {
    Vec3  p{};
    Vec3  outward{};
    float u = 0, v = 0;
};

float smoothstepLocal(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/**
 * Ubisoft/Pegasus displace:
 * - per-bed XZ extrusion (stratification boxes)
 * - Worley F2-F1 macro rock blocks (vertically stretched cells)
 * - fine grit; joint recess between beds
 * - cladding offset so mesh clears the heightfield skin
 */
Vec3 stratifyDisplace(Vec3 p, Vec3 outward, const NoiseField& noise, const CliffBandMeshSettings& s) {
    const float bedH = s.ledgeHeight > 1e-4f ? s.ledgeHeight : 1.f / std::max(0.15f, s.strataVertical);
    // Soft tilt of bedding planes (Houdini HeightField Strata tilt).
    const float tiltedY = p.y + s.strataTilt * (p.x * 0.65f + p.z * 0.35f);
    const float bedF    = tiltedY / bedH;
    const int   bedId   = int(std::floor(bedF));
    const float frac    = bedF - float(bedId);
    // Soft plateau in the middle of each bed, recessed joints at boundaries.
    const float joint     = std::min(frac, 1.f - frac);
    const float jointMask = smoothstepLocal(0.f, 0.18f, joint);                              // 0 at seam, 1 mid-bed
    const float bedScale  = 0.82f + 0.36f * hash01(s.seed ^ uint32_t(bedId * 2654435761u));  // ~[0.82, 1.18]

    // Worley F2-F1 on a vertically stretched domain → rock blocks (Ubisoft/GDC style).
    const float wu    = p.x * s.strataScale + p.z * s.strataScale * 0.15f;
    const float wv    = p.y * s.strataScale * 2.6f + p.z * s.strataScale * 0.35f;
    const float f1    = noise.worleyF1(wu, wv, 0.9f);
    const float f2    = noise.worleyF2(wu, wv, 0.9f);
    const float block = std::clamp((f2 - f1) * 2.2f - 0.15f, -1.f, 1.f);
    const float grit = noise.fbm(p.x * s.strataScale * 3.1f, p.y * s.strataScale * 2.2f, p.z * s.strataScale * 3.1f, 3);
    const float crack =
        std::pow(std::fabs(noise.valueNoise(p.x * s.strataScale * 1.4f + p.z * 0.7f, p.y * 0.12f) - 0.5f) * 2.f, 2.2f);

    const float strataPush = s.strataAmplitude * (bedScale - 1.f) * 2.2f * jointMask;
    const float macroPush  = s.macroAmplitude * (0.70f * block + 0.20f * (grit * 2.f - 1.f) - 0.35f * (1.f - crack));
    const float push       = s.claddingOffset + strataPush + macroPush * std::max(0.35f, s.outwardBias);

    Vec3 out = {p.x + outward.x * push, p.y, p.z + outward.z * push};
    // Mild vertical settle on recessed joints so beds read as shelves.
    out.y -= s.strataAmplitude * 0.18f * (1.f - jointMask);
    return out;
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
        settings.outwardBias < 0.f || settings.outwardBias > 1.f || !std::isfinite(settings.claddingOffset) ||
        settings.claddingOffset < 0.f || !std::isfinite(settings.strataTilt) ||
        !std::isfinite(settings.macroAmplitude) || settings.macroAmplitude < 0.f || settings.smoothIterations < 0 ||
        settings.smoothIterations > 8 || !std::isfinite(settings.uvScaleU) || settings.uvScaleU <= 0.f ||
        !std::isfinite(settings.uvScaleV) || settings.uvScaleV <= 0.f || settings.maxTriangles < 0)
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

    const int w = heights.getWidth();
    const int h = heights.getHeight();

    // 1) Slope threshold per triangle (Pegasus / Ubisoft step 1).
    std::vector<uint8_t> steep(size_t((w - 1) * (h - 1)), 0);
    int                  steepCount = 0;
    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            const uint8_t mask             = steepTriMask(heights, *fitness, x, z, settings);
            steep[size_t(z * (w - 1) + x)] = mask;
            if (mask) ++steepCount;
        }
    }
    if (steepCount == 0) {
        out.clear();
        out.setMeta("algorithm", "mesh.cliffband");
        out.setMeta("triangles", "0");
        out.setMeta("seed", std::to_string(settings.seed));
        return Result<int>::success(0);
    }

    const int div = 1 + settings.subdivisions;  // samples along each cell edge
    // Dense sample grid covering the whole map; only steep tris emit.
    const int gu = (w - 1) * div + 1;
    const int gv = (h - 1) * div + 1;

    std::vector<CliffVert> verts(size_t(gu * gv));
    std::vector<uint8_t>   used(size_t(gu * gv), 0);

    auto at = [gu](int iu, int iv) { return size_t(iv * gu + iu); };

    NoiseField noise;
    noise.seed = settings.seed;

    auto subInTri0 = [div](int iu, int iv) { return (float(iu) + 0.5f) + (float(iv) + 0.5f) <= float(div); };

    // 2) Prepare geometry: sample only verts that touch a steep sub-quad.
    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            const uint8_t mask = steep[size_t(z * (w - 1) + x)];
            if (!mask) continue;
            for (int iv = 0; iv < div; ++iv) {
                for (int iu = 0; iu < div; ++iu) {
                    const bool want = subInTri0(iu, iv) ? (mask & 1u) != 0 : (mask & 2u) != 0;
                    if (!want) continue;
                    for (int dj = 0; dj <= 1; ++dj) {
                        for (int di = 0; di <= 1; ++di) {
                            const int    gi = x * div + iu + di;
                            const int    gj = z * div + iv + dj;
                            const size_t id = at(gi, gj);
                            if (used[id]) continue;
                            const float fx = float(x) + float(iu + di) / float(div);
                            const float fz = float(z) + float(iv + dj) / float(div);
                            const float y  = sampleBilinearHeight(heights, fx, fz, settings.heightScale);
                            CliffVert&  v  = verts[id];
                            v.p            = {settings.originX + fx * settings.spacingX, y,
                                              settings.originZ + fz * settings.spacingZ};
                            v.outward =
                                outwardAt(heights, fx, fz, settings.heightScale, settings.spacingX, settings.spacingZ);
                            v.u      = (v.p.x * 0.37f + v.p.z * 0.93f) * settings.uvScaleU;
                            v.v      = v.p.y * settings.uvScaleV;
                            used[id] = 1;
                        }
                    }
                }
            }
        }
    }

    // 3) Stratification + rock displace (Ubisoft steps 3/5, Pegasus displace).
    for (size_t i = 0; i < verts.size(); ++i) {
        if (!used[i]) continue;
        verts[i].p = stratifyDisplace(verts[i].p, verts[i].outward, noise, settings);
    }

    // 4) Laplacian blur on steep verts (shape construction blur).
    for (int pass = 0; pass < settings.smoothIterations; ++pass) {
        std::vector<Vec3> next(verts.size());
        for (size_t i = 0; i < verts.size(); ++i) next[i] = verts[i].p;
        for (int gj = 0; gj < gv; ++gj) {
            for (int gi = 0; gi < gu; ++gi) {
                const size_t id = at(gi, gj);
                if (!used[id]) continue;
                Vec3  sum    = verts[id].p;
                float weight = 1.f;
                auto  accum  = [&](int x, int y) {
                    if (x < 0 || y < 0 || x >= gu || y >= gv) return;
                    const size_t n = at(x, y);
                    if (!used[n]) return;
                    sum += verts[n].p;
                    weight += 1.f;
                };
                accum(gi - 1, gj);
                accum(gi + 1, gj);
                accum(gi, gj - 1);
                accum(gi, gj + 1);
                // Keep 65% original so strata shelves survive the blur.
                const Vec3 avg = sum * (1.f / weight);
                next[id]       = verts[id].p * 0.65f + avg * 0.35f;
            }
        }
        for (size_t i = 0; i < verts.size(); ++i) {
            if (used[i]) verts[i].p = next[i];
        }
    }

    // 5) Emit triangles for steep quads; average face normals.
    MeshBuild         candidate;
    std::vector<Vec3> normals(verts.size(), {0.f, 0.f, 0.f});
    std::vector<int>  emitIndex(verts.size(), -1);
    int               triangles = 0;

    auto ensureIndex = [&](int gi, int gj) -> uint32_t {
        const size_t id = at(gi, gj);
        if (emitIndex[id] >= 0) return uint32_t(emitIndex[id]);
        const CliffVert& v = verts[id];
        const Vec3&      n = normals[id];
        // Normals filled in a second pass — store placeholder then rewrite via MeshBuild is hard;
        // instead accumulate normals first, then emit. So this lambda is used only after normals ready.
        (void)v;
        (void)n;
        return 0;
    };
    (void)ensureIndex;

    // First accumulate face normals from steep sub-quads only.
    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            const uint8_t mask = steep[size_t(z * (w - 1) + x)];
            if (!mask) continue;
            for (int iv = 0; iv < div; ++iv) {
                for (int iu = 0; iu < div; ++iu) {
                    const bool want = subInTri0(iu, iv) ? (mask & 1u) != 0 : (mask & 2u) != 0;
                    if (!want) continue;
                    const int i00 = int(at(x * div + iu, z * div + iv));
                    const int i10 = int(at(x * div + iu + 1, z * div + iv));
                    const int i11 = int(at(x * div + iu + 1, z * div + iv + 1));
                    const int i01 = int(at(x * div + iu, z * div + iv + 1));
                    Vec3      n0 =
                        cross(verts[size_t(i10)].p - verts[size_t(i00)].p, verts[size_t(i01)].p - verts[size_t(i00)].p);
                    Vec3 n1 =
                        cross(verts[size_t(i11)].p - verts[size_t(i10)].p, verts[size_t(i01)].p - verts[size_t(i10)].p);
                    const Vec3 outw = verts[size_t(i00)].outward;
                    if (n0.x * outw.x + n0.z * outw.z < 0.f) n0 = n0 * -1.f;
                    if (n1.x * outw.x + n1.z * outw.z < 0.f) n1 = n1 * -1.f;
                    normals[size_t(i00)] += n0;
                    normals[size_t(i10)] += n0;
                    normals[size_t(i01)] += n0;
                    normals[size_t(i10)] += n1;
                    normals[size_t(i11)] += n1;
                    normals[size_t(i01)] += n1;
                }
            }
        }
    }
    for (size_t i = 0; i < normals.size(); ++i) {
        if (used[i]) normals[i] = normalize(normals[i]);
    }

    candidate.reserve(steepCount * (div + 1) * (div + 1) * 2, steepCount * div * div * 6);
    for (int z = 0; z < h - 1; ++z) {
        for (int x = 0; x < w - 1; ++x) {
            const uint8_t mask = steep[size_t(z * (w - 1) + x)];
            if (!mask) continue;
            for (int iv = 0; iv < div; ++iv) {
                for (int iu = 0; iu < div; ++iu) {
                    const bool want = subInTri0(iu, iv) ? (mask & 1u) != 0 : (mask & 2u) != 0;
                    if (!want) continue;
                    const int ids[4] = {int(at(x * div + iu, z * div + iv)), int(at(x * div + iu + 1, z * div + iv)),
                                        int(at(x * div + iu + 1, z * div + iv + 1)),
                                        int(at(x * div + iu, z * div + iv + 1))};
                    uint32_t  base[4];
                    for (int k = 0; k < 4; ++k) {
                        const size_t id = size_t(ids[k]);
                        if (emitIndex[id] < 0) {
                            const CliffVert& v = verts[id];
                            const Vec3&      n = normals[id];
                            emitIndex[id]      = candidate.getVertexCount();
                            candidate.addVertex(v.p.x, v.p.y, v.p.z, n.x, n.y, n.z, v.u, v.v);
                        }
                        base[k] = uint32_t(emitIndex[id]);
                    }
                    // Drop sub-quads that flattened after displace/blur (floating shelf cards).
                    const Vec3& pa = verts[size_t(ids[0])].p;
                    const Vec3& pb = verts[size_t(ids[1])].p;
                    const Vec3& pc = verts[size_t(ids[2])].p;
                    const Vec3& pd = verts[size_t(ids[3])].p;
                    if (triSlope(pa, pb, pd) < settings.slopeMin * 0.85f &&
                        triSlope(pb, pc, pd) < settings.slopeMin * 0.85f)
                        continue;
                    candidate.addTriangle(base[0], base[1], base[2]);
                    candidate.addTriangle(base[0], base[2], base[3]);
                    triangles += 2;
                    if (settings.maxTriangles > 0 && triangles > settings.maxTriangles) {
                        return Result<int>::failure(Diagnostic::error(
                            DiagnosticCode::Failed, "cliff.band: triangle budget exceeded", "maxTriangles"));
                    }
                }
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
