#include "procgen/PointDelta.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>

using namespace eve::procgen;

namespace {

PointSet makePointSet(int count, std::uint64_t identityNamespace = 91) {
    PointSet points;
    for (int index = 0; index < count; ++index) {
        ProcgenPoint point;
        point.x    = float(index);
        point.seed = std::uint32_t(index + 1);
        const int appended = points.appendPoint(point);
        (void)appended;
        points.trySetIntAttribute(std::size_t(index), "kind", index % 3).expect("point delta fixture metadata");
    }
    points.assignPointIds(identityNamespace).expect("point delta fixture ids");
    return points;
}

}  // namespace

TEST_CASE("procgen.pointIdentity.isDeterministicAndPreservedByTransforms") {
    const PointSet first  = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
    const PointSet second = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
    REQUIRE_EQ(first.getCount(), second.getCount());
    for (int index = 0; index < first.getCount(); ++index) {
        CHECK_NE(first.getPointId(index), std::uint64_t(0));
        CHECK_EQ(first.getPointId(index), second.getPointId(index));
    }
    const PointSet transformed = transformPointSet3D(first, 10.f, 2.f, -4.f, 0.f, 30.f, 0.f, 2.f, 2.f, 2.f);
    REQUIRE_EQ(transformed.getCount(), first.getCount());
    for (int index = 0; index < first.getCount(); ++index)
        CHECK_EQ(transformed.getPointId(index), first.getPointId(index));
}

TEST_CASE("procgen.pointIdentity.assignmentIsTransactional") {
    PointSet legacy = makePointSet(3);
    ProcgenPoint duplicate;
    duplicate.id = legacy.getPointId(0);
    const int duplicateIndex = legacy.appendPoint(duplicate);
    (void)duplicateIndex;
    const auto before = legacy.points();
    auto       failed = legacy.assignPointIds(33);
    CHECK(!failed.ok());
    REQUIRE_EQ(legacy.points().size(), before.size());
    for (std::size_t index = 0; index < before.size(); ++index) CHECK_EQ(legacy.points()[index].id, before[index].id);

    PointSet unassigned;
    unassigned.add(0.f, 0.f, 0.f);
    unassigned.add(1.f, 0.f, 0.f);
    PointSet repeated = unassigned;
    unassigned.assignPointIds(44).expect("assign legacy ids");
    repeated.assignPointIds(44).expect("repeat legacy ids");
    CHECK_EQ(unassigned.getPointId(0), repeated.getPointId(0));
    CHECK_EQ(unassigned.getPointId(1), repeated.getPointId(1));
}

TEST_CASE("procgen.pointIdentity.setOperationsPreferStableIdentity") {
    PointSet first = makePointSet(2);
    PointSet moved = first;
    moved.setPosition(0, 100.f, 0.f, 0.f);
    CHECK_EQ(unionPointSets(first, moved).getCount(), 2);
    CHECK_EQ(intersectPointSets(first, moved).getCount(), 2);

    PointSet distinct;
    ProcgenPoint samePosition = first.points()[0];
    samePosition.id           = derivePointId(999, 1);
    const int distinctIndex = distinct.appendPoint(samePosition);
    (void)distinctIndex;
    CHECK_EQ(unionPointSets(first, distinct).getCount(), 3);
}

TEST_CASE("procgen.pointDelta.roundTripsAddUpdateRemoveAndReorder") {
    PointSet before = makePointSet(4);
    PointSet after;
    after.appendPointFrom(before, 2).expect("reorder point");
    after.appendPointFrom(before, 0).expect("reorder point");
    after.setPosition(1, 20.f, 3.f, -2.f);
    after.trySetStringAttribute(1, "asset", "oak").expect("updated metadata");
    ProcgenPoint added;
    added.id   = derivePointId(92, 10);
    added.x    = 9.f;
    added.seed = 88;
    const int addedIndex = after.appendPoint(added);
    (void)addedIndex;
    after.trySetStringAttribute(2, "asset", "rock").expect("added metadata");

    auto deltaResult = diffPointSets(before, after);
    REQUIRE(deltaResult.ok());
    PointDelta delta = std::move(deltaResult).takeValue();
    CHECK_EQ(delta.added.getCount(), 1);
    CHECK_EQ(delta.updated.getCount(), 2);
    CHECK_EQ(delta.removed.size(), std::size_t(2));
    auto applied = applyPointDelta(before, delta);
    REQUIRE(applied.ok());
    auto expectedFingerprint = fingerprintPointSet(after);
    auto actualFingerprint   = fingerprintPointSet(applied.value());
    REQUIRE(expectedFingerprint.ok());
    REQUIRE(actualFingerprint.ok());
    CHECK_EQ(actualFingerprint.value(), expectedFingerprint.value());
}

TEST_CASE("procgen.pointDelta.rejectsStaleBaseWithoutMutation") {
    PointSet before = makePointSet(3);
    PointSet after  = before;
    after.setPosition(0, 5.f, 0.f, 0.f);
    auto delta = diffPointSets(before, after);
    REQUIRE(delta.ok());
    before.setPosition(1, 7.f, 0.f, 0.f);
    auto applied = applyPointDelta(before, delta.value());
    CHECK(!applied.ok());
    CHECK_EQ(before.getX(1), 7.f);
}

TEST_CASE("procgen.pointSet.largeIdentityUnionAvoidsQuadraticScan") {
    PointSet first  = makePointSet(10000, 700);
    PointSet second = makePointSet(10000, 701);
    CHECK_EQ(unionPointSets(first, second).getCount(), 20000);
}
