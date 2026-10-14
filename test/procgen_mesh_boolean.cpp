#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
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
#include "zeroerr/unittest.h"

// procgen_mesh_boolean.cpp
namespace {

eve::procgen::MeshBuild cube(float centerX) {
    eve::procgen::MeshBuild mesh;
    const float             x0 = centerX - 1.f, x1 = centerX + 1.f;
    const float             positions[8][3] = {{x0, -1, -1}, {x1, -1, -1}, {x1, 1, -1}, {x0, 1, -1},
                                               {x0, -1, 1},  {x1, -1, 1},  {x1, 1, 1},  {x0, 1, 1}};
    for (const auto& p : positions) mesh.addVertex(p[0], p[1], p[2], 0.f, 1.f, 0.f, 0.f, 0.f);
    const int triangles[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    mesh.setActiveGroup("shell");
    for (const auto& triangle : triangles) mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
    return mesh;
}

double signedVolume(const eve::procgen::MeshBuild& mesh) {
    double volume = 0.0;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int    a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        const double ax = mesh.getPositionX(a), ay = mesh.getPositionY(a), az = mesh.getPositionZ(a);
        const double bx = mesh.getPositionX(b), by = mesh.getPositionY(b), bz = mesh.getPositionZ(b);
        const double cx = mesh.getPositionX(c), cy = mesh.getPositionY(c), cz = mesh.getPositionZ(c);
        volume += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    }
    return volume / 6.0;
}

}  // namespace

TEST_CASE("procgen.meshBoolean.closesUnionDifferenceAndIntersection") {
    const auto left         = cube(0.f);
    const auto right        = cube(1.f);
    auto       unionMesh    = eve::procgen::meshBooleanResult(left, right, "union");
    auto       difference   = eve::procgen::meshBooleanResult(left, right, "difference");
    auto       intersection = eve::procgen::meshBooleanResult(left, right, "intersection");
    REQUIRE(unionMesh.ok());
    REQUIRE(difference.ok());
    REQUIRE(intersection.ok());
    CHECK(std::abs(std::abs(signedVolume(unionMesh.value())) - 12.0) < 0.01);
    CHECK(std::abs(std::abs(signedVolume(difference.value())) - 4.0) < 0.01);
    CHECK(std::abs(std::abs(signedVolume(intersection.value())) - 4.0) < 0.01);
    CHECK_EQ(difference.value().getMeta("boolean.operation", ""), std::string("difference"));
    CHECK(difference.value().getGroupCount() >= 2);
}

TEST_CASE("procgen.meshBoolean.rejectsInvalidInputsWithoutMutation") {
    const auto              left = cube(0.f);
    eve::procgen::MeshBuild empty;
    CHECK(!eve::procgen::meshBooleanResult(left, empty, "difference").ok());
    CHECK(!eve::procgen::meshBooleanResult(left, left, "xor").ok());
    CHECK_EQ(left.getVertexCount(), 8);
    CHECK_EQ(left.getIndexCount(), 36);
}

TEST_CASE("procgen.meshBoolean.executesThroughModifierGraph") {
    eve::procgen::MeshModifierGraph graph;
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
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK(std::abs(std::abs(signedVolume(result.value())) - 4.0) < 0.01);
    CHECK_EQ(graph.compiledSegmentCount(), 4);
}

// procgen_mesh_deformation_geometry.cpp
using namespace eve::procgen;

TEST_CASE("procgen.meshDeformationGeometry.extendedPlaneSupportsCurvatureAndSharpNormals") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params smooth;
    smooth.setInt("segmentsX", 4);
    smooth.setInt("segmentsZ", 3);
    smooth.setFloat("width", 4.f);
    smooth.setFloat("depth", 3.f);
    smooth.setFloat("curvature", 1.f);
    MeshBuild   smoothMesh;
    std::string error;
    REQUIRE(registry.generate("mesh.extendedPlane", smooth, smoothMesh, error));
    CHECK_EQ(smoothMesh.getVertexCount(), 20);
    CHECK_EQ(smoothMesh.getIndexCount(), 72);
    float maximumY = 0.f;
    for (int index = 0; index < smoothMesh.getVertexCount(); ++index)
        maximumY = std::max(maximumY, smoothMesh.getPositionY(index));
    CHECK(maximumY > 0.99f);

    Params sharp = smooth;
    sharp.setBool("sharpNormals", true);
    MeshBuild sharpMesh;
    REQUIRE(registry.generate("mesh.extendedPlane", sharp, sharpMesh, error));
    CHECK_EQ(sharpMesh.getVertexCount(), 48);
    CHECK_EQ(sharpMesh.getMeta("normalMode", ""), "sharp");
}

TEST_CASE("procgen.meshDeformationGeometry.hexGridIsDeterministicAndConfigurable") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setInt("columns", 3);
    params.setInt("rows", 2);
    params.setFloat("radius", 0.5f);
    params.setFloat("height", 0.2f);
    params.setFloat("randomHeight", 1.f);
    params.setSeed(77);
    MeshBuild   a, b;
    std::string error;
    REQUIRE(registry.generate("mesh.hexGrid", params, a, error));
    REQUIRE(registry.generate("mesh.hexGrid", params, b, error));
    CHECK_EQ(a.positions(), b.positions());
    CHECK_EQ(a.getVertexCount(), 42);
    CHECK_EQ(a.getIndexCount(), 108);

    params.setBool("planar", true);
    params.setBool("flipFaces", true);
    MeshBuild planar;
    REQUIRE(registry.generate("mesh.hexGrid", params, planar, error));
    for (int index = 0; index < planar.getVertexCount(); ++index) CHECK(std::abs(planar.getPositionY(index)) < 1e-6f);
    CHECK(planar.getNormalY(0) < 0.f);
}

// procgen_mesh_deformation_session.cpp
using namespace eve;
using namespace eve::procgen;

namespace {

MeshBuild sculptPlane() {
    MeshBuild mesh;
    mesh.addVertex(-1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f);
    mesh.addVertex(-1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addVertex(1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addTriangle(0, 2, 1);
    mesh.addTriangle(0, 3, 2);
    mesh.addTriangle(2, 3, 4);
    mesh.addTriangle(1, 2, 4);
    return mesh;
}

class FakeMeshCompute final : public IMeshDeformationCompute {
public:
    Result<std::vector<float>> deform(MeshDeformationComputeRequest request) override {
        ++calls;
        lastOperation = request.operation;
        for (std::size_t i = 1; i < request.positions.size(); i += 3u) request.positions[i] += 0.25f;
        return Result<std::vector<float>>::success(std::move(request.positions));
    }
    int                             calls         = 0;
    MeshDeformationComputeOperation lastOperation = MeshDeformationComputeOperation::Inflate;
};

}  // namespace

TEST_CASE("procgen.meshDeformationSession.gpuProviderIsExplicitAndCommitsAtomically") {
    auto* previous = cap::query<IMeshDeformationCompute>();
    if (previous) cap::revoke<IMeshDeformationCompute>(previous);
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    auto       absent   = session.applyBrushGpuResult("inflate", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!absent.ok());
    CHECK_EQ(session.revision(), revision);

    FakeMeshCompute provider;
    cap::provide<IMeshDeformationCompute>(&provider);
    REQUIRE(session.applyBrushGpuResult("smooth", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f).ok());
    CHECK_EQ(provider.calls, 1);
    CHECK(provider.lastOperation == MeshDeformationComputeOperation::Smooth);
    CHECK(std::abs(session.currentMeshResult().value().getPositionY(2) - 0.25f) < 1e-6f);
    REQUIRE(session.applyImpactGpuResult(0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 1.f, 1.f, 1.f, 1.f).ok());
    CHECK(provider.lastOperation == MeshDeformationComputeOperation::Impact);
    CHECK_EQ(session.undoCount(), 2);
    cap::revoke<IMeshDeformationCompute>(&provider);
    if (previous) cap::provide<IMeshDeformationCompute>(previous);
}

TEST_CASE("procgen.meshDeformationSession.sculptsUndoesRestoresAndBakes") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    CHECK(session.isInitialized());
    const auto initialRevision = session.revision();

    REQUIRE(session.applyBrushResult("inflate", 0.f, 0.f, 0.f, 0.75f, 1.f, 1.f).ok());
    auto sculpted = session.currentMeshResult();
    REQUIRE(sculpted.ok());
    CHECK(sculpted.value().getPositionY(2) > 0.99f);
    CHECK_EQ(session.undoCount(), 1);
    CHECK(session.revision() > initialRevision);

    REQUIRE(session.undoResult().ok());
    auto undone = session.currentMeshResult();
    REQUIRE(undone.ok());
    CHECK(std::abs(undone.value().getPositionY(2)) < 1e-6f);

    REQUIRE(session.applyBrushResult("dent", 0.f, 0.f, 0.f, 0.75f, 0.5f, 1.f).ok());
    REQUIRE(session.bakeResult().ok());
    CHECK_EQ(session.undoCount(), 0);
    REQUIRE(session.applyBrushResult("directional", 0.f, 0.f, 0.f, 0.75f, 0.5f, 1.f, 1.f, 0.f, 0.f).ok());
    REQUIRE(session.restoreResult().ok());
    auto restored = session.currentMeshResult();
    REQUIRE(restored.ok());
    CHECK(restored.value().getPositionY(2) < -0.49f);
    CHECK(std::abs(restored.value().getPositionX(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.rejectsInvalidEditsAtomically") {
    MeshDeformationSession session;
    auto                   beforeInit = session.applyBrushResult("inflate", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!beforeInit.ok());
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    auto       invalid  = session.applyBrushResult("unknown", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!invalid.ok());
    CHECK_EQ(session.revision(), revision);
    CHECK_EQ(session.undoCount(), 0);
}

TEST_CASE("procgen.meshDeformationSession.accumulatesBoundedPlasticImpacts") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.applyImpactResult(0.f, 0.f, 0.f, 0.f, -4.f, 0.f, 0.8f, 0.5f, 1.5f, 0.3f).ok());
    auto first = session.currentMeshResult();
    REQUIRE(first.ok());
    CHECK(std::abs(first.value().getPositionY(2) + 0.3f) < 1e-6f);
    REQUIRE(session.applyImpactResult(0.f, -0.3f, 0.f, 0.f, -4.f, 0.f, 0.8f, 0.5f, 1.5f, 0.3f).ok());
    auto second = session.currentMeshResult();
    REQUIRE(second.ok());
    CHECK(std::abs(second.value().getPositionY(2) + 0.3f) < 1e-6f);
    CHECK_EQ(session.undoCount(), 2);
    REQUIRE(session.recoverSurfaceResult(1.f, 100.f).ok());
    auto afterRecovery = session.currentMeshResult();
    REQUIRE(afterRecovery.ok());
    CHECK(std::abs(afterRecovery.value().getPositionY(2) + 0.3f) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.vertexBlocksMatchFullImpactAndInvalidateOnUnindexedEdits") {
    MeshDeformationSession fullScan;
    MeshDeformationSession blocked;
    REQUIRE(fullScan.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.prepareImpactVertexBlocksResult(4).ok());
    CHECK(blocked.hasImpactVertexBlocks());
    CHECK(blocked.impactVertexBlockCount() > 0);
    REQUIRE(fullScan.applyImpactResult(0.f, 0.f, 0.f, 0.f, -2.f, 0.f, 0.8f, 0.25f, 1.5f, 0.4f).ok());
    REQUIRE(blocked.applyImpactResult(0.f, 0.f, 0.f, 0.f, -2.f, 0.f, 0.8f, 0.25f, 1.5f, 0.4f).ok());
    auto expected = fullScan.currentMeshResult();
    auto actual   = blocked.currentMeshResult();
    REQUIRE(expected.ok());
    REQUIRE(actual.ok());
    CHECK_EQ(expected.value().positions(), actual.value().positions());
    CHECK(blocked.hasImpactVertexBlocks());

    REQUIRE(blocked.applyBrushResult("inflate", 0.f, 0.f, 0.f, 0.8f, 0.1f, 1.f).ok());
    CHECK(!blocked.hasImpactVertexBlocks());
    const auto revision = blocked.revision();
    CHECK(!blocked.prepareImpactVertexBlocksResult(0).ok());
    CHECK_EQ(blocked.revision(), revision);
}

TEST_CASE("procgen.meshDeformationSession.interactiveSurfaceRecoversElasticAndPreservesPlasticDisplacement") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.applySurfaceContactResult(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.8f, 1.f, 0.5f, 1.f, 0.25f)
                .ok());
    auto contacted = session.currentMeshResult();
    REQUIRE(contacted.ok());
    CHECK(std::abs(contacted.value().getPositionX(2) - 0.5f) < 1e-6f);
    CHECK(std::abs(contacted.value().getPositionY(2) + 1.f) < 1e-6f);
    const auto revision = session.revision();
    REQUIRE(session.recoverSurfaceResult(1.f, 100.f).ok());
    auto recovered = session.currentMeshResult();
    REQUIRE(recovered.ok());
    CHECK(std::abs(recovered.value().getPositionX(2) - 0.125f) < 1e-5f);
    CHECK(std::abs(recovered.value().getPositionY(2) + 0.25f) < 1e-5f);
    CHECK(session.revision() > revision);
    CHECK_EQ(session.undoCount(), 1);
    REQUIRE(session.undoResult().ok());
    auto undone = session.currentMeshResult();
    REQUIRE(undone.ok());
    CHECK(std::abs(undone.value().getPositionX(2)) < 1e-6f);
    CHECK(std::abs(undone.value().getPositionY(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.interactiveSurfaceRejectsInvalidContactAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(
        !session.applySurfaceContactResult(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 0.f).ok());
    CHECK(!session.recoverSurfaceResult(-1.f, 1.f).ok());
    CHECK_EQ(session.revision(), revision);
    CHECK_EQ(session.undoCount(), 0);
}

TEST_CASE("procgen.meshDeformationSession.meshSlimeUsesBoundedDeterministicSpringDynamics") {
    MeshDeformationSession first;
    MeshDeformationSession second;
    REQUIRE(first.initializeResult(sculptPlane()).ok());
    REQUIRE(second.initializeResult(sculptPlane()).ok());
    for (auto* session : {&first, &second}) {
        REQUIRE(session->applySlimeImpulseResult(0.f, 0.f, 0.f, 1.f, 2.f, 0.f, 0.8f, 1.f).ok());
        for (int step = 0; step < 20; ++step) REQUIRE(session->stepSlimeResult(0.01f, 12.f, 1.5f, 3.f).ok());
    }
    auto a = first.currentMeshResult();
    auto b = second.currentMeshResult();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().positions() == b.value().positions());
    CHECK(a.value().getPositionX(2) > 0.f);
    CHECK(a.value().getPositionY(2) > 0.f);
    CHECK(a.value().getPositionY(2) < 0.6f);
    CHECK_EQ(first.undoCount(), 1);
    REQUIRE(first.undoResult().ok());
    CHECK(std::abs(first.currentMeshResult().value().getPositionY(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.colliderRefreshSupportsAllModesAndOwningOffsetSnapshot") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.configureColliderRefreshResult("once", 0.f, 1.f, -2.f, 3.f).ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(!session.updateColliderRefreshResult(0.f).value());
    auto collider = session.colliderMeshResult();
    REQUIRE(collider.ok());
    CHECK(std::abs(collider.value().getPositionX(2) - 1.f) < 1e-6f);
    CHECK(std::abs(collider.value().getPositionY(2) + 2.f) < 1e-6f);
    CHECK(std::abs(collider.value().getPositionZ(2) - 3.f) < 1e-6f);
    CHECK_EQ(collider.value().getMeta("purpose", ""), "dynamicCollider");

    REQUIRE(session.configureColliderRefreshResult("everyFrame", 0.f).ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(session.updateColliderRefreshResult(0.016f).value());
    REQUIRE(session.configureColliderRefreshResult("interval", 0.1f).ok());
    const float scriptLikeA = static_cast<float>(0.04);
    const float scriptLikeB = static_cast<float>(0.06);
    CHECK(!session.updateColliderRefreshResult(scriptLikeA).value());
    CHECK(session.updateColliderRefreshResult(scriptLikeB).value());
    CHECK(!session.updateColliderRefreshResult(0.01f).value());
    REQUIRE(session.configureColliderRefreshResult("manual", 0.f).ok());
    CHECK(!session.updateColliderRefreshResult(1.f).value());
    REQUIRE(session.requestColliderRefreshResult().ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(!session.updateColliderRefreshResult(0.f).value());
}

TEST_CASE("procgen.meshDeformationSession.colliderRefreshRejectsInvalidConfigurationAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(!session.configureColliderRefreshResult("interval", 0.f).ok());
    CHECK(!session.configureColliderRefreshResult("unknown", 1.f).ok());
    CHECK(!session.requestColliderRefreshResult().ok());
    CHECK_EQ(session.revision(), revision);
}

TEST_CASE("procgen.meshDeformationSession.runtimeVertexEditorSelectsMovesAndManipulatesUndoably") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    auto sphere = session.selectVerticesSphereResult(0.f, 0.f, 0.f, 0.1f, true);
    REQUIRE(sphere.ok());
    CHECK_EQ(sphere.value(), 1);
    REQUIRE(session.moveSelectedVerticesResult(1.f, 0.5f, 0.f).ok());
    CHECK(std::abs(session.currentMeshResult().value().getPositionX(2) - 1.f) < 1e-6f);
    CHECK(std::abs(session.currentMeshResult().value().getPositionY(2) - 0.5f) < 1e-6f);
    REQUIRE(session.undoResult().ok());
    CHECK(std::abs(session.currentMeshResult().value().getPositionX(2)) < 1e-6f);

    auto box = session.selectVerticesBoxResult(-1.1f, -0.1f, -1.1f, 0.1f, 0.1f, 1.1f, true);
    REQUIRE(box.ok());
    CHECK_EQ(box.value(), 3);
    REQUIRE(session.manipulateSelectedVerticesResult("pull", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.25f).ok());
    CHECK(session.currentMeshResult().value().getPositionX(0) < -1.f);
    REQUIRE(session.manipulateSelectedVerticesResult("grab", 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f).ok());
    CHECK(session.currentMeshResult().value().getPositionY(2) > 0.49f);
    session.clearVertexSelection();
    CHECK_EQ(session.selectedVertexCount(), 0);
    CHECK(!session.moveSelectedVerticesResult(1.f, 0.f, 0.f).ok());
}

TEST_CASE("procgen.meshDeformationSession.runtimeVertexEditorRejectsInvalidSelectionAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(!session.selectVerticesSphereResult(0.f, 0.f, 0.f, -1.f).ok());
    CHECK(!session.selectVerticesBoxResult(1.f, 0.f, 0.f, -1.f, 1.f, 1.f).ok());
    CHECK(!session.manipulateSelectedVerticesResult("warp", 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f).ok());
    CHECK_EQ(session.revision(), revision);
}

// procgen_mesh_merge_adhere.cpp
namespace {

eve::procgen::MeshBuild unitCube(float cx, float cy, float cz, float half = 0.5f) {
    eve::procgen::MeshBuild mesh;
    const float             x0 = cx - half, x1 = cx + half;
    const float             y0 = cy - half, y1 = cy + half;
    const float             z0 = cz - half, z1 = cz + half;
    const float             positions[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                                               {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    for (const auto& p : positions) mesh.addVertex(p[0], p[1], p[2], 0.f, 1.f, 0.f, 0.f, 0.f);
    const int triangles[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    mesh.setActiveGroup("shell");
    for (const auto& triangle : triangles) mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
    return mesh;
}

eve::procgen::MeshBuild flatPlaneY0(float half = 2.f) {
    eve::procgen::MeshBuild mesh;
    mesh.addVertex(-half, 0.f, -half, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(half, 0.f, -half, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(half, 0.f, half, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addVertex(-half, 0.f, half, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.setActiveGroup("ground");
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

}  // namespace

TEST_CASE("procgen.meshMerge.defaultSkipsContactBlend") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(0.f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(1.2f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    CHECK(!plan.getEnableContactBlend());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("0"));
    CHECK(!merged.value().hasVertexColors());
    CHECK_EQ(merged.value().getVertexCount(), 16);
}

TEST_CASE("procgen.meshMerge.emptyPlanFails") {
    eve::procgen::MeshMergePlan plan;
    CHECK(!eve::procgen::mergeStaticMeshes(plan).ok());
}

TEST_CASE("procgen.meshMerge.optInContactBlendWritesWeights") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(0.f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(0.9f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    REQUIRE(plan.setEnableContactBlend(true).ok());
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius     = 0.75f;
    params.materialRadius = 0.75f;
    params.strength       = 1.f;
    params.normalsBlend   = 1.f;
    params.materialBlend  = 1.f;
    params.falloff        = "linear";
    REQUIRE(plan.setContactBlendParams(params).ok());
    // Keep world positions for contact distance (skip first-source pivot).
    REQUIRE(plan.setPivotMode(eve::procgen::MeshMergePlan::PivotMode::WorldOrigin).ok());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("1"));
    REQUIRE(merged.value().hasVertexColors());
    int positive = 0;
    for (int v = 0; v < merged.value().getVertexCount(); ++v) {
        const float w = merged.value().getColor(v, 3);
        CHECK(w >= 0.f);
        CHECK(w <= 1.f);
        if (w > 0.f) ++positive;
    }
    CHECK(positive > 0);
}

TEST_CASE("procgen.meshContactBlend.againstSurfaceSoftSnap") {
    const auto                           cube   = unitCube(0.f, 0.4f, 0.f, 0.25f);
    const auto                           ground = flatPlaneY0();
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius        = 1.f;
    params.materialRadius    = 1.f;
    params.strength          = 1.f;
    params.normalsBlend      = 1.f;
    params.materialBlend     = 1.f;
    params.softSnapPositions = true;
    params.falloff           = "smooth";
    auto adhered             = eve::procgen::meshContactBlendAgainstSurfaceResult(cube, ground, params);
    REQUIRE(adhered.ok());
    float minY = 1e9f;
    for (int v = 0; v < adhered.value().getVertexCount(); ++v) minY = std::min(minY, adhered.value().getPositionY(v));
    CHECK(minY < 0.2f);
    REQUIRE(adhered.value().hasVertexColors());
    CHECK_EQ(adhered.value().getMeta("contactBlend.normals", ""), std::string("recalculated+smoothed"));
}

TEST_CASE("procgen.meshMerge.softSnapThenWeldSharesContactVerts") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(-0.55f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(0.55f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    REQUIRE(plan.setEnableContactBlend(true).ok());
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius        = 1.2f;
    params.materialRadius    = 1.2f;
    params.strength          = 1.f;
    params.normalsBlend      = 1.f;
    params.materialBlend     = 1.f;
    params.softSnapPositions = true;
    params.falloff           = "smooth";
    REQUIRE(plan.setContactBlendParams(params).ok());
    REQUIRE(plan.setWeldTolerance(0.08f).ok());
    REQUIRE(plan.setPivotMode(eve::procgen::MeshMergePlan::PivotMode::WorldOrigin).ok());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.normals", ""), std::string("recalculated+smoothed"));
    // Two unit cubes start with 16 verts; post-blend weld must collapse some contact verts.
    CHECK(merged.value().getVertexCount() < 16);
}

TEST_CASE("procgen.meshAdhereLive.paramsChangeRevisionAndRemoveRestores") {
    eve::procgen::MeshAdhereLive live;
    const auto                   cube   = unitCube(0.f, 0.5f, 0.f, 0.25f);
    const auto                   ground = flatPlaneY0();
    REQUIRE(live.activateResult(cube, ground).ok());
    REQUIRE(live.evaluateResult(false).ok());
    const auto rev1 = live.revision();
    REQUIRE(live.derivedMesh() != nullptr);
    const float y1 = live.derivedMesh()->getPositionY(0);

    eve::procgen::MeshContactBlendParams strong = live.params();
    strong.strength                             = 1.f;
    strong.edgeRadius                           = 2.f;
    strong.materialRadius                       = 2.f;
    strong.softSnapPositions                    = true;
    REQUIRE(live.setParamsResult(strong).ok());
    CHECK(live.isDirty());
    REQUIRE(live.evaluateResult(false).ok());
    CHECK(live.revision() > rev1);
    REQUIRE(live.derivedMesh() != nullptr);
    const float y2 = live.derivedMesh()->getPositionY(0);
    CHECK(live.revision() > rev1);
    (void)y1;
    (void)y2;

    auto snapshot = live.derivedMeshResult();
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().getVertexCount(), cube.getVertexCount());
    CHECK(live.isActive());

    const int sourceVerts = cube.getVertexCount();
    live.removeSetup();
    CHECK(!live.isActive());
    CHECK(live.derivedMesh() == nullptr);
    CHECK(!live.derivedMeshResult().ok());
    CHECK_EQ(cube.getVertexCount(), sourceVerts);
}

TEST_CASE("procgen.meshAdhere.executesThroughModifierGraph") {
    eve::procgen::MeshModifierGraph graph;
    REQUIRE(graph.addNode("src", "mesh.input").ok());
    REQUIRE(graph.addNode("surf", "mesh.input").ok());
    REQUIRE(graph.addNode("adhere", "deform.meshAdhere").ok());
    REQUIRE(graph.addNode("out", "mesh.output").ok());
    REQUIRE(graph.connect("src", "adhere", 0).ok());
    REQUIRE(graph.connect("surf", "adhere", 1).ok());
    REQUIRE(graph.connect("adhere", "out", 0).ok());
    REQUIRE(graph.setNodeMesh("src", unitCube(0.f, 0.4f, 0.f, 0.25f)).ok());
    REQUIRE(graph.setNodeMesh("surf", flatPlaneY0()).ok());
    REQUIRE(graph.setNodeFloat("adhere", "edgeRadius", 1.f).ok());
    REQUIRE(graph.setNodeFloat("adhere", "materialRadius", 1.f).ok());
    REQUIRE(graph.setNodeFloat("adhere", "strength", 1.f).ok());
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getMeta("deformer", ""), std::string("deform.meshAdhere"));
}

TEST_CASE("procgen.meshContactBlend.rejectsMismatchedSourceIds") {
    const auto                           mesh = unitCube(0.f, 0.f, 0.f);
    std::vector<std::int32_t>            ids(4, 0);
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius = 1.f;
    CHECK(!eve::procgen::meshContactBlendResult(mesh, ids, params).ok());
}
