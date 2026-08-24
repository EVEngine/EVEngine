#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/GpuDrivenTypes.h"
#include "graphics/IndirectBuilder.h"

using namespace eve::graphics;

namespace {

GpuMeshRecord makeMeshRecord(uint32_t indexCount) {
    GpuMeshRecord rec{};
    rec.indexCount = indexCount;
    rec.firstIndex = 0;
    rec.vertexBase = 0;
    rec.indexType = 1;
    return rec;
}

}  // namespace

TEST_CASE("IndirectBuilder.mergesBuckets") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(36));  // mesh 0
    table.push_back(makeMeshRecord(72));  // mesh 1

    IndirectBuilder b;
    b.add(0, 0, 0, 0);  // seq, mesh, material, pipeline
    b.add(1, 0, 0, 0);
    b.add(2, 1, 0, 0);
    b.add(3, 0, 1, 0);
    REQUIRE(b.build(table) == 3);

    const auto &cmds = b.commands();
    REQUIRE(cmds.size() == 3);
    CHECK(cmds[0].instanceCount == 2);  // material 0 + mesh 0
    CHECK(cmds[0].firstInstance == 0);
    CHECK(cmds[0].indexCount == 36);
    CHECK(cmds[1].instanceCount == 1);  // material 0 + mesh 1
    CHECK(cmds[1].firstInstance == 2);
    CHECK(cmds[1].indexCount == 72);
    CHECK(cmds[2].instanceCount == 1);  // material 1 + mesh 0
    CHECK(cmds[2].firstInstance == 3);

    const auto &order = b.sortedInstanceOrder();
    REQUIRE(order.size() == 4);
    CHECK(order[0] == 0);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
    CHECK(order[3] == 3);
}

TEST_CASE("IndirectBuilder.stableOrderWithinBucket") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(7, 0, 5, 0);
    b.add(3, 0, 5, 0);
    b.add(9, 0, 5, 0);
    REQUIRE(b.build(table) == 1);
    CHECK(b.commands()[0].instanceCount == 3);

    // Stable: sequence numbers inside the bucket drive the tiebreak (7, 3, 9 → 3, 7, 9).
    const auto &order = b.sortedInstanceOrder();
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 0);
    CHECK(order[2] == 2);
}

TEST_CASE("IndirectBuilder.pipelineSeparatesBuckets") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(0, 0, 0, 1);
    b.add(1, 0, 0, 2);
    REQUIRE(b.build(table) == 2);  // same mesh+material, different pipeline
    CHECK(b.commands()[0].instanceCount == 1);
    CHECK(b.commands()[1].instanceCount == 1);
}

TEST_CASE("IndirectBuilder.skipsUnknownMesh") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(0, 0, 0, 0);
    b.add(1, 99, 0, 0);  // meshId outside the table
    REQUIRE(b.build(table) == 1);
    CHECK(b.commands()[0].instanceCount == 1);
    CHECK(b.commands()[0].firstInstance == 0);
}
