#include "procgen/IncrementalBuild.h"
#include "procgen/Semantic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {

bool containsChange(const IncrementalBuildDelta& delta, int x, int z, bool removed) {
    for (int index = 0; index < delta.getCount(); ++index)
        if (delta.getClusterX(index) == x && delta.getClusterZ(index) == z && delta.isRemoved(index) == removed)
            return true;
    return false;
}

}  // namespace

TEST_CASE("procgen.incrementalBuild.rebuildsOnlyDirtyClustersAndEmitsRemovals") {
    Grid2D grid;
    grid.resize(8, 2);
    grid.setCell(1, 0, int(Semantic::Floor));
    grid.setCell(5, 0, int(Semantic::Floor));
    PointSet points;
    const int leftPoint  = points.add(1.5f, 0.f, 0.5f);
    const int rightPoint = points.add(5.5f, 0.f, 0.5f);

    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    IncrementalBuildExecutor incremental;
    auto first = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().getCount(), 2);
    CHECK_EQ(incremental.getCachedClusterCount(), 2);
    CHECK(containsChange(first.value(), 0, 0, false));
    CHECK(containsChange(first.value(), 1, 0, false));
    CHECK(incremental.getCachedArtifacts(0, 0).ok());

    auto unchanged = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().getCount(), 0);

    grid.setDetail(1, 0, 7);
    auto interior = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(interior.ok());
    CHECK_EQ(interior.value().getCount(), 1);
    CHECK(containsChange(interior.value(), 0, 0, false));

    grid.setCell(3, 0, int(Semantic::Floor));
    auto boundary = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(boundary.ok());
    CHECK_EQ(boundary.value().getCount(), 2);
    CHECK(containsChange(boundary.value(), 0, 0, false));
    CHECK(containsChange(boundary.value(), 1, 0, false));

    points.setPosition(rightPoint, 6.5f, 0.f, 0.5f);
    auto pointEdit = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(pointEdit.ok());
    CHECK_EQ(pointEdit.value().getCount(), 1);
    CHECK(containsChange(pointEdit.value(), 1, 0, false));

    grid.setCell(1, 0, int(Semantic::Empty));
    grid.setCell(3, 0, int(Semantic::Empty));
    points.setPosition(leftPoint, 5.75f, 0.f, 0.5f);
    auto removed = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(removed.ok());
    CHECK(containsChange(removed.value(), 0, 0, true));
    CHECK_EQ(incremental.getCachedClusterCount(), 1);
    CHECK(!incremental.getCachedArtifacts(0, 0).ok());
}

TEST_CASE("procgen.incrementalBuild.partitionsGlobalMeshAndPreservesCacheOnFailure") {
    Grid2D grid;
    grid.resize(8, 1);
    for (int x = 0; x < 8; ++x) grid.setCell(x, 0, int(Semantic::Floor));
    PointSet points;
    points.add(1.f, 0.f, 0.f);

    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    IncrementalBuildExecutor incremental;
    auto delta = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(delta.ok());
    REQUIRE_EQ(delta.value().getCount(), 2);
    auto leftArtifacts  = incremental.getCachedArtifacts(0, 0);
    auto rightArtifacts = incremental.getCachedArtifacts(1, 0);
    REQUIRE(leftArtifacts.ok());
    REQUIRE(rightArtifacts.ok());
    auto leftMesh  = leftArtifacts.value().getMesh(0);
    auto rightMesh = rightArtifacts.value().getMesh(0);
    REQUIRE(leftMesh.ok());
    REQUIRE(rightMesh.ok());
    auto full = stack.execute(grid, points);
    REQUIRE(full.ok());
    auto fullMesh = full.value().getMesh(0);
    REQUIRE(fullMesh.ok());
    CHECK_EQ(leftMesh.value().getIndexCount() + rightMesh.value().getIndexCount(), fullMesh.value().getIndexCount());
    CHECK(leftMesh.value().getPositionX(0) < 4.f);
    CHECK(rightMesh.value().getPositionX(0) >= 4.f);

    ObjectBuildLayer oriented;
    REQUIRE(oriented.addAsset("props/oriented", 1.f).ok());
    REQUIRE(oriented.setOrientation(1.f, 0.f, false).ok());
    BuildLayerStack failingStack;
    REQUIRE(failingStack.addObjectLayer("oriented", true, oriented).ok());
    auto failed = incremental.update(failingStack, grid, points, 4, 1.f);
    CHECK(!failed.ok());
    CHECK_EQ(incremental.getCachedClusterCount(), 2);
    CHECK(incremental.getCachedArtifacts(0, 0).ok());
    CHECK(!incremental.update(stack, grid, points, 0, 1.f).ok());
    CHECK(!incremental.update(stack, grid, points, 4, 0.f).ok());
}
