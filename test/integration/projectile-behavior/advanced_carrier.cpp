// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "weapon"
}
*/
// clang-format on

#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {
using namespace eve;
using namespace eve::weapon;
Duration seconds(double value) {
    auto result = Duration::fromSeconds(value);
    REQUIRE(result.ok());
    return result.value();
}
CarrierRecipe recipe(CarrierMotionOp op) {
    CarrierRecipe result;
    result.id        = *LogicalId::parse("advanced:test");
    result.speed     = 10.0;
    result.lifetime  = seconds(4.0);
    result.motionOps = {op, {CarrierMotionOpKind::IntegrateLinear}};
    result.triggers  = {{CarrierTriggerKind::OnExpire}};
    result.impacts   = {{CarrierImpactKind::Release, CarrierTriggerKind::OnExpire}};
    return result;
}
class Target final : public IProjectileTargetProvider {
public:
    ProjectilePoint         point{-10.0, 0.0, 0.0};
    Result<ProjectilePoint> position(ecs::EntityHandle) const override {
        return Result<ProjectilePoint>::success(point);
    }
};
class Wall final : public ICarrierHitProbe {
public:
    ProjectileVector             normal{-1.0, 0.0, 0.0};
    Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion&, const CarrierMotion&) const override {
        return Result<std::vector<Contact>>::success({{{}, {1.0, 0.0, 0.0}, normal}});
    }
};
}  // namespace

TEST_CASE("advancedCarrier.rotationDragAndSignedAcceleration") {
    CarrierMotionOp rotate{CarrierMotionOpKind::RotateVelocity};
    rotate.angularRateDegrees = 90.0;
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(recipe(rotate), {{}, {1.0, 0.0, 0.0}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(1.0)).ok());
    const auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(std::abs(state->motion.velocity.x) < 1e-9);
    CHECK(std::abs(state->motion.velocity.z + 10.0) < 1e-9);
    CarrierMotionOp drag{CarrierMotionOpKind::ApplyDrag};
    drag.drag = std::log(2.0);
    CombatCarrierRuntime damping;
    auto                 damped = damping.spawn(recipe(drag), {{}, {1.0, 0.0, 0.0}});
    REQUIRE(damped.ok());
    REQUIRE(damping.update(seconds(1.0)).ok());
    CHECK(std::abs(damping.find(damped.value())->motion.velocity.x - 5.0) < 1e-9);
    CarrierMotionOp brake{CarrierMotionOpKind::Accelerate};
    brake.acceleration = -100.0;
    CombatCarrierRuntime braking;
    auto                 stopped = braking.spawn(recipe(brake), {{}, {1.0, 0.0, 0.0}});
    REQUIRE(stopped.ok());
    REQUIRE(braking.update(seconds(1.0)).ok());
    CHECK(braking.find(stopped.value())->motion.velocity.x == 0.0);
}

TEST_CASE("advancedCarrier.antiparallelHomingAndOrbitProviderContract") {
    CarrierMotionOp homing{CarrierMotionOpKind::SteerHoming};
    homing.maxTurnRateDegrees = 90.0;
    CombatCarrierRuntime runtime;
    CarrierSpawnRequest  request{{}, {1.0, 0.0, 0.0}, ecs::EntityHandle{}};
    auto                 spawned = runtime.spawn(recipe(homing), request);
    REQUIRE(spawned.ok());
    Target target;
    REQUIRE(runtime.update(seconds(1.0), &target).ok());
    const auto v = runtime.find(spawned.value())->motion.velocity;
    CHECK(std::abs(v.x) < 1e-9);
    CHECK(std::abs(v.y - 10.0) < 1e-9);
    CarrierMotionOp orbit{CarrierMotionOpKind::SteerOrbit};
    orbit.maxTurnRateDegrees = 360.0;
    orbit.angularRateDegrees = 90.0;
    orbit.orbitRadius        = 10.0;
    CombatCarrierRuntime orbiting;
    CHECK(!orbiting.spawn(recipe(orbit), {{}, {1.0, 0.0, 0.0}}).ok());
    auto orbiter = orbiting.spawn(recipe(orbit), request);
    REQUIRE(orbiter.ok());
    CHECK(!orbiting.update(seconds(0.1)).ok());
    CHECK(orbiting.find(orbiter.value())->age.isZero());
    REQUIRE(orbiting.update(seconds(1.0), &target).ok());
    CHECK(std::abs(orbiting.find(orbiter.value())->motion.velocity.z + 10.0) < 1e-9);
}

TEST_CASE("advancedCarrier.seededWanderSnapshotReplayAndCodec") {
    CarrierMotionOp wander{CarrierMotionOpKind::SteerWander};
    wander.maxTurnRateDegrees = 80.0;
    wander.curveAmplitude     = 0.8;
    wander.curveFrequencyHz   = 2.0;
    wander.randomSeed         = 73;
    auto original             = recipe(wander);
    auto encoded              = encodeCarrierRecipe(original);
    REQUIRE(encoded.ok());
    auto decoded = decodeCarrierRecipe(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().motionOps[0].randomSeed == 73);
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(decoded.value(), {{}, {1.0, 0.0, 0.0}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(0.2)).ok());
    const auto snapshot = runtime.snapshot();
    for (int i = 0; i < 20; ++i) REQUIRE(runtime.update(seconds(0.05)).ok());
    const auto expected = runtime.find(spawned.value());
    REQUIRE(runtime.restore(snapshot).ok());
    for (int i = 0; i < 20; ++i) REQUIRE(runtime.update(seconds(0.05)).ok());
    const auto replay = runtime.find(spawned.value());
    CHECK(replay->motion.position == expected->motion.position);
    CHECK(replay->motion.velocity == expected->motion.velocity);
    CHECK(replay->distanceTravelled == expected->distanceTravelled);
}

TEST_CASE("advancedCarrier.distanceSpeedApexAndLifetimeTriggers") {
    auto path = recipe({CarrierMotionOpKind::ApplyGravity, 20.0});
    path.triggers.push_back({CarrierTriggerKind::OnDistance, {}, {}, 0.0, 2.0});
    path.triggers.push_back({CarrierTriggerKind::OnApex});
    path.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnDistance});
    path.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnApex});
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(path, {{}, {0.0, 1.0, 0.0}});
    REQUIRE(spawned.ok());
    auto first = runtime.update(seconds(0.25));
    REQUIRE(first.ok());
    CHECK(first.value().events.empty());
    auto second = runtime.update(seconds(0.5));
    REQUIRE(second.ok());
    CHECK(second.value().events.size() == 2);
    auto third = runtime.update(seconds(0.1));
    REQUIRE(third.ok());
    CHECK(third.value().events.empty());
    CarrierMotionOp brake{CarrierMotionOpKind::Accelerate};
    brake.acceleration = -8.0;
    auto slow          = recipe(brake);
    slow.triggers.push_back({CarrierTriggerKind::OnSpeedBelow, {}, {}, 0.0, 5.0});
    slow.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnSpeedBelow});
    CombatCarrierRuntime deceleration;
    REQUIRE(deceleration.spawn(slow, {{}, {1.0, 0.0, 0.0}}).ok());
    auto crossing = deceleration.update(seconds(1.0));
    REQUIRE(crossing.ok());
    CHECK(crossing.value().events.size() == 1);
    auto expired = deceleration.update(seconds(10.0));
    REQUIRE(expired.ok());
    CHECK(deceleration.activeCount() == 0);
}

TEST_CASE("advancedCarrier.bounceContactAndInvalidSnapshotRollback") {
    auto bouncing = recipe({CarrierMotionOpKind::IntegrateLinear});
    bouncing.motionOps.resize(1);
    bouncing.triggers.push_back({CarrierTriggerKind::OnHit});
    CarrierImpact bounce{CarrierImpactKind::Bounce, CarrierTriggerKind::OnHit};
    bounce.bounceCount = 2;
    bounce.restitution = 0.5;
    bouncing.impacts.push_back(bounce);
    bouncing.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnHit});
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(bouncing, {{}, {1.0, 0.0, 0.0}});
    REQUIRE(spawned.ok());
    Wall wall;
    REQUIRE(runtime.update(seconds(0.2), nullptr, &wall).ok());
    const auto state = runtime.find(spawned.value());
    CHECK(std::abs(state->motion.position.x - 0.5) < 1e-9);
    CHECK(state->motion.velocity.x == -5.0);
    CHECK(state->bounceRemaining == 1);
    wall.normal = {};
    CHECK(!runtime.update(seconds(0.2), nullptr, &wall).ok());
    CHECK(runtime.find(spawned.value())->age == state->age);
    auto invalid                        = runtime.snapshot();
    invalid.slots[0].state->handle.slot = 1;
    CHECK(!runtime.restore(invalid).ok());
    invalid                                   = runtime.snapshot();
    invalid.slots[0].state->distanceTravelled = std::numeric_limits<double>::quiet_NaN();
    CHECK(!runtime.restore(invalid).ok());
    CHECK(runtime.find(spawned.value())->motion.velocity == state->motion.velocity);
}

TEST_CASE("advancedCarrier.predictiveLeadAndStoppedSpeedRecovery") {
    CarrierMotionOp predictive{CarrierMotionOpKind::SteerPredictive};
    predictive.maxTurnRateDegrees = 360.0;
    predictive.predictionSeconds  = 1.5;
    CombatCarrierRuntime runtime;
    Target               target;
    target.point = {20.0, 0.0, 0.0};
    auto spawned = runtime.spawn(recipe(predictive), {{}, {1.0, 0.0, 0.0}, ecs::EntityHandle{}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(0.1), &target).ok());
    target.point.y = 1.0;
    REQUIRE(runtime.update(seconds(0.1), &target).ok());
    const auto velocity = runtime.find(spawned.value())->motion.velocity;
    CHECK(velocity.y > 4.0);
    CarrierMotionOp wave{CarrierMotionOpKind::SpeedOscillate};
    wave.curveAmplitude   = 20.0;
    wave.curveFrequencyHz = 1.0;
    CombatCarrierRuntime pulsing;
    auto                 pulse = pulsing.spawn(recipe(wave), {{}, {1.0, 0.0, 0.0}});
    REQUIRE(pulse.ok());
    REQUIRE(pulsing.update(seconds(0.75)).ok());
    CHECK(pulsing.find(pulse.value())->motion.velocity.x == 0.0);
    REQUIRE(pulsing.update(seconds(0.5)).ok());
    CHECK(pulsing.find(pulse.value())->motion.velocity.x > 29.0);
}

TEST_CASE("advancedCarrier.intervalBudgetRecipeMutationAndExpiryBoundaries") {
    auto original = recipe({CarrierMotionOpKind::IntegrateLinear});
    original.motionOps.resize(1);
    original.lifetime = seconds(0.5);
    original.triggers.push_back({CarrierTriggerKind::OnInterval, {}, seconds(0.1)});
    original.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnInterval});
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(original, {{}, {1.0, 0.0, 0.0}});
    REQUIRE(spawned.ok());
    CHECK(runtime.registerRecipe(original).ok());
    auto replacement  = original;
    replacement.speed = 20.0;
    CHECK(!runtime.registerRecipe(replacement).ok());
    auto expiry = runtime.update(seconds(10.0));
    REQUIRE(expiry.ok());
    // Expire must not suppress periodic firings that occurred earlier in the step.
    CHECK(expiry.value().events.size() == 5);
    CHECK(runtime.activeCount() == 0);
    REQUIRE(runtime.registerRecipe(replacement).ok());
    auto excessive = recipe({CarrierMotionOpKind::IntegrateLinear});
    excessive.motionOps.resize(1);
    excessive.triggers.push_back({CarrierTriggerKind::OnInterval, {}, Duration::fromNanoseconds(1)});
    CombatCarrierRuntime bounded;
    auto                 boundedHandle = bounded.spawn(excessive, {{}, {1.0, 0.0, 0.0}});
    REQUIRE(boundedHandle.ok());
    CHECK(!bounded.update(seconds(1.0)).ok());
    CHECK(bounded.find(boundedHandle.value())->age.isZero());
}

TEST_CASE("advancedCarrier.curvesRemainBoundedAcrossManyPeriods") {
    for (auto kind : {CarrierMotionOpKind::CurveSway, CarrierMotionOpKind::CurveHelix}) {
        CarrierMotionOp curve{kind};
        curve.curveAmplitude   = 1.25;
        curve.curveFrequencyHz = 1.0;
        auto curved            = recipe(curve);
        curved.lifetime        = seconds(10.0);
        CombatCarrierRuntime runtime;
        auto                 spawned = runtime.spawn(curved, {{}, {1.0, 0.0, 0.0}});
        REQUIRE(spawned.ok());
        for (int i = 0; i < 400; ++i) {
            REQUIRE(runtime.update(seconds(0.01)).ok());
            const auto state = runtime.find(spawned.value());
            CHECK(std::abs(state->motion.position.z) <= 1.25 + 1e-8);
            CHECK(std::abs(state->motion.position.y) <= 2.5 + 1e-8);
        }
        const auto state = runtime.find(spawned.value());
        CHECK(std::abs(state->motion.position.x - 40.0) < 1e-8);
        CHECK(std::abs(state->motion.position.y) < 1e-8);
        CHECK(std::abs(state->motion.position.z) < 1e-8);
    }
}

TEST_CASE("advancedCarrier.roughBounceReplayAndSnapshotVersionValidation") {
    auto rough = recipe({CarrierMotionOpKind::IntegrateLinear});
    rough.motionOps.resize(1);
    rough.triggers.push_back({CarrierTriggerKind::OnHit});
    CarrierImpact bounce{CarrierImpactKind::Bounce, CarrierTriggerKind::OnHit};
    bounce.bounceCount         = 5;
    bounce.bounceFriction      = 0.4;
    bounce.bounceSpreadDegrees = 25.0;
    bounce.randomSeed          = 99;
    rough.impacts.push_back(bounce);
    auto encoded = encodeCarrierRecipe(rough);
    REQUIRE(encoded.ok());
    auto decoded = decodeCarrierRecipe(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value() == rough);
    CombatCarrierRuntime runtime;
    auto                 spawned = runtime.spawn(rough, {{}, {1.0, 0.5, 0.0}});
    REQUIRE(spawned.ok());
    const auto before = runtime.snapshot();
    Wall       wall;
    REQUIRE(runtime.update(seconds(0.2), nullptr, &wall).ok());
    const auto expected = runtime.find(spawned.value());
    CHECK(expected->motion.velocity.x < 0.0);
    REQUIRE(runtime.restore(before).ok());
    REQUIRE(runtime.update(seconds(0.2), nullptr, &wall).ok());
    CHECK(runtime.find(spawned.value())->motion.velocity == expected->motion.velocity);
    auto bad          = runtime.snapshot();
    bad.schemaVersion = 1;
    CHECK(!runtime.restore(bad).ok());
    bad                         = runtime.snapshot();
    bad.slots[0].state->heading = {};
    CHECK(!runtime.restore(bad).ok());
    bad                  = runtime.snapshot();
    bad.recipes[0].speed = 17.0;
    CHECK(!runtime.restore(bad).ok());
    CHECK(runtime.find(spawned.value())->motion.velocity == expected->motion.velocity);
}

TEST_CASE("advancedCarrier.invalidOperatorAndTriggerCompositionIsRejected") {
    CombatCarrierRuntime runtime;
    auto                 invalid = recipe({static_cast<CarrierMotionOpKind>(255)});
    REQUIRE(!runtime.spawn(invalid, {{}, {1.0, 0.0, 0.0}}).ok());
    invalid = recipe({CarrierMotionOpKind::IntegrateLinear});
    REQUIRE(!runtime.spawn(invalid, {{}, {1.0, 0.0, 0.0}}).ok());
    invalid.motionOps.resize(1);
    invalid.triggers.push_back({CarrierTriggerKind::OnExpire});
    REQUIRE(!runtime.spawn(invalid, {{}, {1.0, 0.0, 0.0}}).ok());
    invalid.triggers.resize(1);
    invalid.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnApex});
    REQUIRE(!runtime.spawn(invalid, {{}, {1.0, 0.0, 0.0}}).ok());
    REQUIRE(runtime.activeCount() == 0);
    auto valid = recipe({CarrierMotionOpKind::IntegrateLinear});
    valid.motionOps.resize(1);
    REQUIRE(!runtime.spawn(valid, {{}, {}}).ok());
    REQUIRE(!runtime.findRecipe(valid.id).has_value());
    CarrierVolleySpec invalidVolley;
    invalidVolley.count = 0;
    REQUIRE(!runtime.spawnVolley(valid, {{}, {1.0, 0.0, 0.0}}, invalidVolley).ok());
    REQUIRE(!runtime.findRecipe(valid.id).has_value());
}

TEST_CASE("advancedCarrier.triggerChronologyAndBouncePathRespectContact") {
    auto timed = recipe({CarrierMotionOpKind::IntegrateLinear});
    timed.motionOps.resize(1);
    timed.triggers.push_back({CarrierTriggerKind::OnFuse, seconds(0.05)});
    timed.triggers.push_back({CarrierTriggerKind::OnHit});
    CarrierImpact splash{CarrierImpactKind::Splash, CarrierTriggerKind::OnFuse};
    splash.splashRadius = 2.0;
    timed.impacts.push_back(splash);
    timed.impacts.push_back({CarrierImpactKind::Release, CarrierTriggerKind::OnFuse});
    timed.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit});
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.spawn(timed, {{}, {1.0, 0.0, 0.0}}).ok());
    Wall wall;
    auto earlyFuse = runtime.update(seconds(0.2), nullptr, &wall);
    REQUIRE(earlyFuse.ok());
    REQUIRE(earlyFuse.value().events.size() == 1);
    CHECK(earlyFuse.value().events[0].trigger == CarrierTriggerKind::OnFuse);
    CHECK(std::abs(earlyFuse.value().events[0].position.x - 0.5) < 1e-9);
    auto bouncing = recipe({CarrierMotionOpKind::IntegrateLinear});
    bouncing.motionOps.resize(1);
    bouncing.triggers.push_back({CarrierTriggerKind::OnHit});
    bouncing.triggers.push_back({CarrierTriggerKind::OnDistance, {}, {}, 0.0, 1.5});
    CarrierImpact bounce{CarrierImpactKind::Bounce, CarrierTriggerKind::OnHit};
    bounce.bounceCount = 2;
    bouncing.impacts.push_back(bounce);
    bouncing.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnDistance});
    CombatCarrierRuntime ricochet;
    auto                 spawned = ricochet.spawn(bouncing, {{}, {1.0, 0.0, 0.0}});
    REQUIRE(spawned.ok());
    auto contact = ricochet.update(seconds(0.2), nullptr, &wall);
    REQUIRE(contact.ok());
    REQUIRE(contact.value().events.size() == 1);
    CHECK(std::abs(contact.value().events[0].age.seconds() - 0.15) < 1e-9);
    CHECK(std::abs(ricochet.find(spawned.value())->distanceTravelled - 2.0) < 1e-9);
}

namespace {
class MotionFunction final : public ICarrierMotionFunction {
public:
    std::function<Result<CarrierMotion>(const CarrierState&, const CarrierMotion&, Duration)> function;
    Result<CarrierMotion> evaluate(const CarrierState& state, const CarrierMotion& proposed,
                                   Duration dt) const override {
        return function(state, proposed, dt);
    }
};
}  // namespace

TEST_CASE("advancedCarrier.customMotionAtomicityReplayAndReentrancy") {
    CombatCarrierRuntime runtime;
    auto                 spec = recipe({CarrierMotionOpKind::ApplyGravity});
    spec.motionOps            = {{CarrierMotionOpKind::IntegrateLinear}};
    auto first                = runtime.spawn(spec, {{}, {1, 0, 0}});
    auto second               = runtime.spawn(spec, {{}, {1, 0, 0}});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto     saved = runtime.snapshot();
    MotionFunction function;
    function.function = [&](const CarrierState& state, const CarrierMotion&, Duration dt) {
        REQUIRE(state.distanceTravelled == 0.0);
        REQUIRE(!runtime.release(first.value()).ok());
        REQUIRE(!runtime.restore(saved).ok());
        REQUIRE(!runtime.configurePool(10).ok());
        REQUIRE(!runtime.spawn(spec, {{}, {1, 0, 0}}).ok());
        REQUIRE(!runtime.update(dt).ok());
        return Result<CarrierMotion>::success({{2, 3, 0}, {4, 5, 0}});
    };
    REQUIRE(runtime.updateWithMotion(seconds(0.5), function).ok());
    REQUIRE(runtime.find(first.value())->motion.position.y == 3.0);
    REQUIRE(std::abs(runtime.find(first.value())->distanceTravelled - std::sqrt(13.0)) < 1e-9);
    REQUIRE(runtime.restore(saved).ok());
    REQUIRE(runtime.updateWithMotion(seconds(0.5), function).ok());
    REQUIRE(runtime.find(first.value())->motion.position.y == 3.0);
    REQUIRE(runtime.restore(saved).ok());
    function.function = [](const CarrierState& state, const CarrierMotion& proposed, Duration) {
        if (state.handle.slot == 1) throw std::runtime_error("motion failed");
        auto output       = proposed;
        output.position.y = 200;
        return Result<CarrierMotion>::success(output);
    };
    REQUIRE(!runtime.updateWithMotion(seconds(0.5), function).ok());
    REQUIRE(runtime.find(first.value())->motion.position.y == 0.0);
    REQUIRE(runtime.find(first.value())->age == Duration::zero());
    function.function = [](const CarrierState&, const CarrierMotion& proposed, Duration) {
        auto output       = proposed;
        output.velocity.x = std::numeric_limits<double>::infinity();
        return Result<CarrierMotion>::success(output);
    };
    REQUIRE(!runtime.updateWithMotion(seconds(0.5), function).ok());
    REQUIRE(runtime.find(first.value())->age == Duration::zero());
    REQUIRE(runtime.update(seconds(0.5)).ok());
}

TEST_CASE("advancedCarrier.customMotionCollisionAndClampedLifetime") {
    CombatCarrierRuntime runtime;
    auto                 spec = recipe({CarrierMotionOpKind::ApplyGravity});
    spec.motionOps            = {{CarrierMotionOpKind::IntegrateLinear}};
    spec.triggers.push_back({CarrierTriggerKind::OnHit});
    spec.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit});
    spec.impacts.push_back({CarrierImpactKind::Bounce, CarrierTriggerKind::OnHit});
    spec.impacts.back().bounceCount = 2;
    auto spawned                    = runtime.spawn(spec, {{}, {1, 0, 0}});
    REQUIRE(spawned.ok());
    MotionFunction function;
    function.function = [&](const CarrierState& state, const CarrierMotion& proposed, Duration dt) {
        REQUIRE(dt <= seconds(4));
        if (!state.age.isZero()) return Result<CarrierMotion>::success(proposed);
        return Result<CarrierMotion>::success({{4, 0, 0}, {1, 0, 0}});
    };
    Wall wall;
    auto frame = runtime.updateWithMotion(seconds(10), function, nullptr, &wall);
    REQUIRE(frame.ok());
    // Collision precedes lifetime; retirement still occurs unconditionally.
    REQUIRE(runtime.activeCount() == 0);
    REQUIRE(!frame.value().events.empty());
}
