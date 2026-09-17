#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/mesh/MeshUvProjection.h"

#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::procgen;

namespace {
MeshBuild triangle() {
    MeshBuild mesh;
    mesh.addVertex(0.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f);
    mesh.addVertex(1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f);
    mesh.addVertex(0.f,0.f,1.f,0.f,1.f,0.f,0.f,0.f);
    mesh.addTriangle(0,1,2);
    return mesh;
}
}

TEST_CASE("procgen.meshUvProjection.projectsAndMapsDynamicSurface") {
    auto projected = projectMeshUvResult(triangle(), "planar", 1.f, 0.f, 0.f);
    REQUIRE(projected.ok());
    auto uv = mapMeshSurfacePointToUvResult(projected.value(), 0, 0.25f, 0.f, 0.25f);
    REQUIRE(uv.ok());
    CHECK(std::abs(uv.value().u - 0.25f) < 1e-6f);
    CHECK(std::abs(uv.value().v - 0.25f) < 1e-6f);
    CHECK_EQ(projected.value().getMeta("uvProjection", ""), "planar");
}

TEST_CASE("procgen.meshUvProjection.executesThroughModifierGraph") {
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("source", "mesh.input").ok());
    REQUIRE(graph.addNode("uv", "mesh.projectUv").ok());
    REQUIRE(graph.addNode("output", "mesh.output").ok());
    REQUIRE(graph.connect("source", "uv", 0).ok());
    REQUIRE(graph.connect("uv", "output", 0).ok());
    REQUIRE(graph.setNodeMesh("source", triangle()).ok());
    REQUIRE(graph.setNodeString("uv", "mode", "box").ok());
    auto built = graph.executeResult("output");
    REQUIRE(built.ok());
    CHECK_EQ(built.value().getMeta("uvProjection", ""), "box");
}

TEST_CASE("procgen.meshUvProjection.rejectsInvalidMeshesAndTriangles") {
    MeshBuild empty;
    CHECK(!projectMeshUvResult(empty, "planar").ok());
    CHECK(!projectMeshUvResult(triangle(), "invalid").ok());
    CHECK(!mapMeshSurfacePointToUvResult(triangle(), 3, 0.f, 0.f, 0.f).ok());
}
