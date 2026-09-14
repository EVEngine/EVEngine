#include "weapon/ProjectileRuntime.h"

#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <map>
#include <string_view>

namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::weapon::ProjectileDefinition definition(eve::weapon::ProjectileMode mode) {
    eve::weapon::ProjectileDefinition result;
    result.id                 = id("combat-projectile:test");
    result.mode               = mode;
    result.speed              = 10.0;
    result.gravity            = 10.0;
    result.maxTurnRateDegrees = 90.0;
    result.lifetime           = eve::Duration::fromNanoseconds(2000000000);
    return result;
}

class TargetProvider final : public eve::weapon::IProjectileTargetProvider {
public:
    [[nodiscard]] eve::Result<eve::weapon::ProjectilePoint> position(ecs::EntityHandle target) const override {
        ++queries;
        auto found = positions.find(target.id);
        if (reject || found == positions.end())
            return eve::Result<eve::weapon::ProjectilePoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "target unavailable"));
        return eve::Result<eve::weapon::ProjectilePoint>::success(found->second);
    }

    mutable bool                                          reject  = false;
    mutable int                                           queries = 0;
    std::map<std::uint32_t, eve::weapon::ProjectilePoint> positions;
};

}  // namespace

TEST_CASE("projectileRuntime.integratesLinearAndBallisticModes") {
    eve::weapon::ProjectileRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    auto linear = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(linear.ok());
    auto ballistic = runtime.spawn(definition(eve::weapon::ProjectileMode::Ballistic), {{}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(ballistic.ok());

    auto update = runtime.update(eve::Duration::fromNanoseconds(500000000));
    REQUIRE(update.ok());
    auto linearState = runtime.find(linear.value());
    REQUIRE(linearState.has_value());
    CHECK(std::fabs(linearState->position.x - 5.0) < 1e-9);
    auto ballisticState = runtime.find(ballistic.value());
    REQUIRE(ballisticState.has_value());
    CHECK(std::fabs(ballisticState->position.y - 2.5) < 1e-9);
}

TEST_CASE("projectileRuntime.homingFailureRollsBackEveryProjectile") {
    eve::weapon::ProjectileRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    ecs::EntityHandle target;
    target.id         = 42;
    target.generation = 7;
    auto linear       = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(linear.ok());
    auto homing = runtime.spawn(definition(eve::weapon::ProjectileMode::Homing), {{}, {1.0, 0.0, 0.0}, target});
    REQUIRE(homing.ok());
    TargetProvider targets;
    targets.reject = true;

    auto update = runtime.update(eve::Duration::fromNanoseconds(500000000), &targets);
    CHECK(!update.ok());
    REQUIRE(runtime.find(linear.value()).has_value());
    const auto linearPosition = runtime.find(linear.value())->position;
    CHECK(std::fabs(linearPosition.x) < 1e-9);
    CHECK(std::fabs(linearPosition.y) < 1e-9);
    CHECK(std::fabs(linearPosition.z) < 1e-9);
    const auto homingPosition = runtime.find(homing.value())->position;
    CHECK(std::fabs(homingPosition.x) < 1e-9);
    CHECK(std::fabs(homingPosition.y) < 1e-9);
    CHECK(std::fabs(homingPosition.z) < 1e-9);
}

TEST_CASE("projectileRuntime.homingTurnsByInjectedRate") {
    eve::weapon::ProjectileRuntime runtime;
    ecs::EntityHandle              target;
    target.id         = 7;
    target.generation = 1;
    auto spawned      = runtime.spawn(definition(eve::weapon::ProjectileMode::Homing), {{}, {1.0, 0.0, 0.0}, target});
    REQUIRE(spawned.ok());
    TargetProvider targets;
    targets.positions[target.id] = {0.0, 0.0, 10.0};

    auto update = runtime.update(eve::Duration::fromNanoseconds(500000000), &targets);
    REQUIRE(update.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(state->velocity.x > 0.0);
    CHECK(state->velocity.z > 0.0);
    CHECK(std::fabs(std::sqrt(state->velocity.x * state->velocity.x + state->velocity.z * state->velocity.z) - 10.0) <
          1e-9);
}

TEST_CASE("projectileRuntime.poolReuseInvalidatesOldGeneration") {
    eve::weapon::ProjectileRuntime runtime;
    REQUIRE(runtime.configurePool(1).ok());
    auto first = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(first.ok());
    auto exhausted = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    CHECK(!exhausted.ok());
    REQUIRE(runtime.release(first.value()).ok());
    auto second = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(second.ok());
    CHECK_EQ(first.value().slot, second.value().slot);
    CHECK(first.value().generation != second.value().generation);
    CHECK(!runtime.release(first.value()).ok());
}

TEST_CASE("projectileRuntime.expiryReturnsSlotToPool") {
    eve::weapon::ProjectileRuntime runtime;
    auto                           shortLived = definition(eve::weapon::ProjectileMode::Linear);
    shortLived.lifetime                       = eve::Duration::fromNanoseconds(100);
    auto spawned                              = runtime.spawn(shortLived, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto update = runtime.update(eve::Duration::fromNanoseconds(100));
    REQUIRE(update.ok());
    REQUIRE_EQ(update.value().released.size(), 1u);
    CHECK_EQ(runtime.activeCount(), 0u);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("projectileRuntime.snapshotRestoresTrajectoryAndEmptySlotGenerationsAtomically") {
    eve::weapon::ProjectileRuntime source;
    REQUIRE(source.configurePool(2).ok());
    auto first = source.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(first.ok());
    REQUIRE(source.update(eve::Duration::fromNanoseconds(250000000)).ok());
    const auto snapshot = source.snapshot();

    eve::weapon::ProjectileRuntime restored;
    REQUIRE(restored.restore(snapshot).ok());
    REQUIRE_EQ(restored.capacity(), 2u);
    REQUIRE_EQ(restored.activeCount(), 1u);
    auto state = restored.find(first.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->position.x - 2.5) < 1e-9);
    REQUIRE(restored.release(first.value()).ok());
    auto reused = restored.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value().slot, first.value().slot);
    CHECK(reused.value().generation != first.value().generation);

    auto invalid = snapshot;
    invalid.slots[0].state->position.x = std::numeric_limits<double>::quiet_NaN();
    const auto before = restored.snapshot();
    CHECK(!restored.restore(invalid).ok());
    CHECK_EQ(restored.activeCount(), 1u);
    CHECK(restored.find(reused.value()).has_value());
    CHECK_EQ(restored.snapshot().slots.size(), before.slots.size());
}
