#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gts_mesh_splitter)
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include "PrivateTestScope.h"
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/procgen/GtsTerrainLodPackage.h"
#include "image/ImageData.h"
#include "procgen/GtsMeshSimplifier.h"
#include "procgen/GtsMeshSplitter.h"
#include "procgen/GtsTerrainExportSettings.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/heightmap/Heightmap.h"

namespace test_gts_mesh_contract {
using namespace eve::procgen;
namespace {
MeshBuild crossingQuad() {
    MeshBuild mesh;
    mesh.setActiveGroup("terrain");
    mesh.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    mesh.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    mesh.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    mesh.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}
Heightmap slope(int size) {
    Heightmap map(size, size);
    for (int z = 0; z < size; ++z)
        for (int x = 0; x < size; ++x) map.setHeight(x, z, float(x + z * 2) / float((size - 1) * 3));
    return map;
}
}  // namespace

TEST_CASE("GTS terrain base mesh documents resolution sampling and atomic failure") {
    SUB_CASE("all save resolutions preserve world bounds and normalized normals") {
        const int vertices[] = {289, 81, 25, 9, 4};
        const int indices[]  = {1536, 384, 96, 24, 6};
        for (int level = 0; level <= 4; ++level) {
            MeshBuild  mesh;
            const auto built = buildGtsTerrainBaseMesh(mesh, slope(17), static_cast<GtsTerrainSaveResolution>(level),
                                                       160.f, 48.f, 80.f);
            REQUIRE(built.ok());
            CHECK_EQ(mesh.getVertexCount(), vertices[level]);
            CHECK_EQ(mesh.getIndexCount(), indices[level]);
            const int last = mesh.getVertexCount() - 1;
            CHECK_EQ(mesh.getPositionX(last), 160.f);
            CHECK_EQ(mesh.getPositionY(last), 48.f);
            CHECK_EQ(mesh.getPositionZ(last), 80.f);
            CHECK_EQ(mesh.getUvU(last), 1.f);
            CHECK_EQ(mesh.getUvV(last), 1.f);
            for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
                const float x = mesh.getNormalX(vertex), y = mesh.getNormalY(vertex), z = mesh.getNormalZ(vertex);
                CHECK(std::fabs(std::sqrt(x * x + y * y + z * z) - 1.f) < 1e-4f);
                CHECK(y > 0.f);
            }
        }
    };
    SUB_CASE("unsupported resolution and malformed source leave the existing mesh intact") {
        auto      mesh             = crossingQuad();
        const int originalVertices = mesh.getVertexCount();
        const int originalIndices  = mesh.getIndexCount();
        CHECK(!buildGtsTerrainBaseMesh(mesh, slope(17), static_cast<GtsTerrainSaveResolution>(5), 1.f, 1.f, 1.f).ok());
        CHECK(!buildGtsTerrainBaseMesh(mesh, slope(6), GtsTerrainSaveResolution::Quarter, 1.f, 1.f, 1.f).ok());
        auto nonFinite = slope(17);
        nonFinite.setHeight(3, 2, std::numeric_limits<float>::quiet_NaN());
        CHECK(!buildGtsTerrainBaseMesh(mesh, nonFinite, GtsTerrainSaveResolution::Full, 1.f, 1.f, 1.f).ok());
        CHECK_EQ(mesh.getVertexCount(), originalVertices);
        CHECK_EQ(mesh.getIndexCount(), originalIndices);
    };
}

TEST_CASE("GTS terrain LOD builds every tile and commits only a complete valid plan") {
    const auto       map = slope(65);
    GtsTerrainLodSet lods;
    auto built = buildDefaultGtsTerrainLodsFromHeightmapInto(lods, map, GtsTerrainSaveResolution::Quarter, 160.f, 32.f,
                                                             80.f, 3, GtsMeshPivot::None);
    REQUIRE(built.ok());
    CHECK_EQ(lods.getColumnCount(), 4);
    CHECK_EQ(lods.getRowCount(), 4);
    CHECK_EQ(lods.getTileCount(), 16);
    CHECK_EQ(lods.getLevelCount(), 4);
    for (int tile = 0; tile < lods.getTileCount(); ++tile) {
        const auto* value = lods.tileAt(tile);
        REQUIRE(value != nullptr);
        CHECK_EQ(value->levels.size(), 4u);
        CHECK(!value->levels.front().empty());
    }
    const auto before = lods.snapshotJson();
    REQUIRE(before.ok());
    CHECK(!buildDefaultGtsTerrainLodsFromHeightmapInto(lods, map, GtsTerrainSaveResolution::Quarter, 160.f, 32.f, 80.f,
                                                       6, GtsMeshPivot::None)
               .ok());
    const auto after = lods.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
}

TEST_CASE("GTS tile splitter clips the source surface into local meshes with recoverable world positions") {
    SUB_CASE("four clipped cells retain UV and normals") {
        const auto split = splitGtsMesh(crossingQuad(), 1, 1, GtsMeshPivot::None);
        REQUIRE(split.ok());
        CHECK_EQ(split.value().getColumnCount(), 2);
        CHECK_EQ(split.value().getRowCount(), 2);
        CHECK_EQ(split.value().getTileCount(), 4);
        for (int tileIndex = 0; tileIndex < 4; ++tileIndex) {
            const auto* tile = split.value().tileAt(tileIndex);
            REQUIRE(tile != nullptr);
            CHECK(!tile->mesh.empty());
            for (int v = 0; v < tile->mesh.getVertexCount(); ++v) {
                CHECK(tile->mesh.getUvU(v) >= 0.f);
                CHECK(tile->mesh.getUvU(v) <= 1.f);
                CHECK(tile->mesh.getUvV(v) >= 0.f);
                CHECK(tile->mesh.getUvV(v) <= 1.f);
                CHECK_EQ(tile->mesh.getNormalY(v), 1.f);
            }
        }
    };
    SUB_CASE("center pivot records a transform without changing world bounds") {
        const auto split = splitGtsMesh(crossingQuad(), 1, 0, GtsMeshPivot::CenterXZ);
        REQUIRE(split.ok());
        const auto* right = split.value().tileAt(1);
        REQUIRE(right != nullptr);
        CHECK_EQ(right->offsetX, 1.5f);
        CHECK_EQ(right->offsetZ, 1.f);
        float minX = 99.f, maxX = -99.f;
        for (int i = 0; i < right->mesh.getVertexCount(); ++i) {
            const float worldX = right->mesh.getPositionX(i) + right->offsetX;
            minX               = std::min(minX, worldX);
            maxX               = std::max(maxX, worldX);
        }
        CHECK_EQ(minX, 1.f);
        CHECK_EQ(maxX, 2.f);
    };
    SUB_CASE("empty and nonfinite sources and negative subdivisions reject") {
        MeshBuild empty;
        CHECK(!splitGtsMesh(empty, 0, 0).ok());
        CHECK(!splitGtsMesh(crossingQuad(), -1, 1).ok());
        auto invalid           = crossingQuad();
        invalid.positions()[0] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!splitGtsMesh(invalid, 1, 1).ok());
    };
}

TEST_CASE("GTS simplifier reduces interior topology and preserves caller output on failure") {
    MeshBuild grid;
    grid.setActiveGroup("terrain");
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x)
            grid.addVertex(float(x), x == 1 && z == 1 ? .2f : 0.f, float(z), 0, 1, 0, x / 2.f, z / 2.f);
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 2; ++x) {
            const uint32_t a = z * 3 + x, b = a + 1, c = a + 3, d = c + 1;
            grid.addTriangle(a, b, d);
            grid.addTriangle(a, d, c);
        }
    SUB_CASE("interior edges collapse but border policy preserves more triangles") {
        GtsMeshSimplificationOptions options;
        options.preserveBorderEdges = false;
        MeshBuild  reduced;
        const auto count = simplifyGtsMesh(reduced, grid, .5f, options);
        REQUIRE(count.ok());
        CHECK(count.value() > 0);
        CHECK(count.value() <= 4);
        CHECK_EQ(reduced.getGroupName(0), std::string("terrain"));
        options.preserveBorderEdges = true;
        MeshBuild border;
        REQUIRE(simplifyGtsMesh(border, grid, .1f, options).ok());
        CHECK(border.getIndexCount() / 3 >= 4);
    };
    SUB_CASE("invalid options do not publish a partial result; input-output alias is supported") {
        auto alias = crossingQuad();
        REQUIRE(simplifyGtsMesh(alias, alias, .5f, {}).ok());
        CHECK(!alias.empty());
        auto                         output = crossingQuad();
        const int                    before = output.getIndexCount();
        GtsMeshSimplificationOptions invalid;
        invalid.maxIterationCount = 0;
        CHECK(!simplifyGtsMesh(output, grid, .5f, invalid).ok());
        CHECK_EQ(output.getIndexCount(), before);
    };
}

TEST_CASE("GTS export settings round trip a LOD plan and reject partial restoration") {
    auto near = makeGtsTerrainImpostorLod(0);
    auto far  = makeGtsTerrainImpostorLod(2);
    REQUIRE(near.ok());
    REQUIRE(far.ok());
    CHECK_EQ(int(near.value().saveResolution), 1);
    CHECK_EQ(near.value().textureExportResolution, 2048);
    CHECK_EQ(int(far.value().saveResolution), 3);
    near.value().screenRelativeTransitionHeight = .8f;
    far.value().screenRelativeTransitionHeight  = .2f;
    far.value().simplifyQuality                 = .5f;
    GtsTerrainExportSettings settings;
    REQUIRE(settings.setSourceLods({near.value(), far.value()}).ok());
    const auto compiled = settings.compileSourceLevels();
    REQUIRE(compiled.ok());
    REQUIRE_EQ(compiled.value().size(), 2u);
    CHECK_EQ(compiled.value()[1].quality, .5f);
    CHECK_EQ(compiled.value()[1].screenRelativeTransitionHeight, .2f);
    const auto snapshot = settings.snapshotJson();
    REQUIRE(snapshot.ok());
    GtsTerrainExportSettings restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    std::string unknown = snapshot.value();
    unknown.insert(1, "\"unknown\":1,");
    CHECK(!restored.restoreJson(unknown).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    far.value().screenRelativeTransitionHeight = .9f;
    CHECK(!restored.setSourceLods({near.value(), far.value()}).ok());
    CHECK_EQ(restored.getSourceLodCount(), 2);
}

TEST_CASE("GTS export workflow drives tile geometry and rejects unsupported collider mode atomically") {
    auto source = makeGtsTerrainImpostorLod(1);
    REQUIRE(source.ok());
    source.value().screenRelativeTransitionHeight = .02f;
    source.value().simplifyQuality                = 1.f;
    GtsTerrainExportSettings settings;
    REQUIRE(settings.setSourceLods({source.value()}).ok());
    GtsTerrainExportWorkflow workflow;
    workflow.colliderType            = GtsTerrainColliderType::Mesh;
    workflow.colliderResolution      = GtsTerrainSaveResolution::Quarter;
    workflow.colliderSimplifyQuality = .5f;
    REQUIRE(settings.configureWorkflow(workflow).ok());
    const auto       map = slope(17);
    GtsTerrainLodSet lods;
    REQUIRE(buildGtsTerrainExportLodsFromHeightmapInto(lods, map, settings, 16.f, 8.f, 16.f, 1, GtsMeshPivot::CenterXZ)
                .ok());
    CHECK_EQ(lods.getTileCount(), 4);
    CHECK_EQ(lods.getLevelCount(), 1);
    MeshBuild collider;
    REQUIRE(buildGtsTerrainColliderMeshFromHeightmapInto(collider, map, workflow, 16.f, 8.f, 16.f).ok());
    CHECK(!collider.empty());
    CHECK(collider.getVertexCount() <= 25);
    const int before      = collider.getIndexCount();
    workflow.colliderType = GtsTerrainColliderType::Heightfield;
    CHECK(!buildGtsTerrainColliderMeshFromHeightmapInto(collider, map, workflow, 16.f, 8.f, 16.f).ok());
    CHECK_EQ(collider.getIndexCount(), before);
}

TEST_CASE("GTS OBJ encoder maps terrain coordinates and face mode into stable text") {
    Heightmap map(3, 3);
    map.setHeight(2, 2, 1.f);
    const auto triangles =
        encodeGtsTerrainObj(map, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f, GtsTerrainObjFaceMode::Triangles);
    REQUIRE(triangles.ok());
    CHECK(triangles.value().starts_with("# Unity terrain OBJ File\n"));
    CHECK(triangles.value().find("v -2 3 4\n") != std::string::npos);
    CHECK(triangles.value().find("vt 1 1\n") != std::string::npos);
    CHECK(triangles.value().find("f 1/1 4/4 2/2\n") != std::string::npos);
    const auto quads =
        encodeGtsTerrainObj(map, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f, GtsTerrainObjFaceMode::Quads);
    REQUIRE(quads.ok());
    CHECK(quads.value().find("f 1/1 4/4 5/5 2/2\n") != std::string::npos);
    CHECK(
        !encodeGtsTerrainObj(map, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f, static_cast<GtsTerrainObjFaceMode>(2))
             .ok());
}

}  // namespace test_gts_mesh_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_uv_projection)
#include <cmath>
#include "PrivateTestScope.h"
#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/mesh/MeshUvProjection.h"

namespace test_mesh_uv_contract {
using namespace eve::procgen;
namespace {
MeshBuild triangle() {
    MeshBuild mesh;
    mesh.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    mesh.addVertex(1, 0, 0, 0, 1, 0, 0, 0);
    mesh.addVertex(0, 0, 1, 0, 1, 0, 0, 0);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("Mesh UV projection maps a surface point through the projected triangle") {
    const auto projected = projectMeshUvResult(triangle(), "planar", 1.f, 0.f, 0.f);
    REQUIRE(projected.ok());
    const auto mapped = mapMeshSurfacePointToUvResult(projected.value(), 0, .25f, 0.f, .25f);
    REQUIRE(mapped.ok());
    CHECK(std::fabs(mapped.value().u - .25f) < 1e-6f);
    CHECK(std::fabs(mapped.value().v - .25f) < 1e-6f);
    CHECK_EQ(projected.value().getMeta("uvProjection", ""), std::string("planar"));
}

TEST_CASE("Mesh UV projection rejects empty or unknown input and runs inside modifier graph") {
    MeshBuild empty;
    CHECK(!projectMeshUvResult(empty, "planar").ok());
    CHECK(!projectMeshUvResult(triangle(), "invalid").ok());
    CHECK(!mapMeshSurfacePointToUvResult(triangle(), 3, 0, 0, 0).ok());
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("source", "mesh.input").ok());
    REQUIRE(graph.addNode("uv", "mesh.projectUv").ok());
    REQUIRE(graph.addNode("output", "mesh.output").ok());
    REQUIRE(graph.connect("source", "uv", 0).ok());
    REQUIRE(graph.connect("uv", "output", 0).ok());
    REQUIRE(graph.setNodeMesh("source", triangle()).ok());
    REQUIRE(graph.setNodeString("uv", "mode", "box").ok());
    const auto projected = graph.executeResult("output");
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().getMeta("uvProjection", ""), std::string("box"));
}
}  // namespace test_mesh_uv_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_modifier_graph)
#include <cmath>
#include "PrivateTestScope.h"
#include "procgen/mesh/MeshModifierGraph.h"

namespace test_mesh_modifier_contract {
using namespace eve;
using namespace eve::procgen;
namespace {
MeshBuild triangle() {
    MeshBuild mesh;
    mesh.addVertex(0, 0, 0, 0, 0, 1, 0, 0);
    mesh.addVertex(1, 0, 0, 0, 0, 1, 1, 0);
    mesh.addVertex(0, 1, 0, 0, 0, 1, 0, 1);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("Mesh modifier graph compiles linear vertex chains and caches unchanged inputs") {
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("source", "mesh.input").ok());
    REQUIRE(graph.addNode("move", "deform.transform").ok());
    REQUIRE(graph.addNode("twist", "deform.twist").ok());
    REQUIRE(graph.addNode("out", "mesh.output").ok());
    REQUIRE(graph.connect("source", "move").ok());
    REQUIRE(graph.connect("move", "twist").ok());
    REQUIRE(graph.connect("twist", "out").ok());
    REQUIRE(graph.setNodeFloat("move", "x", 2.f).ok());
    REQUIRE(graph.setNodeFloat("move", "maskRadius", .25f).ok());
    REQUIRE(graph.setNodeFloat("twist", "angle", 0.f).ok());
    REQUIRE(graph.setNodeMesh("source", triangle()).ok());
    const auto output = graph.executeResult("out");
    REQUIRE(output.ok());
    CHECK_EQ(output.value().getVertexCount(), 3);
    CHECK(std::fabs(output.value().getPositionX(0) - 2.f) < 1e-6f);
    CHECK(std::fabs(output.value().getPositionX(1) - 1.f) < 1e-6f);
    CHECK_EQ(graph.compiledSegmentCount(), 3);
    CHECK_EQ(graph.fusedOperationCount(), 2);
    REQUIRE(graph.executeResult("out").ok());
    CHECK_EQ(graph.metrics().size(), 1u);
    CHECK(graph.metrics().front().cacheHit);
}

TEST_CASE("Mesh modifier graph rejects cycles and invalid parameters with diagnostics") {
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("source", "mesh.input").ok());
    REQUIRE(graph.addNode("bend", "deform.bend").ok());
    REQUIRE(graph.addNode("twist", "deform.twist").ok());
    REQUIRE(graph.connect("bend", "twist").ok());
    REQUIRE(graph.connect("twist", "bend").ok());
    const auto cycle = graph.executeResult("twist");
    REQUIRE(!cycle.ok());
    REQUIRE(cycle.status().primaryDiagnostic() != nullptr);
    CHECK(cycle.status().primaryDiagnostic()->code() == DiagnosticCode::Conflict);
    REQUIRE(graph.disconnect("bend").ok());
    REQUIRE(graph.connect("source", "bend").ok());
    REQUIRE(graph.setNodeString("bend", "axis", "diagonal").ok());
    REQUIRE(graph.setNodeMesh("source", triangle()).ok());
    const auto axis = graph.executeResult("bend");
    REQUIRE(!axis.ok());
    REQUIRE(axis.status().primaryDiagnostic() != nullptr);
    CHECK(axis.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("Spline tube generator consumes an owning path and validates radial budget") {
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("tube", "mesh.splineTube").ok());
    const auto absent = graph.executeResult("tube");
    REQUIRE(!absent.ok());
    CHECK(absent.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);
    SplinePath path;
    REQUIRE(path.setKindResult("linear").ok());
    REQUIRE(path.addPointResult({0.f, 0.f, 0.f}).ok());
    REQUIRE(path.addPointResult({0.f, 2.f, 0.f}).ok());
    REQUIRE(graph.setNodeSplinePath("tube", path).ok());
    REQUIRE(graph.setNodeFloat("tube", "radius", .5f).ok());
    REQUIRE(graph.setNodeInt("tube", "pathSegments", 4).ok());
    REQUIRE(graph.setNodeInt("tube", "radialSegments", 2).ok());
    const auto invalid = graph.executeResult("tube");
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
    REQUIRE(graph.setNodeInt("tube", "radialSegments", 8).ok());
    REQUIRE(graph.setNodeInt("tube", "cap", 1).ok());
    const auto mesh = graph.executeResult("tube");
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().getVertexCount(), 65);
    CHECK_EQ(mesh.value().getIndexCount(), 240);
    CHECK_EQ(mesh.value().getGroupName(0), std::string("tube"));
    CHECK_EQ(mesh.value().getGroupName(1), std::string("caps"));
    CHECK_EQ(mesh.value().getMeta("generator", ""), std::string("mesh.splineTube"));
}

}  // namespace test_mesh_modifier_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_boolean)
#include <algorithm>
#include <cmath>
#include <string>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/MeshDeformationCompute.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/mesh/MeshAdhereLive.h"
#include "procgen/mesh/MeshBoolean.h"
#include "procgen/mesh/MeshContactBlend.h"
#include "procgen/mesh/MeshDeformationSession.h"
#include "procgen/mesh/MeshMerge.h"
#include "procgen/mesh/MeshModifierGraph.h"

namespace test_mesh_editing_contract {
using namespace eve::procgen;
namespace {
MeshBuild cube(float centerX) {
    MeshBuild   mesh;
    const float x0 = centerX - 1.f, x1 = centerX + 1.f;
    const float positions[8][3] = {{x0, -1, -1}, {x1, -1, -1}, {x1, 1, -1}, {x0, 1, -1},
                                   {x0, -1, 1},  {x1, -1, 1},  {x1, 1, 1},  {x0, 1, 1}};
    for (const auto& point : positions) mesh.addVertex(point[0], point[1], point[2], 0.f, 1.f, 0.f, 0.f, 0.f);
    const int triangles[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    mesh.setActiveGroup("shell");
    for (const auto& triangle : triangles) mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
    return mesh;
}
double volume(const MeshBuild& mesh) {
    double signedSixfold = 0;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int    a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        const double ax = mesh.getPositionX(a), ay = mesh.getPositionY(a), az = mesh.getPositionZ(a);
        const double bx = mesh.getPositionX(b), by = mesh.getPositionY(b), bz = mesh.getPositionZ(b);
        const double cx = mesh.getPositionX(c), cy = mesh.getPositionY(c), cz = mesh.getPositionZ(c);
        signedSixfold += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    }
    return std::fabs(signedSixfold / 6.0);
}
MeshBuild sculptPlane() {
    MeshBuild mesh;
    mesh.addVertex(-1, 0, -1, 0, 1, 0, 0, 0);
    mesh.addVertex(1, 0, -1, 0, 1, 0, 1, 0);
    mesh.addVertex(0, 0, 0, 0, 1, 0, .5f, .5f);
    mesh.addVertex(-1, 0, 1, 0, 1, 0, 0, 1);
    mesh.addVertex(1, 0, 1, 0, 1, 0, 1, 1);
    mesh.addTriangle(0, 2, 1);
    mesh.addTriangle(0, 3, 2);
    mesh.addTriangle(2, 3, 4);
    mesh.addTriangle(1, 2, 4);
    return mesh;
}
MeshBuild ground() {
    MeshBuild mesh;
    mesh.addVertex(-2, 0, -2, 0, 1, 0, 0, 0);
    mesh.addVertex(2, 0, -2, 0, 1, 0, 1, 0);
    mesh.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    mesh.addVertex(-2, 0, 2, 0, 1, 0, 0, 1);
    mesh.setActiveGroup("ground");
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}
}  // namespace

TEST_CASE("Mesh boolean defines union and difference and intersection volumes for overlapping cubes") {
    const auto left = cube(0.f), right = cube(1.f);
    SUB_CASE("closed output volumes and operation metadata") {
        struct Operation {
            const char* name;
            double      expected;
        };
        for (const Operation operation : {Operation{"union", 12.}, {"difference", 4.}, {"intersection", 4.}}) {
            const auto result = meshBooleanResult(left, right, operation.name);
            REQUIRE(result.ok());
            CHECK(std::fabs(volume(result.value()) - operation.expected) < .01);
            CHECK_EQ(result.value().getMeta("boolean.operation", ""), std::string(operation.name));
            CHECK(result.value().getIndexCount() > 0);
        }
    };
    SUB_CASE("invalid operands and operation leave both inputs owned by caller") {
        MeshBuild empty;
        CHECK(!meshBooleanResult(left, empty, "difference").ok());
        CHECK(!meshBooleanResult(left, right, "xor").ok());
        CHECK_EQ(left.getVertexCount(), 8);
        CHECK_EQ(right.getIndexCount(), 36);
    };
    SUB_CASE("modifier graph executes equivalent difference") {
        MeshModifierGraph graph;
        REQUIRE(graph.addNode("left", "mesh.input").ok());
        REQUIRE(graph.addNode("right", "mesh.input").ok());
        REQUIRE(graph.addNode("cut", "mesh.boolean").ok());
        REQUIRE(graph.addNode("out", "mesh.output").ok());
        REQUIRE(graph.connect("left", "cut", 0).ok());
        REQUIRE(graph.connect("right", "cut", 1).ok());
        REQUIRE(graph.connect("cut", "out", 0).ok());
        REQUIRE(graph.setNodeString("cut", "operation", "difference").ok());
        REQUIRE(graph.setNodeMesh("left", cube(0.f)).ok());
        REQUIRE(graph.setNodeMesh("right", cube(1.f)).ok());
        const auto cut = graph.executeResult("out");
        REQUIRE(cut.ok());
        CHECK(std::fabs(volume(cut.value()) - 4.) < .01);
    };
}

TEST_CASE("Procedural deformation geometry exposes topology and normals through recipe parameters") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    SUB_CASE("curved plane keeps shared smooth vertices while sharp mode duplicates corners") {
        Params params;
        params.setInt("segmentsX", 4);
        params.setInt("segmentsZ", 3);
        params.setFloat("width", 4.f);
        params.setFloat("depth", 3.f);
        params.setFloat("curvature", 1.f);
        MeshBuild   smooth;
        std::string error;
        REQUIRE(registry.generate("mesh.extendedPlane", params, smooth, error));
        CHECK_EQ(smooth.getVertexCount(), 20);
        CHECK_EQ(smooth.getIndexCount(), 72);
        float peak = 0.f;
        for (int i = 0; i < smooth.getVertexCount(); ++i) peak = std::max(peak, smooth.getPositionY(i));
        CHECK(peak > .99f);
        params.setBool("sharpNormals", true);
        MeshBuild sharp;
        REQUIRE(registry.generate("mesh.extendedPlane", params, sharp, error));
        CHECK_EQ(sharp.getVertexCount(), 48);
        CHECK_EQ(sharp.getMeta("normalMode", ""), std::string("sharp"));
    };
    SUB_CASE("hex grid seed replays height variation and planar mode removes it") {
        Params params;
        params.setInt("columns", 3);
        params.setInt("rows", 2);
        params.setFloat("radius", .5f);
        params.setFloat("height", .2f);
        params.setFloat("randomHeight", 1.f);
        params.setSeed(77);
        MeshBuild   first, replay;
        std::string error;
        REQUIRE(registry.generate("mesh.hexGrid", params, first, error));
        REQUIRE(registry.generate("mesh.hexGrid", params, replay, error));
        CHECK(first.positions() == replay.positions());
        CHECK_EQ(first.getVertexCount(), 42);
        CHECK_EQ(first.getIndexCount(), 108);
        params.setBool("planar", true);
        params.setBool("flipFaces", true);
        MeshBuild planar;
        REQUIRE(registry.generate("mesh.hexGrid", params, planar, error));
        for (int i = 0; i < planar.getVertexCount(); ++i) CHECK(std::fabs(planar.getPositionY(i)) < 1e-6f);
        CHECK(planar.getNormalY(0) < 0.f);
    };
}

TEST_CASE("Mesh deformation session owns an editable copy with undo and bake boundaries") {
    SUB_CASE("inflation changes the center; undo and bake define separate baselines") {
        MeshDeformationSession session;
        CHECK(!session.applyBrushResult("inflate", 0, 0, 0, 1, 1, 1).ok());
        REQUIRE(session.initializeResult(sculptPlane()).ok());
        const auto before = session.revision();
        REQUIRE(session.applyBrushResult("inflate", 0, 0, 0, .75f, 1, 1).ok());
        auto raised = session.currentMeshResult();
        REQUIRE(raised.ok());
        CHECK(raised.value().getPositionY(2) > .99f);
        CHECK_EQ(session.undoCount(), 1);
        CHECK(session.revision() > before);
        REQUIRE(session.undoResult().ok());
        CHECK(std::fabs(session.currentMeshResult().value().getPositionY(2)) < 1e-6f);
        REQUIRE(session.applyBrushResult("dent", 0, 0, 0, .75f, .5f, 1).ok());
        REQUIRE(session.bakeResult().ok());
        CHECK_EQ(session.undoCount(), 0);
        REQUIRE(session.applyBrushResult("directional", 0, 0, 0, .75f, .5f, 1, 1, 0, 0).ok());
        REQUIRE(session.restoreResult().ok());
        CHECK(session.currentMeshResult().value().getPositionY(2) < -.49f);
    };
    SUB_CASE("unsupported brush leaves revision and undo history unchanged") {
        MeshDeformationSession session;
        REQUIRE(session.initializeResult(sculptPlane()).ok());
        const auto revision = session.revision();
        CHECK(!session.applyBrushResult("unknown", 0, 0, 0, 1, 1, 1).ok());
        CHECK_EQ(session.revision(), revision);
        CHECK_EQ(session.undoCount(), 0);
    };
    SUB_CASE("plastic impact is capped and persists through elastic recovery") {
        MeshDeformationSession session;
        REQUIRE(session.initializeResult(sculptPlane()).ok());
        REQUIRE(session.applyImpactResult(0, 0, 0, 0, -4, 0, .8f, .5f, 1.5f, .3f).ok());
        CHECK(std::fabs(session.currentMeshResult().value().getPositionY(2) + .3f) < 1e-6f);
        REQUIRE(session.applyImpactResult(0, -.3f, 0, 0, -4, 0, .8f, .5f, 1.5f, .3f).ok());
        REQUIRE(session.recoverSurfaceResult(1.f, 100.f).ok());
        CHECK(std::fabs(session.currentMeshResult().value().getPositionY(2) + .3f) < 1e-6f);
    };
}

TEST_CASE("Mesh impact blocks accelerate a query without changing geometry and invalidate after brush edits") {
    MeshDeformationSession full, blocked;
    REQUIRE(full.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.prepareImpactVertexBlocksResult(4).ok());
    CHECK(blocked.hasImpactVertexBlocks());
    REQUIRE(full.applyImpactResult(0, 0, 0, 0, -2, 0, .8f, .25f, 1.5f, .4f).ok());
    REQUIRE(blocked.applyImpactResult(0, 0, 0, 0, -2, 0, .8f, .25f, 1.5f, .4f).ok());
    CHECK_EQ(full.currentMeshResult().value().positions(), blocked.currentMeshResult().value().positions());
    REQUIRE(blocked.applyBrushResult("inflate", 0, 0, 0, .8f, .1f, 1.f).ok());
    CHECK(!blocked.hasImpactVertexBlocks());
    const auto before = blocked.revision();
    CHECK(!blocked.prepareImpactVertexBlocksResult(0).ok());
    CHECK_EQ(blocked.revision(), before);
}

TEST_CASE("Mesh merge defaults to separate surfaces and opt-in blend publishes contact weights") {
    SUB_CASE("empty plan rejects and default merge retains both cube vertex sets") {
        MeshMergePlan empty;
        CHECK(!mergeStaticMeshes(empty).ok());
        MeshMergePlan plan;
        REQUIRE(plan.appendSource(cube(0.f), 0, 0, 0, 0, 1, 1, 1, "a").ok());
        REQUIRE(plan.appendSource(cube(1.2f), 0, 0, 0, 0, 1, 1, 1, "b").ok());
        CHECK(!plan.getEnableContactBlend());
        const auto merged = mergeStaticMeshes(plan);
        REQUIRE(merged.ok());
        CHECK_EQ(merged.value().getVertexCount(), 16);
        CHECK(!merged.value().hasVertexColors());
        CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("0"));
    };
    SUB_CASE("contact blend weights stay normalized and distinguish touching vertices") {
        MeshMergePlan plan;
        REQUIRE(plan.appendSource(cube(0.f), 0, 0, 0, 0, 1, 1, 1, "a").ok());
        REQUIRE(plan.appendSource(cube(.9f), 0, 0, 0, 0, 1, 1, 1, "b").ok());
        REQUIRE(plan.setEnableContactBlend(true).ok());
        MeshContactBlendParams blend;
        blend.edgeRadius     = .75f;
        blend.materialRadius = .75f;
        blend.strength       = 1.f;
        blend.normalsBlend   = 1.f;
        blend.materialBlend  = 1.f;
        blend.falloff        = "linear";
        REQUIRE(plan.setContactBlendParams(blend).ok());
        REQUIRE(plan.setPivotMode(MeshMergePlan::PivotMode::WorldOrigin).ok());
        const auto merged = mergeStaticMeshes(plan);
        REQUIRE(merged.ok());
        REQUIRE(merged.value().hasVertexColors());
        CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("1"));
        bool contact = false;
        for (int i = 0; i < merged.value().getVertexCount(); ++i) {
            const float weight = merged.value().getColor(i, 3);
            CHECK(weight >= 0.f);
            CHECK(weight <= 1.f);
            contact |= weight > 0.f;
        }
        CHECK(contact);
    };
}

TEST_CASE("Mesh adhesion projects toward a separate surface and releases derived state on removal") {
    auto                   source = cube(0.f);
    auto                   base   = ground();
    MeshContactBlendParams blend;
    blend.edgeRadius        = 1.f;
    blend.materialRadius    = 1.f;
    blend.strength          = 1.f;
    blend.normalsBlend      = 1.f;
    blend.softSnapPositions = true;
    blend.falloff           = "smooth";
    SUB_CASE("soft snapping moves source vertices toward ground and leaves source owned") {
        const auto adhered = meshContactBlendAgainstSurfaceResult(source, base, blend);
        REQUIRE(adhered.ok());
        CHECK(adhered.value().hasVertexColors());
        CHECK_EQ(adhered.value().getMeta("contactBlend.normals", ""), std::string("recalculated+smoothed"));
        CHECK_EQ(source.getVertexCount(), 8);
    };
    SUB_CASE("live derived mesh revision increases after parameter edit then is removed") {
        MeshAdhereLive live;
        REQUIRE(live.activateResult(source, base).ok());
        REQUIRE(live.evaluateResult(false).ok());
        const auto first = live.revision();
        REQUIRE(live.derivedMesh() != nullptr);
        REQUIRE(live.setParamsResult(blend).ok());
        CHECK(live.isDirty());
        REQUIRE(live.evaluateResult(false).ok());
        CHECK(live.revision() > first);
        const auto snapshot = live.derivedMeshResult();
        REQUIRE(snapshot.ok());
        CHECK_EQ(snapshot.value().getVertexCount(), source.getVertexCount());
        live.removeSetup();
        CHECK(!live.isActive());
        CHECK(!live.derivedMeshResult().ok());
    };
}

}  // namespace test_mesh_editing_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_lsystem)
#include <set>
#include <string>
#include "PrivateTestScope.h"
#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/LSystem.h"

namespace test_lsystem_contract {
using namespace eve::procgen;

TEST_CASE("L-system grammar documents deterministic expansion and seeded alternatives") {
    SUB_CASE("one replacement rule grows with iteration count and replays exactly") {
        LSystem grammar;
        grammar.setAxiom("F");
        grammar.addRule('F', "F[+F]F[-F]F");
        grammar.setIterations(3);
        grammar.setSeed(42);
        const auto third = grammar.derive();
        CHECK(!third.empty());
        CHECK_EQ(grammar.derive(), third);
        grammar.setIterations(4);
        CHECK(grammar.derive().size() > third.size());
    };
    SUB_CASE("same seed selects same stochastic grammar while a seed range explores alternatives") {
        LSystem grammar;
        grammar.setAxiom("A");
        grammar.addRules('A', {"F[+A]A", "F[-A]A", "FA"}, {1.f, 1.f, 1.f});
        grammar.setIterations(4);
        grammar.setSeed(7);
        const auto chosen = grammar.derive();
        grammar.setSeed(7);
        CHECK_EQ(grammar.derive(), chosen);
        std::set<std::string> variants;
        for (int seed = 1; seed <= 8; ++seed) {
            grammar.setSeed(seed);
            variants.insert(grammar.derive());
        }
        CHECK(variants.size() >= 2u);
    };
    SUB_CASE("brackets produce branch segments and restore trunk depth") {
        LSystem grammar;
        grammar.setAxiom("F[+F]F[-F]F");
        grammar.setIterations(1);
        grammar.setAngle(25.f);
        grammar.setStep(1.f);
        LSystemResult output;
        grammar.generate(output);
        CHECK(!output.derivation.empty());
        CHECK(!output.segments.empty());
        bool trunk = false, branch = false;
        for (const auto& segment : output.segments) {
            trunk |= segment.depth == 0;
            branch |= segment.depth > 0;
            if (segment.leaf) CHECK(segment.leafSize > 0.f);
        }
        CHECK(trunk);
        REQUIRE(branch);
    };
}

TEST_CASE("L-system facade owns script handles and rejects unknown recipe styles") {
    Procgen procgen;
    auto    handleResult = procgen.newLSystemHandle();
    REQUIRE(handleResult.ok());
    auto handle  = std::move(handleResult).takeValue();
    auto grammar = procgen.resolveLSystem(handle);
    REQUIRE(grammar.isBound());
    grammar->setAxiom("F");
    grammar->addRule('F', "F[+F]F[-F]F");
    grammar->setIterations(2);
    PointSet points;
    grammar->toPointSet(points);
    CHECK(points.getCount() > 0);
    REQUIRE(procgen.release(handle).ok());

    auto paramsResult = Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle = std::move(paramsResult).takeValue();
    auto params       = Procgen::resolve(paramsHandle);
    REQUIRE(params.isBound());
    params->setString("style", "bogus");
    const auto rejected = procgen.buildMeshHandle("mesh.lsystem", paramsHandle);
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().describe().find("unknown style") != std::string::npos);
    REQUIRE(Procgen::release(paramsHandle).ok());
}

}  // namespace test_lsystem_contract
#endif
