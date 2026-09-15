#include "procgen/mesh/MeshModifierGraph.h"

#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve;
using namespace eve::procgen;

namespace {

MeshBuild triangle(float offsetX = 0.f) {
    MeshBuild mesh;
    mesh.addVertex(offsetX + 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(offsetX + 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(offsetX + 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

MeshBuild tetrahedron(float tipOffset = 0.f) {
    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addVertex(tipOffset, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addTriangle(0, 2, 1);
    mesh.addTriangle(0, 1, 3);
    mesh.addTriangle(1, 2, 3);
    mesh.addTriangle(2, 0, 3);
    return mesh;
}

MeshBuild fitSurface() {
    MeshBuild mesh;
    mesh.addVertex(-2.f, -2.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(2.f, -2.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(2.f, 2.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    mesh.addVertex(-2.f, 2.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

void requireOk(Result<void> result) { REQUIRE(result.ok()); }

}  // namespace

TEST_CASE("procgen.meshModifierGraph.fusesConsecutiveVertexOperations") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.addNode("twist", "deform.twist"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("source", "move"));
    requireOk(graph.connect("move", "twist"));
    requireOk(graph.connect("twist", "out"));
    requireOk(graph.setNodeFloat("move", "x", 2.f));
    requireOk(graph.setNodeFloat("move", "maskRadius", 0.25f));
    requireOk(graph.setNodeFloat("twist", "angle", 0.f));
    requireOk(graph.setNodeMesh("source", triangle()));

    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    const auto output = std::move(result).takeValue();
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK(std::abs(output.getPositionX(0) - 2.f) < 1e-6f);
    CHECK(std::abs(output.getPositionX(1) - 1.f) < 1e-6f);
    CHECK_EQ(graph.compiledSegmentCount(), 3);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    auto cached = graph.executeResult("out");
    REQUIRE(cached.ok());
    CHECK_EQ(graph.metrics().size(), std::size_t(1));
    CHECK(graph.metrics().front().cacheHit);
}

TEST_CASE("procgen.meshModifierGraph.appendAndWeldAreFusionBarriers") {
    MeshModifierGraph graph;
    for (const auto* node : {"left", "right"}) requireOk(graph.addNode(node, "mesh.input"));
    requireOk(graph.addNode("append", "mesh.append"));
    requireOk(graph.addNode("weld", "mesh.weld"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("left", "append", 0));
    requireOk(graph.connect("right", "append", 1));
    requireOk(graph.connect("append", "weld"));
    requireOk(graph.connect("weld", "out"));
    requireOk(graph.setNodeFloat("weld", "tolerance", 0.001f));
    requireOk(graph.setNodeMesh("left", triangle()));
    requireOk(graph.setNodeMesh("right", triangle()));

    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    const auto output = std::move(result).takeValue();
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK_EQ(output.getIndexCount(), 6);
    CHECK_EQ(graph.fusedOperationCount(), 0);
}

TEST_CASE("procgen.meshModifierGraph.rejectsCyclesAndInvalidParameters") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("a", "deform.bend"));
    requireOk(graph.addNode("b", "deform.twist"));
    requireOk(graph.connect("b", "a"));
    requireOk(graph.connect("a", "b"));
    auto cycle = graph.executeResult("b");
    REQUIRE(!cycle.ok());
    const auto* diagnostic = cycle.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->code() == DiagnosticCode::Conflict);

    auto invalid = graph.setNodeString("a", "axis", "diagonal");
    REQUIRE(invalid.ok());
    requireOk(graph.disconnect("a"));
    requireOk(graph.connect("source", "a"));
    requireOk(graph.setNodeMesh("source", triangle()));
    auto badAxis = graph.executeResult("a");
    REQUIRE(!badAxis.ok());
    diagnostic = badAxis.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.supportsFfdMorphAndSpherify") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("ffd", "deform.ffd"));
    requireOk(graph.addNode("sphere", "deform.spherify"));
    requireOk(graph.connect("source", "ffd"));
    requireOk(graph.connect("ffd", "sphere"));
    requireOk(graph.setNodeFloat("ffd", "p001x", 0.5f));
    requireOk(graph.setNodeFloat("sphere", "weight", 0.f));
    requireOk(graph.setNodeMesh("source", tetrahedron()));
    auto deformed = graph.executeResult("sphere");
    REQUIRE(deformed.ok());
    CHECK(std::abs(deformed.value().getPositionX(3) - 0.5f) < 1e-6f);

    MeshModifierGraph morph;
    requireOk(morph.addNode("a", "mesh.input"));
    requireOk(morph.addNode("b", "mesh.input"));
    requireOk(morph.addNode("blend", "deform.morph"));
    requireOk(morph.connect("a", "blend", 0));
    requireOk(morph.connect("b", "blend", 1));
    requireOk(morph.setNodeFloat("blend", "weight", 0.5f));
    requireOk(morph.setNodeMesh("a", tetrahedron()));
    requireOk(morph.setNodeMesh("b", tetrahedron(1.f)));
    auto blended = morph.executeResult("blend");
    REQUIRE(blended.ok());
    CHECK(std::abs(blended.value().getPositionX(3) - 0.5f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.meshEffectorSupportsOneToFourWeightedNodesAndFusion") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("effector", "deform.effector"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("source", "effector"));
    requireOk(graph.connect("effector", "move"));
    requireOk(graph.connect("move", "out"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeInt("effector", "pointCount", 2));
    requireOk(graph.setNodeFloat("effector", "p0radius", 0.4f));
    requireOk(graph.setNodeFloat("effector", "p0dy", 2.f));
    requireOk(graph.setNodeFloat("effector", "p1x", 1.f));
    requireOk(graph.setNodeFloat("effector", "p1radius", 0.4f));
    requireOk(graph.setNodeFloat("effector", "p1dx", 1.f));
    requireOk(graph.setNodeFloat("effector", "p1dy", 0.f));
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionY(0) - 2.f) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(1) - 2.f) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(2) - 1.f) < 1e-6f);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    MeshModifierGraph invalid;
    requireOk(invalid.addNode("source", "mesh.input"));
    requireOk(invalid.addNode("effector", "deform.effector"));
    requireOk(invalid.connect("source", "effector"));
    requireOk(invalid.setNodeMesh("source", triangle()));
    requireOk(invalid.setNodeInt("effector", "pointCount", 5));
    CHECK(!invalid.executeResult("effector").ok());
}

TEST_CASE("procgen.meshModifierGraph.meshFitProjectsOntoSecondInputWithoutPhysicsDependency") {
    MeshBuild source = triangle();
    for (std::size_t index = 2; index < source.positions().size(); index += 3u) source.positions()[index] = 1.f;
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("surface", "mesh.input"));
    requireOk(graph.addNode("fit", "deform.meshFit"));
    requireOk(graph.connect("source", "fit", 0));
    requireOk(graph.connect("surface", "fit", 1));
    requireOk(graph.setNodeMesh("source", source));
    requireOk(graph.setNodeMesh("surface", fitSurface()));
    requireOk(graph.setNodeFloat("fit", "directionY", 0.f));
    requireOk(graph.setNodeFloat("fit", "directionZ", -1.f));
    requireOk(graph.setNodeFloat("fit", "surfaceOffset", 0.2f));
    auto fitted = graph.executeResult("fit");
    REQUIRE(fitted.ok());
    for (int vertex = 0; vertex < fitted.value().getVertexCount(); ++vertex)
        CHECK(std::abs(fitted.value().getPositionZ(vertex) - 0.2f) < 1e-6f);
    CHECK_EQ(fitted.value().getMeta("deformer", ""), "deform.meshFit");

    const auto revision = graph.revision();
    requireOk(graph.setNodeFloat("fit", "directionX", 0.f));
    requireOk(graph.setNodeFloat("fit", "directionZ", 0.f));
    CHECK(!graph.executeResult("fit").ok());
    CHECK(graph.revision() > revision);
}

TEST_CASE("procgen.meshModifierGraph.supportsSubdivisionAndPlaneCut") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("subdivide", "mesh.subdivide"));
    requireOk(graph.addNode("cut", "mesh.cutPlane"));
    requireOk(graph.connect("source", "subdivide"));
    requireOk(graph.connect("subdivide", "cut"));
    requireOk(graph.setNodeInt("subdivide", "levels", 1));
    requireOk(graph.setNodeFloat("cut", "normalX", 1.f));
    requireOk(graph.setNodeFloat("cut", "normalY", 0.f));
    requireOk(graph.setNodeFloat("cut", "distance", 0.25f));
    requireOk(graph.setNodeInt("cut", "cap", 1));
    requireOk(graph.setNodeMesh("source", tetrahedron()));
    auto result = graph.executeResult("cut");
    REQUIRE(result.ok());
    CHECK(result.value().getIndexCount() > 0);
    CHECK_EQ(result.value().getGroupName(result.value().getGroupCount() - 1), "__cut_cap");
    for (int i = 0; i < result.value().getVertexCount(); ++i) CHECK(result.value().getPositionX(i) >= 0.25f - 1e-6f);

    MeshModifierGraph subdivision;
    requireOk(subdivision.addNode("source", "mesh.input"));
    requireOk(subdivision.addNode("subdivide", "mesh.subdivide"));
    requireOk(subdivision.connect("source", "subdivide"));
    requireOk(subdivision.setNodeMesh("source", triangle()));
    auto subdivided = subdivision.executeResult("subdivide");
    REQUIRE(subdivided.ok());
    CHECK_EQ(subdivided.value().getIndexCount(), 12);
}

TEST_CASE("procgen.meshModifierGraph.deformsAlongCubicSplineOffsets") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("spline", "deform.spline"));
    requireOk(graph.connect("source", "spline"));
    requireOk(graph.setNodeFloat("spline", "c3x", 2.f));
    requireOk(graph.setNodeMesh("source", triangle()));
    auto result = graph.executeResult("spline");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(2) - 2.f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.deformsAlongOwningSplinePath") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(path.addPointResult({2.f, 2.f, 0.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeSplinePath("path", path));

    auto result = graph.executeResult("path");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(2) - 2.f) < 1e-5f);
    CHECK(std::abs(result.value().getPositionY(2) - 2.f) < 1e-5f);
}

TEST_CASE("procgen.meshModifierGraph.requiresReadySplinePathBinding") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", triangle()));

    auto missing = graph.executeResult("path");
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK(missing.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);

    SplinePath incomplete;
    requireOk(incomplete.addPointResult({0.f, 0.f, 0.f}));
    auto rejected = graph.setNodeSplinePath("path", incomplete);
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.status().primaryDiagnostic() != nullptr);
    CHECK(rejected.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);
}

TEST_CASE("procgen.meshModifierGraph.splineFrameStaysContinuousNearVerticalTangents") {
    SplinePath path;
    requireOk(path.setKindResult("bezier"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.2f, 1.f, 0.f}));
    requireOk(path.addPointResult({0.f, 2.f, 0.f, -0.2f, -1.f, 0.f, 0.f, 0.f, 0.f}));

    MeshBuild strip;
    for (float y : {0.f, 0.49f, 0.51f, 1.f}) strip.addVertex(1.f, y, 0.f, 0.f, 0.f, 1.f, 0.f, y);
    strip.addTriangle(0, 1, 2);
    strip.addTriangle(1, 3, 2);

    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", strip));
    requireOk(graph.setNodeSplinePath("path", path));
    auto result = graph.executeResult("path");
    REQUIRE(result.ok());
    const float dx = result.value().getPositionX(2) - result.value().getPositionX(1);
    const float dy = result.value().getPositionY(2) - result.value().getPositionY(1);
    const float dz = result.value().getPositionZ(2) - result.value().getPositionZ(1);
    CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 0.2f);
}

TEST_CASE("procgen.meshModifierGraph.generatesCappedSplineTubeWithoutMeshInput") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 2.f, 0.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeFloat("tube", "radius", 0.5f));
    requireOk(graph.setNodeInt("tube", "pathSegments", 4));
    requireOk(graph.setNodeInt("tube", "radialSegments", 8));
    requireOk(graph.setNodeInt("tube", "cap", 1));

    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    const auto& mesh = result.value();
    CHECK_EQ(mesh.getVertexCount(), 65);
    CHECK_EQ(mesh.getIndexCount(), 240);
    CHECK_EQ(mesh.getGroupCount(), 2);
    CHECK_EQ(mesh.getGroupName(0), "tube");
    CHECK_EQ(mesh.getGroupName(1), "caps");
    CHECK_EQ(mesh.getMeta("generator", ""), "mesh.splineTube");
    for (int vertex = 45; vertex < 55; ++vertex) CHECK(std::abs(mesh.getNormalY(vertex) + 1.f) < 1e-6f);
    CHECK(std::abs(mesh.getPositionY(0)) < 1e-6f);
    CHECK(
        std::abs(std::sqrt(mesh.getPositionX(0) * mesh.getPositionX(0) + mesh.getPositionZ(0) * mesh.getPositionZ(0)) -
                 0.5f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.closesSplineTubeSeamAndOmitsCaps") {
    SplinePath path;
    requireOk(path.setKindResult("catmullRom"));
    path.setClosed(true);
    requireOk(path.addPointResult({-1.f, 0.f, -1.f}));
    requireOk(path.addPointResult({1.f, 0.f, -1.f}));
    requireOk(path.addPointResult({1.f, 0.f, 1.f}));
    requireOk(path.addPointResult({-1.f, 0.f, 1.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "pathSegments", 8));
    requireOk(graph.setNodeInt("tube", "radialSegments", 6));
    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    const auto& mesh = result.value();
    CHECK_EQ(mesh.getVertexCount(), 63);
    CHECK_EQ(mesh.getIndexCount(), 288);
    CHECK_EQ(mesh.getGroupCount(), 1);
    for (int radial = 0; radial <= 6; ++radial) {
        const int seam = 8 * 7 + radial;
        CHECK(std::abs(mesh.getPositionX(radial) - mesh.getPositionX(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getPositionY(radial) - mesh.getPositionY(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getPositionZ(radial) - mesh.getPositionZ(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getNormalX(radial) - mesh.getNormalX(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getUvU(seam) - 1.f) < 1e-6f);
    }
}

TEST_CASE("procgen.meshModifierGraph.rejectsInvalidSplineTubeBudgets") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    auto missing = graph.executeResult("tube");
    REQUIRE(!missing.ok());
    CHECK(missing.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);

    SplinePath path;
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "radialSegments", 2));
    auto invalid = graph.executeResult("tube");
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.generatesSplineRibbonAndSolidRoadGroups") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 4.f}));

    MeshModifierGraph ribbon;
    requireOk(ribbon.addNode("road", "mesh.splineRibbon"));
    requireOk(ribbon.setNodeSplinePath("road", path));
    requireOk(ribbon.setNodeFloat("road", "width", 2.f));
    requireOk(ribbon.setNodeInt("road", "pathSegments", 4));
    auto flat = ribbon.executeResult("road");
    REQUIRE(flat.ok());
    CHECK_EQ(flat.value().getVertexCount(), 10);
    CHECK_EQ(flat.value().getIndexCount(), 24);
    CHECK_EQ(flat.value().getGroupCount(), 1);
    CHECK_EQ(flat.value().getGroupName(0), "surface");
    CHECK(std::abs(flat.value().getPositionX(0) + 1.f) < 1e-6f);
    CHECK(std::abs(flat.value().getPositionX(1) - 1.f) < 1e-6f);

    MeshModifierGraph solid;
    requireOk(solid.addNode("road", "mesh.splineRibbon"));
    requireOk(solid.setNodeSplinePath("road", path));
    requireOk(solid.setNodeFloat("road", "width", 2.f));
    requireOk(solid.setNodeFloat("road", "thickness", 0.25f));
    requireOk(solid.setNodeInt("road", "pathSegments", 4));
    auto road = solid.executeResult("road");
    REQUIRE(road.ok());
    CHECK_EQ(road.value().getVertexCount(), 48);
    CHECK_EQ(road.value().getIndexCount(), 108);
    CHECK_EQ(road.value().getGroupCount(), 4);
    CHECK_EQ(road.value().getGroupName(0), "surface");
    CHECK_EQ(road.value().getGroupName(1), "bottom");
    CHECK_EQ(road.value().getGroupName(2), "sides");
    CHECK_EQ(road.value().getGroupName(3), "caps");
    CHECK_EQ(road.value().getMeta("generator", ""), "mesh.splineRibbon");
}

TEST_CASE("procgen.meshModifierGraph.splineGeneratorsConsumePointProfile") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 2.f}));
    requireOk(path.setPointProfileResult(1, 90.f, 2.f, 0.5f));
    MeshModifierGraph graph;
    requireOk(graph.addNode("ribbon", "mesh.splineRibbon"));
    requireOk(graph.setNodeSplinePath("ribbon", path));
    requireOk(graph.setNodeInt("ribbon", "pathSegments", 2));
    auto result = graph.executeResult("ribbon");
    REQUIRE(result.ok());
    const int   lastLeft  = 4;
    const int   lastRight = 5;
    const float dx        = result.value().getPositionX(lastRight) - result.value().getPositionX(lastLeft);
    const float dy        = result.value().getPositionY(lastRight) - result.value().getPositionY(lastLeft);
    const float dz        = result.value().getPositionZ(lastRight) - result.value().getPositionZ(lastLeft);
    CHECK(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - 4.f) < 1e-5f);
    CHECK(std::abs(dy) > 3.9f);
}

TEST_CASE("procgen.meshModifierGraph.extrudesOwningClosedProfileWithIndependentCaps") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 2.f}));
    requireOk(path.setPointProfileResult(1, 45.f, 2.f, 0.5f));
    SplineProfile square{{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}}, true};

    MeshModifierGraph graph;
    requireOk(graph.addNode("extrude", "mesh.splineExtrude"));
    requireOk(graph.setNodeSplinePath("extrude", path));
    requireOk(graph.setNodeSplineProfile("extrude", square));
    requireOk(graph.setNodeInt("extrude", "pathSegments", 2));
    auto result = graph.executeResult("extrude");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 23);
    CHECK_EQ(result.value().getIndexCount(), 60);
    CHECK_EQ(result.value().getGroupCount(), 2);
    CHECK_EQ(result.value().getGroupName(0), "profile");
    CHECK_EQ(result.value().getGroupName(1), "caps");
    CHECK_EQ(result.value().getMeta("generator", ""), "mesh.splineExtrude");
    for (int vertex = 15; vertex < 19; ++vertex) CHECK(result.value().getNormalZ(vertex) < -0.999f);
    for (int vertex = 19; vertex < 23; ++vertex) CHECK(result.value().getNormalZ(vertex) > 0.999f);
}

TEST_CASE("procgen.meshModifierGraph.extrudesOpenUProfileWithoutImplicitCaps") {
    SplinePath path;
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    SplineProfile     channel{{{-1.f, 1.f}, {-1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}}, false};
    MeshModifierGraph graph;
    requireOk(graph.addNode("channel", "mesh.splineExtrude"));
    requireOk(graph.setNodeSplinePath("channel", path));
    requireOk(graph.setNodeSplineProfile("channel", channel));
    requireOk(graph.setNodeInt("channel", "pathSegments", 1));
    auto result = graph.executeResult("channel");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 8);
    CHECK_EQ(result.value().getIndexCount(), 18);
    CHECK_EQ(result.value().getGroupCount(), 1);
    CHECK_EQ(result.value().getMeta("profileClosed", ""), "0");
}

TEST_CASE("procgen.meshModifierGraph.rejectsInvalidProfileAtomically") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("extrude", "mesh.splineExtrude"));
    const auto    revision = graph.revision();
    SplineProfile invalid{{{0.f, 0.f}, {1.f, 0.f}, {1.f, 0.f}}, true};
    auto          rejected = graph.setNodeSplineProfile("extrude", invalid);
    REQUIRE(!rejected.ok());
    CHECK_EQ(graph.revision(), revision);
    CHECK(rejected.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.generatesDisconnectedSplineChunksWithoutBridgeTriangles") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(path.addPointResult({8.f, 0.f, 0.f}));
    requireOk(path.addPointResult({8.f, 1.f, 0.f}));
    requireOk(path.setPointChunkBreakResult(2, true));
    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "pathSegments", 2));
    requireOk(graph.setNodeInt("tube", "radialSegments", 4));
    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 54);
    CHECK_EQ(result.value().getIndexCount(), 144);
    CHECK_EQ(result.value().getGroupCount(), 2);
    CHECK_EQ(result.value().getMeta("chunks", ""), "2");
    for (int index = 0; index < result.value().getIndexCount(); index += 3) {
        const bool firstChunk = result.value().getIndex(index) < 27;
        CHECK_EQ(result.value().getIndex(index + 1) < 27, firstChunk);
        CHECK_EQ(result.value().getIndex(index + 2) < 27, firstChunk);
    }
}

TEST_CASE("procgen.meshModifierGraph.noiseKeepsHardEdgeSeamsClosed") {
    MeshBuild seam;
    seam.addVertex(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    seam.addVertex(1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
    seam.addVertex(0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
    seam.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    seam.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    seam.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    seam.addTriangle(0, 1, 2);
    seam.addTriangle(3, 4, 5);
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("noise", "deform.noise"));
    requireOk(graph.connect("source", "noise"));
    requireOk(graph.setNodeFloat("noise", "amplitude", 0.4f));
    requireOk(graph.setNodeMesh("source", seam));
    auto result = graph.executeResult("noise");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0) - result.value().getPositionX(3)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(0) - result.value().getPositionY(3)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionZ(0) - result.value().getPositionZ(3)) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.soundReactConsumesAnalyzedLevelAndFuses") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("sound", "deform.soundReact"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.connect("source", "sound"));
    requireOk(graph.connect("sound", "move"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeFloat("sound", "level", 0.75f));
    requireOk(graph.setNodeFloat("sound", "threshold", 0.25f));
    requireOk(graph.setNodeFloat("sound", "strength", 2.f));
    requireOk(graph.setNodeFloat("sound", "frequency", 0.f));
    requireOk(graph.setNodeFloat("sound", "phase", 1.5707963268f));
    requireOk(graph.setNodeString("sound", "axis", "y"));
    auto result = graph.executeResult("move");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-5f);
    CHECK(std::abs(result.value().getPositionX(1) - 2.f) < 1e-5f);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    requireOk(graph.setNodeFloat("sound", "level", 1.1f));
    auto invalid = graph.executeResult("sound");
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}
