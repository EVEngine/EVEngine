#include "sensing/Targeting.h"
#include "common/Capability.h"
#include "common/Identity.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World3D.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <type_traits>
#include <utility>

namespace {

using eve::LogicalId;
using eve::PersistentId;
using eve::physics::TargetingLineOfSightAdapter;
using eve::physics::World3D;
using eve::sensing::CoordinateSpace;
using eve::sensing::GridPoint;
using eve::sensing::ILineOfSightQuery;
using eve::sensing::ISensingCandidateProvider;
using eve::sensing::LineOfSightResult;
using eve::sensing::SensingCandidateProvider;
using eve::sensing::SubjectRef;
using eve::sensing::TargetCandidate;
using eve::sensing::TargetDomain;
using eve::sensing::TargetingQuery;
using eve::sensing::TargetingResolver;
using eve::sensing::TargetingSpec;
using eve::sensing::TargetLocation;
using eve::sensing::WorldArea;
using eve::sensing::WorldPoint;

SubjectRef subject(const char* text) {
    auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

WorldPoint world3D(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    return std::move(point).expect("test World3D point must be valid");
}

struct ResetCapabilities {
    ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
    ~ResetCapabilities() { eve::cap::detail::clearAllRaw(); }
};

class VisibleLineOfSight final : public ILineOfSightQuery {
public:
    explicit VisibleLineOfSight(bool visible) : visible_(visible) {}

    eve::Result<LineOfSightResult> query(const TargetLocation& from, const TargetLocation& to) const override {
        if (from.index() != to.index())
            return eve::Result<LineOfSightResult>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "test LOS received mixed coordinate spaces"));
        if (from.index() == 1)
            return eve::Result<LineOfSightResult>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test LOS does not support grid coordinates"));
        return eve::Result<LineOfSightResult>::success(LineOfSightResult{visible_, std::nullopt});
    }

private:
    bool visible_ = true;
};

}  // namespace

TEST_CASE("targeting.typesKeepIdentityAndCoordinateSpacesSeparate") {
    CHECK((!std::is_convertible_v<PersistentId, SubjectRef>));
    CHECK((!std::is_convertible_v<SubjectRef, PersistentId>));
    CHECK((!std::is_convertible_v<GridPoint, WorldPoint>));

    const auto p2     = world2D(1.f, 2.f);
    const auto p3     = world3D(1.f, 2.f, 3.f);
    auto       circle = WorldArea::circle2D(p2, 4.f);
    REQUIRE(circle.ok());
    CHECK_EQ(circle.value().space(), CoordinateSpace::World2D);

    auto wrongCircle = WorldArea::circle2D(p3, 4.f);
    CHECK(!wrongCircle.ok());
    CHECK_EQ(wrongCircle.code(), eve::StatusCode::Rejected);

    const auto grid = GridPoint::grid2D(1, 2);
    CHECK_EQ(grid.space(), CoordinateSpace::Grid2D);
    CHECK_NE(grid.space(), p2.space());

    eve::sensing::TargetSet set;
    std::move(set.setPoint(p2)).expect("target point");
    auto gridArea = eve::sensing::GridArea::box2D(GridPoint::grid2D(0, 0), GridPoint::grid2D(2, 2));
    REQUIRE(gridArea.ok());
    auto mixedArea = set.setArea(std::move(gridArea).expect("grid area"));
    CHECK(!mixedArea.ok());
    CHECK_EQ(mixedArea.code(), eve::StatusCode::Rejected);
}

TEST_CASE("targeting.providerAndResolverApplyGenericConstraints") {
    ResetCapabilities        reset;
    SensingCandidateProvider provider;
    const auto               origin   = subject("00000000-0000-7000-8000-000000000001");
    const auto               enemy    = subject("00000000-0000-7000-8000-000000000002");
    const auto               farEnemy = subject("00000000-0000-7000-8000-000000000003");
    const auto               ally     = subject("00000000-0000-7000-8000-000000000004");
    auto                     zoneId   = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    auto zone = eve::sensing::ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());

    REQUIRE(
        provider.upsert(TargetCandidate{enemy, world2D(3.f, 4.f), TargetDomain::Enemy, {"unit", "visible"}, {*zone}})
            .ok());
    REQUIRE(provider.upsert(TargetCandidate{farEnemy, world2D(50.f, 0.f), TargetDomain::Enemy, {"unit"}, {}}).ok());
    REQUIRE(
        provider.upsert(TargetCandidate{ally, world2D(2.f, 0.f), TargetDomain::Ally, {"unit", "visible"}, {}}).ok());
    eve::cap::provide<ISensingCandidateProvider>(&provider);

    TargetingSpec spec;
    spec.space        = CoordinateSpace::World2D;
    spec.domain       = TargetDomain::Enemy;
    spec.minCount     = 1;
    spec.maxCount     = 2;
    spec.maxRange     = 10.f;
    spec.requiredTags = {"unit"};
    spec.zone         = *zone;
    spec.worldArea    = std::move(WorldArea::circle2D(world2D(0.f, 0.f), 10.f)).expect("area");

    TargetingQuery query{origin, world2D(0.f, 0.f), spec};
    auto           resolved = TargetingResolver{}.resolve(query);
    REQUIRE(resolved.ok());
    CHECK_EQ(resolved.value().subjects().size(), 1u);
    CHECK_EQ(resolved.value().subjects()[0], enemy);
    CHECK(!resolved.value().primary().has_value());
}

TEST_CASE("targeting.missingCapabilitiesAreUnsupported") {
    ResetCapabilities reset;
    TargetingQuery    query{subject("00000000-0000-7000-8000-000000000001"), world2D(0.f, 0.f), TargetingSpec{}};

    auto noCandidates = TargetingResolver{}.resolve(query);
    CHECK(!noCandidates.ok());
    CHECK_EQ(noCandidates.code(), eve::StatusCode::Unsupported);

    TargetingQuery mixed{query.origin, GridPoint::grid2D(0, 0), query.spec};
    auto           mixedResult = TargetingResolver{}.resolve(mixed);
    CHECK(!mixedResult.ok());
    CHECK_EQ(mixedResult.code(), eve::StatusCode::Rejected);
    REQUIRE(mixedResult.error());
    CHECK_EQ(mixedResult.error()->code(), eve::DiagnosticCode::InvalidArgument);

    SensingCandidateProvider provider;
    eve::cap::provide<ISensingCandidateProvider>(&provider);
    query.spec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    auto noLos             = TargetingResolver{}.resolve(query);
    CHECK(!noLos.ok());
    CHECK_EQ(noLos.code(), eve::StatusCode::Unsupported);
}

TEST_CASE("targeting.losCapabilityIsInjectedAndNeverConvertsGridCoordinates") {
    ResetCapabilities        reset;
    SensingCandidateProvider provider;
    const auto               origin     = subject("00000000-0000-7000-8000-000000000001");
    const auto               target     = subject("00000000-0000-7000-8000-000000000002");
    const auto               gridTarget = subject("00000000-0000-7000-8000-000000000003");
    REQUIRE(provider.upsert(TargetCandidate{target, world3D(1.f, 0.f, 0.f), TargetDomain::Enemy, {}, {}}).ok());
    REQUIRE(provider.upsert(TargetCandidate{gridTarget, GridPoint::grid2D(1, 0), TargetDomain::Enemy, {}, {}}).ok());
    eve::cap::provide<ISensingCandidateProvider>(&provider);

    TargetingSpec spec;
    spec.space       = CoordinateSpace::World3D;
    spec.domain      = TargetDomain::Enemy;
    spec.minCount    = 1;
    spec.maxCount    = 1;
    spec.maxRange    = 10.f;
    spec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    TargetingQuery query{origin, world3D(0.f, 0.f, 0.f), spec};

    VisibleLineOfSight hidden(false);
    eve::cap::provide<ILineOfSightQuery>(&hidden);
    auto hiddenResult = TargetingResolver{}.resolve(query);
    CHECK(!hiddenResult.ok());
    CHECK_EQ(hiddenResult.code(), eve::StatusCode::Rejected);
    REQUIRE(hiddenResult.error());
    CHECK_EQ(hiddenResult.error()->code(), eve::DiagnosticCode::PreconditionViolation);

    VisibleLineOfSight visible(true);
    eve::cap::provide<ILineOfSightQuery>(&visible);
    auto visibleResult = TargetingResolver{}.resolve(query);
    REQUIRE(visibleResult.ok());
    CHECK_EQ(visibleResult.value().subjects().size(), 1u);

    TargetingSpec gridSpec;
    gridSpec.space       = CoordinateSpace::Grid2D;
    gridSpec.minCount    = 0;
    gridSpec.maxCount    = 1;
    gridSpec.lineOfSight = eve::sensing::LineOfSightMode::Required;
    TargetingQuery gridQuery{origin, GridPoint::grid2D(0, 0), gridSpec};
    auto           gridResult = TargetingResolver{}.resolve(gridQuery);
    CHECK(!gridResult.ok());
    CHECK_EQ(gridResult.code(), eve::StatusCode::Unsupported);
}

TEST_CASE("targeting.physicsAdapterRequiresOneActiveWorld") {
    TargetingLineOfSightAdapter adapter;
    const auto                  from = world3D(0.f, 0.f, 0.f);
    const auto                  to   = world3D(1.f, 0.f, 0.f);

    auto noWorld = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!noWorld.ok());
    CHECK_EQ(noWorld.code(), eve::StatusCode::Unsupported);

    World3D first(0.f, 0.f, 0.f, false);
    auto    firstRegistration = adapter.addWorld(&first);
    REQUIRE(firstRegistration.ok());
    auto duplicateRegistration = adapter.addWorld(&first);
    REQUIRE(duplicateRegistration.ok());
    CHECK_EQ(duplicateRegistration.code(), eve::StatusCode::NoOp);

    World3D second(0.f, 0.f, 0.f, false);
    auto    conflictingRegistration = adapter.addWorld(&second);
    CHECK(!conflictingRegistration.ok());
    CHECK_EQ(conflictingRegistration.code(), eve::StatusCode::Conflict);

    auto present = adapter.query(TargetLocation{from}, TargetLocation{to});
    REQUIRE(present.ok());
    CHECK(present.value().visible);

    first.destroy();
    auto invalidWorld = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!invalidWorld.ok());
    CHECK_EQ(invalidWorld.code(), eve::StatusCode::Unsupported);

    auto removed = adapter.removeWorld(&first);
    REQUIRE(removed.ok());
    auto afterRemove = adapter.query(TargetLocation{from}, TargetLocation{to});
    CHECK(!afterRemove.ok());
    CHECK_EQ(afterRemove.code(), eve::StatusCode::Unsupported);
}
