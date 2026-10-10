#include "procgen/road/RoadBake.h"

#include "common/Diagnostic.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen::road {
namespace {

template <class T>
Result<T> terrainFail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.road"));
}

}  // namespace

Result<RoadTerrainConformReceipt> conformHeightmapToRoadWithReceipt(
    Heightmap& terrain, const MeshBuild& roadMesh, const RoadTerrainConformOptions& options) {
    if (terrain.getWidth() <= 0 || terrain.getHeight() <= 0 || roadMesh.getIndexCount() % 3 != 0 ||
        !std::isfinite(options.originX) || !std::isfinite(options.originZ) || !std::isfinite(options.cellSize) ||
        options.cellSize <= 0.f || !std::isfinite(options.heightScale) || options.heightScale <= 0.f ||
        !std::isfinite(options.blendDistance) || options.blendDistance < 0.f ||
        !std::isfinite(options.maxVerticalDelta) || options.maxVerticalDelta < 0.f || options.maximumWork == 0u)
        return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvalidArgument,
                                                      "road terrain conformance options are invalid", "terrain");
    if (options.blendDistance / options.cellSize > 256.f)
        return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvalidArgument,
                                                      "road terrain blend radius exceeds 256 cells",
                                                      "terrain.blendDistance");

    const int  width = terrain.getWidth(), height = terrain.getHeight();
    const auto sampleCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> target(sampleCount, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> targetDelta(sampleCount, std::numeric_limits<float>::max());
    std::vector<std::uint8_t> skippedBridge(sampleCount, 0u), skippedTunnel(sampleCount, 0u);
    std::size_t        work = 0u;
    const int          triangleCount = roadMesh.getIndexCount() / 3;
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        const int group = roadMesh.getTriangleGroup(triangle);
        if (group < 0 || roadMesh.getGroupName(group) != "asphalt") continue;
        const int ia = roadMesh.getIndex(triangle * 3), ib = roadMesh.getIndex(triangle * 3 + 1);
        const int ic = roadMesh.getIndex(triangle * 3 + 2);
        if (ia < 0 || ib < 0 || ic < 0 || ia >= roadMesh.getVertexCount() || ib >= roadMesh.getVertexCount() ||
            ic >= roadMesh.getVertexCount())
            return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvariantViolation,
                                                          "road mesh contains an invalid triangle index", "roadMesh");
        const float ax = roadMesh.getPositionX(ia), ay = roadMesh.getPositionY(ia), az = roadMesh.getPositionZ(ia);
        const float bx = roadMesh.getPositionX(ib), by = roadMesh.getPositionY(ib), bz = roadMesh.getPositionZ(ib);
        const float cx = roadMesh.getPositionX(ic), cy = roadMesh.getPositionY(ic), cz = roadMesh.getPositionZ(ic);
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
            !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(cx) || !std::isfinite(cy) ||
            !std::isfinite(cz))
            return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvariantViolation,
                                                          "road mesh contains non-finite positions", "roadMesh");
        const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(denominator) <= 1e-7f) continue;
        const int minX = std::max(0, static_cast<int>(std::floor((std::min({ax, bx, cx}) - options.originX) /
                                                                 options.cellSize)));
        const int maxX = std::min(width - 1, static_cast<int>(std::ceil((std::max({ax, bx, cx}) - options.originX) /
                                                                       options.cellSize)));
        const int minZ = std::max(0, static_cast<int>(std::floor((std::min({az, bz, cz}) - options.originZ) /
                                                                 options.cellSize)));
        const int maxZ = std::min(height - 1, static_cast<int>(std::ceil((std::max({az, bz, cz}) - options.originZ) /
                                                                        options.cellSize)));
        if (minX > maxX || minZ > maxZ) continue;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (++work > options.maximumWork)
                    return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::PreconditionViolation,
                                                                  "road terrain rasterization exceeds the work budget",
                                                                  "terrain");
                const float worldX = options.originX + static_cast<float>(x) * options.cellSize;
                const float worldZ = options.originZ + static_cast<float>(z) * options.cellSize;
                const float wa = ((bz - cz) * (worldX - cx) + (cx - bx) * (worldZ - cz)) / denominator;
                const float wb = ((cz - az) * (worldX - cx) + (ax - cx) * (worldZ - cz)) / denominator;
                const float wc = 1.f - wa - wb;
                if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
                const float roadY = wa * ay + wb * by + wc * cy;
                const auto  index = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x);
                const float terrainY = terrain.height(x, z) * options.heightScale;
                const float delta = std::fabs(roadY - terrainY);
                if (delta <= options.maxVerticalDelta && delta < targetDelta[index]) {
                    target[index]      = roadY;
                    targetDelta[index] = delta;
                } else if (delta > options.maxVerticalDelta) {
                    if (roadY > terrainY)
                        skippedBridge[index] = 1u;
                    else
                        skippedTunnel[index] = 1u;
                }
            }
        }
    }

    std::vector<std::size_t> covered;
    for (std::size_t i = 0; i < target.size(); ++i)
        if (std::isfinite(target[i])) covered.push_back(i);
    RoadTerrainConformReceipt receipt;
    receipt.width  = width;
    receipt.height = height;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        if (std::isfinite(target[index])) continue;
        receipt.skippedBridgeSamples += skippedBridge[index] != 0u;
        receipt.skippedTunnelSamples += skippedTunnel[index] != 0u;
    }
    if (covered.empty()) return Result<RoadTerrainConformReceipt>::success(std::move(receipt));
    const int  blendCells = static_cast<int>(std::ceil(options.blendDistance / options.cellSize));
    const auto diameter   = static_cast<std::size_t>(blendCells * 2 + 1);
    if (covered.size() > options.maximumWork / (diameter * diameter))
        return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::PreconditionViolation,
                                                      "road terrain blending exceeds the work budget", "terrain");

    std::vector<float> weights(sampleCount, 0.f), blendedTarget(sampleCount, 0.f);
    for (const auto sourceIndex : covered) {
        const int sourceX = static_cast<int>(sourceIndex % static_cast<std::size_t>(width));
        const int sourceZ = static_cast<int>(sourceIndex / static_cast<std::size_t>(width));
        for (int dz = -blendCells; dz <= blendCells; ++dz) {
            for (int dx = -blendCells; dx <= blendCells; ++dx) {
                const int x = sourceX + dx, z = sourceZ + dz;
                if (x < 0 || z < 0 || x >= width || z >= height) continue;
                const float distance = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                if (distance > static_cast<float>(blendCells)) continue;
                const float weight = blendCells == 0 ? 1.f : 1.f - distance / (static_cast<float>(blendCells) + 0.5f);
                const auto index = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x);
                if (weight > weights[index]) {
                    weights[index]       = weight;
                    blendedTarget[index] = target[sourceIndex];
                }
            }
        }
    }

    Heightmap candidate = terrain;
    receipt.sampleIndices.reserve(covered.size());
    receipt.before.reserve(covered.size());
    receipt.after.reserve(covered.size());
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x);
            if (weights[index] <= 0.f) continue;
            const float oldHeight = terrain.height(x, z);
            const float newHeight =
                oldHeight + (blendedTarget[index] / options.heightScale - oldHeight) * weights[index];
            if (newHeight != oldHeight) {
                candidate.setHeight(x, z, newHeight);
                receipt.sampleIndices.push_back(index);
                receipt.before.push_back(oldHeight);
                receipt.after.push_back(newHeight);
            }
        }
    }
    terrain = std::move(candidate);
    return Result<RoadTerrainConformReceipt>::success(std::move(receipt));
}

Result<int> conformHeightmapToRoad(Heightmap& terrain, const MeshBuild& roadMesh,
                                   const RoadTerrainConformOptions& options) {
    auto receipt = conformHeightmapToRoadWithReceipt(terrain, roadMesh, options);
    if (!receipt.ok()) return Result<int>::failure(receipt.status());
    return Result<int>::success(static_cast<int>(receipt.value().sampleIndices.size()));
}

Result<int> restoreHeightmapFromRoad(Heightmap& terrain, const RoadTerrainConformReceipt& receipt) {
    if (terrain.getWidth() != receipt.width || terrain.getHeight() != receipt.height || receipt.width <= 0 ||
        receipt.height <= 0 || receipt.sampleIndices.size() != receipt.before.size() ||
        receipt.sampleIndices.size() != receipt.after.size())
        return terrainFail<int>(DiagnosticCode::InvalidArgument, "road terrain receipt shape is invalid", "receipt");
    const auto sampleCount = static_cast<std::size_t>(receipt.width) * static_cast<std::size_t>(receipt.height);
    std::size_t previous = 0;
    for (std::size_t i = 0; i < receipt.sampleIndices.size(); ++i) {
        const auto index = receipt.sampleIndices[i];
        if (index >= sampleCount || (i > 0 && index <= previous) || !std::isfinite(receipt.before[i]) ||
            !std::isfinite(receipt.after[i]))
            return terrainFail<int>(DiagnosticCode::InvalidArgument, "road terrain receipt samples are invalid",
                                    "receipt.samples");
        previous    = index;
        const int x = static_cast<int>(index % static_cast<std::size_t>(receipt.width));
        const int z = static_cast<int>(index / static_cast<std::size_t>(receipt.width));
        if (terrain.height(x, z) != receipt.after[i])
            return terrainFail<int>(DiagnosticCode::Conflict,
                                    "road terrain receipt is stale and would overwrite a newer edit", "terrain");
    }
    Heightmap candidate = terrain;
    for (std::size_t i = 0; i < receipt.sampleIndices.size(); ++i) {
        const auto index = receipt.sampleIndices[i];
        const int  x     = static_cast<int>(index % static_cast<std::size_t>(receipt.width));
        const int  z     = static_cast<int>(index / static_cast<std::size_t>(receipt.width));
        candidate.setHeight(x, z, receipt.before[i]);
    }
    terrain = std::move(candidate);
    return Result<int>::success(static_cast<int>(receipt.sampleIndices.size()));
}

}  // namespace eve::procgen::road
