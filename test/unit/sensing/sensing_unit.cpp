#include "zeroerr/assert.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Sensing.h"
#include "sensing/TargetingPipeline.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// sensing.cpp
using namespace eve::sensing;

TEST_CASE("sensing.filtersAndSortsCandidates") {
    SensingWorld w;
    auto         b = w.upsert("b", 3, 4, "red", "unit,tank", "p1");
    auto         a = w.upsert("a", 1, 0, "blue", "unit,infantry", "p1,p2");
    auto         c = w.upsert("c", 2, 0, "red", "building", "p1");
    REQUIRE(b.ok());
    REQUIRE(a.ok());
    REQUIRE(c.ok());
    auto count = w.circle(0, 0, 10, "unit", "", "", "blue", "p1", 8);
    REQUIRE(count.ok());
    CHECK_EQ(count.value(), 1);
    auto candidate = w.resultAt(0);
    REQUIRE(candidate.has_value());
    CHECK_EQ(candidate->get().id, std::string("b"));
}
TEST_CASE("sensing.ordersByDistanceThenIdAndLimits") {
    SensingWorld w;
    auto         z = w.upsert("z", 1, 0, "", "u", "");
    auto         a = w.upsert("a", -1, 0, "", "u", "");
    REQUIRE(z.ok());
    REQUIRE(a.ok());
    auto count = w.circle(0, 0, 2, "u", "", "", "", "", 1);
    REQUIRE(count.ok());
    CHECK_EQ(count.value(), 1);
    auto candidate = w.resultAt(0);
    REQUIRE(candidate.has_value());
    CHECK_EQ(candidate->get().id, std::string("a"));
}
TEST_CASE("sensing.snapshotIsTransactional") {
    SensingWorld w;
    auto         inserted = w.upsert("u", 2, 3, "f", "b,a", "p");
    REQUIRE(inserted.ok());
    auto         s = w.snapshotJson();
    SensingWorld x;
    auto         restored = x.restoreJson(s);
    REQUIRE(restored.ok());
    CHECK_EQ(x.snapshotJson(), s);
    auto before   = x.snapshotJson();
    auto rejected = x.restoreJson("{}");
    CHECK(!rejected.ok());
    CHECK_EQ(x.snapshotJson(), before);
}

TEST_CASE("sensing.querySpecRanksAndHonorsCountPolicy") {
    SensingWorld w;
    REQUIRE(w.upsert("near", 1, 0, "red", "unit", "").ok());
    REQUIRE(w.upsert("mid", 3, 0, "red", "unit", "").ok());
    REQUIRE(w.upsert("far", 9, 0, "red", "unit", "").ok());

    QuerySpec truncate;
    truncate.requiredTags = {"unit"};
    truncate.minRange     = 0.f;
    truncate.maxRange     = 10.f;
    truncate.maxCount     = 2;
    truncate.countPolicy  = CountPolicy::TruncateToMax;
    truncate.sortKey      = SortKey::DistanceAscending;
    auto ranked           = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, truncate);
    REQUIRE(ranked.ok());
    CHECK_EQ(ranked.value().size(), 2u);
    CHECK_EQ(ranked.value().ranked()[0].id, std::string("near"));
    CHECK_EQ(ranked.value().ranked()[1].id, std::string("mid"));
    CHECK_EQ(w.resultAt(0)->get().id, std::string("near"));

    QuerySpec fail   = truncate;
    fail.countPolicy = CountPolicy::FailIfOutOfRange;
    auto failed      = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, fail);
    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Rejected);

    QuerySpec sugarLike;
    sugarLike.shape        = QueryCircle{0.f, 0.f, 10.f};
    sugarLike.requiredTags = {"unit"};
    sugarLike.maxRange     = 10.f;
    sugarLike.maxCount     = 8;
    sugarLike.countPolicy  = CountPolicy::TruncateToMax;
    auto viaQuery          = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, sugarLike);
    auto viaCircle         = w.circle(0.f, 0.f, 10.f, "unit", "", "", "", "", 8);
    REQUIRE(viaQuery.ok());
    REQUIRE(viaCircle.ok());
    CHECK_EQ(viaQuery.value().size(), static_cast<std::size_t>(viaCircle.value()));
}

TEST_CASE("sensing.queryConeFiltersByFacing") {
    SensingWorld w;
    REQUIRE(w.upsert("ahead", 5.f, 0.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("side", 0.f, 5.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("behind", -5.f, 0.f, "red", "unit", "").ok());

    QuerySpec cone;
    cone.shape        = QueryCone{0.f, 0.f, 1.f, 0.f, 0.785398163f, 10.f};
    cone.requiredTags = {"unit"};
    cone.maxRange     = 10.f;
    cone.countPolicy  = CountPolicy::TruncateToMax;
    cone.sortKey      = SortKey::DistanceAscending;
    auto ranked       = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, cone);
    REQUIRE(ranked.ok());
    CHECK_EQ(ranked.value().size(), 1u);
    CHECK_EQ(ranked.value().ranked()[0].id, std::string("ahead"));
}

TEST_CASE("sensing.executePresetRunsBuiltinConeSelect") {
    SensingWorld w;
    REQUIRE(w.upsert("ahead", 4.f, 0.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("left", 0.f, 4.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("farAhead", 40.f, 0.f, "red", "unit", "").ok());

    TargetingPipeline pipeline = TargetingPipeline::withBuiltins();
    TargetingPreset   preset;
    preset.id = "test.coneSelect";
    {
        TargetingTaskStep select;
        select.taskId                 = "sensing.select.world";
        select.querySpec.maxRange     = 20.f;
        select.querySpec.requiredTags = {"unit"};
        select.querySpec.countPolicy  = CountPolicy::TruncateToMax;
        select.querySpec.sortKey      = SortKey::None;
        preset.steps.push_back(std::move(select));
    }
    {
        TargetingTaskStep cone;
        cone.taskId        = "sensing.filter.cone";
        cone.coneHalfAngle = 0.5f;
        cone.coneRange     = 20.f;
        preset.steps.push_back(std::move(cone));
    }
    {
        TargetingTaskStep sort;
        sort.taskId = "sensing.sort.distance";
        preset.steps.push_back(std::move(sort));
    }
    {
        TargetingTaskStep truncate;
        truncate.taskId        = "sensing.sort.truncate";
        truncate.truncateCount = 4;
        preset.steps.push_back(std::move(truncate));
    }
    REQUIRE(pipeline.registerPreset(std::move(preset)).ok());

    TargetingSourceContext context;
    context.world  = &w;
    context.origin = QueryOrigin{0.f, 0.f, std::nullopt};
    context.dirX   = 1.f;
    context.dirY   = 0.f;
    auto executed  = pipeline.executePreset(context, "test.coneSelect");
    REQUIRE(executed.ok());
    CHECK_EQ(executed.value().size(), 1u);
    CHECK_EQ(executed.value().ranked()[0].id, std::string("ahead"));

    // Shared builtins path used by SensingWorld::executePreset
    auto viaWorld = w.executePreset("sensing.builtin.coneSelect", 0.f, 0.f, 1.f, 0.f);
    REQUIRE(viaWorld.ok());
    CHECK_EQ(viaWorld.value(), 1);
    CHECK_EQ(w.resultAt(0)->get().id, std::string("ahead"));
}


TEST_CASE("sensing.perceptionFactsFromProjectsRankedCandidates") {
    SensingWorld w;
    REQUIRE(w.upsert("a", 1.f, 0.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("b", 3.f, 0.f, "red", "unit", "").ok());
    QuerySpec spec;
    spec.requiredTags = {"unit"};
    spec.maxRange     = 10.f;
    spec.maxCount     = 8;
    spec.countPolicy  = CountPolicy::TruncateToMax;
    spec.sortKey      = SortKey::DistanceAscending;
    auto ranked       = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, spec);
    REQUIRE(ranked.ok());
    const auto facts = perceptionFactsFrom(ranked.value());
    REQUIRE(facts.size() == 2u);
    CHECK_EQ(facts[0].subjectId, std::string("a"));
    CHECK_EQ(facts[1].subjectId, std::string("b"));
    CHECK(facts[0].score > facts[1].score);
}

TEST_CASE("sensing.spatialIndexMatchesLinearScan") {
    SensingWorld linear;
    SensingWorld spatial;
    REQUIRE(spatial.setSpatialIndexEnabled(true, 8.f).ok());
    CHECK(spatial.spatialIndexEnabled());
    CHECK(!linear.spatialIndexEnabled());

    // Cluster near origin plus far distractors outside the circle broadphase.
    REQUIRE(linear.upsert("near", 1.f, 0.f, "red", "unit", "").ok());
    REQUIRE(linear.upsert("mid", 4.f, 0.f, "red", "unit", "").ok());
    REQUIRE(linear.upsert("far", 50.f, 0.f, "red", "unit", "").ok());
    REQUIRE(linear.upsert("noise", 80.f, 80.f, "blue", "prop", "").ok());
    REQUIRE(spatial.upsert("near", 1.f, 0.f, "red", "unit", "").ok());
    REQUIRE(spatial.upsert("mid", 4.f, 0.f, "red", "unit", "").ok());
    REQUIRE(spatial.upsert("far", 50.f, 0.f, "red", "unit", "").ok());
    REQUIRE(spatial.upsert("noise", 80.f, 80.f, "blue", "prop", "").ok());

    QuerySpec spec;
    spec.shape        = QueryCircle{0.f, 0.f, 10.f};
    spec.requiredTags = {"unit"};
    spec.maxRange     = 10.f;
    spec.maxCount     = 8;
    spec.countPolicy  = CountPolicy::TruncateToMax;
    spec.sortKey      = SortKey::DistanceAscending;

    auto a = linear.query(QueryOrigin{0.f, 0.f, std::nullopt}, spec);
    auto b = spatial.query(QueryOrigin{0.f, 0.f, std::nullopt}, spec);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a.value().size() == b.value().size());
    REQUIRE(a.value().size() == 2u);
    CHECK_EQ(a.value().ranked()[0].id, b.value().ranked()[0].id);
    CHECK_EQ(a.value().ranked()[1].id, b.value().ranked()[1].id);

    const std::string debug = spatial.debugLastQueryJson();
    CHECK(debug.find("\"schema\":\"eve.sensing.lastQuery\"") != std::string::npos);
    CHECK(debug.find("\"used\":true") != std::string::npos);
    CHECK(debug.find("\"enabled\":true") != std::string::npos);
    CHECK(debug.find("\"id\":\"near\"") != std::string::npos);
    // Broadphase should not visit the distant distractors.
    const bool scannedTight =
        debug.find("\"scanned\":2") != std::string::npos || debug.find("\"scanned\":3") != std::string::npos;
    CHECK(scannedTight);
}

TEST_CASE("sensing.spatialIndexRejectsInvalidCellSize") {
    SensingWorld w;
    auto         bad = w.setSpatialIndexEnabled(true, 0.f);
    CHECK(!bad.ok());
    CHECK(!w.spatialIndexEnabled());
    REQUIRE(w.setSpatialIndexEnabled(true, 16.f).ok());
    CHECK(w.spatialIndexEnabled());
    REQUIRE(w.setSpatialIndexEnabled(false).ok());
    CHECK(!w.spatialIndexEnabled());
}

// sensing_line_of_sight_router.cpp
namespace {

/** @brief A backend that answers everything one way, so routing is what the test observes. */
class StubBackend final : public eve::sensing::ILineOfSightQuery {
public:
    explicit StubBackend(bool answer) : answer_(answer) {}

    [[nodiscard]] eve::Result<eve::sensing::LineOfSightResult> query(
        const eve::sensing::TargetLocation&, const eve::sensing::TargetLocation&) const override {
        ++calls;
        eve::sensing::LineOfSightResult result;
        result.visible = answer_;
        return eve::Result<eve::sensing::LineOfSightResult>::success(result);
    }

    mutable int calls = 0;

private:
    bool answer_ = false;
};

[[maybe_unused]] eve::sensing::TargetLocation grid(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

[[maybe_unused]] eve::sensing::TargetLocation world(float x, float y, float z) {
    // World points are validated on construction, so the test checks the Result here instead of
    // letting a bad literal silently become an invalid location.
    auto point = eve::sensing::WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return eve::sensing::TargetLocation(std::move(point).takeValue());
}

}  // namespace

TEST_CASE("sensing.routerDispatchesByCoordinateSpaceAndNeverInvokesTheWrongBackend") {
    eve::sensing::LineOfSightRouter router;
    StubBackend                     gridBackend(true);
    StubBackend                     worldBackend(false);

    REQUIRE(router.addProvider(eve::sensing::CoordinateSpace::Grid2D, &gridBackend).ok());
    REQUIRE(router.addProvider(eve::sensing::CoordinateSpace::World3D, &worldBackend).ok());

    auto gridQuery = router.query(grid(0, 0), grid(3, 0));
    REQUIRE(gridQuery.ok());
    CHECK(gridQuery.value().visible);
    CHECK_EQ(gridBackend.calls, 1);
    // The world backend must not have been consulted at all: routing is the whole point.
    CHECK_EQ(worldBackend.calls, 0);

    auto worldQuery = router.query(world(0.f, 0.f, 0.f), world(1.f, 0.f, 0.f));
    REQUIRE(worldQuery.ok());
    CHECK(!worldQuery.value().visible);
    CHECK_EQ(worldBackend.calls, 1);
    CHECK_EQ(gridBackend.calls, 1);
}

TEST_CASE("sensing.routerReportsUnclaimedSpacesInsteadOfAnsweringForThem") {
    eve::sensing::LineOfSightRouter router;
    StubBackend                     gridBackend(true);

    // Provider absent: the space is refused with a diagnostic naming it, never a silent false
    // (which a targeting resolver would read as "no line of sight" and quietly drop the action).
    auto absent = router.query(grid(0, 0), grid(1, 0));
    CHECK(!absent.ok());
    REQUIRE(absent.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(absent.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    CHECK_EQ(absent.status().primaryDiagnostic()->message(),
             std::string("no line-of-sight backend for coordinate space grid_2d"));
    CHECK(router.spaces().empty());

    REQUIRE(router.addProvider(eve::sensing::CoordinateSpace::Grid2D, &gridBackend).ok());
    const auto claimed = router.spaces();
    REQUIRE_EQ(claimed.size(), std::size_t{1});
    CHECK(claimed[0] == eve::sensing::CoordinateSpace::Grid2D);
    CHECK(router.hasProvider(eve::sensing::CoordinateSpace::Grid2D));
    CHECK(!router.hasProvider(eve::sensing::CoordinateSpace::Grid3D));
    CHECK(router.query(grid(0, 0), grid(1, 0)).ok());
}

TEST_CASE("sensing.routerRejectsMixedSpaceQueriesInsteadOfConverting") {
    eve::sensing::LineOfSightRouter router;
    StubBackend                     gridBackend(true);
    REQUIRE(router.addProvider(eve::sensing::CoordinateSpace::Grid2D, &gridBackend).ok());

    // A grid endpoint and a world endpoint are different spaces even when their numbers look
    // compatible; the interface forbids converting between them, so this is a caller error.
    auto mixed = router.query(grid(0, 0), world(1.f, 0.f, 0.f));
    CHECK(!mixed.ok());
    REQUIRE(mixed.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(mixed.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);
    CHECK_EQ(gridBackend.calls, 0);
}

TEST_CASE("sensing.ensureRouterRegistersTheCapabilityOnceAndKeepsForeignProviders") {
    // No provider yet: the first call registers the router as the capability.
    auto first = eve::sensing::ensureLineOfSightRouter();
    REQUIRE(first.ok());
    CHECK(first.code() == eve::StatusCode::Applied);
    eve::sensing::LineOfSightRouter* router = first.value();
    CHECK(eve::cap::query<eve::sensing::ILineOfSightQuery>() == router);

    // Idempotent: a second backend asking for the router gets the same one, not a second table.
    auto second = eve::sensing::ensureLineOfSightRouter();
    REQUIRE(second.ok());
    CHECK(second.code() == eve::StatusCode::NoOp);
    CHECK(second.value() == router);

    // The pipeline path works through the capability: a claimed space answers, an unclaimed one
    // reports Unsupported rather than "not visible".
    StubBackend gridBackend(true);
    REQUIRE(router->addProvider(eve::sensing::CoordinateSpace::Grid2D, &gridBackend).ok());
    auto* viaCapability = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(viaCapability == router);
    CHECK(viaCapability->query(grid(0, 0), grid(2, 0)).value().visible);
    CHECK_EQ(viaCapability->query(world(0.f, 0.f, 0.f), world(1.f, 0.f, 0.f)).status().primaryDiagnostic()->code(),
             eve::DiagnosticCode::Unsupported);

    // A project that registered its own provider keeps it: asking for the router is refused
    // instead of silently replacing the project's implementation.
    StubBackend foreign(false);
    eve::cap::provide<eve::sensing::ILineOfSightQuery>(&foreign);
    auto refused = eve::sensing::ensureLineOfSightRouter();
    CHECK(!refused.ok());
    REQUIRE(refused.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(refused.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    CHECK(eve::cap::query<eve::sensing::ILineOfSightQuery>() == &foreign);
}

TEST_CASE("sensing.routerOwnsNoBackendAndRefusesToReplaceOne") {
    eve::sensing::LineOfSightRouter router;
    StubBackend                     first(true);
    StubBackend                     second(false);

    // Replacing a claim would reintroduce "the effective backend depends on registration order".
    REQUIRE(router.addProvider(eve::sensing::CoordinateSpace::Grid2D, &first).ok());
    auto conflict = router.addProvider(eve::sensing::CoordinateSpace::Grid2D, &second);
    CHECK(!conflict.ok());
    REQUIRE(conflict.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(conflict.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    CHECK_EQ(router.query(grid(0, 0), grid(1, 0)).value().visible, true);

    // A null backend is refused rather than stored as "claimed but unusable".
    auto nullBackend = router.addProvider(eve::sensing::CoordinateSpace::Grid3D, nullptr);
    CHECK(!nullBackend.ok());
    CHECK_EQ(nullBackend.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);

    // One module cannot release another's claim, and releasing twice is safe.
    auto foreign = router.removeProvider(eve::sensing::CoordinateSpace::Grid2D, &second);
    REQUIRE(foreign.ok());
    CHECK(foreign.code() == eve::StatusCode::NoOp);
    CHECK(router.hasProvider(eve::sensing::CoordinateSpace::Grid2D));

    auto released = router.removeProvider(eve::sensing::CoordinateSpace::Grid2D, &first);
    REQUIRE(released.ok());
    CHECK(released.code() == eve::StatusCode::Applied);
    CHECK(!router.hasProvider(eve::sensing::CoordinateSpace::Grid2D));
    // Back to the unclaimed answer, not to a stale backend.
    CHECK_EQ(router.query(grid(0, 0), grid(1, 0)).status().primaryDiagnostic()->code(),
             eve::DiagnosticCode::Unsupported);
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
