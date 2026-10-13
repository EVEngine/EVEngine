#include "sensing/Sensing.h"
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/TargetingPipeline.h"
#include "simplesquirrel/simplesquirrel.hpp"
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

TEST_CASE("sensing.worldHandleAndScriptResultContract") {
    auto created = Sensing::newWorld();
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();
    auto       borrowed  = Sensing::resolve(reference);
    REQUIRE(borrowed.isBound());
    auto released = Sensing::release(reference);
    REQUIRE(released.ok());
    CHECK(Sensing::isStale(reference));
    CHECK(!Sensing::resolve(reference).isBound());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local sensing = eve.Sensing();
        local created = sensing.newWorld();
        if (created.ok && created.value.ownership() == "owned" && !created.value.isStale()) {
            local world = created.value;
            local inserted = world.upsert("script.subject", 0.0, 0.0, "red", "unit", "all");
            local queried = world.circle(0.0, 0.0, 2.0, "unit", "", "", "", "all", 4);
            local candidate = world.resultAt(0);
            local snapshot = world.snapshotJson();
            local restored = snapshot.ok ? world.restoreJson(snapshot.value) : { ok = false };
            local rejected = world.restoreJson("{}");
            local released = world.release();
            if (inserted.ok && queried.ok && queried.value == 1 && candidate != null &&
                snapshot.ok && restored.ok && !rejected.ok && released.ok && world.isStale())
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
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

eve::sensing::TargetLocation grid(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

eve::sensing::TargetLocation world(float x, float y, float z) {
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
