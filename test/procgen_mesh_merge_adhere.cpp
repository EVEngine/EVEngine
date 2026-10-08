#include "procgen/mesh/MeshAdhereLive.h"
#include "procgen/mesh/MeshContactBlend.h"
#include "procgen/mesh/MeshMerge.h"
#include "procgen/mesh/MeshModifierGraph.h"

#include <zeroerr/unittest.h>

#include <cmath>
#include <cstdint>
#include <vector>

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
    const auto cube   = unitCube(0.f, 0.4f, 0.f, 0.25f);
    const auto ground = flatPlaneY0();
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
    for (int v = 0; v < adhered.value().getVertexCount(); ++v)
        minY = std::min(minY, adhered.value().getPositionY(v));
    CHECK(minY < 0.2f);
    REQUIRE(adhered.value().hasVertexColors());
    CHECK_EQ(adhered.value().getMeta("contactBlend.normals", ""), std::string("recalculated"));
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
    CHECK_EQ(merged.value().getMeta("contactBlend.normals", ""), std::string("recalculated"));
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
    const auto mesh = unitCube(0.f, 0.f, 0.f);
    std::vector<std::int32_t> ids(4, 0);
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius = 1.f;
    CHECK(!eve::procgen::meshContactBlendResult(mesh, ids, params).ok());
}
