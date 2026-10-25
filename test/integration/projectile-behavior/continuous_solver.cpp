// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "weapon"
}
*/
// clang-format on

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include "common/Diagnostic.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
using namespace eve;
using namespace eve::weapon;
Duration timeOf(double value) {
    auto result = Duration::fromSeconds(value);
    REQUIRE(result.ok());
    return result.value();
}
CarrierRecipe shell() {
    CarrierRecipe value;
    value.id        = *LogicalId::parse("solver:shell");
    value.speed     = 10;
    value.lifetime  = timeOf(4);
    value.motionOps = {{CarrierMotionOpKind::IntegrateLinear}};
    value.triggers  = {{CarrierTriggerKind::OnHit}, {CarrierTriggerKind::OnExpire}};
    CarrierImpact bounce{CarrierImpactKind::Bounce, CarrierTriggerKind::OnHit};
    bounce.bounceCount = 100;
    value.impacts      = {{CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit},
                          bounce,
                          {CarrierImpactKind::Release, CarrierTriggerKind::OnHit},
                          {CarrierImpactKind::Release, CarrierTriggerKind::OnExpire}};
    return value;
}
class Corridor final : public ICarrierHitProbe {
public:
    mutable int                  calls  = 0;
    int                          failAt = -1;
    Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion& from,
                                       const CarrierMotion& to) const override {
        if (++calls == failAt)
            return Result<std::vector<Contact>>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected later sweep failure"));
        const double dx = to.position.x - from.position.x;
        if (std::abs(dx) < 1e-12) return Result<std::vector<Contact>>::success({});
        const double wall = dx > 0 ? 1.0 : 0.0;
        const double t    = (wall - from.position.x) / dx;
        if (t < 0 || t > 1) return Result<std::vector<Contact>>::success({});
        Contact hit;
        hit.point    = {wall, from.position.y + (to.position.y - from.position.y) * t, 0};
        hit.normal   = {dx > 0 ? -1.0 : 1.0, 0, 0};
        hit.fraction = t;
        return Result<std::vector<Contact>>::success({hit});
    }
};
class Function final : public ICarrierMotionFunction {
public:
    std::function<Result<CarrierMotion>(const CarrierState&, const CarrierMotion&, Duration)> run;
    Result<CarrierMotion> evaluate(const CarrierState& s, const CarrierMotion& m, Duration dt) const override {
        return run(s, m, dt);
    }
};
}  // namespace

TEST_CASE("carrierSolver.remainderMultipleBouncesAndFramePartition") {
    CombatCarrierRuntime whole, split;
    const auto           spec = shell();
    auto                 a    = whole.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}});
    auto                 b    = split.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    Corridor corridor;
    auto     frame = whole.update(timeOf(0.4), nullptr, &corridor);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        REQUIRE(std::abs(frame.value().events[i].age.seconds() - (0.05 + 0.1 * static_cast<double>(i))) < 1e-8);
    for (int i = 0; i < 40; ++i) REQUIRE(split.update(timeOf(0.01), nullptr, &corridor).ok());
    auto x = whole.find(a.value()), y = split.find(b.value());
    REQUIRE(x.has_value());
    REQUIRE(y.has_value());
    REQUIRE(std::abs(x->motion.position.x - 0.5) < 1e-8);
    REQUIRE(std::abs(x->motion.position.x - y->motion.position.x) < 1e-8);
    REQUIRE(x->motion.velocity == y->motion.velocity);
    REQUIRE(x->age == timeOf(0.4));
    REQUIRE(x->bounceRemaining == 96);
    REQUIRE(std::abs(x->distanceTravelled - 4.0) < 1e-8);
}

TEST_CASE("carrierSolver.intervalDistanceAndExpirySurviveBounce") {
    CombatCarrierRuntime runtime;
    auto                 spec = shell();
    spec.lifetime             = timeOf(0.4);
    spec.triggers.push_back({CarrierTriggerKind::OnInterval, {}, timeOf(0.1)});
    spec.triggers.push_back({CarrierTriggerKind::OnDistance, {}, {}, 0, 2.0});
    spec.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnInterval});
    spec.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnDistance});
    spec.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnExpire});
    REQUIRE(runtime.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}}).ok());
    Corridor corridor;
    auto     frame = runtime.update(timeOf(0.8), nullptr, &corridor);
    REQUIRE(frame.ok());
    int      intervals = 0, distances = 0, expired = 0;
    Duration previous = Duration::zero();
    for (const auto& event : frame.value().events) {
        REQUIRE(event.age >= previous);
        previous = event.age;
        if (event.trigger == CarrierTriggerKind::OnInterval) ++intervals;
        if (event.trigger == CarrierTriggerKind::OnDistance) {
            ++distances;
            REQUIRE(std::abs(event.age.seconds() - 0.2) < 1e-8);
        }
        if (event.trigger == CarrierTriggerKind::OnExpire) ++expired;
    }
    REQUIRE(intervals == 4);
    REQUIRE(distances == 1);
    REQUIRE(expired == 1);
    REQUIRE(runtime.activeCount() == 0);
}

TEST_CASE("carrierSolver.budgetsAndLaterProviderFailureRollbackWholeFrame") {
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.configureSimulation({Duration::zero(), 256, 2, 4096}).ok());
    auto handle = runtime.spawn(shell(), {{0.5, 0, 0}, {1, 0, 0}});
    REQUIRE(handle.ok());
    Corridor corridor;
    REQUIRE(!runtime.update(timeOf(0.4), nullptr, &corridor).ok());
    REQUIRE(runtime.find(handle.value())->age.isZero());
    REQUIRE(runtime.find(handle.value())->bounceRemaining == 100);
    REQUIRE(std::abs(runtime.find(handle.value())->motion.position.x - 0.5) < 1e-12);
    corridor.calls  = 0;
    corridor.failAt = 2;
    REQUIRE(!runtime.update(timeOf(0.2), nullptr, &corridor).ok());
    REQUIRE(runtime.find(handle.value())->age.isZero());
    corridor.failAt = -1;
    REQUIRE(runtime.update(timeOf(0.2), nullptr, &corridor).ok());
}

TEST_CASE("carrierSolver.substepBoundsSnapshotAndCallbackRemainder") {
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.configureSimulation({timeOf(0.01), 256, 64, 4096}).ok());
    auto spec     = shell();
    spec.triggers = {{CarrierTriggerKind::OnExpire}};
    spec.impacts  = {{CarrierImpactKind::Release, CarrierTriggerKind::OnExpire}};
    auto h        = runtime.spawn(spec, {{0, 0, 0}, {1, 0, 0}});
    REQUIRE(h.ok());
    auto saved = runtime.snapshot();
    REQUIRE(saved.schemaVersion == 4);
    REQUIRE(saved.simulation.maxStep == timeOf(0.01));
    int      calls = 0;
    Function fn;
    fn.run = [&](const CarrierState& s, const CarrierMotion& proposed, Duration dt) {
        REQUIRE(dt > Duration::zero());
        REQUIRE(dt <= timeOf(0.01));
        REQUIRE(s.age == timeOf(static_cast<double>(calls) * 0.01));
        ++calls;
        return Result<CarrierMotion>::success(proposed);
    };
    REQUIRE(runtime.updateWithMotion(timeOf(0.105), fn).ok());
    REQUIRE(calls == 11);
    REQUIRE(runtime.find(h.value())->age == timeOf(0.105));
    REQUIRE(std::abs(runtime.find(h.value())->motion.position.x - 1.05) < 1e-9);
    REQUIRE(!runtime.configureSimulation({}).ok());
    auto invalid                     = saved;
    invalid.simulation.maxIterations = 0;
    REQUIRE(!runtime.restore(invalid).ok());
    REQUIRE(runtime.find(h.value())->age == timeOf(0.105));
    invalid               = saved;
    invalid.schemaVersion = 2;
    REQUIRE(!runtime.restore(invalid).ok());
    REQUIRE(runtime.restore(saved).ok());
    calls = 0;
    REQUIRE(runtime.updateWithMotion(timeOf(0.105), fn).ok());
    REQUIRE(calls == 11);

    CombatCarrierRuntime bounce;
    REQUIRE(bounce.spawn(shell(), {{0, 0, 0}, {1, 0, 0}}).ok());
    Corridor corridor;
    int      proposals = 0;
    fn.run             = [&](const CarrierState& s, const CarrierMotion& proposed, Duration dt) {
        if (proposals++ == 0)
            REQUIRE(dt == timeOf(0.15));
        else {
            REQUIRE(s.age == timeOf(0.1));
            REQUIRE(dt == timeOf(0.05));
            REQUIRE(s.motion.velocity.x < 0);
        }
        return Result<CarrierMotion>::success(proposed);
    };
    REQUIRE(bounce.updateWithMotion(timeOf(0.15), fn, nullptr, &corridor).ok());
    REQUIRE(proposals == 2);
}

TEST_CASE("carrierSolver.zeroProgressAndMalformedContactsAreBounded") {
    class Stuck final : public ICarrierHitProbe {
    public:
        double                       fraction = 0;
        Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion& a,
                                           const CarrierMotion& b) const override {
            Contact contact;
            contact.point    = a.position;
            contact.normal   = {b.velocity.x > 0 ? -1.0 : 1.0, 0, 0};
            contact.fraction = fraction;
            return Result<std::vector<Contact>>::success({contact});
        }
    } stuck;
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.configureSimulation({Duration::zero(), 4, 64, 4096}).ok());
    auto h = runtime.spawn(shell(), {{0.5, 0, 0}, {1, 0, 0}});
    REQUIRE(h.ok());
    REQUIRE(!runtime.update(timeOf(0.1), nullptr, &stuck).ok());
    REQUIRE(runtime.find(h.value())->age.isZero());
    stuck.fraction = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!runtime.update(timeOf(0.1), nullptr, &stuck).ok());
    stuck.fraction = 1.1;
    REQUIRE(!runtime.update(timeOf(0.1), nullptr, &stuck).ok());
    REQUIRE(runtime.find(h.value())->bounceRemaining == 100);
}

TEST_CASE("carrierSolver.thresholdTimesAndDeferredChildren") {
    CombatCarrierRuntime runtime;
    auto                 spec = shell();
    spec.speed                = 5;
    spec.motionOps            = {{CarrierMotionOpKind::ApplyGravity}, {CarrierMotionOpKind::IntegrateLinear}};
    spec.motionOps[0].gravity = 10;
    spec.triggers             = {{CarrierTriggerKind::OnApex}, {CarrierTriggerKind::OnExpire}};
    spec.impacts              = {{CarrierImpactKind::EmitHit, CarrierTriggerKind::OnApex}};
    REQUIRE(runtime.spawn(spec, {{}, {0, 1, 0}}).ok());
    auto apex = runtime.update(timeOf(1));
    REQUIRE(apex.ok());
    REQUIRE(apex.value().events.size() == 1);
    REQUIRE(apex.value().events[0].age == timeOf(0.5));

    CombatCarrierRuntime spawning;
    REQUIRE(spawning.configureSimulation({timeOf(0.01), 256, 64, 4096}).ok());
    auto child     = shell();
    child.id       = *LogicalId::parse("solver:child");
    child.triggers = {{CarrierTriggerKind::OnExpire}};
    child.impacts.clear();
    REQUIRE(spawning.registerRecipe(child).ok());
    auto parent = child;
    parent.id   = *LogicalId::parse("solver:parent");
    parent.triggers.push_back({CarrierTriggerKind::OnInterval, {}, timeOf(0.05)});
    CarrierImpact spawn{CarrierImpactKind::SpawnChild, CarrierTriggerKind::OnInterval};
    spawn.childRecipeId = child.id;
    parent.impacts.push_back(spawn);
    REQUIRE(spawning.spawn(parent, {{}, {1, 0, 0}}).ok());
    auto frame = spawning.update(timeOf(0.1));
    REQUIRE(frame.ok());
    REQUIRE(frame.value().childSpawns.size() == 2);
    REQUIRE(spawning.activeCount() == 3);
    for (const auto& state : spawning.states())
        if (state.recipeId == child.id) REQUIRE(state.age.isZero());
}

TEST_CASE("carrierSolver.predictiveSamplesOncePerOuterUpdate") {
    class Target final : public IProjectileTargetProvider {
    public:
        mutable int             calls = 0;
        ProjectilePoint         point{20, 0, 0};
        Result<ProjectilePoint> position(ecs::EntityHandle) const override {
            ++calls;
            return Result<ProjectilePoint>::success(point);
        }
    } target;
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.configureSimulation({timeOf(0.01), 256, 64, 4096}).ok());
    auto            spec = shell();
    CarrierMotionOp predictive{CarrierMotionOpKind::SteerPredictive};
    predictive.maxTurnRateDegrees = 360;
    predictive.predictionSeconds  = 1.5;
    spec.motionOps                = {predictive, {CarrierMotionOpKind::IntegrateLinear}};
    spec.triggers                 = {{CarrierTriggerKind::OnExpire}};
    spec.impacts.clear();
    auto h = runtime.spawn(spec, {{}, {1, 0, 0}, ecs::EntityHandle{}});
    REQUIRE(h.ok());
    REQUIRE(runtime.update(timeOf(0.1), &target).ok());
    target.point.y = 1;
    REQUIRE(runtime.update(timeOf(0.1), &target).ok());
    REQUIRE(target.calls == 2);
    REQUIRE(runtime.find(h.value())->motion.velocity.y > 4.0);
    REQUIRE(runtime.find(h.value())->previousTargetSampleAge == timeOf(0.1));
}


TEST_CASE("carrierSolver.pierceDeduplicatesAcrossSubstepsAndRestore") {
    class Overlap final : public ICarrierHitProbe {
    public:
        std::uint32_t                generation = 1;
        std::type_index              kind{typeid(void)};
        Result<std::vector<Contact>> query(CarrierHandle, const CarrierMotion&,
                                           const CarrierMotion& to) const override {
            Contact contact;
            contact.target.id         = 7;
            contact.target.generation = generation;
            contact.target.type       = kind;
            contact.point             = to.position;
            contact.normal            = {-1, 0, 0};
            contact.fraction          = 1.0;
            return Result<std::vector<Contact>>::success({contact, contact});
        }
    } overlap;
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.configureSimulation({timeOf(0.01), 256, 64, 4096}).ok());
    auto spec = shell();
    spec.impacts.erase(spec.impacts.begin() + 1);
    CarrierImpact pierce{CarrierImpactKind::Pierce, CarrierTriggerKind::OnHit};
    pierce.pierceCount = 3;
    spec.impacts.insert(spec.impacts.begin() + 1, pierce);
    auto h = runtime.spawn(spec, {{}, {1, 0, 0}});
    REQUIRE(h.ok());
    auto first = runtime.update(timeOf(0.1), nullptr, &overlap);
    REQUIRE(first.ok());
    REQUIRE(first.value().events.size() == 1);
    REQUIRE(runtime.find(h.value())->pierceRemaining == 2);
    const auto saved  = runtime.snapshot();
    auto       second = runtime.update(timeOf(0.1), nullptr, &overlap);
    REQUIRE(second.ok());
    REQUIRE(second.value().events.empty());
    REQUIRE(runtime.restore(saved).ok());
    overlap.generation = 2;
    auto reused        = runtime.update(timeOf(0.1), nullptr, &overlap);
    REQUIRE(reused.ok());
    REQUIRE(reused.value().events.size() == 1);
    REQUIRE(runtime.find(h.value())->pierceRemaining == 1);
    overlap.kind       = typeid(int);
    auto differentType = runtime.update(timeOf(0.1), nullptr, &overlap);
    REQUIRE(differentType.ok());
    REQUIRE(differentType.value().events.size() == 1);
    REQUIRE(runtime.find(h.value())->pierceRemaining == 0);
    auto bad = runtime.snapshot();
    bad.slots[h.value().slot].state->piercedTargets.push_back(bad.slots[h.value().slot].state->piercedTargets.front());
    REQUIRE(!runtime.restore(bad).ok());
}

TEST_CASE("carrierSolver.impactSpeedCrossingAndLaterCallbackFailure") {
    CombatCarrierRuntime runtime;
    auto                 spec   = shell();
    spec.impacts[1].restitution = 0.25;
    spec.triggers.push_back({CarrierTriggerKind::OnSpeedBelow, {}, {}, 0, 5});
    spec.impacts.push_back({CarrierImpactKind::EmitHit, CarrierTriggerKind::OnSpeedBelow});
    auto h = runtime.spawn(spec, {{0, 0, 0}, {1, 0, 0}});
    REQUIRE(h.ok());
    auto     saved = runtime.snapshot();
    Corridor corridor;
    auto     frame = runtime.update(timeOf(0.2), nullptr, &corridor);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.size() == 2);
    REQUIRE(frame.value().events[1].trigger == CarrierTriggerKind::OnSpeedBelow);
    REQUIRE(frame.value().events[1].age == timeOf(0.1));
    REQUIRE(std::abs(runtime.find(h.value())->motion.position.x - 0.75) < 1e-9);
    REQUIRE(runtime.restore(saved).ok());
    Function function;
    function.run = [](const CarrierState& state, const CarrierMotion& proposed, Duration) {
        if (!state.age.isZero()) throw std::runtime_error("injected residual callback failure");
        return Result<CarrierMotion>::success(proposed);
    };
    REQUIRE(!runtime.updateWithMotion(timeOf(0.2), function, nullptr, &corridor).ok());
    REQUIRE(runtime.find(h.value())->age.isZero());
    REQUIRE(runtime.find(h.value())->bounceRemaining == 100);
}

TEST_CASE("carrierSolver.optionalCollisionAndWorldSweep") {
    auto spec              = shell();
    spec.collisionEnabled  = false;
    spec.physicsWorldSweep = true;
    CombatCarrierRuntime disabled;
    auto                 handle = disabled.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}});
    REQUIRE(handle.ok());
    Corridor unused;
    unused.failAt = 1;
    auto frame    = disabled.update(timeOf(0.2), nullptr, &unused, nullptr, &unused);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.empty());
    REQUIRE(unused.calls == 0);
    REQUIRE(disabled.find(handle.value())->motion.position.x == 2.5);
    REQUIRE(disabled.update(timeOf(0.1)).ok());
    auto encoded = encodeCarrierRecipe(spec);
    REQUIRE(encoded.ok());
    auto decoded = decodeCarrierRecipe(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value() == spec);
    CombatCarrierRuntime restored;
    REQUIRE(restored.registerRecipe(spec).ok());
    REQUIRE(restored.restore(disabled.snapshot()).ok());
    REQUIRE(restored.update(timeOf(0.1)).ok());

    spec.collisionEnabled = true;
    spec.impacts          = {{CarrierImpactKind::EmitTrigger, CarrierTriggerKind::OnHit}};
    CombatCarrierRuntime selected;
    auto                 live = selected.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}});
    REQUIRE(live.ok());
    REQUIRE(!selected.update(timeOf(0.1), nullptr, &unused).ok());
    REQUIRE(selected.find(live.value())->age.isZero());
    REQUIRE(unused.calls == 0);
    Corridor world;
    frame = selected.update(timeOf(0.1), nullptr, &unused, nullptr, &world);
    REQUIRE(frame.ok());
    REQUIRE(world.calls == 1);
    REQUIRE(unused.calls == 0);
    REQUIRE(frame.value().events.size() == 1);
    REQUIRE(frame.value().events[0].impact == CarrierImpactKind::EmitTrigger);
    REQUIRE(frame.value().events[0].trigger == CarrierTriggerKind::OnHit);
    REQUIRE(frame.value().events[0].damage == 0.0);
    REQUIRE(selected.activeCount() == 1);
    REQUIRE(selected.find(live.value())->motion.position.x == 1.5);
    REQUIRE(selected.find(live.value())->motion.velocity.x == 10.0);
    encoded = encodeCarrierRecipe(spec);
    REQUIRE(encoded.ok());
    decoded = decodeCarrierRecipe(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value() == spec);

    spec.physicsWorldSweep = false;
    CombatCarrierRuntime ordinary;
    REQUIRE(ordinary.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}}).ok());
    REQUIRE(ordinary.update(timeOf(0.1), nullptr, &world, nullptr, &unused).ok());
    REQUIRE(world.calls == 2);
    REQUIRE(unused.calls == 0);
    spec.physicsWorldSweep = true;
    spec.triggers          = {{CarrierTriggerKind::OnExpire}};
    spec.impacts           = {{CarrierImpactKind::Release, CarrierTriggerKind::OnExpire}};
    CombatCarrierRuntime noTrigger;
    REQUIRE(noTrigger.spawn(spec, {{0.5, 0, 0}, {1, 0, 0}}).ok());
    REQUIRE(noTrigger.update(timeOf(0.1)).ok());
}
