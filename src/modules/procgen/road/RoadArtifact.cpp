#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadBakeInternal.h"
#include "procgen/road/RoadRecipes.h"

// RoadArtifact.cpp
namespace eve::procgen {
namespace {

Bounds meshBounds(const MeshBuild& mesh) {
    Bounds bounds;
    for (int i = 0; i < mesh.getVertexCount(); ++i)
        bounds.include(mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i));
    return bounds;
}

Bounds pointBounds(const PointSet& points) {
    Bounds bounds;
    for (const auto& point : points.points()) bounds.include(point.x, point.y, point.z);
    return bounds;
}

Result<BuildKey> childKey(const BuildKey& parent, std::string_view role) {
    auto key = BuildKey::fromCanonical(parent.format() + "/" + std::string(role));
    if (!key)
        return Result<BuildKey>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "cannot derive road artifact child key", "role"));
    return Result<BuildKey>::success(std::move(*key));
}

bool isStructuralColliderGroup(std::string_view group) {
    return group != "marking" && group != "markingYellow" && group != "nav";
}

bool isDrivableColliderGroup(std::string_view group) { return group == "asphalt"; }

template <class IncludeGroup>
Collider roadCollider(const MeshBuild& mesh, IncludeGroup&& includeGroup) {
    Collider                                         result;
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    const int                                        triangleCount = mesh.getIndexCount() / 3;
    result.indices.reserve(mesh.indices().size());
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        const int group = mesh.getTriangleGroup(triangle);
        if (group < 0 || !includeGroup(mesh.getGroupName(group))) continue;
        for (int corner = 0; corner < 3; ++corner) {
            const auto    source      = static_cast<std::uint32_t>(mesh.getIndex(triangle * 3 + corner));
            const auto    found       = remap.find(source);
            std::uint32_t destination = 0;
            if (found == remap.end()) {
                destination = static_cast<std::uint32_t>(result.vertices.size() / 3u);
                remap.emplace(source, destination);
                const int   sourceIndex = static_cast<int>(source);
                const float x           = mesh.getPositionX(sourceIndex);
                const float y           = mesh.getPositionY(sourceIndex);
                const float z           = mesh.getPositionZ(sourceIndex);
                result.vertices.insert(result.vertices.end(), {x, y, z});
                result.bounds.include(x, y, z);
            } else {
                destination = found->second;
            }
            result.indices.push_back(destination);
        }
    }
    return result;
}

struct RoadChunkCoordinate {
    int         x   = 0;
    int         z   = 0;
    int         lod = 0;
    friend bool operator<(const RoadChunkCoordinate& lhs, const RoadChunkCoordinate& rhs) noexcept {
        if (lhs.lod != rhs.lod) return lhs.lod < rhs.lod;
        if (lhs.z != rhs.z) return lhs.z < rhs.z;
        return lhs.x < rhs.x;
    }
};

struct RoadChunkAccumulator {
    MeshBuild                                        mesh;
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    std::vector<float>                               colors;
};

std::uint64_t hashChunkMesh(const MeshBuild& mesh) {
    std::uint64_t hash   = 14695981039346656037ull;
    auto          append = [&](std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= static_cast<std::uint8_t>((value >> (byte * 8)) & 0xffu);
            hash *= 1099511628211ull;
        }
    };
    for (float value : mesh.positions()) append(std::bit_cast<std::uint32_t>(value));
    for (float value : mesh.normals()) append(std::bit_cast<std::uint32_t>(value));
    for (float value : mesh.uvs()) append(std::bit_cast<std::uint32_t>(value));
    for (float value : mesh.colors()) append(std::bit_cast<std::uint32_t>(value));
    for (std::uint32_t value : mesh.indices()) append(value);
    for (int value : mesh.triangleGroups()) append(static_cast<std::uint32_t>(value));
    for (const auto& name : mesh.groupNames())
        for (const unsigned char byte : name) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    return hash;
}

Result<std::map<RoadChunkCoordinate, MeshBuild>> splitRoadMeshIntoChunks(const MeshBuild& source, float chunkSize,
                                                                         int lod) {
    std::map<RoadChunkCoordinate, RoadChunkAccumulator> accumulators;
    const int                                           triangleCount = source.getIndexCount() / 3;
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        const int                 ia = source.getIndex(triangle * 3);
        const int                 ib = source.getIndex(triangle * 3 + 1);
        const int                 ic = source.getIndex(triangle * 3 + 2);
        const float centerX = (source.getPositionX(ia) + source.getPositionX(ib) + source.getPositionX(ic)) / 3.f;
        const float centerZ = (source.getPositionZ(ia) + source.getPositionZ(ib) + source.getPositionZ(ic)) / 3.f;
        const RoadChunkCoordinate coordinate{static_cast<int>(std::floor(centerX / chunkSize)),
                                             static_cast<int>(std::floor(centerZ / chunkSize)), lod};
        auto&                     accumulator = accumulators[coordinate];
        const int                 sourceGroup = source.getTriangleGroup(triangle);
        accumulator.mesh.setActiveGroup(sourceGroup >= 0 ? source.getGroupName(sourceGroup) : "default");
        std::uint32_t destination[3]{};
        const int     sourceIndices[3] = {ia, ib, ic};
        for (int corner = 0; corner < 3; ++corner) {
            const auto sourceIndex = static_cast<std::uint32_t>(sourceIndices[corner]);
            const auto found       = accumulator.remap.find(sourceIndex);
            if (found != accumulator.remap.end()) {
                destination[corner] = found->second;
                continue;
            }
            destination[corner] = static_cast<std::uint32_t>(accumulator.mesh.getVertexCount());
            accumulator.remap.emplace(sourceIndex, destination[corner]);
            const int vertex = static_cast<int>(sourceIndex);
            accumulator.mesh.addVertex(source.getPositionX(vertex), source.getPositionY(vertex),
                                       source.getPositionZ(vertex), source.getNormalX(vertex),
                                       source.getNormalY(vertex), source.getNormalZ(vertex), source.getUvU(vertex),
                                       source.getUvV(vertex));
            if (source.hasVertexColors())
                for (int component = 0; component < 4; ++component)
                    accumulator.colors.push_back(source.getColor(vertex, component));
        }
        accumulator.mesh.addTriangle(destination[0], destination[1], destination[2]);
    }

    std::map<RoadChunkCoordinate, MeshBuild> chunks;
    for (auto& [coordinate, accumulator] : accumulators) {
        if (!accumulator.colors.empty()) {
            auto colors = accumulator.mesh.setVertexColors(std::move(accumulator.colors));
            if (!colors.ok()) return Result<std::map<RoadChunkCoordinate, MeshBuild>>::failure(colors.status());
        }
        chunks.emplace(coordinate, std::move(accumulator.mesh));
    }
    return Result<std::map<RoadChunkCoordinate, MeshBuild>>::success(std::move(chunks));
}

Result<ArtifactPart> roadChunkPart(std::string role, ArtifactId root, MeshBuild mesh, RoadChunkCoordinate coordinate) {
    std::ostringstream canonical;
    canonical << "eve.procgen.road.chunk.v1/" << role << '/' << std::hex << std::setfill('0') << std::setw(16)
              << hashChunkMesh(mesh);
    auto key = BuildKey::fromCanonical(canonical.str());
    if (!key)
        return Result<ArtifactPart>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "cannot derive road chunk content key", role));
    eve::Value::Object metadata;
    metadata.emplace("role", eve::Value(role));
    metadata.emplace("lod", eve::Value(static_cast<std::int64_t>(coordinate.lod)));
    metadata.emplace("chunkX", eve::Value(static_cast<std::int64_t>(coordinate.x)));
    metadata.emplace("chunkZ", eve::Value(static_cast<std::int64_t>(coordinate.z)));
    const ArtifactId partId = root.child(role);
    const Bounds     bounds = meshBounds(mesh);
    return makeArtifactPart(std::move(role), partId, ArtifactType::MeshData, eve::SchemaVersion(1), std::move(*key),
                            bounds, {root.child("mesh")}, std::move(metadata), std::move(mesh));
}

float edgeSign(float ax, float az, float bx, float bz, float px, float pz) {
    return (px - bx) * (az - bz) - (ax - bx) * (pz - bz);
}

bool pointInTriangleXZ(float px, float pz, float ax, float az, float bx, float bz, float cx, float cz) {
    const bool negative = edgeSign(ax, az, bx, bz, px, pz) < 0.f || edgeSign(bx, bz, cx, cz, px, pz) < 0.f ||
                          edgeSign(cx, cz, ax, az, px, pz) < 0.f;
    const bool positive = edgeSign(ax, az, bx, bz, px, pz) > 0.f || edgeSign(bx, bz, cx, cz, px, pz) > 0.f ||
                          edgeSign(cx, cz, ax, az, px, pz) > 0.f;
    return !(negative && positive);
}

Grid2D roadFootprint(const MeshBuild& mesh, const Bounds& bounds, float requestedCellSize, float padding = 0.f) {
    Grid2D grid;
    if (!bounds.isValid()) return grid;
    const float originX  = bounds.minX - padding;
    const float originZ  = bounds.minZ - padding;
    const float extentX  = std::max(requestedCellSize, bounds.maxX - bounds.minX + padding * 2.f);
    const float extentZ  = std::max(requestedCellSize, bounds.maxZ - bounds.minZ + padding * 2.f);
    const float cellSize = std::max(requestedCellSize, std::max(extentX, extentZ) / 512.f);
    const int   width    = std::clamp(static_cast<int>(std::ceil(extentX / cellSize)), 1, 512);
    const int   height   = std::clamp(static_cast<int>(std::ceil(extentZ / cellSize)), 1, 512);
    grid.resize(width, height);
    grid.fill(0);
    grid.setMeta("originX", std::to_string(originX));
    grid.setMeta("originZ", std::to_string(originZ));
    grid.setMeta("cellSize", std::to_string(cellSize));
    grid.setMeta("semantics", "road_footprint");

    const int triangleCount = mesh.getIndexCount() / 3;
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        const int group = mesh.getTriangleGroup(triangle);
        if (group < 0) continue;
        const std::string name = mesh.getGroupName(group);
        if (name != "asphalt" && name != "sidewalk" && name != "curb") continue;
        const int   ia = mesh.getIndex(triangle * 3);
        const int   ib = mesh.getIndex(triangle * 3 + 1);
        const int   ic = mesh.getIndex(triangle * 3 + 2);
        const float ax = mesh.getPositionX(ia), az = mesh.getPositionZ(ia);
        const float bx = mesh.getPositionX(ib), bz = mesh.getPositionZ(ib);
        const float cx = mesh.getPositionX(ic), cz = mesh.getPositionZ(ic);
        const int   minX =
            std::clamp(static_cast<int>(std::floor((std::min({ax, bx, cx}) - originX) / cellSize)), 0, width - 1);
        const int maxX =
            std::clamp(static_cast<int>(std::floor((std::max({ax, bx, cx}) - originX) / cellSize)), 0, width - 1);
        const int minZ =
            std::clamp(static_cast<int>(std::floor((std::min({az, bz, cz}) - originZ) / cellSize)), 0, height - 1);
        const int maxZ =
            std::clamp(static_cast<int>(std::floor((std::max({az, bz, cz}) - originZ) / cellSize)), 0, height - 1);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = originX + (static_cast<float>(x) + 0.5f) * cellSize;
                const float pz = originZ + (static_cast<float>(z) + 0.5f) * cellSize;
                if (pointInTriangleXZ(px, pz, ax, az, bx, bz, cx, cz)) grid.setCell(x, z, 1);
            }
        }
    }
    return grid;
}

Grid2D roadSurfaceWeights(Grid2D grid, float cellSize, float blendDistance) {
    const int          width = grid.getWidth(), height = grid.getHeight();
    const float        infinity = static_cast<float>(width + height + 1);
    std::vector<float> distance(static_cast<std::size_t>(width * height), infinity);
    const auto         index = [width](int x, int z) { return static_cast<std::size_t>(z * width + x); };
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x)
            if (grid.getCell(x, z) != 0) distance[index(x, z)] = 0.f;

    constexpr float diagonal = 1.41421356f;
    auto            relax    = [&](int x, int z, int nx, int nz, float cost) {
        if (nx < 0 || nz < 0 || nx >= width || nz >= height) return;
        distance[index(x, z)] = std::min(distance[index(x, z)], distance[index(nx, nz)] + cost);
    };
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            relax(x, z, x - 1, z, 1.f);
            relax(x, z, x, z - 1, 1.f);
            relax(x, z, x - 1, z - 1, diagonal);
            relax(x, z, x + 1, z - 1, diagonal);
        }
    }
    for (int z = height; z-- > 0;) {
        for (int x = width; x-- > 0;) {
            relax(x, z, x + 1, z, 1.f);
            relax(x, z, x, z + 1, 1.f);
            relax(x, z, x + 1, z + 1, diagonal);
            relax(x, z, x - 1, z + 1, diagonal);
        }
    }
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const float metres = distance[index(x, z)] * cellSize;
            const float weight = distance[index(x, z)] == 0.f
                                     ? 1.f
                                     : (blendDistance > 0.f ? std::clamp(1.f - metres / blendDistance, 0.f, 1.f) : 0.f);
            grid.setDetail(x, z, static_cast<int>(std::lround(weight * 255.f)));
        }
    }
    grid.setMeta("semantics", "road_surface_weight");
    grid.setMeta("weightChannel", "detail");
    grid.setMeta("weightScale", "255");
    grid.setMeta("blendDistance", std::to_string(blendDistance));
    return grid;
}

Result<ArtifactPart> roadPart(std::string role, ArtifactId root, const BuildKey& rootKey, ArtifactType type,
                              Bounds bounds, ArtifactLeafPayload payload, std::vector<ArtifactId> dependencies = {}) {
    auto key = childKey(rootKey, role);
    if (!key.ok()) return Result<ArtifactPart>::failure(key.status());
    eve::Value::Object metadata;
    metadata.emplace("role", eve::Value(role));
    return makeArtifactPart(role, root.child(role), type, eve::SchemaVersion(1), std::move(key).takeValue(), bounds,
                            std::move(dependencies), std::move(metadata), std::move(payload));
}

Result<void> appendNavigationParts(const road::RoadOverlay& overlay, ArtifactId root, const BuildKey& rootKey,
                                   std::vector<ArtifactPart>& parts) {
    auto append = [&](const road::RoadPolyline& line, std::string role) -> Result<void> {
        // Fully junction-trimmed lanes legitimately have no visible samples.
        // They carry no artifact data and must not invalidate the whole build.
        if (line.xyz.empty()) return Result<void>::success();
        if (line.xyz.size() % 3u != 0u)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "road overlay coordinates are not packed xyz triples", role));
        if (!std::all_of(line.xyz.begin(), line.xyz.end(), [](float value) { return std::isfinite(value); }))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "road overlay contains non-finite coordinates", role));
        PointSet points;
        points.reserve(line.xyz.size() / 3);
        for (std::size_t i = 0; i + 2 < line.xyz.size(); i += 3)
            points.add(line.xyz[i], line.xyz[i + 1], line.xyz[i + 2]);
        auto metadata = setPointDataIntAttribute(points, "in_edge", line.inEdge);
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "out_edge", line.outEdge);
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "in_lane", line.inLane);
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "out_lane", line.outLane);
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "in_direction", static_cast<int>(line.inDirection));
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "out_direction", static_cast<int>(line.outDirection));
        if (!metadata.ok()) return metadata;
        metadata = setPointDataIntAttribute(points, "traffic_priority", line.trafficPriority);
        if (!metadata.ok()) return metadata;
        auto speed = setPointDataFloatAttribute(points, "speed_limit_mps", line.speedLimitMps);
        if (!speed.ok()) return speed;
        // Compute before moving `points`: function argument evaluation order
        // must not decide whether the bounds see a moved-from PointSet.
        const Bounds bounds = pointBounds(points);
        auto         part =
            roadPart(role, root, rootKey, ArtifactType::PointSet, bounds, std::move(points), {root.child("mesh")});
        if (!part.ok()) return Result<void>::failure(part.status());
        parts.push_back(std::move(part).takeValue());
        return Result<void>::success();
    };
    for (std::size_t i = 0; i < overlay.lanes.size(); ++i) {
        auto added = append(overlay.lanes[i], "navigation/lane/" + std::to_string(i));
        if (!added.ok()) return added;
    }
    for (std::size_t i = 0; i < overlay.turns.size(); ++i) {
        auto added = append(overlay.turns[i], "navigation/turn/" + std::to_string(i));
        if (!added.ok()) return added;
    }
    return Result<void>::success();
}

}  // namespace

Result<GeneratedArtifact> generateRoadNetworkArtifact(const Params& params, ArtifactId id) {
    if (id.isNil())
        return Result<GeneratedArtifact>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "road artifact identity must not be nil", "id"));
    auto key = BuildKey::forRecipe("mesh.roadNetwork", params);
    if (!key)
        return Result<GeneratedArtifact>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "road parameters cannot form a build key", "params"));
    auto baked = road::bakeRoadNetworkRecipe(params);
    if (!baked.ok()) return Result<GeneratedArtifact>::failure(baked.status());

    road::RoadBakeResult result           = std::move(baked).takeValue();
    const Bounds         bounds           = meshBounds(result.mesh);
    Collider             collider         = roadCollider(result.mesh, isStructuralColliderGroup);
    Collider             drivableCollider = roadCollider(result.mesh, isDrivableColliderGroup);
    const float          chunkSize        = params.getFloat("chunkSize", 64.f);
    const int            lodCount         = params.getInt("lodCount", 3);
    if (!std::isfinite(chunkSize) || chunkSize < 4.f || chunkSize > 1024.f || lodCount < 1 || lodCount > 4)
        return Result<GeneratedArtifact>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "road chunkSize must be in [4,1024] and lodCount in [1,4]", "chunks"));
    std::vector<std::map<RoadChunkCoordinate, MeshBuild>> lodChunks;
    lodChunks.reserve(static_cast<std::size_t>(lodCount));
    auto primaryChunks = splitRoadMeshIntoChunks(result.mesh, chunkSize, 0);
    if (!primaryChunks.ok()) return Result<GeneratedArtifact>::failure(primaryChunks.status());
    lodChunks.push_back(std::move(primaryChunks).takeValue());
    const int basePathSegments = std::max(4, params.getInt("pathSegments", 48));
    const int baseTurnSamples  = std::max(4, params.getInt("turnSamples", 12));
    for (int lod = 1; lod < lodCount; ++lod) {
        Params lodParams = params;
        lodParams.setInt("pathSegments", std::max(4, basePathSegments >> lod));
        lodParams.setInt("turnSamples", std::max(4, baseTurnSamples >> lod));
        lodParams.setFloat("junctionChordError",
                           std::min(1.f, params.getFloat("junctionChordError", 0.10f) * static_cast<float>(1 << lod)));
        lodParams.setBool("navigation", false);
        lodParams.setBool("placements", false);
        auto lodBake = road::bakeRoadNetworkRecipe(lodParams);
        if (!lodBake.ok()) return Result<GeneratedArtifact>::failure(lodBake.status());
        auto chunks = splitRoadMeshIntoChunks(lodBake.value().mesh, chunkSize, lod);
        if (!chunks.ok()) return Result<GeneratedArtifact>::failure(chunks.status());
        lodChunks.push_back(std::move(chunks).takeValue());
    }
    const float maskCellSize         = std::max(0.1f, params.getFloat("maskCellSize", 1.f));
    const float terrainBlendDistance = params.getFloat("terrainBlendDistance", 2.f);
    if (!std::isfinite(terrainBlendDistance) || terrainBlendDistance < 0.f || terrainBlendDistance > 64.f)
        return Result<GeneratedArtifact>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "road terrain blend distance must be finite and in [0,64]", "terrainBlendDistance"));
    Grid2D      footprint       = roadFootprint(result.mesh, bounds, maskCellSize);
    Grid2D      surfaceWeights  = roadFootprint(result.mesh, bounds, maskCellSize, terrainBlendDistance + maskCellSize);
    const float surfaceCellSize = std::stof(surfaceWeights.getMeta("cellSize", "1"));
    surfaceWeights              = roadSurfaceWeights(std::move(surfaceWeights), surfaceCellSize, terrainBlendDistance);
    if (!bounds.isValid() || !collider.isValid() || !drivableCollider.isValid() || footprint.getWidth() <= 0 ||
        footprint.getHeight() <= 0 || surfaceWeights.getWidth() <= 0 || surfaceWeights.getHeight() <= 0)
        return Result<GeneratedArtifact>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "road bake produced incomplete artifact data"));

    std::vector<ArtifactPart> parts;
    std::size_t               chunkCount = 0;
    for (const auto& chunks : lodChunks) chunkCount += chunks.size();
    parts.reserve(6 + chunkCount + result.overlay.lanes.size() + result.overlay.turns.size());
    auto meshPart = roadPart("mesh", id, *key, ArtifactType::MeshData, bounds, std::move(result.mesh));
    if (!meshPart.ok()) return Result<GeneratedArtifact>::failure(meshPart.status());
    parts.push_back(std::move(meshPart).takeValue());
    auto colliderPart = roadPart("collider", id, *key, ArtifactType::Collider, collider.bounds, std::move(collider),
                                 {id.child("mesh")});
    if (!colliderPart.ok()) return Result<GeneratedArtifact>::failure(colliderPart.status());
    parts.push_back(std::move(colliderPart).takeValue());
    auto drivableColliderPart = roadPart("collider/drivable", id, *key, ArtifactType::Collider, drivableCollider.bounds,
                                         std::move(drivableCollider), {id.child("mesh")});
    if (!drivableColliderPart.ok()) return Result<GeneratedArtifact>::failure(drivableColliderPart.status());
    parts.push_back(std::move(drivableColliderPart).takeValue());
    for (auto& chunks : lodChunks) {
        for (auto& [coordinate, chunk] : chunks) {
            const std::string role = "render/chunk/" + std::to_string(coordinate.x) + "/" +
                                     std::to_string(coordinate.z) + "/lod/" + std::to_string(coordinate.lod);
            auto part = roadChunkPart(role, id, std::move(chunk), coordinate);
            if (!part.ok()) return Result<GeneratedArtifact>::failure(part.status());
            parts.push_back(std::move(part).takeValue());
        }
    }
    auto maskPart =
        roadPart("topology", id, *key, ArtifactType::Grid, bounds, std::move(footprint), {id.child("mesh")});
    if (!maskPart.ok()) return Result<GeneratedArtifact>::failure(maskPart.status());
    parts.push_back(std::move(maskPart).takeValue());
    const float surfacePadding = terrainBlendDistance + surfaceCellSize;
    Bounds      surfaceBounds  = bounds;
    surfaceBounds.include(bounds.minX - surfacePadding, bounds.minY, bounds.minZ - surfacePadding);
    surfaceBounds.include(bounds.maxX + surfacePadding, bounds.maxY, bounds.maxZ + surfacePadding);
    auto surfacePart = roadPart("terrain/surface-weight", id, *key, ArtifactType::Grid, surfaceBounds,
                                std::move(surfaceWeights), {id.child("mesh")});
    if (!surfacePart.ok()) return Result<GeneratedArtifact>::failure(surfacePart.status());
    parts.push_back(std::move(surfacePart).takeValue());
    if (!result.placements.empty()) {
        const Bounds placementBounds = pointBounds(result.placements);
        auto         placementPart   = roadPart("placements", id, *key, ArtifactType::PointSet, placementBounds,
                                                std::move(result.placements), {id.child("mesh")});
        if (!placementPart.ok()) return Result<GeneratedArtifact>::failure(placementPart.status());
        parts.push_back(std::move(placementPart).takeValue());
    }
    auto navigation = appendNavigationParts(result.overlay, id, *key, parts);
    if (!navigation.ok()) return Result<GeneratedArtifact>::failure(navigation.status());

    CompositeArtifact composite;
    composite.children = std::move(parts);
    eve::Value::Object metadata;
    metadata.emplace("algorithm", eve::Value("mesh.roadNetwork"));
    metadata.emplace("determinism", eve::Value("seeded_cpu"));
    metadata.emplace("terrainMaskRole", eve::Value("topology"));
    metadata.emplace("terrainSurfaceWeightRole", eve::Value("terrain/surface-weight"));
    metadata.emplace("roadChunkRolePrefix", eve::Value("render/chunk/"));
    metadata.emplace("drivableColliderRole", eve::Value("collider/drivable"));
    metadata.emplace("lodCount", eve::Value(static_cast<std::int64_t>(lodCount)));
    metadata.emplace("chunkSize", eve::Value(static_cast<double>(chunkSize)));
    return makeArtifact(id, ArtifactType::Composite, eve::SchemaVersion(2), *key, bounds, {}, std::move(metadata),
                        std::move(composite));
}

}  // namespace eve::procgen

// RoadTerrain.cpp
namespace eve::procgen::road {
namespace {

template <class T>
Result<T> terrainFail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.road"));
}

}  // namespace

Result<RoadTerrainConformReceipt> conformHeightmapToRoadWithReceipt(Heightmap& terrain, const MeshBuild& roadMesh,
                                                                    const RoadTerrainConformOptions& options) {
    if (terrain.getWidth() <= 0 || terrain.getHeight() <= 0 || roadMesh.getIndexCount() % 3 != 0 ||
        !std::isfinite(options.originX) || !std::isfinite(options.originZ) || !std::isfinite(options.cellSize) ||
        options.cellSize <= 0.f || !std::isfinite(options.heightScale) || options.heightScale <= 0.f ||
        !std::isfinite(options.blendDistance) || options.blendDistance < 0.f ||
        !std::isfinite(options.maxVerticalDelta) || options.maxVerticalDelta < 0.f || options.maximumWork == 0u)
        return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvalidArgument,
                                                      "road terrain conformance options are invalid", "terrain");
    if (options.blendDistance / options.cellSize > 256.f)
        return terrainFail<RoadTerrainConformReceipt>(
            DiagnosticCode::InvalidArgument, "road terrain blend radius exceeds 256 cells", "terrain.blendDistance");

    const int                 width = terrain.getWidth(), height = terrain.getHeight();
    const auto                sampleCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float>        target(sampleCount, std::numeric_limits<float>::quiet_NaN());
    std::vector<float>        targetDelta(sampleCount, std::numeric_limits<float>::max());
    std::vector<std::uint8_t> skippedBridge(sampleCount, 0u), skippedTunnel(sampleCount, 0u);
    std::size_t               work          = 0u;
    const int                 triangleCount = roadMesh.getIndexCount() / 3;
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
            !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz))
            return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::InvariantViolation,
                                                          "road mesh contains non-finite positions", "roadMesh");
        const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(denominator) <= 1e-7f) continue;
        const int minX =
            std::max(0, static_cast<int>(std::floor((std::min({ax, bx, cx}) - options.originX) / options.cellSize)));
        const int maxX = std::min(
            width - 1, static_cast<int>(std::ceil((std::max({ax, bx, cx}) - options.originX) / options.cellSize)));
        const int minZ =
            std::max(0, static_cast<int>(std::floor((std::min({az, bz, cz}) - options.originZ) / options.cellSize)));
        const int maxZ = std::min(
            height - 1, static_cast<int>(std::ceil((std::max({az, bz, cz}) - options.originZ) / options.cellSize)));
        if (minX > maxX || minZ > maxZ) continue;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (++work > options.maximumWork)
                    return terrainFail<RoadTerrainConformReceipt>(DiagnosticCode::PreconditionViolation,
                                                                  "road terrain rasterization exceeds the work budget",
                                                                  "terrain");
                const float worldX = options.originX + static_cast<float>(x) * options.cellSize;
                const float worldZ = options.originZ + static_cast<float>(z) * options.cellSize;
                const float wa     = ((bz - cz) * (worldX - cx) + (cx - bx) * (worldZ - cz)) / denominator;
                const float wb     = ((cz - az) * (worldX - cx) + (ax - cx) * (worldZ - cz)) / denominator;
                const float wc     = 1.f - wa - wb;
                if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
                const float roadY = wa * ay + wb * by + wc * cy;
                const auto  index =
                    static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                const float terrainY = terrain.height(x, z) * options.heightScale;
                const float delta    = std::fabs(roadY - terrainY);
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
                const auto  index =
                    static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
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
            const auto index =
                static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
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
    const auto  sampleCount = static_cast<std::size_t>(receipt.width) * static_cast<std::size_t>(receipt.height);
    std::size_t previous    = 0;
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

// RoadTraffic.cpp
namespace eve::procgen::road::detail {
namespace {

struct Approach {
    const RoadEdge*   edge = nullptr;
    const RoadNode*   node = nullptr;
    SplinePath        spline;
    float             length         = 0.f;
    float             socketDistance = 0.f;
    float             halfWidth      = 0.f;
    RoadLaneDirection direction      = RoadLaneDirection::Forward;
};

const char* controlRole(RoadJunctionControl control) {
    switch (control) {
        case RoadJunctionControl::Yield: return "junction.control.yield";
        case RoadJunctionControl::Stop: return "junction.control.stop";
        case RoadJunctionControl::Signal: return "junction.control.signal";
        case RoadJunctionControl::Uncontrolled: break;
    }
    return "junction.control.uncontrolled";
}

}  // namespace

Result<void> bakeTrafficControlPoints(PointSet& placements, const RoadNetwork& network,
                                      const RoadBakeOptions& options) {
    std::vector<Approach> approaches;
    for (const auto& node : network.nodes()) {
        if (node.junctionControl == RoadJunctionControl::Uncontrolled) continue;
        const auto incidentCount =
            std::count_if(network.edges().begin(), network.edges().end(),
                          [&](const RoadEdge& edge) { return edge.from == node.id || edge.to == node.id; });
        if (incidentCount < 2) continue;

        int  maximumPriority = 0;
        bool hasIncidentEdge = false;
        for (const auto& edge : network.edges()) {
            if (edge.from != node.id && edge.to != node.id) continue;
            maximumPriority =
                hasIncidentEdge ? std::max(maximumPriority, edge.style.trafficPriority) : edge.style.trafficPriority;
            hasIncidentEdge = true;
        }

        bool hasLowerPriority = false;
        for (const auto& edge : network.edges())
            if ((edge.from == node.id || edge.to == node.id) && edge.style.trafficPriority < maximumPriority)
                hasLowerPriority = true;

        for (const auto& edge : network.edges()) {
            RoadLaneDirection direction;
            if (edge.to == node.id && edge.lanesForward > 0)
                direction = RoadLaneDirection::Forward;
            else if (edge.from == node.id && edge.lanesBackward > 0)
                direction = RoadLaneDirection::Backward;
            else
                continue;
            if (node.junctionControl == RoadJunctionControl::Yield && hasLowerPriority &&
                edge.style.trafficPriority == maximumPriority)
                continue;

            auto spline = edgeSplineForBake(edge);
            if (!spline.ok()) return Result<void>::failure(spline.status());
            auto length = spline.value().lengthResult(32);
            if (!length.ok()) return Result<void>::failure(length.status());
            auto profile = makeRoadProfile(edge.style, edge.lanesForward, edge.lanesBackward);
            if (!profile.ok()) return Result<void>::failure(profile.status());
            approaches.push_back({&edge, &node, std::move(spline).takeValue(), length.value(),
                                  junctionSocketDistanceForBake(network, node, edge, length.value()),
                                  profile.value().halfWidth, direction});
        }
    }

    if (static_cast<std::size_t>(placements.getCount()) + approaches.size() >
        static_cast<std::size_t>(options.maximumPlacements))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "road traffic-control anchors exceed the placement budget",
                                                       "placements", {}, "procgen.road"));

    for (const auto& approach : approaches) {
        const bool  forward  = approach.direction == RoadLaneDirection::Forward;
        const float distance = forward ? approach.length - approach.socketDistance : approach.socketDistance;
        auto        frame    = approach.spline.travelFrameResult(distance, "clamp", 32);
        if (!frame.ok()) return Result<void>::failure(frame.status());
        const auto& f             = frame.value();
        const float directionSign = forward ? 1.f : -1.f;
        const float lateral       = (forward ? 1.f : -1.f) * (approach.halfWidth + options.sideObjectOffset);
        const float dx            = f.forwardX * directionSign;
        const float dy            = f.forwardY * directionSign;
        const float dz            = f.forwardZ * directionSign;
        const int   row           = placements.add(f.sample.x + f.sideX * lateral, f.sample.y + f.sideY * lateral,
                                                   f.sample.z + f.sideZ * lateral);
        placements.setNormal(row, f.upX, f.upY, f.upZ);
        placements.setYaw(row, std::atan2(dx, dz) * 57.2957795f);
        const char* role = controlRole(approach.node->junctionControl);
        placements.setPointSeed(row,
                                deriveSeed(approach.node->id, std::string(role) + std::to_string(approach.edge->id)));
        const std::uint64_t ordinal =
            static_cast<std::uint64_t>(approach.edge->id) * 2u + static_cast<std::uint64_t>(approach.direction) + 1u;
        auto status = placements.trySetPointId(row, derivePointId(0x4a43545200000000ull | approach.node->id, ordinal));
        if (!status.ok()) return status;
        status = placements.trySetStringAttribute(row, "road_role", role);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_node_id", approach.node->id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_edge_id", approach.edge->id);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_side", 1);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "traffic_priority", approach.edge->style.trafficPriority);
        if (!status.ok()) return status;
        status = placements.trySetIntAttribute(row, "road_lane_direction", static_cast<int>(approach.direction));
        if (!status.ok()) return status;
        status = placements.trySetFloatAttribute(row, "road_distance", distance);
        if (!status.ok()) return status;
        status = placements.trySetVectorAttribute(row, "road_direction", dx, dy, dz);
        if (!status.ok()) return status;
    }
    return Result<void>::success();
}

}  // namespace eve::procgen::road::detail
