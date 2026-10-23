#include "procgen/algorithms/CaveSurfaceReactivity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "procgen/MeshBuild.h"
#include "procgen/algorithms/CaveWaterTable.h"
#include "procgen/algorithms/CaveWetness.h"

// CaveSurfaceReactivity.cpp
namespace eve::procgen {
namespace {

struct Normal {
    float x     = 0.f;
    float y     = 0.f;
    float z     = 0.f;
    bool  valid = false;
};

size_t voxelIndex(int x, int y, int z, int nx, int ny) {
    return size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny);
}

Normal densityNormal(const std::vector<float>& density, int x, int y, int z, int nx, int ny, float hx, float hy,
                     float hz) {
    const float gx = (density[voxelIndex(x + 1, y, z, nx, ny)] - density[voxelIndex(x - 1, y, z, nx, ny)]) / (2.f * hx);
    const float gy = (density[voxelIndex(x, y + 1, z, nx, ny)] - density[voxelIndex(x, y - 1, z, nx, ny)]) / (2.f * hy);
    const float gz = (density[voxelIndex(x, y, z + 1, nx, ny)] - density[voxelIndex(x, y, z - 1, nx, ny)]) / (2.f * hz);
    const float length = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (length <= 1e-6f) return {};
    return {gx / length, gy / length, gz / length, true};
}

}  // namespace

CaveSurfaceReactivityResult evolveCaveSurfaceByReactivity(std::vector<float>&       density,
                                                          const std::vector<float>& rateField, int nx, int ny, int nz,
                                                          float strength, int iterations) {
    CaveSurfaceReactivityResult result;
    if (strength <= 0.f || iterations <= 0 || nx < 5 || ny < 5 || nz < 5 || density.size() != rateField.size() ||
        density.size() != size_t(nx) * size_t(ny) * size_t(nz))
        return result;

    const float hx = 2.f / float(nx - 1), hy = 2.f / float(ny - 1), hz = 2.f / float(nz - 1);
    const float cellScale                           = std::min({hx, hy, hz});
    const float band                                = cellScale * 2.5f;
    const std::array<std::array<int, 3>, 6> offsets = {
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::vector<float>   source(density.size());
    std::vector<uint8_t> affected(density.size(), uint8_t(0));

    for (int iteration = 0; iteration < iterations; ++iteration) {
        source = density;
        for (int z = 2; z < nz - 2; ++z) {
            for (int y = 2; y < ny - 2; ++y) {
                for (int x = 2; x < nx - 2; ++x) {
                    const size_t center = voxelIndex(x, y, z, nx, ny);
                    const float  value  = source[center];
                    if (std::fabs(value) > band) continue;
                    const Normal centerNormal = densityNormal(source, x, y, z, nx, ny, hx, hy, hz);
                    if (!centerNormal.valid) continue;

                    float dispersion = 0.f;
                    int   samples    = 0;
                    for (const auto& offset : offsets) {
                        const int sx = x + offset[0], sy = y + offset[1], sz = z + offset[2];
                        if (std::fabs(source[voxelIndex(sx, sy, sz, nx, ny)]) > band * 1.5f) continue;
                        const Normal neighbor = densityNormal(source, sx, sy, sz, nx, ny, hx, hy, hz);
                        if (!neighbor.valid) continue;
                        const float alignment = std::fabs(centerNormal.x * neighbor.x + centerNormal.y * neighbor.y +
                                                          centerNormal.z * neighbor.z);
                        dispersion += 1.f - std::clamp(alignment, 0.f, 1.f);
                        ++samples;
                    }
                    if (samples < 2) continue;
                    const float normalDispersion = std::clamp(std::sqrt(dispersion / float(samples)) * 1.8f, 0.f, 1.f);
                    if (normalDispersion <= 1e-4f) continue;

                    const float surfaceWeight  = 1.f - std::clamp(std::fabs(value) / band, 0.f, 1.f);
                    const float rateMultiplier = std::clamp(rateField[center], 0.25f, 2.5f);
                    const float retreat        = strength * cellScale * 0.045f * surfaceWeight * normalDispersion *
                                          rateMultiplier / float(iterations);
                    density[center] -= retreat;
                    if (affected[center] == 0) {
                        affected[center] = 1;
                        ++result.affectedVoxels;
                    }
                    result.maximumRetreat = std::max(result.maximumRetreat, retreat);
                    result.totalRetreat += retreat;
                    result.maximumNormalDispersion = std::max(result.maximumNormalDispersion, normalDispersion);
                    result.minimumRateMultiplier   = std::min(result.minimumRateMultiplier, rateMultiplier);
                    result.maximumRateMultiplier   = std::max(result.maximumRateMultiplier, rateMultiplier);
                }
            }
        }
    }
    return result;
}

}  // namespace eve::procgen

// CaveWaterTable.cpp
namespace eve::procgen {
namespace {

float smoothstep(float lo, float hi, float value) {
    const float t = std::clamp((value - lo) / std::max(hi - lo, 1e-6f), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

CaveWaterTableSample sampleCaveWaterTableCorrosion(const CaveWaterTableInput& input) {
    CaveWaterTableSample result;
    // passageAngle is zero at the crown and +/- pi/2 at the walls. Restricting
    // retreat to sidewalls prevents a horizontal density slice from cutting floors.
    result.sidewallMask       = std::pow(std::fabs(std::sin(input.passageAngle)), 1.45f);
    const float seedPhase     = float(input.seed % 1009u) * 0.006227141f;
    const float halfThickness = 0.022f + input.fluctuation * 0.035f;
    for (int stage = 0; stage < input.stages; ++stage) {
        const float nominalLevel = input.level - float(stage) * input.stageDrop;
        // Long-wavelength variation retains a recognizably level belt without making
        // it an artificial perfectly planar cut through every chamber.
        const float localLevel =
            nominalLevel + std::sin(input.along * 1.7f + seedPhase + float(stage) * 1.91f) * halfThickness * 0.22f;
        const float verticalDistance = std::fabs(input.y - localLevel);
        const float band             = 1.f - smoothstep(halfThickness, halfThickness * 2.6f, verticalDistance);
        const float preservation     = std::pow(0.78f, float(stage));
        const float erosion          = band * preservation * result.sidewallMask;
        if (erosion > result.erosion) {
            result.erosion       = erosion;
            result.dominantStage = stage;
            result.dominantLevel = nominalLevel;
        }
    }
    return result;
}

}  // namespace eve::procgen

// CaveWetness.cpp
namespace eve::procgen {
namespace {

struct WetVertex {
    float px = 0.f, py = 0.f, pz = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
    float u = 0.f, v = 0.f;
    float wetness = 0.f;
};

uint32_t hash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float latticeNoise(float x, float y, float z, uint32_t seed) {
    const int   ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    const float fx = x - float(ix), fy = y - float(iy), fz = z - float(iz);
    auto        smooth = [](float t) { return t * t * (3.f - 2.f * t); };
    auto        sample = [seed](int sx, int sy, int sz) {
        const uint32_t h = hash(uint32_t(sx) * 73856093u ^ uint32_t(sy) * 19349663u ^ uint32_t(sz) * 83492791u ^ seed);
        return float(h & 0xffffu) / 32767.5f - 1.f;
    };
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float ux = smooth(fx), uy = smooth(fy), uz = smooth(fz);
    const float x00 = lerp(sample(ix, iy, iz), sample(ix + 1, iy, iz), ux);
    const float x10 = lerp(sample(ix, iy + 1, iz), sample(ix + 1, iy + 1, iz), ux);
    const float x01 = lerp(sample(ix, iy, iz + 1), sample(ix + 1, iy, iz + 1), ux);
    const float x11 = lerp(sample(ix, iy + 1, iz + 1), sample(ix + 1, iy + 1, iz + 1), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

WetVertex interpolate(WetVertex a, WetVertex b) {
    const float denominator = a.wetness - b.wetness;
    const float t           = std::fabs(denominator) > 1e-12f ? std::clamp(a.wetness / denominator, 0.f, 1.f) : 0.5f;
    auto        lerp        = [t](float from, float to) { return from + (to - from) * t; };
    return {lerp(a.px, b.px), lerp(a.py, b.py), lerp(a.pz, b.pz),
            lerp(a.nx, b.nx), lerp(a.ny, b.ny), lerp(a.nz, b.nz),
            lerp(a.u, b.u),   lerp(a.v, b.v),   0.f};
}

std::vector<WetVertex> clip(const WetVertex (&triangle)[3], bool keepWet) {
    std::vector<WetVertex> input{triangle, triangle + 3};
    std::vector<WetVertex> output;
    output.reserve(4);
    for (size_t i = 0; i < input.size(); ++i) {
        const WetVertex current       = input[i];
        const WetVertex next          = input[(i + 1) % input.size()];
        const bool      currentInside = keepWet ? current.wetness >= 0.f : current.wetness <= 0.f;
        const bool      nextInside    = keepWet ? next.wetness >= 0.f : next.wetness <= 0.f;
        if (currentInside) output.push_back(current);
        if (currentInside != nextInside) output.push_back(interpolate(current, next));
    }
    return output;
}

void emitPolygon(MeshBuild& mesh, const std::vector<WetVertex>& polygon, const std::string& group) {
    if (polygon.size() < 3) return;
    mesh.setActiveGroup(group);
    for (size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
        const uint32_t base = uint32_t(mesh.getVertexCount());
        for (const WetVertex& vertex : {polygon[0], polygon[corner], polygon[corner + 1]})
            mesh.addVertex(vertex.px, vertex.py, vertex.pz, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v);
        mesh.addTriangle(base, base + 1, base + 2);
    }
}

}  // namespace

float caveWetnessField(CaveHydrologyVec3 point, const std::vector<CaveHydrologyPoint>& drainageSpine,
                       float fallbackRadius, uint32_t seed) {
    float nearestDistance2 = 1e9f;
    float nearestY         = 0.f;
    float nearestRadius    = fallbackRadius;
    for (size_t i = 1; i < drainageSpine.size(); ++i) {
        const CaveHydrologyVec3 start = drainageSpine[i - 1].position;
        const CaveHydrologyVec3 end   = drainageSpine[i].position;
        const CaveHydrologyVec3 segment{end.x - start.x, end.y - start.y, end.z - start.z};
        const float             length2 = segment.x * segment.x + segment.y * segment.y + segment.z * segment.z;
        const CaveHydrologyVec3 relative{point.x - start.x, point.y - start.y, point.z - start.z};
        const float             t =
            length2 > 1e-8f
                            ? std::clamp((relative.x * segment.x + relative.y * segment.y + relative.z * segment.z) / length2, 0.f,
                                         1.f)
                            : 0.f;
        const CaveHydrologyVec3 nearest{start.x + segment.x * t, start.y + segment.y * t, start.z + segment.z * t};
        const float             dx = point.x - nearest.x, dy = point.y - nearest.y, dz = point.z - nearest.z;
        const float             distance2 = dx * dx + dy * dy + dz * dz;
        if (distance2 < nearestDistance2) {
            nearestDistance2 = distance2;
            nearestY         = nearest.y;
            nearestRadius = drainageSpine[i - 1].radius + (drainageSpine[i].radius - drainageSpine[i - 1].radius) * t;
        }
    }
    const float radius2       = std::max(nearestRadius * nearestRadius, 1e-8f);
    const float drainage      = (radius2 * 6.25f - nearestDistance2) / (radius2 * 6.25f);
    const float gravity       = (nearestY - nearestRadius * 0.22f - point.y) / std::max(nearestRadius, 1e-4f);
    const float surfaceAccess = latticeNoise(point.x * 4.f, point.y * 2.f, point.z * 4.f, seed ^ 0x68e31da4u) + 0.32f;
    return std::min({drainage, gravity, surfaceAccess});
}

CaveWetnessRefinement refineCaveWetnessBoundary(MeshBuild& mesh, const std::vector<CaveHydrologyPoint>& drainageSpine,
                                                float fallbackRadius, uint32_t seed, bool splitBoundary) {
    MeshBuild refined;
    refined.reserve(mesh.getVertexCount() * 2, mesh.getIndexCount() * 2);
    refined.setActiveGroup("caveWalls");
    refined.setActiveGroup("speleothems");
    refined.setActiveGroup("wetWalls");
    for (int group = 0; group < mesh.getGroupCount(); ++group) {
        const std::string name = mesh.getGroupName(group);
        if (name != "caveWalls" && name != "wetWalls" && name != "speleothems") refined.setActiveGroup(name);
    }
    CaveWetnessRefinement result;
    for (int triangleIndex = 0; triangleIndex < mesh.getIndexCount() / 3; ++triangleIndex) {
        WetVertex triangle[3];
        for (int corner = 0; corner < 3; ++corner) {
            const int               vertex = mesh.getIndex(triangleIndex * 3 + corner);
            const CaveHydrologyVec3 point{mesh.getPositionX(vertex) * 2.f, mesh.getPositionY(vertex) * 2.f,
                                          mesh.getPositionZ(vertex) * 2.f};
            triangle[corner] = {mesh.getPositionX(vertex),
                                mesh.getPositionY(vertex),
                                mesh.getPositionZ(vertex),
                                mesh.getNormalX(vertex),
                                mesh.getNormalY(vertex),
                                mesh.getNormalZ(vertex),
                                mesh.getUvU(vertex),
                                mesh.getUvV(vertex),
                                caveWetnessField(point, drainageSpine, fallbackRadius, seed)};
        }
        const std::string sourceGroup = mesh.getGroupName(mesh.getTriangleGroup(triangleIndex));
        if (sourceGroup != "caveWalls" && sourceGroup != "wetWalls") {
            emitPolygon(refined, {triangle[0], triangle[1], triangle[2]}, sourceGroup);
            continue;
        }
        const bool anyWet = triangle[0].wetness >= 0.f || triangle[1].wetness >= 0.f || triangle[2].wetness >= 0.f;
        const bool anyDry = triangle[0].wetness < 0.f || triangle[1].wetness < 0.f || triangle[2].wetness < 0.f;
        if (!splitBoundary || !anyWet || !anyDry) {
            const CaveHydrologyVec3 center{(triangle[0].px + triangle[1].px + triangle[2].px) * (2.f / 3.f),
                                           (triangle[0].py + triangle[1].py + triangle[2].py) * (2.f / 3.f),
                                           (triangle[0].pz + triangle[1].pz + triangle[2].pz) * (2.f / 3.f)};
            const bool              wet = caveWetnessField(center, drainageSpine, fallbackRadius, seed) > 0.f;
            emitPolygon(refined, {triangle[0], triangle[1], triangle[2]}, wet ? "wetWalls" : "caveWalls");
            continue;
        }
        ++result.boundaryTriangles;
        const std::vector<WetVertex> wet = clip(triangle, true);
        const std::vector<WetVertex> dry = clip(triangle, false);
        const int emitted = int(wet.size() >= 3 ? wet.size() - 2 : 0) + int(dry.size() >= 3 ? dry.size() - 2 : 0);
        result.addedTriangles += emitted - 1;
        emitPolygon(refined, wet, "wetWalls");
        emitPolygon(refined, dry, "caveWalls");
    }
    mesh = std::move(refined);
    return result;
}

}  // namespace eve::procgen
