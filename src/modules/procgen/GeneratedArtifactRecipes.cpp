#include <algorithm>
#include <string>
#include <utility>
#include "procgen/GeneratedArtifact.h"
#include "procgen/algorithms/CastleMesh.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/algorithms/MarchingCubes.h"
namespace eve::procgen {
namespace {
#include "procgen/GeneratedArtifactDiagnostics.inc"
Bounds boundsForMesh(const MeshBuild &mesh) noexcept {
    Bounds bounds;
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        bounds.include(mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i));
    }
    return bounds;
}

Bounds boundsForGrid(const Grid2D &grid) noexcept {
    Bounds bounds;
    if (grid.getWidth() <= 0 || grid.getHeight() <= 0) return bounds;
    bounds.include(0.f, 0.f, 0.f);
    bounds.include(float(grid.getWidth()), 0.f, float(grid.getHeight()));
    return bounds;
}

Bounds boundsForPoints(const PointSet &points) noexcept {
    Bounds bounds;
    for (const ProcgenPoint &point : points.points()) bounds.include(point.x, point.y, point.z);
    return bounds;
}

Collider colliderForMesh(const MeshBuild &mesh) {
    Collider collider;
    collider.vertices = mesh.positions();
    collider.indices  = mesh.indices();
    collider.bounds   = boundsForMesh(mesh);
    return collider;
}

Grid2D makeHexTopology(const Params &params) {
    const int width  = std::clamp(params.getInt("width", 32), 1, 256);
    const int height = std::clamp(params.getInt("height", 24), 1, 256);
    Grid2D    topology;
    topology.resize(width, height);
    const std::uint32_t seed = params.getSeed();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // This is a compact cell-topology projection. Elevation and dense
            // geometry remain in the mesh recipe; no per-cell ECS objects are
            // created merely to publish topology.
            const std::uint32_t value = seed ^ (std::uint32_t(x) * 0x9e3779b9u) ^ (std::uint32_t(y) * 0x85ebca6bu);
            topology.setCell(x, y, int((value ^ (value >> 16u)) % 10u));
        }
    }
    topology.setMeta("role", "topology");
    topology.setMeta("algorithm", "mesh.hexterrain");
    topology.setMeta("coordinateSpace", "hex_axial");
    return topology;
}

PointSet makeHexAnchors(const Params &params) {
    const float radius = params.getFloat("radius", 1.f);
    const int   width  = std::max(2, params.getInt("width", 32));
    const int   height = std::max(2, params.getInt("height", 24));
    const float xStep  = radius * 1.5f;
    const float zStep  = radius * 1.7320508076f;
    PointSet    anchors;
    anchors.add(0.f, 0.f, 0.f);
    anchors.add(float(width - 1) * xStep, 0.f, 0.f);
    anchors.add(0.f, 0.f, float(height - 1) * zStep);
    anchors.add(float(width - 1) * xStep, 0.f, (float(height - 1) + float((width - 1) & 1) * .5f) * zStep);
    anchors.setStringAttribute(0, "role", "spawn");
    anchors.setStringAttribute(1, "role", "edge");
    anchors.setStringAttribute(2, "role", "edge");
    anchors.setStringAttribute(3, "role", "edge");
    return anchors;
}

Grid2D makeCastleTopology(const Params &params) {
    const int rings = std::clamp(params.getInt("rings", 2), 1, 4);
    Grid2D    topology;
    topology.resize(rings, 1);
    for (int ring = 0; ring < rings; ++ring) topology.setCell(ring, 0, ring + 1);
    topology.setMeta("role", "topology");
    topology.setMeta("algorithm", "mesh.castle");
    topology.setMeta("coordinateSpace", "castle_ring");
    return topology;
}

PointSet makeCastleAnchors(const Params &params) {
    const float width     = std::max(16.f, params.getFloat("width", 42.f));
    const float depth     = std::max(16.f, params.getFloat("depth", 36.f));
    const float gateWidth = std::clamp(params.getFloat("gateWidth", 5.f), 2.f, width * .3f);
    const float keepWidth = std::clamp(params.getFloat("keepWidth", width * .25f), 6.f, width * .55f);
    const float keepDepth = std::clamp(params.getFloat("keepDepth", depth * .24f), 6.f, depth * .55f);
    PointSet    anchors;
    anchors.add(0.f, 0.f, -depth * .5f - 1.f);
    anchors.add(-width * .5f + gateWidth, 0.f, -depth * .5f);
    anchors.add(width * .5f - gateWidth, 0.f, -depth * .5f);
    anchors.add(-keepWidth * .5f, 0.f, -keepDepth * .5f);
    anchors.add(keepWidth * .5f, 0.f, keepDepth * .5f);
    anchors.setStringAttribute(0, "role", "gate");
    anchors.setStringAttribute(1, "role", "outer_gate_left");
    anchors.setStringAttribute(2, "role", "outer_gate_right");
    anchors.setStringAttribute(3, "role", "keep_min");
    anchors.setStringAttribute(4, "role", "keep_max");
    return anchors;
}

eve::Result<ArtifactPart> partFromMesh(std::string role, ArtifactId parent, const BuildKey &parentKey, MeshBuild mesh,
                                       eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForMesh(mesh);
    return makeArtifactPart(std::move(role), childId, ArtifactType::MeshData, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {}, std::move(metadata), std::move(mesh));
}

eve::Result<ArtifactPart> partFromCollider(std::string role, ArtifactId parent, const BuildKey &parentKey,
                                           Collider collider, eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = collider.bounds;
    return makeArtifactPart(std::move(role), childId, ArtifactType::Collider, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {parent.child("mesh")}, std::move(metadata),
                            std::move(collider));
}

eve::Result<ArtifactPart> partFromGrid(std::string role, ArtifactId parent, const BuildKey &parentKey, Grid2D grid,
                                       eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForGrid(grid);
    return makeArtifactPart(std::move(role), childId, ArtifactType::Grid, eve::SchemaVersion(1), std::move(*childKey),
                            bounds, {}, std::move(metadata), std::move(grid));
}

eve::Result<ArtifactPart> partFromPoints(std::string role, ArtifactId parent, const BuildKey &parentKey,
                                         PointSet points, eve::Value::Object metadata) {
    auto childKey = BuildKey::fromCanonical(parentKey.format() + "/" + role);
    if (!childKey)
        return rejectedPart(eve::DiagnosticCode::InvalidArgument, "cannot derive child build key for " + role);
    const ArtifactId childId = parent.child(role);
    const Bounds     bounds  = boundsForPoints(points);
    return makeArtifactPart(std::move(role), childId, ArtifactType::PointSet, eve::SchemaVersion(1),
                            std::move(*childKey), bounds, {}, std::move(metadata), std::move(points));
}

template <class T>
bool appendPart(eve::Result<ArtifactPart> &&result, std::vector<ArtifactPart> &parts, eve::Status &failure) {
    if (!result.ok()) {
        failure = result.status();
        return false;
    }
    parts.emplace_back(std::move(result).takeValue());
    return true;
}

eve::Result<GeneratedArtifact> generateComposite(const Params &params, ArtifactId id, std::string_view recipe,
                                                 bool (*generateMesh)(const Params &, MeshBuild &, std::string &),
                                                 Grid2D topology, PointSet anchors) {
    if (id.isNil())
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "top-level artifact identity must not be nil");
    const auto key = BuildKey::forRecipe(recipe, params);
    if (!key)
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "recipe or parameters cannot form a build key");

    MeshBuild   mesh;
    std::string error;
    if (!generateMesh(params, mesh, error)) {
        return rejectedArtifact(eve::DiagnosticCode::Failed,
                                error.empty() ? "procedural mesh generation failed" : error);
    }
    const Collider            collider = colliderForMesh(mesh);
    std::vector<ArtifactPart> parts;
    parts.reserve(4);
    eve::Status        failure;
    eve::Value::Object meshMeta;
    meshMeta.emplace("role", eve::Value("mesh"));
    meshMeta.emplace("densePayload", eve::Value(true));
    auto meshPart = partFromMesh("mesh", id, *key, std::move(mesh), std::move(meshMeta));
    if (!appendPart<ArtifactPart>(std::move(meshPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object colliderMeta;
    colliderMeta.emplace("role", eve::Value("collider"));
    colliderMeta.emplace("source", eve::Value("mesh"));
    auto colliderPart = partFromCollider("collider", id, *key, collider, std::move(colliderMeta));
    if (!appendPart<ArtifactPart>(std::move(colliderPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object topologyMeta;
    topologyMeta.emplace("role", eve::Value("topology"));
    auto topologyPart = partFromGrid("topology", id, *key, std::move(topology), std::move(topologyMeta));
    if (!appendPart<ArtifactPart>(std::move(topologyPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    eve::Value::Object anchorsMeta;
    anchorsMeta.emplace("role", eve::Value("anchors"));
    anchorsMeta.emplace("coordinateSpace", eve::Value("world"));
    auto anchorsPart = partFromPoints("anchors", id, *key, std::move(anchors), std::move(anchorsMeta));
    if (!appendPart<ArtifactPart>(std::move(anchorsPart), parts, failure))
        return eve::Result<GeneratedArtifact>::failure(std::move(failure));

    CompositeArtifact composite;
    composite.children = std::move(parts);
    eve::Value::Object metadata;
    metadata.emplace("algorithm", eve::Value(std::string(recipe)));
    metadata.emplace("childCount", eve::Value(std::int64_t(composite.children.size())));
    metadata.emplace("determinism", eve::Value("seeded_cpu"));
    metadata.emplace("payloadPolicy", eve::Value("dense data stays typed"));
    const Bounds bounds = composite.children.front().bounds;
    return makeArtifact(id, ArtifactType::Composite, eve::SchemaVersion(1), *key, bounds, {}, std::move(metadata),
                        GeneratedArtifact::Payload(std::move(composite)));
}


}  // namespace
eve::Result<GeneratedArtifact> generateHexTerrainArtifact(const Params &params, ArtifactId id) {
    return generateComposite(params, id, "mesh.hexterrain", generateHexTerrainMesh, makeHexTopology(params),
                             makeHexAnchors(params));
}

eve::Result<GeneratedArtifact> generateCastleArtifact(const Params &params, ArtifactId id) {
    return generateComposite(params, id, "mesh.castle", generateCastleMesh, makeCastleTopology(params),
                             makeCastleAnchors(params));
}

eve::Result<GeneratedArtifact> generateMeshArtifact(std::string_view recipeId, const Params &params, ArtifactId id) {
    if (recipeId == "mesh.hexterrain") return generateHexTerrainArtifact(params, id);
    if (recipeId == "mesh.castle") return generateCastleArtifact(params, id);
    if (recipeId == "mesh.roadNetwork") return generateRoadNetworkArtifact(params, id);
    if (id.isNil())
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "top-level artifact identity must not be nil");
    const auto key = BuildKey::forRecipe(recipeId, params);
    if (!key)
        return rejectedArtifact(eve::DiagnosticCode::InvalidArgument, "recipe or parameters cannot form a build key");
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild   mesh;
    std::string error;
    if (!MeshRecipeRegistry::instance().generate(std::string(recipeId), params, mesh, error))
        return rejectedArtifact(eve::DiagnosticCode::Failed,
                                error.empty() ? "procedural mesh generation failed" : error);
    eve::Value::Object metadata;
    metadata.emplace("algorithm", eve::Value(std::string(recipeId)));
    metadata.emplace("determinism", eve::Value("seeded_cpu"));
    const Bounds bounds = boundsForMesh(mesh);
    return makeArtifact(id, ArtifactType::MeshData, eve::SchemaVersion(1), *key, bounds, {}, std::move(metadata),
                        std::move(mesh));
}


}  // namespace eve::procgen
