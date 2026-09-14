#include "procgen/mesh/MeshBoolean.h"
#include "procgen/mesh/MeshModifierGraph.h"

#include <zeroerr/unittest.h>

#include <cmath>

namespace {

eve::procgen::MeshBuild cube(float centerX) {
    eve::procgen::MeshBuild mesh;
    const float x0 = centerX - 1.f, x1 = centerX + 1.f;
    const float positions[8][3] = {{x0, -1, -1}, {x1, -1, -1}, {x1, 1, -1}, {x0, 1, -1},
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
        const int a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        const double ax = mesh.getPositionX(a), ay = mesh.getPositionY(a), az = mesh.getPositionZ(a);
        const double bx = mesh.getPositionX(b), by = mesh.getPositionY(b), bz = mesh.getPositionZ(b);
        const double cx = mesh.getPositionX(c), cy = mesh.getPositionY(c), cz = mesh.getPositionZ(c);
        volume += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    }
    return volume / 6.0;
}

}  // namespace

TEST_CASE("procgen.meshBoolean.closesUnionDifferenceAndIntersection") {
    const auto left  = cube(0.f);
    const auto right = cube(1.f);
    auto unionMesh = eve::procgen::meshBooleanResult(left, right, "union");
    auto difference = eve::procgen::meshBooleanResult(left, right, "difference");
    auto intersection = eve::procgen::meshBooleanResult(left, right, "intersection");
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
    const auto left = cube(0.f);
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
