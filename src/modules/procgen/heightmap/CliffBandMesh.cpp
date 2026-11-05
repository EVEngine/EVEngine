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

float edgeSlope(float run, float relief) {
    const float hypot = std::sqrt(run * run + relief * relief);
    if (!(hypot > 1e-8f)) return 0.f;
    return 1.f - (run / hypot);
}

float sampleFitness(const Heightmap& fitness, int x0, int z0, int x1, int z1) {
    return 0.5f * (fitness.height(x0, z0) + fitness.height(x1, z1));
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

struct CliffColumn {
    float x = 0, z = 0;
    float topY = 0, botY = 0;
    Vec3  outward{1.f, 0.f, 0.f};
};

float worldHeight(const Heightmap& heights, int x, int z, float heightScale) {
    return heights.height(x, z) * heightScale;
}

float maxPerpRelief(const Heightmap& heights, int x0, int z0, int x1, int z1, float heightScale) {
    // Compare against relief along the wall tangent so accordion fins lose to the
    // dominant scarp facing.
    const int w        = heights.getWidth();
    const int h        = heights.getHeight();
    float     m        = 0.f;
    auto      consider = [&](int ax, int az, int bx, int bz) {
        if (ax < 0 || bx < 0 || az < 0 || bz < 0 || ax >= w || bx >= w || az >= h || bz >= h) return;
        m = std::max(m,
                     std::abs(worldHeight(heights, ax, az, heightScale) - worldHeight(heights, bx, bz, heightScale)));
    };
    if (x0 != x1) {
        consider(x0, z0, x0, z0 - 1);
        consider(x0, z0, x0, z0 + 1);
        consider(x1, z0, x1, z0 - 1);
        consider(x1, z0, x1, z0 + 1);
    } else {
        consider(x0, z0, x0 - 1, z0);
        consider(x0, z0, x0 + 1, z0);
        consider(x0, z1, x0 - 1, z1);
        consider(x0, z1, x0 + 1, z1);
    }
    return m;
}

bool isCliffEdge(const Heightmap& heights, const Heightmap& fitness, int x0, int z0, int x1, int z1, float run,
                 const CliffBandMeshSettings& s, float& outTop, float& outBot, Vec3& outOutward) {
    const float h0     = worldHeight(heights, x0, z0, s.heightScale);
    const float h1     = worldHeight(heights, x1, z1, s.heightScale);
    const float relief = std::abs(h0 - h1);
    if (!(relief > 1e-4f) || relief < s.minRelief) return false;
    const float slope = edgeSlope(run, relief);
    if (slope < s.slopeMin || slope > s.slopeMax) return false;
    if (sampleFitness(fitness, x0, z0, x1, z1) < 0.02f) return false;
    // Keep the dominant facing only — suppresses wafer fins on jagged scarps.
    if (relief + 1e-3f < maxPerpRelief(heights, x0, z0, x1, z1, s.heightScale) * 0.92f) return false;
    outTop = std::max(h0, h1);
    outBot = std::min(h0, h1);
    if (x0 != x1) {
        outOutward = normalize(h0 >= h1 ? Vec3{float(x1 - x0), 0.f, 0.f} : Vec3{float(x0 - x1), 0.f, 0.f});
    } else {
        outOutward = normalize(h0 >= h1 ? Vec3{0.f, 0.f, float(z1 - z0)} : Vec3{0.f, 0.f, float(z0 - z1)});
    }
    return true;
}

Vec3 rockDisplace(Vec3 p, Vec3 outward, float topY, float botY, float along, float v01, const NoiseField& noise,
                  const CliffBandMeshSettings& s, uint32_t seed) {
    const float relief = std::max(1e-3f, topY - botY);
    // Soft terraces: bias toward beds without hard stair cuts that shred the face.
    float y = p.y;
    if (s.ledgeHeight > 1e-4f) {
        const float q   = std::floor(y / s.ledgeHeight) * s.ledgeHeight;
        const float bed = 0.38f;
        y               = y * (1.f - bed) + q * bed;
        y               = std::clamp(y, botY, topY);
    }

    const float strata =
        noise.ridged(p.x * s.strataScale, y * s.strataVertical + p.z * s.strataScale * 0.31f, 5) * 2.f - 1.f;
    const float beds  = noise.ridged(p.x * s.strataScale * 0.45f + 3.1f, y * s.strataVertical * 1.35f, 3) * 2.f - 1.f;
    const float pits  = noise.fbm(p.x * s.strataScale * 2.4f, y * s.strataVertical * 1.9f + along * 0.2f, 4);
    const float crack = noise.fbm(along * s.strataScale * 1.7f, y * s.strataVertical * 0.55f, 3);

    // Continuous geometric shelves: sine bed wave + ridged ledge (not discrete stairs).
    const float midMask = std::clamp(1.f - std::abs(v01 * 2.f - 1.f), 0.f, 1.f);
    const float bedWave =
        std::sin(y * s.strataVertical * 6.2831853f + noise.valueNoise(along * 0.12f, y * 0.05f) * 1.35f);
    const float softShelf =
        bedWave * 0.55f +
        (noise.ridged(p.x * s.strataScale * 0.5f, y * s.strataVertical * 1.6f, 3) * 2.f - 1.f) * 0.45f;
    // Vertical fracture recesses (continuous in along — keeps welded strips).
    const float fracture =
        std::pow(std::fabs(noise.valueNoise(along * s.strataScale * 0.95f, y * 0.07f) - 0.5f) * 2.f, 2.4f);

    // Hex-style lip / foot: stronger near top and toe.
    const float endMask = std::clamp(1.f - std::abs(v01 * 2.f - 1.f) * 0.55f, 0.f, 1.f);
    const float lipMask = std::pow(std::clamp(v01, 0.f, 1.f), 1.6f);
    const float toeMask = std::pow(std::clamp(1.f - v01, 0.f, 1.f), 1.35f);
    const float jitter  = hash01(seed ^ uint32_t(along * 97.f) ^ uint32_t(y * 13.f)) - 0.42f;

    const float baseAmp = s.strataAmplitude;
    float       amount  = baseAmp * (0.42f * strata + 0.38f * beds - 0.16f * pits - 0.10f * crack);
    // Geometric beds must read as shelves under soft light — weight shelves hard.
    amount += baseAmp * 1.45f * softShelf * (0.25f + 0.75f * midMask);
    amount -= baseAmp * 0.72f * (1.f - fracture);
    amount += baseAmp * 0.48f * lipMask * (jitter * 0.28f + beds * 0.30f);
    amount += baseAmp * 0.78f * toeMask * (pits - 0.20f);
    amount *= 0.55f + 0.45f * endMask;

    const float push = amount * std::max(0.30f, s.outwardBias);
    // Lift on shelf peaks so bedding reads in silhouette, not only in normals.
    const float lift =
        amount * (0.16f + 0.28f * lipMask + 0.18f * std::max(0.f, softShelf)) * (1.f - s.outwardBias * 0.40f);
    Vec3 out = {p.x + outward.x * push, y + lift, p.z + outward.z * push};

    // Mild continuous shear into fractures — keep seams welded.
    const Vec3 alongDir = normalize(cross(outward, {0.f, 1.f, 0.f}));
    out += alongDir * (baseAmp * 0.18f * crack * (0.4f + 0.6f * fracture));

    // Clamp so displacement cannot collapse the relief to a sliver.
    out.y = std::clamp(out.y, botY - 0.05f * relief, topY + 0.08f * relief);
    return out;
}

bool emitContinuousStrip(MeshBuild& out, const std::vector<CliffColumn>& cols, const CliffBandMeshSettings& s,
                         NoiseField& noise, int& triangles) {
    if (cols.size() < 2) return true;
    const int divU = std::max(1, 1 + s.subdivisions);  // along wall
    const int divV = std::max(2, 2 + s.subdivisions);  // vertical — denser for rock beds
    const int nu   = int(cols.size() - 1) * divU + 1;
    const int nv   = divV + 1;

    std::vector<Vec3>  positions(size_t(nu * nv));
    std::vector<Vec3>  normals(size_t(nu * nv), {0.f, 0.f, 0.f});
    std::vector<float> us(size_t(nu * nv));
    std::vector<float> vs(size_t(nu * nv));

    float              along = 0.f;
    std::vector<float> alongAt(cols.size(), 0.f);
    for (size_t i = 1; i < cols.size(); ++i) {
        along += length(Vec3{cols[i].x - cols[i - 1].x, 0.f, cols[i].z - cols[i - 1].z});
        alongAt[i] = along;
    }

    // Smooth lip/toe along the strip to kill sawtooth silhouettes on jagged rims.
    std::vector<float> topS(cols.size()), botS(cols.size());
    for (size_t i = 0; i < cols.size(); ++i) {
        float topSum = 0.f, botSum = 0.f;
        int   n = 0;
        for (int d = -2; d <= 2; ++d) {
            const int j = int(i) + d;
            if (j < 0 || j >= int(cols.size())) continue;
            topSum += cols[size_t(j)].topY;
            botSum += cols[size_t(j)].botY;
            ++n;
        }
        topS[i] = topSum / float(std::max(1, n));
        botS[i] = botSum / float(std::max(1, n));
    }

    // Sit clearly in front of the heightfield's steep triangles (Pegasus cladding).
    const float push = 0.55f * std::max(s.spacingX, s.spacingZ) + 0.55f * s.strataAmplitude;
    for (size_t c = 0; c + 1 < cols.size(); ++c) {
        for (int iu = 0; iu <= divU; ++iu) {
            if (c > 0 && iu == 0) continue;  // shared column with previous segment
            const float t    = float(iu) / float(divU);
            const int   xi   = int(c) * divU + iu;
            const float x    = cols[c].x * (1.f - t) + cols[c + 1].x * t;
            const float z    = cols[c].z * (1.f - t) + cols[c + 1].z * t;
            const float topY = topS[c] * (1.f - t) + topS[c + 1] * t;
            const float botY = botS[c] * (1.f - t) + botS[c + 1] * t;
            Vec3        outw = normalize(cols[c].outward * (1.f - t) + cols[c + 1].outward * t);
            const float al   = alongAt[c] * (1.f - t) + alongAt[c + 1] * t;
            for (int iv = 0; iv <= divV; ++iv) {
                const float v01 = float(iv) / float(divV);
                // v=0 at top lip (matches UV V-by-height convention after flip for materials).
                const float y0  = topY * (1.f - v01) + botY * v01;
                Vec3        p   = {x + outw.x * push, y0, z + outw.z * push};
                p               = rockDisplace(p, outw, topY, botY, al, v01, noise, s, s.seed ^ uint32_t(xi * 131u));
                const size_t id = size_t(xi * nv + iv);
                positions[id]   = p;
                // U along ledge, V-by-height so strata albedo/normal beds stay horizontal.
                us[id] = al * s.uvScaleU;
                vs[id] = (topY - p.y) * s.uvScaleV;
            }
        }
    }

    auto idx = [nv](int xi, int iv) { return xi * nv + iv; };
    for (int xi = 0; xi < nu - 1; ++xi) {
        for (int iv = 0; iv < nv - 1; ++iv) {
            const int i00 = idx(xi, iv);
            const int i10 = idx(xi + 1, iv);
            const int i11 = idx(xi + 1, iv + 1);
            const int i01 = idx(xi, iv + 1);
            Vec3      n0 =
                cross(positions[size_t(i10)] - positions[size_t(i00)], positions[size_t(i01)] - positions[size_t(i00)]);
            Vec3 n1 =
                cross(positions[size_t(i11)] - positions[size_t(i10)], positions[size_t(i01)] - positions[size_t(i10)]);
            // Prefer outward-facing (away from cliff interior / toward basin).
            Vec3 approxOut = cols[size_t(std::min(size_t(xi / std::max(1, divU)), cols.size() - 1))].outward;
            if (n0.x * approxOut.x + n0.z * approxOut.z < 0.f) n0 = n0 * -1.f;
            if (n1.x * approxOut.x + n1.z * approxOut.z < 0.f) n1 = n1 * -1.f;
            normals[size_t(i00)] += n0;
            normals[size_t(i10)] += n0;
            normals[size_t(i01)] += n0;
            normals[size_t(i10)] += n1;
            normals[size_t(i11)] += n1;
            normals[size_t(i01)] += n1;
        }
    }
    for (Vec3& n : normals) n = normalize(n);

    for (int xi = 0; xi < nu - 1; ++xi) {
        for (int iv = 0; iv < nv - 1; ++iv) {
            const int      i00  = idx(xi, iv);
            const int      i10  = idx(xi + 1, iv);
            const int      i11  = idx(xi + 1, iv + 1);
            const int      i01  = idx(xi, iv + 1);
            const uint32_t base = uint32_t(out.getVertexCount());
            auto           add  = [&](int i) {
                const Vec3& p = positions[size_t(i)];
                const Vec3& n = normals[size_t(i)];
                out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, us[size_t(i)], vs[size_t(i)]);
            };
            add(i00);
            add(i10);
            add(i11);
            add(i01);
            out.addTriangle(base, base + 1u, base + 2u);
            out.addTriangle(base, base + 2u, base + 3u);
            triangles += 2;
            if (s.maxTriangles > 0 && triangles > s.maxTriangles) return false;
        }
    }
    return true;
}

bool emitFacingStrips(MeshBuild& out, const Heightmap& heights, const Heightmap& fitness, bool xFacing,
                      const CliffBandMeshSettings& s, NoiseField& noise, int& triangles) {
    const int w = heights.getWidth();
    const int h = heights.getHeight();
    if (xFacing) {
        // Dual edges between (x,z)-(x+1,z); strips run along Z for each x interface.
        for (int x = 0; x < w - 1; ++x) {
            std::vector<CliffColumn> run;
            run.reserve(size_t(h));
            auto flush = [&]() {
                if (run.size() >= 2) {
                    if (!emitContinuousStrip(out, run, s, noise, triangles)) return false;
                }
                run.clear();
                return true;
            };
            for (int z = 0; z < h; ++z) {
                float top = 0, bot = 0;
                Vec3  outward;
                if (isCliffEdge(heights, fitness, x, z, x + 1, z, s.spacingX, s, top, bot, outward)) {
                    const float mx = s.originX + (float(x) + 0.5f) * s.spacingX;
                    const float mz = s.originZ + float(z) * s.spacingZ;
                    if (!run.empty()) {
                        const int prevZ = int(std::lround((run.back().z - s.originZ) / s.spacingZ));
                        if (z != prevZ + 1 ||
                            (run.back().outward.x * outward.x + run.back().outward.z * outward.z) < 0.f) {
                            if (!flush()) return false;
                        }
                    }
                    run.push_back({mx, mz, top, bot, outward});
                } else {
                    if (!flush()) return false;
                }
            }
            if (!flush()) return false;
        }
    } else {
        for (int z = 0; z < h - 1; ++z) {
            std::vector<CliffColumn> run;
            run.reserve(size_t(w));
            auto flush = [&]() {
                if (run.size() >= 2) {
                    if (!emitContinuousStrip(out, run, s, noise, triangles)) return false;
                }
                run.clear();
                return true;
            };
            for (int x = 0; x < w; ++x) {
                float top = 0, bot = 0;
                Vec3  outward;
                if (isCliffEdge(heights, fitness, x, z, x, z + 1, s.spacingZ, s, top, bot, outward)) {
                    const float mx = s.originX + float(x) * s.spacingX;
                    const float mz = s.originZ + (float(z) + 0.5f) * s.spacingZ;
                    if (!run.empty()) {
                        const int prevX = int(std::lround((run.back().x - s.originX) / s.spacingX));
                        if (x != prevX + 1 ||
                            (run.back().outward.x * outward.x + run.back().outward.z * outward.z) < 0.f) {
                            if (!flush()) return false;
                        }
                    }
                    run.push_back({mx, mz, top, bot, outward});
                } else {
                    if (!flush()) return false;
                }
            }
            if (!flush()) return false;
        }
    }
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
        settings.outwardBias < 0.f || settings.outwardBias > 1.f || !std::isfinite(settings.uvScaleU) ||
        settings.uvScaleU <= 0.f || !std::isfinite(settings.uvScaleV) || settings.uvScaleV <= 0.f ||
        settings.maxTriangles < 0)
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
    noise.seed    = settings.seed;
    int triangles = 0;
    candidate.reserve(heights.getWidth() * heights.getHeight() * 24, heights.getWidth() * heights.getHeight() * 36);

    if (!emitFacingStrips(candidate, heights, *fitness, true, settings, noise, triangles) ||
        !emitFacingStrips(candidate, heights, *fitness, false, settings, noise, triangles)) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "cliff.band: triangle budget exceeded", "maxTriangles"));
    }

    out.clear();
    out = std::move(candidate);
    out.setMeta("algorithm", "mesh.cliffband");
    out.setMeta("triangles", std::to_string(triangles));
    out.setMeta("seed", std::to_string(settings.seed));
    return Result<int>::success(triangles);
}

}  // namespace eve::procgen
