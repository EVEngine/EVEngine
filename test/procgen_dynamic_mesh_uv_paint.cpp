#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/MeshUvProjection.h"
#include "procgen/Procgen.h"

#include "image/ImageData.h"
#include "zeroerr/unittest.h"

using namespace eve;

namespace {
procgen::MeshBuild paintTriangle(float lift = 0.f) {
    procgen::MeshBuild mesh;
    mesh.addVertex(0.f,lift,0.f,0.f,1.f,0.f,0.f,0.f);
    mesh.addVertex(1.f,lift,0.f,0.f,1.f,0.f,1.f,0.f);
    mesh.addVertex(0.f,lift,1.f,0.f,1.f,0.f,0.f,1.f);
    mesh.addTriangle(0,1,2);
    return mesh;
}
}

TEST_CASE("procgen.dynamicMeshUvPaint.reusesCanonicalPainterAcrossMeshUpdates") {
    image::ImageData pixels(16, 16, "RGBA8");
    procgen::DynamicMeshUvPaintSession session;
    REQUIRE(session.initializeResult(paintTriangle(), pixels).ok());
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 0.f, 0.25f, 2.f, 1.f, 0.f, 0.f, 1.f).ok());
    const auto paintRevision = session.paintRevision();
    REQUIRE(session.updateMeshResult(paintTriangle(1.f)).ok());
    CHECK_EQ(session.paintRevision(), paintRevision);
    REQUIRE(session.paintSurfacePointResult(0, 0.25f, 1.f, 0.25f, 2.f, 0.f, 1.f, 0.f, 1.f).ok());
    CHECK(session.paintRevision() > paintRevision);
    REQUIRE(session.undoResult().ok());
    REQUIRE(session.currentImageResult().ok());
}

TEST_CASE("procgen.dynamicMeshUvPaint.moduleHandleDetectsRelease") {
    auto* procgen = procgen::Procgen::create();
    auto created = procgen->newDynamicMeshUvPaintSessionHandle();
    REQUIRE(created.ok());
    const auto handle = created.value();
    CHECK(procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
    REQUIRE(procgen->release(handle).ok());
    CHECK(procgen->isStale(handle));
    CHECK(!procgen->resolveDynamicMeshUvPaintSession(handle).isBound());
}
