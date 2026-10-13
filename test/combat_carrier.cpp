#include <cmath>
#include <map>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>
#include "common/Diagnostic.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "weapon/CombatCarrier.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// combat_carrier.cpp
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return duration.value();
}

class TargetProvider final : public eve::weapon::IProjectileTargetProvider {
public:
    [[nodiscard]] eve::Result<eve::weapon::ProjectilePoint> position(ecs::EntityHandle target) const override {
        auto found = positions.find(target.id);
        if (reject || found == positions.end())
            return eve::Result<eve::weapon::ProjectilePoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "target unavailable"));
        return eve::Result<eve::weapon::ProjectilePoint>::success(found->second);
    }

    mutable bool                                          reject = false;
    std::map<std::uint32_t, eve::weapon::ProjectilePoint> positions;
};

class HitProbe final : public eve::weapon::ICarrierHitProbe {
public:
    [[nodiscard]] eve::Result<std::vector<Contact>> query(eve::weapon::CarrierHandle, const eve::weapon::CarrierMotion&,
                                                          const eve::weapon::CarrierMotion& current) const override {
        ++queries;
        if (reject)
            return eve::Result<std::vector<Contact>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "hit probe failed"));
        std::vector<Contact> contacts;
        for (const auto& planned : plannedHits) {
            if (std::fabs(current.position.x - planned.atX) < 1e-9) {
                Contact contact;
                contact.target = planned.target;
                contact.point  = current.position;
                contact.normal = planned.normal;
                contacts.push_back(contact);
            }
        }
        return eve::Result<std::vector<Contact>>::success(std::move(contacts));
    }

    struct PlannedHit {
        double                        atX = 0.0;
        ecs::EntityHandle             target{};
        eve::weapon::ProjectileVector normal{-1.0, 0.0, 0.0};
    };

    mutable int             queries = 0;
    mutable bool            reject  = false;
    std::vector<PlannedHit> plannedHits;
};

eve::weapon::CarrierRecipe linearShell(std::string_view recipeId, double damage) {
    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id(recipeId);
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::EmitHit, eve::weapon::CarrierTriggerKind::OnHit, damage});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});
    return recipe;
}

}  // namespace

TEST_CASE("combatCarrier.composesGravityAndIntegrateMotion") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:ballistic");
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::ApplyGravity, 10.0});
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(recipe, {{}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.5));
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    // v0=(0,10,0), after gravity vy=5, integrate y=2.5
    CHECK(std::fabs(state->motion.position.y - 2.5) < 1e-9);
}

TEST_CASE("combatCarrier.fuseSplashReleasesWithoutHitProbe") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:grenade");
    recipe.lifetime = seconds(5.0);
    recipe.speed    = 1.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    eve::weapon::CarrierTrigger fuse;
    fuse.kind = eve::weapon::CarrierTriggerKind::OnFuse;
    fuse.fuse = seconds(0.4);
    recipe.triggers.push_back(fuse);
    eve::weapon::CarrierImpact splash;
    splash.kind         = eve::weapon::CarrierImpactKind::Splash;
    splash.on           = eve::weapon::CarrierTriggerKind::OnFuse;
    splash.damage       = 40.0;
    splash.splashRadius = 3.0;
    splash.damageType   = "Combat.Damage.Explosive";
    recipe.impacts.push_back(splash);
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnFuse});

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto before = runtime.update(seconds(0.3));
    REQUIRE(before.ok());
    CHECK(before.value().events.empty());
    CHECK(runtime.find(spawned.value()).has_value());

    auto boom = runtime.update(seconds(0.2));
    REQUIRE(boom.ok());
    REQUIRE(boom.value().events.size() == 1);
    CHECK(boom.value().events[0].impact == eve::weapon::CarrierImpactKind::Splash);
    CHECK(std::fabs(boom.value().events[0].splashRadius - 3.0) < 1e-9);
    CHECK(boom.value().released.size() == 1);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combatCarrier.pierceSuppressesReleaseUntilBudgetExhausted") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe = linearShell("carrier:piercing", 10.0);
    eve::weapon::CarrierImpact pierce;
    pierce.kind        = eve::weapon::CarrierImpactKind::Pierce;
    pierce.on          = eve::weapon::CarrierTriggerKind::OnHit;
    pierce.pierceCount = 2;
    // Insert pierce before Release so order is EmitHit, Pierce, Release.
    recipe.impacts.insert(recipe.impacts.begin() + 1, pierce);

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe          hits;
    ecs::EntityHandle targetA;
    targetA.id         = 1;
    targetA.generation = 1;
    ecs::EntityHandle targetB;
    targetB.id         = 2;
    targetB.generation = 1;
    ecs::EntityHandle targetC;
    targetC.id         = 3;
    targetC.generation = 1;
    hits.plannedHits.push_back({5.0, targetA});
    hits.plannedHits.push_back({10.0, targetB});
    hits.plannedHits.push_back({15.0, targetC});

    auto first = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(first.ok());
    REQUIRE(first.value().events.size() == 1);
    CHECK(first.value().released.empty());
    CHECK(runtime.find(spawned.value()).has_value());
    CHECK(runtime.find(spawned.value())->pierceRemaining == 1);

    auto second = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(second.ok());
    REQUIRE(second.value().events.size() == 1);
    CHECK(second.value().released.empty());
    CHECK(runtime.find(spawned.value())->pierceRemaining == 0);

    auto third = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(third.ok());
    REQUIRE(third.value().events.size() == 1);
    CHECK(third.value().released.size() == 1);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combatCarrier.bounceReflectsVelocityAndKeepsCarrierAlive") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe = linearShell("carrier:bounce", 5.0);
    eve::weapon::CarrierImpact bounce;
    bounce.kind        = eve::weapon::CarrierImpactKind::Bounce;
    bounce.on          = eve::weapon::CarrierTriggerKind::OnHit;
    bounce.bounceCount = 1;
    bounce.restitution = 1.0;
    recipe.impacts.insert(recipe.impacts.begin() + 1, bounce);

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe          hits;
    ecs::EntityHandle wall;
    wall.id         = 9;
    wall.generation = 1;
    hits.plannedHits.push_back({5.0, wall, {-1.0, 0.0, 0.0}});

    auto frame = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(frame.ok());
    CHECK(frame.value().released.empty());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(state->motion.velocity.x < 0.0);
    CHECK(state->bounceRemaining == 0);
}

TEST_CASE("combatCarrier.spawnChildOnExpireUsesFreedSlot") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());

    eve::weapon::CarrierRecipe child = linearShell("carrier:shard", 3.0);
    // Child without OnHit so update needs no probe.
    child.triggers = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    child.impacts  = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    REQUIRE(runtime.registerRecipe(child).ok());

    eve::weapon::CarrierRecipe parent;
    parent.id       = id("carrier:cluster");
    parent.lifetime = seconds(0.5);
    parent.speed    = 4.0;
    parent.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    parent.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    eve::weapon::CarrierImpact spawnChild;
    spawnChild.kind          = eve::weapon::CarrierImpactKind::SpawnChild;
    spawnChild.on            = eve::weapon::CarrierTriggerKind::OnExpire;
    spawnChild.childRecipeId = child.id;
    parent.impacts.push_back(spawnChild);
    parent.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(parent, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    CHECK(runtime.activeCount() == 1);

    auto frame = runtime.update(seconds(0.5));
    REQUIRE(frame.ok());
    CHECK(frame.value().released.size() == 1);
    CHECK(frame.value().childSpawns.size() == 1);
    CHECK(runtime.activeCount() == 1);
    auto states = runtime.states();
    REQUIRE(states.size() == 1);
    CHECK(states[0].recipeId == child.id);
}

TEST_CASE("combatCarrier.hitProbeFailureRollsBackWholeFrame") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    auto spawned = runtime.spawn(linearShell("carrier:rollback", 1.0), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe hits;
    hits.reject = true;
    auto frame  = runtime.update(seconds(0.5), nullptr, &hits);
    CHECK(!frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.x) < 1e-9);
}

TEST_CASE("combatCarrier.bridgeFromProjectileDefinitionPreservesLinearMotion") {
    eve::weapon::ProjectileDefinition definition;
    definition.id       = id("projectile:bridge");
    definition.mode     = eve::weapon::ProjectileMode::Linear;
    definition.speed    = 10.0;
    definition.lifetime = seconds(2.0);
    auto recipe         = eve::weapon::carrierRecipeFromProjectile(definition, 12.0);
    REQUIRE(recipe.ok());

    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto spawned = runtime.spawn(recipe.value(), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.5), nullptr, nullptr);
    // Recipe includes OnHit — missing probe must fail observably.
    CHECK(!frame.ok());

    // Rebuild without OnHit for motion-only check via custom recipe from bridge fields.
    eve::weapon::CarrierRecipe motionOnly = recipe.value();
    motionOnly.triggers                   = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    motionOnly.impacts = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    eve::weapon::CombatCarrierRuntime runtime2;
    REQUIRE(runtime2.configurePool(2).ok());
    auto spawned2 = runtime2.spawn(motionOnly, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned2.ok());
    auto ok = runtime2.update(seconds(0.5));
    REQUIRE(ok.ok());
    auto state = runtime2.find(spawned2.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.x - 5.0) < 1e-9);
}

class BodyDefenseProvider final : public eve::weapon::ICarrierBodyDefenseProvider {
public:
    [[nodiscard]] eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>> sample(
        eve::weapon::CarrierHandle, ecs::EntityHandle, const eve::weapon::CarrierMotion&) const override {
        return eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>>::success(bodies);
    }

    std::vector<eve::weapon::CarrierBodyDefenseSample> bodies;
};

TEST_CASE("combatCarrier.homingAvoidsFrontBodyDefenseLaterally") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:smart-missile");
    recipe.lifetime = seconds(5.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::SteerHoming, 0.0, 360.0});
    eve::weapon::CarrierMotionOp avoid;
    avoid.kind               = eve::weapon::CarrierMotionOpKind::SteerAvoidBody;
    avoid.maxTurnRateDegrees = 360.0;
    avoid.avoidLookAhead     = 8.0;
    avoid.avoidStrength      = 4.0;
    avoid.avoidRadiusPadding = 0.5;
    recipe.motionOps.push_back(avoid);
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    ecs::EntityHandle target;
    target.id         = 7;
    target.generation = 1;
    TargetProvider targets;
    targets.positions[target.id] = {20.0, 0.0, 0.0};

    BodyDefenseProvider                   bodies;
    eve::weapon::CarrierBodyDefenseSample shield;
    shield.center           = {5.0, 0.0, 0.0};
    shield.radius           = 2.0;
    shield.facing           = {-1.0, 0.0, 0.0};  // faces the incoming carrier
    shield.frontConeDegrees = 90.0;
    bodies.bodies.push_back(shield);

    auto spawned = runtime.spawn(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, target});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.2), &targets, nullptr, &bodies);
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    // Avoidance should introduce a lateral (Z) component instead of flying straight into the shield.
    CHECK(std::fabs(state->motion.velocity.z) > 1e-3);
}

TEST_CASE("combatCarrier.spawnVolleyFanFiresDistinctDirections") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(8).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:pellet");
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    eve::weapon::CarrierVolleySpec volley;
    volley.count         = 3;
    volley.pattern       = eve::weapon::CarrierVolleyPattern::Fan;
    volley.spreadDegrees = 90.0;

    auto handles = runtime.spawnVolley(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}}, volley);
    REQUIRE(handles.ok());
    REQUIRE(handles.value().size() == 3);
    CHECK(runtime.activeCount() == 3);

    REQUIRE(runtime.update(seconds(0.5)).ok());
    auto states = runtime.states();
    REQUIRE(states.size() == 3);
    // Outer pellets should have diverged in Z after flying forward.
    double minZ = states[0].motion.position.z;
    double maxZ = states[0].motion.position.z;
    for (const auto& state : states) {
        minZ = std::min(minZ, state.motion.position.z);
        maxZ = std::max(maxZ, state.motion.position.z);
    }
    CHECK(maxZ - minZ > 1.0);
}

TEST_CASE("combatCarrier.curveSwayAddsLateralOscillation") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:wavy");
    recipe.lifetime = seconds(3.0);
    recipe.speed    = 10.0;
    eve::weapon::CarrierMotionOp sway;
    sway.kind             = eve::weapon::CarrierMotionOpKind::CurveSway;
    sway.curveAmplitude   = 1.0;
    sway.curveFrequencyHz = 1.0;
    recipe.motionOps.push_back(sway);
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.25));
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(state->motion.position.x > 0.0);
    CHECK(std::fabs(state->motion.position.z) > 1e-6);
}

TEST_CASE("combatCarrier.snapshotRestoreRoundTrip") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    eve::weapon::CarrierRecipe recipe = linearShell("carrier:snap", 1.0);
    recipe.triggers                   = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    recipe.impacts = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    auto spawned   = runtime.spawn(recipe, {{1.0, 2.0, 3.0}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(0.25)).ok());

    auto                              snap = runtime.snapshot();
    eve::weapon::CombatCarrierRuntime restored;
    REQUIRE(restored.configurePool(4).ok());
    REQUIRE(restored.registerRecipe(recipe).ok());
    REQUIRE(restored.restore(snap).ok());
    auto state = restored.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.y - 4.5) < 1e-9);
}

// combat_carrier_script.cpp
TEST_CASE("combatCarrierScript.castAtTargetReportsHitEvents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        created <- weapon.newCarrierRuntime(16);
        pool <- created.value;
        pool.setTargetPosition(7, 1, 5.0, 0.0, 0.0);
        pool.addHitTarget(7, 1, 5.0, 0.0, 0.0, 0.6);
        casted <- pool.castAtTarget([
            { kind = "homing", turnRate = 90 },
            { kind = "pierce", count = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 12, lifetime = 2, element = "fire" }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 7, 1);
        missingTarget <- pool.cast([
            { kind = "homing", turnRate = 90 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 1, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        frame <- pool.update(0.5);
        eventCount <- frame.value.eventCount;
        firstImpact <- frame.value.events[0].impact;
        firstTrigger <- frame.value.events[0].trigger;
        preset <- pool.registerRecipe({
            id = "spell:shell",
            lifetime = 1.5,
            speed = 8.0,
            damage = 4.0,
            motion = "linear"
        });
        spawned <- pool.spawn("spell:shell", 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
        // Preset OnHit recipes must advance even with an empty hit list.
        emptyHitFrame <- pool.update(0.1);
        invalid <- pool.cast([
            { kind = "projectile", id = "spell:a", speed = 1, damage = 1, lifetime = 1 },
            { kind = "homing", turnRate = 10 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        oversized <- pool.addHitTarget(4294967296, 1, 0.0, 0.0, 0.0, 0.5);
        avoidBody <- pool.registerRecipe({
            id = "spell:avoid",
            lifetime = 1.0,
            speed = 5.0,
            motion = [ { kind = "avoidBody", avoidLookAhead = 2.0, maxTurnRateDegrees = 90.0 }, "linear" ],
            triggers = [ "onExpire" ],
            impacts = [ { kind = "release", on = "onExpire" } ]
        });
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK(vm.find("casted").toTable().get<bool>("ok"));
    CHECK(!vm.find("missingTarget").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("eventCount").toInt() >= 1);
    CHECK_EQ(vm.find("firstImpact").toString(), std::string("emitHit"));
    CHECK_EQ(vm.find("firstTrigger").toString(), std::string("onHit"));
    CHECK(vm.find("preset").toTable().get<bool>("ok"));
    CHECK(vm.find("spawned").toTable().get<bool>("ok"));
    CHECK(vm.find("emptyHitFrame").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(!vm.find("oversized").toTable().get<bool>("ok"));
    CHECK(!vm.find("avoidBody").toTable().get<bool>("ok"));
}

TEST_CASE("combatCarrierScript.castUniquifiesRecipeIds") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        pool <- weapon.newCarrierRuntime(16).value;
        first <- pool.cast([
            { kind = "damage", add = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        second <- pool.cast([
            { kind = "damage", multiply = 3 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 1.0, 0.0, 1.0, 0.0, 0.0);
        pool.addHitTarget(1, 1, 5.0, 0.0, 0.0, 0.6);
        pool.addHitTarget(2, 1, 5.0, 1.0, 0.0, 0.6);
        frame <- pool.update(0.5);
        firstRecipe <- first.value.recipeId;
        secondRecipe <- second.value.recipeId;
        eventCount <- frame.value.eventCount;
        dmgA <- frame.value.events[0].damage;
        dmgB <- frame.value.events[1].damage;
    )"));
    CHECK(vm.find("first").toTable().get<bool>("ok"));
    CHECK(vm.find("second").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("firstRecipe").toString() != vm.find("secondRecipe").toString());
    CHECK(vm.find("eventCount").toInt() >= 2);
    const double dmgA = vm.find("dmgA").toFloat();
    const double dmgB = vm.find("dmgB").toFloat();
    // First cast: 5+1=6; second: 5*3=15. Both must survive independently.
    const bool sawSix     = std::fabs(dmgA - 6.0) < 1e-6 || std::fabs(dmgB - 6.0) < 1e-6;
    const bool sawFifteen = std::fabs(dmgA - 15.0) < 1e-6 || std::fabs(dmgB - 15.0) < 1e-6;
    CHECK(sawSix);
    CHECK(sawFifteen);
}
