// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "sensing.cpp"
  ]
}
*/
// clang-format on

#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Sensing.h"
#include "sensing/TargetingPipeline.h"
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

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include "sensing/Sensing.h"
#include "zeroerr/unittest.h"
namespace test_sensing_spatial_cells_migrated {


using namespace eve::sensing;

namespace {
void ensure(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
}  // namespace

TEST_CASE("sensing.spatialCellCandidatesMatchColdQueries") {
    SensingWorld cached;
    SensingWorld cold;
    ensure(cached.setSpatialIndexEnabled(true, 8.f).ok(), "enable cached index");
    ensure(cold.setSpatialIndexEnabled(true, 8.f).ok(), "enable cold index");

    const auto upsert = [&](const char* id, float x, float y, const char* faction, const char* tags) {
        ensure(cached.upsert(id, x, y, faction, tags, "").ok(), "cached upsert");
        ensure(cold.upsert(id, x, y, faction, tags, "").ok(), "cold upsert");
    };
    upsert("west", -7.f, -1.f, "red", "combat-target");
    upsert("edge", 0.f, 0.f, "blue", "combat-target");
    upsert("east", 7.f, 1.f, "red", "combat-target");
    upsert("corner", 8.f, 8.f, "blue", "combat-target");
    upsert("other", 30.f, 30.f, "red", "prop");

    const auto compare = [&](float x, float y, float radius, float warmX) {
        ensure(cold.setSpatialIndexEnabled(true, 8.f).ok(), "rebuild cold index");
        QuerySpec spec;
        spec.shape         = QueryCircle{x, y, radius};
        spec.requiredTags  = {"combat-target"};
        spec.maxRange      = radius;
        QuerySpec warmSpec = spec;
        warmSpec.shape     = QueryCircle{warmX, y, radius};
        ensure(cached.query(QueryOrigin{warmX, y, std::nullopt}, warmSpec).ok(), "warm cell cache");
        auto reused  = cached.query(QueryOrigin{x, y, std::nullopt}, spec);
        auto rebuilt = cold.query(QueryOrigin{x, y, std::nullopt}, spec);
        ensure(reused.ok(), "cached query");
        ensure(rebuilt.ok(), "cold query");
        ensure(cached.debugLastQueryJson() == cold.debugLastQueryJson(), "last-query diagnostics differ");
        ensure(reused.value().size() == rebuilt.value().size(), "candidate count differs");
        for (std::size_t i = 0; i < reused.value().size(); ++i)
            ensure(reused.value().ranked()[i].id == rebuilt.value().ranked()[i].id, "ranked candidate differs");
    };

    compare(-1.f, -1.f, 8.f, -1.f);
    compare(-0.5f, -1.f, 8.f, -1.f);  // Same cell footprint, different exact circle.
    compare(-1.f, -1.f, 8.f, -1.f);   // Cache hit must preserve scanned/debug facts.
    upsert("west", -20.f, -1.f, "red", "combat-target");
    compare(-1.f, -1.f, 8.f, -1.f);  // A position change invalidates the cell cache.
    upsert("edge", 0.f, 0.f, "blue", "prop");
    compare(-1.f, -1.f, 8.f, -1.f);  // Metadata-only upsert is visible through stable ids.
    upsert("edge", 1.f, 0.f, "blue", "prop");
    ensure(cached.subjects().at("edge").x == 1.f, "unchanged metadata must still update position");
    compare(-1.f, -1.f, 8.f, -1.f);
    ensure(cached.upsert("edge", 1.f, 0.f, "blue", "prop,prop,,", "").ok(), "duplicate CSV upsert");
    ensure(cached.subjects().at("edge").tags.size() == 1, "duplicate CSV must preserve tag set");
    ensure(cached.setZones("edge", "zone:test").ok(), "set zone");
    ensure(cached.upsert("edge", 1.f, 0.f, "blue", "prop", "").ok(), "upsert with zone");
    ensure(cached.subjects().at("edge").zones.empty(), "upsert must retain original zone reset semantics");
    ensure(cached.remove("east").ok(), "cached removal");
    ensure(cold.remove("east").ok(), "cold removal");
    compare(-1.f, -1.f, 8.f, -1.f);
    const auto snapshot = cached.snapshotJson();
    ensure(cached.restoreJson(snapshot).ok(), "restore cached world");
    compare(-1.f, -1.f, 8.f, -1.f);  // Restore rebuilds the spatial index and invalidates cached pointers.

    SensingWorld movedCached;
    SensingWorld movedCold;
    ensure(movedCached.setSpatialIndexEnabled(true, 8.f).ok(), "enable moved cached index");
    ensure(movedCold.setSpatialIndexEnabled(true, 8.f).ok(), "enable moved cold index");
    ensure(movedCached.upsert("replacement", -1.f, -1.f, "red", "combat-target", "").ok(),
           "upsert moved cached subject");
    ensure(movedCold.upsert("replacement", -1.f, -1.f, "red", "combat-target", "").ok(), "upsert moved cold subject");
    cached = std::move(movedCached);
    cold   = std::move(movedCold);
    compare(-1.f, -1.f, 8.f, -1.f);  // A moved-in index must replace the prior owner's cache.

    std::optional<SensingWorld> reusedAddress;
    reusedAddress.emplace();
    ensure(reusedAddress->setSpatialIndexEnabled(true, 8.f).ok(), "enable first reused index");
    ensure(reusedAddress->upsert("old", 0.f, 0.f, "red", "combat-target", "").ok(), "upsert old subject");
    QuerySpec reuseSpec;
    reuseSpec.shape    = QueryCircle{0.f, 0.f, 4.f};
    reuseSpec.maxRange = 4.f;
    ensure(reusedAddress->query(QueryOrigin{0.f, 0.f, std::nullopt}, reuseSpec).ok(), "warm reused cache");
    reusedAddress.reset();
    reusedAddress.emplace();
    ensure(reusedAddress->setSpatialIndexEnabled(true, 8.f).ok(), "enable second reused index");
    ensure(reusedAddress->upsert("new", 0.f, 0.f, "red", "combat-target", "").ok(), "upsert new subject");
    auto fresh = reusedAddress->query(QueryOrigin{0.f, 0.f, std::nullopt}, reuseSpec);
    ensure(fresh.ok() && fresh.value().size() == 1 && fresh.value().ranked()[0].id == "new",
           "reused world address must not expose stale cached subjects");
}

}  // namespace test_sensing_spatial_cells_migrated

#include "sensing/Sensing.h"
namespace test_rts_performance_semantics_migrated {
TEST_CASE("rts.performanceSensingNodeIndexSurvivesUpsertEraseRestoreAndMove") {
    eve::sensing::SensingWorld world;
    REQUIRE(world.setSpatialIndexEnabled(true, 2.f).ok());
    REQUIRE(world.upsert("a", 1.f, 1.f, "blue", "unit", "").ok());
    REQUIRE(world.upsert("b", 4.f, 1.f, "red", "unit", "").ok());
    eve::sensing::QuerySpec spec;
    spec.shape       = eve::sensing::QueryCircle{0.f, 0.f, 10.f};
    spec.maxRange    = 10.f;
    spec.maxCount    = 10;
    spec.countPolicy = eve::sensing::CountPolicy::TruncateToMax;
    auto result      = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 2);
    REQUIRE(world.upsert("b", 4.f, 1.f, "blue", "building", "").ok());
    spec.requiredTags = {"unit"};
    result            = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    spec.requiredTags.clear();
    REQUIRE(world.upsert("b", 30.f, 1.f, "red", "unit", "").ok());
    result = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    const auto snapshot = world.snapshotJson();
    REQUIRE(world.remove("a").ok());
    result = world.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 0);
    REQUIRE(world.restoreJson(snapshot).ok());
    eve::sensing::SensingWorld moved(std::move(world));
    result = moved.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
    CHECK_EQ(result.value().ranked()[0].id, "a");
    REQUIRE(moved.setSpatialIndexEnabled(false, 2.f).ok());
    REQUIRE(moved.setSpatialIndexEnabled(true, 3.f).ok());
    result = moved.query({0.f, 0.f, std::nullopt}, spec);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), 1);
}
}  // namespace test_rts_performance_semantics_migrated

#include <cmath>
#include <limits>
#include <stdexcept>
#include "sensing/EnvironmentQuery.h"

namespace test_sensing_environment_query_unit {
using namespace eve;
using namespace eve::sensing;

namespace {
TargetLocation point(float x, float y = 0) {
    auto value = WorldPoint::world2D(x, y);
    REQUIRE(value.ok());
    return value.value();
}
EnvironmentQueryDefinition definition() {
    EnvironmentQueryDefinition d;
    d.id       = "test.environment";
    d.contexts = {"self"};
    d.trace    = EnvironmentTraceLevel::Detailed;
    EnvironmentOption option;
    option.id = "primary";
    EnvironmentTest test;
    test.id           = "distance";
    test.context      = "self";
    test.purpose      = EnvironmentPurpose::Score;
    test.scoreMaximum = 20;
    option.tests.push_back(test);
    d.options.push_back(option);
    return d;
}
EnvironmentRequest request() {
    EnvironmentRequest r;
    r.contexts = {{"self", {point(0)}}};
    r.input    = {{3, point(3), "third"}, {1, point(1), "first"}, {2, point(2), "second"}};
    return r;
}
Result<EnvironmentQueryResult> run(const EnvironmentQueryDefinition& d, EnvironmentRequest r,
                                   const EnvironmentServices& services = {}) {
    auto plan = EnvironmentQueryPlan::compile(d);
    if (!plan) return Result<EnvironmentQueryResult>::failure(plan.status());
    auto instance = plan.value().instantiate(std::move(r));
    if (!instance) return Result<EnvironmentQueryResult>::failure(instance.status());
    return instance.value().run(services);
}
class Visibility final : public ILineOfSightQuery {
public:
    mutable int               calls    = 0;
    float                     minimumX = 0;
    bool                      fail     = false;
    bool                      throws   = false;
    Result<LineOfSightResult> query(const TargetLocation&, const TargetLocation& to) const override {
        ++calls;
        if (throws) throw std::runtime_error("provider exception");
        if (fail)
            return Result<LineOfSightResult>::failure(Diagnostic::error(DiagnosticCode::Failed, "backend failed"));
        const double x = std::visit([](const auto& p) { return double(p.x()); }, to);
        return Result<LineOfSightResult>::success({x >= minimumX, std::nullopt});
    }
};
EnvironmentTest losTest() {
    EnvironmentTest test;
    test.id      = "los";
    test.context = "self";
    test.metric  = EnvironmentMetricKind::LineOfSight;
    test.purpose = EnvironmentPurpose::Filter;
    test.minimum = test.maximum = 1;
    return test;
}
class CustomMetric final : public IEnvironmentMetric {
public:
    bool                      reorderable = false;
    double                    value       = 1;
    EnvironmentMetricContract contract() const noexcept override {
        return {CoordinateSpace::World2D, EnvironmentCost::Medium, reorderable, false};
    }
    Result<double> measure(const EnvironmentItem&, const TargetLocation&, const EnvironmentServices&) const override {
        return Result<double>::success(value);
    }
};
}  // namespace

TEST_CASE("sensing.environment.compiledPlanOwnsDefinitionAndInstancesOwnContexts") {
    auto d    = definition();
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto r        = request();
    auto instance = plan.value().instantiate(r);
    REQUIRE(instance.ok());
    d.options.clear();
    r.contexts[0].locations[0] = point(100);
    auto result                = instance.value().run({});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().ranked.front().item.id, 1u);
    CHECK_EQ(instance.value().trace().state, std::string("completed"));
    auto second = instance.value().run({});
    CHECK(!second.ok());
    auto another = plan.value().instantiate(request());
    REQUIRE(another.ok());
    CHECK(another.value().run({}).ok());
}

TEST_CASE("sensing.environment.validatesTypesContextsAndBudgets") {
    auto d                        = definition();
    d.options[0].tests[0].context = "missing";
    CHECK(!EnvironmentQueryPlan::compile(d).ok());
    d         = definition();
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto r = request();
    r.contexts[0].locations.clear();
    CHECK(!plan.value().instantiate(r).ok());
    r                   = request();
    r.input[0].location = GridPoint::grid2D(1, 1);
    CHECK(!plan.value().instantiate(r).ok());
    r             = request();
    r.input[0].id = r.input[1].id;
    CHECK(!plan.value().instantiate(r).ok());
    d.options[0].generator.maxItems = 2;
    CHECK(!run(d, request()).ok());
    d                            = definition();
    d.options[0].tests[0].weight = std::numeric_limits<double>::infinity();
    CHECK(!EnvironmentQueryPlan::compile(d).ok());
}

TEST_CASE("sensing.environment.ringAndGridGenerateLocationsWithoutSubjects") {
    auto d            = definition();
    d.maxResults      = 32;
    auto& generator   = d.options[0].generator;
    generator.kind    = EnvironmentGeneratorKind::Ring;
    generator.context = "self";
    generator.radius  = 4;
    generator.samples = 8;
    auto result       = run(d, request());
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 8u);
    for (const auto& candidate : result.value().ranked) {
        CHECK(!candidate.item.subjectKey.has_value());
        const auto p = std::get<WorldPoint>(candidate.item.location);
        CHECK(std::abs(std::hypot(p.x(), p.y()) - 4) < 0.0001);
    }
    generator.kind    = EnvironmentGeneratorKind::Grid;
    generator.radius  = 2;
    generator.spacing = 2;
    auto grid         = run(d, request());
    REQUIRE(grid.ok());
    CHECK_EQ(grid.value().ranked.size(), 9u);
    generator.maxItems = 8;
    CHECK(!run(d, request()).ok());
}

TEST_CASE("sensing.environment.multiContextFilterAndAggregateAreIndependent") {
    auto  d                 = definition();
    auto& test              = d.options[0].tests[0];
    test.purpose            = EnvironmentPurpose::FilterAndScore;
    test.maximum            = 3;
    auto r                  = request();
    r.contexts[0].locations = {point(0), point(5)};
    auto all                = run(d, r);
    REQUIRE(all.ok());
    CHECK_EQ(all.value().ranked.size(), 2u);
    test.filterOp  = EnvironmentFilterOp::Any;
    test.aggregate = EnvironmentAggregate::Maximum;
    auto any       = run(d, r);
    REQUIRE(any.ok());
    CHECK_EQ(any.value().ranked.size(), 3u);
    CHECK_EQ(any.value().ranked.front().item.id, 2u);
}

TEST_CASE("sensing.environment.contributionsNormalizeOnceAndExplainRejection") {
    auto d             = definition();
    d.optimize         = false;
    auto& test         = d.options[0].tests[0];
    test.purpose       = EnvironmentPurpose::FilterAndScore;
    test.maximum       = 2;
    test.normalization = EnvironmentNormalization::ActivePopulation;
    auto plan          = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto result = instance.value().run({});
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 2u);
    CHECK_EQ(result.value().ranked[0].score, 1.0);
    CHECK_EQ(result.value().ranked[1].score, 0.0);
    const auto& trace = instance.value().trace();
    REQUIRE(trace.items.size() == 3u);
    CHECK_EQ(trace.items[0].rejectedBy, std::string("distance"));
    CHECK_EQ(trace.items[1].contributions[0].lower, 1.0);
    CHECK_EQ(trace.items[1].contributions[0].upper, 2.0);
    auto json = trace.toValue().toJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("environmentTrace") != std::string::npos);
}

TEST_CASE("sensing.environment.weightsAndReferencePreferenceChangeRanking") {
    auto d               = definition();
    auto other           = d.options[0].tests[0];
    other.id             = "preferredDistance";
    other.preference     = EnvironmentPreference::Reference;
    other.referenceValue = 3;
    other.weight         = 10;
    d.options[0].tests.push_back(other);
    auto result = run(d, request());
    REQUIRE(result.ok());
    CHECK_EQ(result.value().ranked.front().item.id, 3u);
    d.options[0].tests[1].weight = 0;
    auto original                = run(d, request());
    REQUIRE(original.ok());
    CHECK_EQ(original.value().ranked.front().item.id, 1u);
}

TEST_CASE("sensing.environment.optionsOnlyAdvanceOnEmptyNotErrors") {
    auto d                        = definition();
    d.options[0].tests[0].purpose = EnvironmentPurpose::Filter;
    d.options[0].tests[0].maximum = 0;
    auto alternative              = definition().options[0];
    alternative.id                = "alternative";
    d.options.push_back(alternative);
    auto result = run(d, request());
    REQUIRE(result.ok());
    CHECK_EQ(result.value().option, std::string("alternative"));
    d.options[0].tests = {losTest()};
    CHECK(!run(d, request()).ok());
    Visibility provider;
    provider.fail = true;
    CHECK(!run(d, request(), {std::cref(provider)}).ok());
    d.options[0].tests = definition().options[0].tests;
    auto preferred     = run(d, request());
    REQUIRE(preferred.ok());
    CHECK_EQ(preferred.value().option, std::string("primary"));
    auto r = request();
    r.input.clear();
    auto empty = run(d, r);
    REQUIRE(empty.ok());
    CHECK(empty.value().ranked.empty());
}

TEST_CASE("sensing.environment.bestDefersExpensiveFilterAndMatchesExhaustive") {
    auto d = definition();
    d.mode = EnvironmentRunMode::Best;
    d.options[0].tests.insert(d.options[0].tests.begin(), losTest());
    Visibility provider;
    provider.minimumX = 2;
    auto plan         = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto optimized = instance.value().run({std::cref(provider)});
    REQUIRE(optimized.ok());
    CHECK_EQ(optimized.value().ranked.front().item.id, 2u);
    CHECK_EQ(provider.calls, 2);
    CHECK(instance.value().trace().stages.back().deferred);
    provider.calls  = 0;
    d.optimize      = false;
    auto exhaustive = run(d, request(), {std::cref(provider)});
    REQUIRE(exhaustive.ok());
    CHECK_EQ(exhaustive.value().ranked.front().item.id, optimized.value().ranked.front().item.id);
    CHECK_EQ(exhaustive.value().ranked.front().score, optimized.value().ranked.front().score);
    CHECK_EQ(provider.calls, 3);
}

TEST_CASE("sensing.environment.relativeScoresAndCustomBarriersDisableUnsafeDeferral") {
    auto d                              = definition();
    d.mode                              = EnvironmentRunMode::Best;
    d.options[0].tests[0].normalization = EnvironmentNormalization::ActivePopulation;
    d.options[0].tests.push_back(losTest());
    Visibility provider;
    auto       result = run(d, request(), {std::cref(provider)});
    REQUIRE(result.ok());
    CHECK_EQ(provider.calls, 3);
    provider.calls = 0;
    d              = definition();
    d.mode         = EnvironmentRunMode::Best;
    auto metric    = std::make_shared<CustomMetric>();
    auto test      = d.options[0].tests[0];
    test.id        = "barrier";
    test.metric    = EnvironmentMetricKind::Custom;
    test.custom    = metric;
    d.options[0].tests.push_back(test);
    d.options[0].tests.push_back(losTest());
    auto barrier = run(d, request(), {std::cref(provider)});
    REQUIRE(barrier.ok());
    CHECK_EQ(provider.calls, 3);
    metric->value = std::numeric_limits<double>::quiet_NaN();
    CHECK(!run(d, request(), {std::cref(provider)}).ok());
}

TEST_CASE("sensing.environment.traceCapAndTraceOffDoNotChangeResults") {
    auto d          = definition();
    d.maxTraceItems = 1;
    auto plan       = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    auto detailed = instance.value().run({});
    REQUIRE(detailed.ok());
    CHECK(instance.value().trace().truncated);
    CHECK_EQ(instance.value().trace().items.size(), 1u);
    d.trace    = EnvironmentTraceLevel::None;
    auto quiet = run(d, request());
    REQUIRE(quiet.ok());
    CHECK_EQ(quiet.value().ranked.front().score, detailed.value().ranked.front().score);
}

TEST_CASE("sensing.environment.worldCompositionPublishesTraceAndKeepsLegacyResults") {
    SensingWorld world;
    REQUIRE(world.upsert("enemy", 3, 0, "red", "unit", "").ok());
    QuerySpec spec;
    auto      old = world.query({}, spec);
    REQUIRE(old.ok());
    auto input = world.environmentInput({}, spec);
    REQUIRE(input.ok());
    input.value().contexts = {{"self", {point(0)}}};
    auto plan              = EnvironmentQueryPlan::compile(definition());
    REQUIRE(plan.ok());
    auto result = world.executeEnvironmentQuery(plan.value(), std::move(input).takeValue(), {});
    REQUIRE(result.ok());
    CHECK_EQ(*result.value().ranked.front().item.subjectKey, std::string("enemy"));
    CHECK_EQ(world.resultAt(0)->get().id, std::string("enemy"));
    CHECK(world.debugLastQueryJson().find("environmentTrace") != std::string::npos);
    auto d = definition();
    d.options[0].tests.push_back(losTest());
    auto failing = EnvironmentQueryPlan::compile(d);
    REQUIRE(failing.ok());
    CHECK(!world.executeEnvironmentQuery(failing.value(), request(), {}).ok());
    CHECK(world.debugLastQueryJson().find("\"state\":\"failed\"") != std::string::npos);
}

TEST_CASE("sensing.pipeline.collectsBeforeFilteringAndPublishesOnlyOnSuccess") {
    SensingWorld world;
    for (int i = 1; i <= 9; ++i)
        REQUIRE(world.upsert(std::to_string(i), float(i), 0, "red", i == 9 ? "valid" : "", "").ok());
    QuerySpec original;
    REQUIRE(world.query({}, original).ok());
    const auto      before = world.debugLastQueryJson();
    TargetingPreset preset;
    preset.id = "test.finalCount";
    TargetingTaskStep select;
    select.taskId             = "sensing.select.world";
    select.querySpec.maxCount = 1;
    preset.steps.push_back(select);
    TargetingTaskStep filter;
    filter.taskId                 = "sensing.filter.spec";
    filter.querySpec.requiredTags = {"valid"};
    preset.steps.push_back(filter);
    auto pipeline = TargetingPipeline::withBuiltins();
    auto compiled = pipeline.compile(preset);
    REQUIRE(compiled.ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = compiled.value().execute(context);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("9"));
    CHECK_EQ(world.debugLastQueryJson(), before);
    preset.steps[1].querySpec.requiredTags = {"absent"};
    preset.steps[0].querySpec.minCount     = 1;
    CHECK(!pipeline.execute(context, preset).ok());
    CHECK_EQ(world.debugLastQueryJson(), before);
    TargetingTaskStep truncate;
    truncate.taskId = "sensing.sort.truncate";
    preset.steps.insert(preset.steps.begin() + 1, truncate);
    CHECK(!pipeline.compile(preset).ok());
}

TEST_CASE("sensing.environment.spatialInputIdentityAndTiesAreStable") {
    SensingWorld world;
    REQUIRE(world.upsert("z", -1, 0, "", "", "").ok());
    REQUIRE(world.upsert("a", 1, 0, "", "", "").ok());
    QuerySpec spec;
    spec.maxRange = 10;
    auto linear   = world.environmentInput({}, spec);
    REQUIRE(linear.ok());
    REQUIRE(world.setSpatialIndexEnabled(true, 2).ok());
    auto indexed = world.environmentInput({}, spec);
    REQUIRE(indexed.ok());
    REQUIRE(linear.value().input.size() == indexed.value().input.size());
    for (std::size_t i = 0; i < linear.value().input.size(); ++i) {
        CHECK_EQ(linear.value().input[i].id, indexed.value().input[i].id);
        CHECK_EQ(*linear.value().input[i].subjectKey, *indexed.value().input[i].subjectKey);
    }
}

TEST_CASE("sensing.environment.optimizationParityAcrossCandidateSets") {
    Visibility provider;
    auto       d = definition();
    d.mode       = EnvironmentRunMode::Best;
    d.options[0].tests.insert(d.options[0].tests.begin(), losTest());
    for (int minimum = 0; minimum < 12; ++minimum) {
        provider.minimumX = float(minimum);
        EnvironmentRequest r;
        r.contexts = {{"self", {point(0), point(1)}}};
        for (int i = 9; i >= 0; --i) r.input.push_back({std::uint64_t(i), point(float(i)), std::nullopt});
        d.optimize     = true;
        auto optimized = run(d, r, {std::cref(provider)});
        REQUIRE(optimized.ok());
        d.optimize      = false;
        auto exhaustive = run(d, r, {std::cref(provider)});
        REQUIRE(exhaustive.ok());
        REQUIRE(optimized.value().ranked.size() == exhaustive.value().ranked.size());
        if (!optimized.value().ranked.empty()) {
            CHECK_EQ(optimized.value().ranked[0].item.id, exhaustive.value().ranked[0].item.id);
            CHECK_EQ(optimized.value().ranked[0].score, exhaustive.value().ranked[0].score);
        }
    }
}

TEST_CASE("sensing.pipeline.compiledPlanSurvivesRegistryAndPresetReplacement") {
    TargetingPreset preset;
    preset.id = "retained";
    TargetingTaskStep select;
    select.taskId = "sensing.select.world";
    preset.steps  = {select};
    auto plan     = [&]() {
        auto pipeline = TargetingPipeline::withBuiltins();
        auto compiled = pipeline.compile(preset);
        REQUIRE(compiled.ok());
        REQUIRE(pipeline.registerPreset(preset).ok());
        preset.steps[0].querySpec.requiredTags = {"absent"};
        REQUIRE(pipeline.registerPreset(preset).ok());
        return std::move(compiled).takeValue();
    }();
    SensingWorld world;
    REQUIRE(world.upsert("a", 5, 0, "", "", "").ok());
    REQUIRE(world.upsert("z", 1, 0, "", "", "").ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = plan.execute(context);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 2u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("z"));
}

TEST_CASE("sensing.environment.world3DAndGridKeepTheirCoordinateTags") {
    auto d     = definition();
    auto r     = request();
    d.space    = CoordinateSpace::Grid3D;
    r.contexts = {{"self", {TargetLocation{GridPoint::grid3D(0, 0, 0)}}}};
    r.input    = {{0, TargetLocation{GridPoint::grid3D(0, 0, 4)}, std::nullopt}};
    auto grid  = run(d, r);
    REQUIRE(grid.ok());
    CHECK(std::get<GridPoint>(grid.value().ranked.front().item.location).space() == CoordinateSpace::Grid3D);
    d.space      = CoordinateSpace::World3D;
    auto point3D = WorldPoint::world3D(1, 2, 7);
    REQUIRE(point3D.ok());
    r.contexts = {{"self", {TargetLocation{point3D.value()}}}};
    r.input.clear();
    d.options[0].generator.kind    = EnvironmentGeneratorKind::Ring;
    d.options[0].generator.context = "self";
    d.options[0].generator.samples = 4;
    auto ring                      = run(d, r);
    REQUIRE(ring.ok());
    REQUIRE(ring.value().ranked.size() == 4u);
    for (const auto& candidate : ring.value().ranked) CHECK_EQ(std::get<WorldPoint>(candidate.item.location).z(), 7.f);
}

TEST_CASE("sensing.pipeline.implicitDistanceOrderPrecedesTruncation") {
    auto            pipeline = TargetingPipeline::withBuiltins();
    TargetingPreset preset;
    preset.id = "truncate-only";
    TargetingTaskStep select;
    select.taskId = "sensing.select.world";
    TargetingTaskStep truncate;
    truncate.taskId        = "sensing.sort.truncate";
    truncate.truncateCount = 1;
    preset.steps           = {select, truncate};
    SensingWorld world;
    REQUIRE(world.upsert("a", 5, 0, "", "", "").ok());
    REQUIRE(world.upsert("z", 1, 0, "", "", "").ok());
    TargetingSourceContext context{&world, {}, 1, 0};
    auto                   result = pipeline.execute(context, preset);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 1u);
    CHECK_EQ(result.value().ranked()[0].id, std::string("z"));
}

TEST_CASE("sensing.environment.providerExceptionsBecomeFailedDiagnostics") {
    auto d = definition();
    d.options[0].tests.push_back(losTest());
    auto plan = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    Visibility provider;
    provider.throws = true;
    auto result     = instance.value().run({std::cref(provider)});
    REQUIRE(!result.ok());
    CHECK_EQ(instance.value().trace().state, std::string("failed"));
    REQUIRE(instance.value().trace().diagnostics.size() == 1u);
    CHECK(instance.value().trace().diagnostics[0].code() == DiagnosticCode::CallbackFailure);
    CHECK(instance.value().trace().toValue().toJson().ok());
}

TEST_CASE("sensing.environment.constantPopulationUsesIdentityTieBreak") {
    auto d                              = definition();
    d.options[0].tests[0].normalization = EnvironmentNormalization::ActivePopulation;
    auto r                              = request();
    for (auto& item : r.input) item.location = point(5);
    auto result = run(d, r);
    REQUIRE(result.ok());
    REQUIRE(result.value().ranked.size() == 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_EQ(result.value().ranked[i].item.id, i + 1);
        CHECK_EQ(result.value().ranked[i].score, 0.0);
    }
}

TEST_CASE("sensing.environment.costSortPreservesScoreAccumulationOrder") {
    auto d              = definition();
    auto custom         = std::make_shared<CustomMetric>();
    custom->reorderable = true;
    EnvironmentTest expensiveScore;
    expensiveScore.id      = "expensiveScore";
    expensiveScore.context = "self";
    expensiveScore.metric  = EnvironmentMetricKind::Custom;
    expensiveScore.custom  = custom;
    expensiveScore.purpose = EnvironmentPurpose::Score;
    expensiveScore.weight  = 1e16;
    auto cheapFilter       = d.options[0].tests[0];
    cheapFilter.id         = "cheapFilter";
    cheapFilter.purpose    = EnvironmentPurpose::Filter;
    d.options[0].tests     = {expensiveScore, d.options[0].tests[0], losTest(), cheapFilter};
    auto plan              = EnvironmentQueryPlan::compile(d);
    REQUIRE(plan.ok());
    auto instance = plan.value().instantiate(request());
    REQUIRE(instance.ok());
    Visibility provider;
    auto       optimized = instance.value().run({std::cref(provider)});
    REQUIRE(optimized.ok());
    const auto& stages = instance.value().trace().stages;
    REQUIRE(stages.size() == 5u);
    CHECK_EQ(stages[1].test, std::string("cheapFilter"));
    CHECK_EQ(stages[2].test, std::string("los"));
    CHECK_EQ(stages[3].test, std::string("expensiveScore"));
    CHECK_EQ(stages[4].test, std::string("distance"));
    d.optimize      = false;
    auto exhaustive = run(d, request(), {std::cref(provider)});
    REQUIRE(exhaustive.ok());
    REQUIRE(exhaustive.value().ranked.size() == optimized.value().ranked.size());
    for (std::size_t i = 0; i < optimized.value().ranked.size(); ++i) {
        CHECK_EQ(optimized.value().ranked[i].item.id, exhaustive.value().ranked[i].item.id);
        CHECK_EQ(optimized.value().ranked[i].score, exhaustive.value().ranked[i].score);
    }
}
}  // namespace test_sensing_environment_query_unit
