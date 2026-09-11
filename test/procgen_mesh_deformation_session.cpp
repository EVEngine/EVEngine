#include "procgen/mesh/MeshDeformationSession.h"

#include "zeroerr/unittest.h"

#include <cmath>

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

}  // namespace

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
