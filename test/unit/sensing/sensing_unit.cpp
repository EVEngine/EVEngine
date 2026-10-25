#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Sensing.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

using namespace eve::sensing;

namespace {

QuerySpec unitQuery() {
    QuerySpec spec;
    spec.requiredTags = {"unit"};
    spec.maxRange     = 10.f;
    spec.maxCount     = 8;
    return spec;
}

class AnsweringSight final : public ILineOfSightQuery {
public:
    explicit AnsweringSight(bool answer) : answer_(answer) {}

    [[nodiscard]] eve::Result<LineOfSightResult> query(const TargetLocation&, const TargetLocation&) const override {
        ++calls;
        LineOfSightResult result;
        result.visible = answer_;
        return eve::Result<LineOfSightResult>::success(result);
    }

    mutable int calls = 0;

private:
    bool answer_;
};

TargetLocation grid(int x, int y) { return TargetLocation(GridPoint::grid2D(x, y)); }

TargetLocation world(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return TargetLocation(std::move(point).takeValue());
}

}  // namespace

// Given a world of mirrored facts, queries document the complete filter and ranking contract.
TEST_CASE("sensing.query.filtersShapeTagsFactionVisibilityAndSelf") {
    SensingWorld world;
    REQUIRE(world.upsert("self", 0.f, 0.f, "blue", "unit", "p1").ok());
    REQUIRE(world.upsert("near", 1.f, 0.f, "red", "unit,scout", "p1").ok());
    REQUIRE(world.upsert("edge", 10.f, 0.f, "red", "unit", "p1").ok());
    REQUIRE(world.upsert("hidden", 2.f, 0.f, "red", "unit", "p2").ok());
    REQUIRE(world.upsert("building", 3.f, 0.f, "red", "building", "p1").ok());

    auto spec            = unitQuery();
    spec.shape           = QueryCircle{0.f, 0.f, 10.f};
    spec.includeFactions = {"red"};
    spec.visibleTo       = "p1";
    auto result          = world.query(QueryOrigin{0.f, 0.f, std::string("self")}, spec);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().size(), std::size_t{2});
    CHECK_EQ(result.value().ranked()[0].id, std::string("near"));
    CHECK_EQ(result.value().ranked()[1].id, std::string("edge"));
    CHECK_EQ(result.value().ranked()[1].distance, 10.f);
    CHECK_EQ(world.resultAt(0)->get().id, std::string("near"));
    CHECK(!world.resultAt(-1).has_value());
    CHECK(!world.resultAt(2).has_value());

    spec.excludedTags = {"scout"};
    auto withoutScout = world.query(QueryOrigin{0.f, 0.f, std::nullopt}, spec);
    REQUIRE(withoutScout.ok());
    REQUIRE_EQ(withoutScout.value().size(), std::size_t{1});
    CHECK_EQ(withoutScout.value().ranked()[0].id, std::string("edge"));
}

TEST_CASE("sensing.query.boxConeAndDistanceTieHaveExactOutputs") {
    SensingWorld world;
    REQUIRE(world.upsert("z", 1.f, 0.f, "", "unit", "").ok());
    REQUIRE(world.upsert("a", -1.f, 0.f, "", "unit", "").ok());
    REQUIRE(world.upsert("side", 0.f, 2.f, "", "unit", "").ok());
    auto spec  = unitQuery();
    spec.shape = QueryBox{-1.f, -1.f, 1.f, 0.f};
    auto box   = world.query(QueryOrigin{}, spec);
    REQUIRE(box.ok());
    REQUIRE_EQ(box.value().size(), std::size_t{2});
    CHECK_EQ(box.value().ranked()[0].id, std::string("a"));
    CHECK_EQ(box.value().ranked()[1].id, std::string("z"));

    spec.shape = QueryCone{0.f, 0.f, 1.f, 0.f, 0.5f, 5.f};
    auto cone  = world.query(QueryOrigin{}, spec);
    REQUIRE(cone.ok());
    REQUIRE_EQ(cone.value().size(), std::size_t{1});
    CHECK_EQ(cone.value().ranked()[0].id, std::string("z"));
}

TEST_CASE("sensing.query.countPolicyAndInvalidSpecDoNotPublishFailure") {
    SensingWorld world;
    REQUIRE(world.upsert("a", 1.f, 0.f, "", "unit", "").ok());
    REQUIRE(world.upsert("b", 2.f, 0.f, "", "unit", "").ok());
    auto spec      = unitQuery();
    spec.maxCount  = 1;
    auto truncated = world.query(QueryOrigin{}, spec);
    REQUIRE(truncated.ok());
    REQUIRE_EQ(truncated.value().size(), std::size_t{1});
    CHECK_EQ(world.resultAt(0)->get().id, std::string("a"));

    spec.countPolicy = CountPolicy::FailIfOutOfRange;
    auto overflow    = world.query(QueryOrigin{}, spec);
    CHECK(!overflow.ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("a"));

    spec.maxRange = -1.f;
    CHECK(!world.query(QueryOrigin{}, spec).ok());
    CHECK_EQ(world.resultAt(0)->get().id, std::string("a"));

    spec          = unitQuery();
    spec.minCount = 3;
    CHECK(!world.query(QueryOrigin{}, spec).ok());
    spec       = unitQuery();
    spec.shape = QueryCone{0.f, 0.f, 0.f, 0.f, 0.5f, 5.f};
    CHECK(!world.query(QueryOrigin{}, spec).ok());
}

TEST_CASE("sensing.world.mutationAndSnapshotHaveDocumentedLifecycle") {
    SensingWorld world;
    CHECK(!world.upsert("", 0.f, 0.f, "", "", "").ok());
    CHECK(!world.upsert("bad", std::numeric_limits<float>::infinity(), 0.f, "", "", "").ok());
    REQUIRE(world.upsert("b", 2.f, 0.f, "red", "unit,tank", "p1").ok());
    REQUIRE(world.upsert("a", 1.f, 0.f, "blue", "unit", "p2").ok());
    REQUIRE(world.setZones("a", "arena:central").ok());
    CHECK(!world.setZones("missing", "arena:central").ok());
    CHECK(!world.setZones("a", "invalid zone").ok());
    auto snapshot = world.snapshotJson();
    CHECK(snapshot.find("\"schema\":\"eve.sensing.world\"") != std::string::npos);
    CHECK(snapshot.find("\"version\":1") != std::string::npos);
    CHECK(snapshot.find("\"id\":\"a\"") < snapshot.find("\"id\":\"b\""));

    SensingWorld restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    auto before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{}").ok());
    CHECK_EQ(restored.snapshotJson(), before);
    CHECK(!restored.restoreJson("{\"schema\":\"eve.sensing.world\",\"version\":1,\"subjects\":[{}]}").ok());
    CHECK_EQ(restored.snapshotJson(), before);
    REQUIRE(restored.remove("a").ok());
    CHECK(!restored.remove("a").ok());
    CHECK_EQ(restored.subjects().size(), std::size_t{1});
}

TEST_CASE("sensing.spatialIndexMatchesLinearAcrossMutationAndRestore") {
    SensingWorld linear;
    SensingWorld indexed;
    for (auto* world : {&linear, &indexed}) {
        REQUIRE(world->upsert("near", 1.f, 0.f, "", "unit", "").ok());
        REQUIRE(world->upsert("mid", 3.f, 0.f, "", "unit", "").ok());
        REQUIRE(world->upsert("far", 50.f, 0.f, "", "unit", "").ok());
    }
    CHECK(!indexed.setSpatialIndexEnabled(true, 0.f).ok());
    CHECK(!indexed.spatialIndexEnabled());
    REQUIRE(indexed.setSpatialIndexEnabled(true, 8.f).ok());
    auto spec    = unitQuery();
    spec.shape   = QueryCircle{0.f, 0.f, 10.f};
    auto compare = [&]() {
        auto expected = linear.query(QueryOrigin{}, spec);
        auto actual   = indexed.query(QueryOrigin{}, spec);
        REQUIRE(expected.ok());
        REQUIRE(actual.ok());
        REQUIRE_EQ(actual.value().size(), expected.value().size());
        for (std::size_t i = 0; i < actual.value().size(); ++i)
            CHECK_EQ(actual.value().ranked()[i].id, expected.value().ranked()[i].id);
    };
    compare();
    REQUIRE(linear.upsert("far", 2.f, 0.f, "", "unit", "").ok());
    REQUIRE(indexed.upsert("far", 2.f, 0.f, "", "unit", "").ok());
    compare();
    REQUIRE(linear.remove("mid").ok());
    REQUIRE(indexed.remove("mid").ok());
    compare();
    REQUIRE(indexed.restoreJson(linear.snapshotJson()).ok());
    compare();
    CHECK(indexed.debugLastQueryJson().find("\"used\":true") != std::string::npos);
    REQUIRE(indexed.setSpatialIndexEnabled(false).ok());
    CHECK(!indexed.spatialIndexEnabled());
}

TEST_CASE("sensing.router.dispatchesBySpaceAndRejectsUnclaimedOrMixedEndpoints") {
    LineOfSightRouter router;
    AnsweringSight    gridSight(true);
    AnsweringSight    worldSight(false);
    auto absent = router.query(grid(0, 0), grid(1, 0));
    CHECK(!absent.ok());
    CHECK_EQ(absent.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE(router.addProvider(CoordinateSpace::Grid2D, &gridSight).ok());
    REQUIRE(router.addProvider(CoordinateSpace::World3D, &worldSight).ok());
    auto gridResult = router.query(grid(0, 0), grid(1, 0));
    REQUIRE(gridResult.ok());
    CHECK(gridResult.value().visible);
    CHECK_EQ(gridSight.calls, 1);
    CHECK_EQ(worldSight.calls, 0);
    auto worldResult = router.query(world(0.f, 0.f, 0.f), world(1.f, 0.f, 0.f));
    REQUIRE(worldResult.ok());
    CHECK(!worldResult.value().visible);
    CHECK_EQ(worldSight.calls, 1);
    auto mixed = router.query(grid(0, 0), world(1.f, 0.f, 0.f));
    CHECK(!mixed.ok());
    CHECK_EQ(mixed.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);
    CHECK_EQ(gridSight.calls, 1);
    CHECK_EQ(worldSight.calls, 1);
}

TEST_CASE("sensing.router.providerClaimsHaveExplicitOwnership") {
    LineOfSightRouter router;
    AnsweringSight    first(true);
    AnsweringSight    second(false);
    CHECK(!router.addProvider(CoordinateSpace::Grid2D, nullptr).ok());
    REQUIRE(router.addProvider(CoordinateSpace::Grid2D, &first).ok());
    CHECK(!router.addProvider(CoordinateSpace::Grid2D, &second).ok());
    CHECK(router.hasProvider(CoordinateSpace::Grid2D));
    REQUIRE_EQ(router.spaces().size(), std::size_t{1});
    auto foreignRelease = router.removeProvider(CoordinateSpace::Grid2D, &second);
    REQUIRE(foreignRelease.ok());
    CHECK_EQ(foreignRelease.code(), eve::StatusCode::NoOp);
    REQUIRE(router.removeProvider(CoordinateSpace::Grid2D, &first).ok());
    CHECK(!router.hasProvider(CoordinateSpace::Grid2D));
    CHECK(!router.query(grid(0, 0), grid(1, 0)).ok());
}
