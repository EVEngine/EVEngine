#include "sensing/LineOfSightRouter.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <utility>

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
    StubBackend gridBackend(true);
    StubBackend worldBackend(false);

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
    StubBackend gridBackend(true);

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
    StubBackend gridBackend(true);
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
    CHECK_EQ(viaCapability->query(world(0.f, 0.f, 0.f), world(1.f, 0.f, 0.f))
                 .status()
                 .primaryDiagnostic()
                 ->code(),
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
    StubBackend first(true);
    StubBackend second(false);

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
