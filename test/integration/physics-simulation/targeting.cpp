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
using eve::sensing::CountPolicy;
using eve::sensing::GridPoint;
using eve::sensing::ILineOfSightQuery;
using eve::sensing::ISensingCandidateProvider;
using eve::sensing::LineOfSightResult;
using eve::sensing::SensingCandidateProvider;
using eve::sensing::SensingWorld;
using eve::sensing::SensingWorldCandidateProvider;
using eve::sensing::SubjectRef;
using eve::sensing::TargetCandidate;
using eve::sensing::TargetDomain;
using eve::sensing::TargetingQuery;
using eve::sensing::TargetingResolver;
using eve::sensing::TargetingSpec;
using eve::sensing::TargetLocation;
using eve::sensing::WorldArea;
using eve::sensing::WorldPoint;

[[maybe_unused]] SubjectRef subject(const char* text) {
    auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

[[maybe_unused]] WorldPoint world3D(float x, float y, float z) {
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
