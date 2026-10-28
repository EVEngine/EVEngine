#include "procgen/algorithms/CaveNormals.h"

#include "procgen/MeshBuild.h"
#include "procgen/algorithms/CaveFieldSampling.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace eve::procgen {
namespace {

struct VertexKey {
    int x = 0, y = 0, z = 0, island = 0;

    bool operator==(const VertexKey& other) const {
        return x == other.x && y == other.y && z == other.z && island == other.island;
    }
};

uint32_t hashCoordinate(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const {
        size_t value = size_t(hashCoordinate(uint32_t(key.x)));
        value ^= size_t(hashCoordinate(uint32_t(key.y))) << 1u;
        value ^= size_t(hashCoordinate(uint32_t(key.z))) << 2u;
        value ^= size_t(key.island) * 0x9e3779b9u;
        return value;
    }
};

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 normalized(Vec3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return length > 1e-8f ? Vec3{value.x / length, value.y / length, value.z / length} : Vec3{};
}

}  // namespace

CaveNormalStatus applyCaveSurfaceNormals(MeshBuild& mesh, const std::vector<float>& density, int nx, int ny, int nz,
                                         float width, float height, float depth, const std::string& mode, float blend,
                                         std::string& error) {
    if (mode != "faceAverage" && mode != "densityGradient") {
        error = "mesh.cave: unknown surfaceNormalMode '" + mode + "' (use faceAverage|densityGradient)";
        return CaveNormalStatus::unknownMode;
    }

    auto& normals = mesh.normals();
    if (mode == "densityGradient") {
        for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
            const CaveFieldPoint point{mesh.getPositionX(vertex) * 2.f, mesh.getPositionY(vertex) * 2.f,
                                       mesh.getPositionZ(vertex) * 2.f};
            const CaveFieldPoint gradient = sampleCaveDensityGradient(density, nx, ny, nz, point);
            const Vec3   fieldNormal = normalized({-gradient.x / width, -gradient.y / height, -gradient.z / depth});
            const size_t offset      = size_t(vertex) * 3u;
            const Vec3   faceNormal =
                normalized({normals[offset] / width, normals[offset + 1] / height, normals[offset + 2] / depth});
            const Vec3 blended = normalized({faceNormal.x + (fieldNormal.x - faceNormal.x) * blend,
                                             faceNormal.y + (fieldNormal.y - faceNormal.y) * blend,
                                             faceNormal.z + (fieldNormal.z - faceNormal.z) * blend});
            if (blended.x != 0.f || blended.y != 0.f || blended.z != 0.f) {
                normals[offset]     = blended.x;
                normals[offset + 1] = blended.y;
                normals[offset + 2] = blended.z;
            }
        }
        return CaveNormalStatus::applied;
    }

    std::unordered_map<VertexKey, Vec3, VertexKeyHash> normalSums;
    normalSums.reserve(size_t(mesh.getVertexCount()));
    auto vertexKey = [&](int vertex) {
        const int triangle = vertex / 3;
        // Wet and dry limestone are one continuous surface.  Secondary calcite
        // deposits keep a separate smoothing island at their material seam.
        const int island = mesh.getTriangleGroup(triangle) == 1 ? 1 : 0;
        return VertexKey{int(std::lround(mesh.getPositionX(vertex) * 10000.f)),
                         int(std::lround(mesh.getPositionY(vertex) * 10000.f)),
                         int(std::lround(mesh.getPositionZ(vertex) * 10000.f)), island};
    };
    for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
        const size_t offset = size_t(vertex) * 3u;
        const Vec3   worldNormal =
            normalized({normals[offset] / width, normals[offset + 1] / height, normals[offset + 2] / depth});
        normals[offset]     = worldNormal.x;
        normals[offset + 1] = worldNormal.y;
        normals[offset + 2] = worldNormal.z;
        Vec3& sum           = normalSums[vertexKey(vertex)];
        sum.x += worldNormal.x;
        sum.y += worldNormal.y;
        sum.z += worldNormal.z;
    }
    for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
        const Vec3 average = normalized(normalSums[vertexKey(vertex)]);
        if (average.x == 0.f && average.y == 0.f && average.z == 0.f) continue;
        const size_t offset   = size_t(vertex) * 3u;
        const Vec3   smoothed = normalized({normals[offset] + (average.x - normals[offset]) * blend,
                                            normals[offset + 1] + (average.y - normals[offset + 1]) * blend,
                                            normals[offset + 2] + (average.z - normals[offset + 2]) * blend});
        normals[offset]       = smoothed.x;
        normals[offset + 1]   = smoothed.y;
        normals[offset + 2]   = smoothed.z;
    }
    return CaveNormalStatus::applied;
}

}  // namespace eve::procgen
