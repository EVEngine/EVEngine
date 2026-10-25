#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_projectile_runtime)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include "common/Diagnostic.h"
#include "weapon/ProjectileRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// projectile_runtime.cpp
namespace test_projectile_runtime_unit {
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

    auto invalid                       = snapshot;
    invalid.slots[0].state->position.x = std::numeric_limits<double>::quiet_NaN();
    const auto before                  = restored.snapshot();
    CHECK(!restored.restore(invalid).ok());
    CHECK_EQ(restored.activeCount(), 1u);
    CHECK(restored.find(reused.value()).has_value());
    CHECK_EQ(restored.snapshot().slots.size(), before.slots.size());
}
}  // namespace test_projectile_runtime_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_spell_fragment_compiler)
#include "PrivateTestScope.h"
#include <cmath>
#include "common/Diagnostic.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/SpellFragmentCompiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// spell_fragment_compiler.cpp
#include "NumericReadCompatibility.h"
namespace test_spell_fragment_compiler_unit {
TEST_CASE("carrierRecipeCodec.decodesPresetAndRoundTripsFullForm") {
    auto parsed = eve::Value::fromJson(R"({
        "id":"spell:bolt",
        "lifetime":2.0,
        "speed":12.0,
        "damage":10.0,
        "element":"fire",
        "homing":90.0,
        "pierce":2
    })");
    REQUIRE(parsed.ok());
    auto recipe = eve::weapon::decodeCarrierRecipe(parsed.value());
    REQUIRE(recipe.ok());
    CHECK_EQ(recipe.value().id.format(), std::string("spell:bolt"));
    CHECK(recipe.value().speed == 12.0);
    CHECK(recipe.value().motionOps.size() >= 2);

    auto encoded = eve::weapon::encodeCarrierRecipe(recipe.value());
    REQUIRE(encoded.ok());
    auto again = eve::weapon::decodeCarrierRecipe(encoded.value());
    REQUIRE(again.ok());
    CHECK_EQ(again.value().id.format(), recipe.value().id.format());
    CHECK_EQ(again.value().motionOps.size(), recipe.value().motionOps.size());
    CHECK_EQ(again.value().impacts.size(), recipe.value().impacts.size());
}

TEST_CASE("carrierRecipeCodec.rejectsMalformedOptionalNumbers") {
    auto badGravity = eve::Value::fromJson(R"({
        "id":"spell:bad","lifetime":1.0,"speed":5.0,"gravity":"heavy"
    })");
    REQUIRE(badGravity.ok());
    CHECK(!eve::weapon::decodeCarrierRecipe(badGravity.value()).ok());

    auto badMotion = eve::Value::fromJson(R"({
        "id":"spell:bad2","lifetime":1.0,"speed":5.0,
        "motion":[{"kind":"homing","maxTurnRateDegrees":"fast"},"linear"],
        "triggers":["onExpire"],
        "impacts":[{"kind":"release","on":"onExpire"}]
    })");
    REQUIRE(badMotion.ok());
    CHECK(!eve::weapon::decodeCarrierRecipe(badMotion.value()).ok());
}

TEST_CASE("spellFragmentCompiler.appliesModifiersBeforeProjectile") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"fan","count":3,"spread":30},
        {"kind":"homing","turnRate":120},
        {"kind":"pierce","count":2},
        {"kind":"damage","add":5},
        {"kind":"projectile","id":"spell:bolt","speed":10,"damage":8,"lifetime":2,"element":"arcane"}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    REQUIRE(plan.ok());
    CHECK_EQ(plan.value().recipe.id.format(), std::string("spell:bolt"));
    CHECK(plan.value().recipe.speed == 10.0);
    bool foundDamage = false;
    for (const auto& impact : plan.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) {
            CHECK(std::fabs(impact.damage - 13.0) < 1e-9);
            CHECK_EQ(impact.element, std::string("arcane"));
            foundDamage = true;
        }
    }
    CHECK(foundDamage);
    REQUIRE(plan.value().volley.has_value());
    CHECK_EQ(plan.value().volley->count, 3);
    CHECK(plan.value().volley->pattern == eve::weapon::CarrierVolleyPattern::Fan);
}

TEST_CASE("spellFragmentCompiler.rejectsModifierAfterProjectile") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:bolt","speed":10,"damage":1,"lifetime":1},
        {"kind":"homing","turnRate":90}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}

TEST_CASE("spellFragmentCompiler.preservesProjectileLocalHomingPreset") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:seeker","speed":10,"damage":1,"lifetime":2,"homing":45}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    REQUIRE(plan.ok());
    bool foundHoming = false;
    for (const auto& op : plan.value().recipe.motionOps) {
        if (op.kind == eve::weapon::CarrierMotionOpKind::SteerHoming) {
            CHECK(std::fabs(op.maxTurnRateDegrees - 45.0) < 1e-9);
            foundHoming = true;
        }
    }
    CHECK(foundHoming);
}

TEST_CASE("spellFragmentCompiler.appliesDamageOpsLeftToRight") {
    auto addThenMul = eve::Value::fromJson(R"([
        {"kind":"damage","add":10},
        {"kind":"damage","multiply":2},
        {"kind":"projectile","id":"spell:a","speed":1,"damage":5,"lifetime":1}
    ])");
    REQUIRE(addThenMul.ok());
    auto planA = eve::weapon::compileSpellFragments(addThenMul.value());
    REQUIRE(planA.ok());
    // ((5+10)*2) = 30
    double damageA = 0.0;
    for (const auto& impact : planA.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) damageA = impact.damage;
    }
    CHECK(std::fabs(damageA - 30.0) < 1e-9);

    auto mulThenAdd = eve::Value::fromJson(R"([
        {"kind":"damage","multiply":2},
        {"kind":"damage","add":10},
        {"kind":"projectile","id":"spell:b","speed":1,"damage":5,"lifetime":1}
    ])");
    REQUIRE(mulThenAdd.ok());
    auto planB = eve::weapon::compileSpellFragments(mulThenAdd.value());
    REQUIRE(planB.ok());
    // ((5*2)+10) = 20
    double damageB = 0.0;
    for (const auto& impact : planB.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) damageB = impact.damage;
    }
    CHECK(std::fabs(damageB - 20.0) < 1e-9);
}

TEST_CASE("spellFragmentCompiler.rejectsMalformedProjectileNumbers") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:bad","speed":"fast","damage":1,"lifetime":1}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}

TEST_CASE("spellFragmentCompiler.rejectsFullFormProjectileFragments") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"damage","add":1},
        {"kind":"projectile","id":"spell:full","speed":1,"lifetime":1,
         "motion":["linear"],"triggers":["onExpire"],
         "impacts":[{"kind":"release","on":"onExpire"}]}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}
}  // namespace test_spell_fragment_compiler_unit
namespace test_weapon_numeric_read_compatibility {
TEST_CASE("weapon.numericRead.optionalDoubleKeepsAbsenceValuesAndDiagnostics") {
    weapon_numeric_read_compatibility::optionalDoubleKeepsAbsenceValuesAndDiagnostics(_ZEROERR_TEST_CONTEXT);
}
TEST_CASE("weapon.numericRead.optionalIntKeepsIntegralDoubleAndErrorPrecedence") {
    weapon_numeric_read_compatibility::optionalIntKeepsIntegralDoubleAndErrorPrecedence(_ZEROERR_TEST_CONTEXT);
}
}  // namespace test_weapon_numeric_read_compatibility
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weapon_infinite_reload)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// weapon_infinite_reload.cpp
namespace test_weapon_infinite_reload_unit {
TEST_CASE("weapon.infiniteReserveReloadsAcrossMultipleMagazines") {
    ecs::Table          world;
    ecs::ScopedTable    guard(world);
    eve::weapon::Weapon weapons;
    REQUIRE_EQ(weapons.registerWeaponsFromJson(
                   R"([{"id":"infinite","logic":"hitscan","cooldown":0,
                        "ammo":{"mag":1,"reserve":-1,"reload":0.1}}])"),
               1);
    auto* weapon = weapons.newWeapon("infinite");
    REQUIRE(weapon != nullptr);
    eve::weapon::AttackRequest request;
    request.hasTarget = true;
    request.targetX   = 1.0f;
    for (int magazine = 0; magazine < 4; ++magazine) {
        REQUIRE(eve::weapon::WeaponSystem::tryFire(*weapon, request));
        CHECK_EQ(weapon->state()->resource.value, 0.0f);
        eve::weapon::WeaponSystem::update(*weapon, 0.11f);
        CHECK_EQ(weapon->state()->resource.value, 1.0f);
        CHECK_EQ(weapon->state()->resource.reserve, 0);
        CHECK(!weapon->state()->resource.reloading);
    }
}
}  // namespace test_weapon_infinite_reload_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weapon)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponLogic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// weapon.cpp
namespace test_weapon_unit {
using namespace eve;


using namespace eve::weapon;

namespace {

const char* kDefs = R"JSON(
[{"id":"rifle","logic":"hitscan","damage":25,"range":500,"spread":0.5,
  "fireMode":"single","cooldown":0.2,
  "ammo":{"mag":30,"reserve":90,"reload":2.0}},
 {"id":"cannon","logic":"projectile","damage":320,"penetration":260,"range":1200,
  "fireMode":"single","cooldown":4.0,
  "ammo":{"mag":1,"reserve":40,"reload":6.0},
  "projectile":{"type":"shell","speed":900,"gravity":1.0,"aoe":3.0}},
 {"id":"smg","logic":"hitscan","fireMode":"burst","cooldown":0.05,
  "burst":{"size":3,"interval":0.06},
  "ammo":{"mag":30,"reserve":-1,"reload":1.0}},
 {"id":"noreload","logic":"hitscan","ammo":{"mag":5,"reserve":0,"reload":1.0}}]
)JSON";

// 近战 / 阶段机测试定义。
const char* kMeleeDefs = R"JSON(
[{"id":"sword","kind":"melee","logic":"melee","damage":40,"range":1.6,
  "stages":{"windup":0.25,"active":0.10,"recover":0.35},"arc":90,
  "resource":{"kind":"none"}},
 {"id":"cleaver","kind":"melee","logic":"melee","damage":15,"range":1.2,
  "stages":{"windup":0.1,"active":0.05,"recover":0.1},"arc":60,
  "resource":{"kind":"stamina","max":10,"cost":3}}]
)JSON";

// P0 功能测试定义（多弹丸 / 散布 bloom / 后坐 / 射击模式切换 / 开镜 / 伤害类型 / 弹药池）。
const char* kP0Defs = R"JSON(
[{"id":"shotgun","logic":"projectile","damage":10,"cooldown":0.2,
  "ammo":{"mag":2,"reserve":0,"reload":1},
  "projectile":{"type":"pellet","speed":500,"pelletCount":4,"pelletSpread":3}},
 {"id":"bloom","logic":"hitscan","cooldown":0,
  "spreadMin":1,"spreadMax":10,"spreadPerShot":3,"spreadRecover":5,
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"recoil","logic":"hitscan","cooldown":0,"recoilPitch":2,"recoilYaw":-1,"recoilRecover":1,
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"selector","logic":"hitscan","cooldown":0,"fireMode":"auto","fireModes":["single","burst"],
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"fixedsel","logic":"hitscan","cooldown":0,"fireMode":"single","ammo":{"mag":50,"reserve":-1}},
 {"id":"aim","logic":"hitscan","cooldown":0,"zoomFov":40,"ammo":{"mag":50,"reserve":-1}},
 {"id":"typed","logic":"hitscan","cooldown":0,"damageType":"pierce","element":"fire",
  "ammo":{"mag":50,"reserve":-1}},
 {"id":"poolgun","logic":"hitscan","cooldown":0,"ammo":{"mag":2,"reserve":0,"reload":0.5}}]
)JSON";

[[maybe_unused]] int countWeaponView() {
    int  n    = 0;
    auto view = ecs::View<WeaponEntity, WeaponEntity::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

class StubProjectileService : public IProjectileService {
public:
    int  spawns = 0;
    void spawnProjectile(const WeaponEntity&, const FireRequest&) override { ++spawns; }
};

class CountingLogic : public IWeaponLogic {
public:
    int         fires = 0;
    const char* name() const override { return "test.counting"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const AttackRequest&) override { ++fires; }
    void        update(WeaponEntity&, float) override {}
};

class HookLogic : public IWeaponLogic {
public:
    int         begins = 0, channels = 0, fires = 0, ends = 0;
    const char* name() const override { return "test.hook"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const AttackRequest&) override { ++fires; }
    void        update(WeaponEntity&, float) override {}
    void        begin(WeaponEntity&, const AttackRequest&) override { ++begins; }
    void        channel(WeaponEntity&, const AttackRequest&, float) override { ++channels; }
    void        end(WeaponEntity&, const AttackRequest&) override { ++ends; }
};

}  // namespace

TEST_CASE("weapon.defs.registerJson") {
    Weapon mod;
    CHECK_EQ(mod.registerWeaponsFromJson(kDefs), 4);
    CHECK(mod.hasWeaponDefinition("cannon"));
    CHECK_EQ(mod.getWeaponDefinitionLogic("cannon"), std::string("projectile"));
    CHECK_EQ(mod.getWeaponDefinitionDamage("cannon"), 320.f);
    CHECK_EQ(mod.getWeaponDefinitionRange("cannon"), 1200.f);
    CHECK_EQ(mod.registerWeaponsFromJson("[{\"name\":\"no-id\"}]"), 0);
    CHECK_EQ(mod.registerWeaponsFromJson("not json"), 0);
    CHECK_EQ(mod.getWeaponDefinitionLogic("missing"), std::string{});
}

TEST_CASE("weapon.kind.parse") {
    CHECK_EQ(weaponKindName(WeaponKind::Melee), std::string("melee"));
    CHECK_EQ(weaponKindName(WeaponKind::Ranged), std::string("ranged"));
    CHECK_EQ(weaponKindName(WeaponKind::Magic), std::string("magic"));
    CHECK_EQ(weaponKindName(WeaponKind::Missile), std::string("missile"));
    CHECK((weaponKindFromName("melee") == WeaponKind::Melee));
    CHECK((weaponKindFromName("ranged") == WeaponKind::Ranged));
    CHECK((weaponKindFromName("nonsense") == WeaponKind::Ranged));

    Weapon mod;
    mod.registerWeaponsFromJson(R"JSON([{"id":"sw","kind":"melee","logic":"melee"}])JSON");
    WeaponEntity* w = mod.newWeapon("sw");
    REQUIRE(w != nullptr);
    CHECK((w->definition()->def->kind == WeaponKind::Melee));
    CHECK((w->state()->resource.kind == ResourceKind::None));
    CHECK(w->state()->resource.infinite);
}

TEST_CASE("weapon.factory.newWeapon") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    const int before = countWeaponView();

    WeaponEntity* w = mod.newWeapon("cannon");
    REQUIRE(w != nullptr);
    CHECK_EQ(w->state()->resource.value, 1.f);
    CHECK_EQ(w->state()->resource.reserve, 40);
    CHECK_EQ(w->identity()->defId, std::string("cannon"));
    CHECK_EQ(mod.newWeapon("missing"), nullptr);
    CHECK_EQ(countWeaponView(), before + 1);
}

TEST_CASE("weapon.fire.cooldownAndAmmo") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("rifle");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 100.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 29.f);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("fire"));

    CHECK(!mod.fireAt(w, 100.f, 0.f, 0.f));  // 冷却中
    mod.update(0.1f);
    CHECK(!mod.fireAt(w, 100.f, 0.f, 0.f));
    mod.update(0.15f);  // 累计 0.25s > cooldown 0.2s
    CHECK(mod.fireAt(w, 100.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 28.f);
}

TEST_CASE("weapon.fire.emptyAndReload") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("cannon");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 0.f);
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(0), std::string("fire"));
    CHECK_EQ(mod.getEventType(1), std::string("empty"));

    mod.clearEvents();
    mod.startReload(w);
    CHECK(w->state()->resource.reloading);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("reload_start"));

    mod.update(3.0f);
    CHECK(w->state()->resource.reloading);
    mod.update(3.0f);  // 累计 6s = reloadTime
    CHECK(!w->state()->resource.reloading);
    CHECK_EQ(w->state()->resource.value, 1.f);
    CHECK_EQ(w->state()->resource.reserve, 39);
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(1), std::string("reload_end"));
}

TEST_CASE("weapon.fire.noReserveCannotReload") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("noreload");
    REQUIRE(w != nullptr);
    for (int i = 0; i < 5; ++i) CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 0.f);
    mod.clearEvents();
    mod.startReload(w);
    CHECK(!w->state()->resource.reloading);
    CHECK_EQ(mod.getEventCount(), 0);
}

TEST_CASE("weapon.fire.autoReloadOnEmpty") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("cannon");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    // 打空弹匣：冷却结束 + 有备弹时，update 应自动开始装填（无需游戏侧手动调用）。
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 0.f);
    mod.update(4.0f);  // cooldown 4.0s 结束 → 自动装填开始
    CHECK(w->state()->resource.reloading);
    // fireAt 已推 fire+empty 两条，自动装填再推 reload_start。
    CHECK_EQ(mod.getEventType(2), std::string("reload_start"));

    mod.update(1.0f);
    CHECK(w->state()->resource.reloading);
    mod.update(1.0f);  // 累计 4+1+1 = 6s = reloadTime
    CHECK(!w->state()->resource.reloading);
    CHECK_EQ(w->state()->resource.value, 1.f);
    CHECK_EQ(w->state()->resource.reserve, 39);
    CHECK_EQ(mod.getEventType(3), std::string("reload_end"));

    // 无备弹的武器不会被自动装填反复触发事件。
    mod.clearEvents();
    WeaponEntity* nr = mod.newWeapon("noreload");
    REQUIRE(nr != nullptr);
    for (int i = 0; i < 5; ++i) CHECK(mod.fireAt(nr, 0.f, 0.f, 0.f));
    mod.update(2.0f);
    CHECK(!nr->state()->resource.reloading);
    CHECK_EQ(mod.getEventCount(), 6);  // 5 次 fire + 1 次 empty（打空时），无 reload 事件
}

TEST_CASE("weapon.fire.burst") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("smg");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 29.f);
    mod.update(0.07f);  // 续发 2
    mod.update(0.07f);  // 续发 3
    CHECK_EQ(w->state()->resource.value, 27.f);
    CHECK_EQ(mod.getEventCount(), 3);
    for (int i = 0; i < 3; ++i) CHECK_EQ(mod.getEventType(i), std::string("fire"));
}

TEST_CASE("weapon.logic.customRegistration") {
    CountingLogic logic;
    Weapon::registerLogic(&logic);
    CHECK(Weapon::getLogicCount() >= 3);

    Weapon mod;
    CHECK_EQ(mod.registerWeaponsFromJson("[{\"id\":\"count\",\"logic\":\"test.counting\"}]"), 1);
    WeaponEntity* w = mod.newWeapon("count");
    REQUIRE(w != nullptr);
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(logic.fires, 1);
}

TEST_CASE("weapon.projectile.usesService") {
    StubProjectileService svc;
    eve::cap::provide<IProjectileService>(&svc);

    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* cannon = mod.newWeapon("cannon");
    WeaponEntity* rifle  = mod.newWeapon("rifle");
    REQUIRE(cannon != nullptr);
    REQUIRE(rifle != nullptr);

    CHECK(mod.fireAt(cannon, 0.f, 0.f, 0.f));
    CHECK_EQ(svc.spawns, 1);
    CHECK(mod.fireAt(rifle, 0.f, 0.f, 0.f));
    CHECK_EQ(svc.spawns, 1);  // 直射不生成投射物

    eve::cap::revoke<IProjectileService>(&svc);
}

// ---------------------------------------------------------------------------
// v2：近战 / 阶段机 / 资源 / 手持位
// ---------------------------------------------------------------------------

TEST_CASE("weapon.melee.stageMachine") {
    Weapon mod;
    mod.registerWeaponsFromJson(kMeleeDefs);
    WeaponEntity* w = mod.newWeapon("sword");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.attack(w, 0.f, 0.f));
    CHECK_EQ(mod.getStage(w), std::string("windup"));
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("windup_start"));
    CHECK(!mod.canFire(w));  // 阶段中不可再触发

    mod.update(0.3f);  // windup 0.25s 结束 → Active，出 fire 事件
    CHECK_EQ(mod.getStage(w), std::string("active"));
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(1), std::string("fire"));
    CHECK_EQ(mod.getEventArc(1), 90.f);

    mod.update(0.2f);  // active 0.10s 结束 → Recover
    CHECK_EQ(mod.getStage(w), std::string("recover"));
    CHECK_EQ(mod.getEventCount(), 3);
    CHECK_EQ(mod.getEventType(2), std::string("attack_end"));

    mod.update(0.4f);  // recover 0.35s 结束 → Idle
    CHECK_EQ(mod.getStage(w), std::string("idle"));
    CHECK(mod.canFire(w));
}

TEST_CASE("weapon.melee.infiniteNoCost") {
    Weapon mod;
    mod.registerWeaponsFromJson(kMeleeDefs);
    WeaponEntity* w = mod.newWeapon("sword");
    REQUIRE(w != nullptr);
    CHECK(w->state()->resource.infinite);

    CHECK(mod.attack(w, 0.f, 0.f));
    mod.update(0.3f);  // windup→active
    mod.update(0.2f);  // active→recover
    mod.update(0.4f);  // recover→idle
    CHECK(mod.attack(w, 0.f, 0.f));
    mod.update(0.3f);
    mod.update(0.2f);
    mod.update(0.4f);
    CHECK(mod.attack(w, 0.f, 0.f));  // 无消耗，第三次仍可攻击
}

TEST_CASE("weapon.melee.staminaCost") {
    Weapon mod;
    mod.registerWeaponsFromJson(kMeleeDefs);
    WeaponEntity* w = mod.newWeapon("cleaver");
    REQUIRE(w != nullptr);
    CHECK_EQ(w->state()->resource.value, 10.f);

    // cleaver 阶段：windup 0.1 / active 0.05 / recover 0.1
    CHECK(mod.attack(w, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 7.f);
    mod.update(0.15f);
    mod.update(0.1f);
    mod.update(0.15f);
    CHECK(mod.attack(w, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 4.f);
    mod.update(0.15f);
    mod.update(0.1f);
    mod.update(0.15f);
    CHECK(mod.attack(w, 0.f, 0.f));
    CHECK_EQ(w->state()->resource.value, 1.f);
    mod.update(0.15f);
    mod.update(0.1f);
    mod.update(0.15f);

    CHECK(!mod.attack(w, 0.f, 0.f));  // 体力不足（1 < cost 3）
    CHECK_EQ(w->state()->resource.value, 1.f);
    CHECK(!mod.canFire(w));
}

TEST_CASE("weapon.stage.hooks") {
    HookLogic logic;
    Weapon::registerLogic(&logic);
    Weapon mod;
    mod.registerWeaponsFromJson(R"JSON(
        [{"id":"dagger","kind":"melee","logic":"test.hook","damage":8,
          "stages":{"windup":0.2,"active":0.3,"recover":0.1}}])JSON");
    WeaponEntity* w = mod.newWeapon("dagger");
    REQUIRE(w != nullptr);

    CHECK(mod.attack(w, 0.f, 0.f));
    CHECK_EQ(logic.begins, 1);

    mod.update(0.2f);  // windup→Active：fire 一次
    CHECK_EQ(logic.fires, 1);
    mod.update(0.3f);  // Active→Recover：channel ≥1，end 一次
    CHECK(logic.channels >= 1);
    CHECK_EQ(logic.ends, 1);
}

TEST_CASE("weapon.rig.attachAndPose") {
    Weapon mod;
    mod.registerWeaponsFromJson(kMeleeDefs);
    WeaponEntity* w = mod.newWeapon("sword");
    REQUIRE(w != nullptr);

    WeaponRigEntity* rig = mod.newRig("right_hand");
    REQUIRE(rig != nullptr);
    CHECK_EQ(rig->identity()->wield, std::string("right_hand"));
    CHECK(mod.rigAttachWeapon(rig, w));
    CHECK(mod.rigGetWeapon(rig) == w);

    mod.rigSetPose(rig, 0.1f, 0.2f, 0.3f, 0.f, 0.f, 0.f);
    CHECK_EQ(rig->held()->posX, 0.1f);
    CHECK_EQ(rig->held()->posZ, 0.3f);
}

// ---------------------------------------------------------------------------
// P0：多弹丸 / 散布 bloom / 后坐 / 射击模式 / 开镜 / 伤害类型 / 弹药池
// ---------------------------------------------------------------------------

TEST_CASE("weapon.p0.multipellet") {
    StubProjectileService svc;
    eve::cap::provide<IProjectileService>(&svc);
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* w = mod.newWeapon("shotgun");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(svc.spawns, 4);              // 一次触发 4 粒弹丸
    CHECK_EQ(mod.getEventPellets(0), 4);  // 事件记录弹丸数
    CHECK_EQ(mod.getEventCount(), 1);

    eve::cap::revoke<IProjectileService>(&svc);
}

TEST_CASE("weapon.p0.spreadBloom") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* w = mod.newWeapon("bloom");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK_EQ(w->state()->currentSpread, 1.f);  // 起始 = spreadMin
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getSpread(w), 4.f);  // 1 + 3
    CHECK_EQ(mod.getEventSpread(0), 4.f);
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getSpread(w), 7.f);
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getSpread(w), 10.f);  // 上限 spreadMax

    mod.update(1.0f);  // recover 5 → 10-5=5
    CHECK_EQ(mod.getSpread(w), 5.f);
    mod.update(1.0f);
    CHECK_EQ(mod.getSpread(w), 1.f);  // 回稳到 spreadMin
}

TEST_CASE("weapon.p0.recoilModel") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* w = mod.newWeapon("recoil");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getRecoilPitch(w), 2.f);
    CHECK_EQ(mod.getRecoilYaw(w), -1.f);
    CHECK_EQ(mod.getEventRecoilPitch(0), 2.f);  // 本发后坐
    CHECK_EQ(mod.getEventRecoilYaw(0), -1.f);

    mod.update(1.0f);  // recover 1 → pitch 1, yaw 0
    CHECK_EQ(mod.getRecoilPitch(w), 1.f);
    CHECK_EQ(mod.getRecoilYaw(w), 0.f);
}

TEST_CASE("weapon.p0.fireSelector") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* sel = mod.newWeapon("selector");
    REQUIRE(sel != nullptr);
    CHECK_EQ(mod.getFireMode(sel), std::string("auto"));
    CHECK_EQ(mod.getSelectableModeCount(sel), 2);
    CHECK_EQ(mod.getSelectableMode(sel, 0), std::string("single"));
    CHECK(mod.setFireMode(sel, "burst"));
    CHECK_EQ(mod.getFireMode(sel), std::string("burst"));
    CHECK(!mod.setFireMode(sel, "auto"));  // 不在可选列表

    WeaponEntity* fx = mod.newWeapon("fixedsel");
    REQUIRE(fx != nullptr);
    CHECK(!mod.setFireMode(fx, "burst"));  // 固定 single
    CHECK_EQ(mod.getFireMode(fx), std::string("single"));
}

TEST_CASE("weapon.p0.aiming") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* w = mod.newWeapon("aim");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK_EQ(mod.getZoomFov(w), 40.f);
    CHECK(!mod.isAiming(w));
    CHECK(mod.setAiming(w, true));
    CHECK(mod.isAiming(w));
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("aim_in"));
    CHECK(mod.setAiming(w, false));
    CHECK(!mod.isAiming(w));
    CHECK_EQ(mod.getEventType(1), std::string("aim_out"));
    CHECK(!mod.setAiming(w, false));  // 已是 false，无变化
}

TEST_CASE("weapon.p0.damageTypeAndElement") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    WeaponEntity* w = mod.newWeapon("typed");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getEventDamageType(0), std::string("pierce"));
    CHECK_EQ(mod.getEventElement(0), std::string("fire"));
}

TEST_CASE("weapon.p0.sharedAmmoPool") {
    Weapon mod;
    mod.registerWeaponsFromJson(kP0Defs);
    AmmoPoolEntity* pool = mod.newAmmoPool("pistol_ammo", "pistol", 100);
    REQUIRE(pool != nullptr);
    mod.ammoPoolAdd(pool, 20);
    CHECK_EQ(mod.ammoPoolGetCount(pool), 20);

    WeaponEntity* a = mod.newWeapon("poolgun");
    REQUIRE(a != nullptr);
    CHECK(mod.bindAmmoPool(a, pool));
    CHECK(mod.getAmmoPool(a) == pool);
    mod.clearEvents();

    CHECK(mod.fireAt(a, 0.f, 0.f, 0.f));
    CHECK(mod.fireAt(a, 0.f, 0.f, 0.f));
    CHECK_EQ(a->state()->resource.value, 0.f);
    mod.update(0.5f);  // 自动装填，从池取 2 发
    CHECK_EQ(a->state()->resource.value, 2.f);
    CHECK_EQ(mod.ammoPoolGetCount(pool), 18);
}

TEST_CASE("weapon.mount.limitsAndAim") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponMountEntity* m = mod.newMount("turret");
    REQUIRE(m != nullptr);
    mod.mountSetLimits(m, -90.f, 90.f, -10.f, 20.f, 45.f, 0.f);

    WeaponEntity* w = mod.newWeapon("cannon");
    CHECK(mod.mountAttachWeapon(m, w));
    mod.mountAimAt(m, 200.f, 50.f);
    CHECK_EQ(w->aim()->desiredYaw, 90.f);  // 夹到限位
    CHECK_EQ(w->aim()->desiredPitch, 20.f);

    mod.update(1.0f);  // turnSpeed 45°/s
    CHECK_EQ(w->aim()->yaw, 45.f);
    mod.update(1.0f);
    CHECK_EQ(w->aim()->yaw, 90.f);
    CHECK_EQ(m->state()->yaw, 90.f);  // 挂点跟随武器

    mod.mountDestroy(m);
    mod.mountAimAt(m, 0.f, 0.f);
    CHECK_EQ(w->aim()->desiredYaw, 90.f);  // 已毁挂点不再瞄准
}

TEST_CASE("weapon.mount.aimsWithoutWeapon") {
    Weapon             mod;
    WeaponMountEntity* m = mod.newMount("pintle");
    REQUIRE(m != nullptr);
    mod.mountSetLimits(m, -45.f, 45.f, -5.f, 5.f, 0.f, 0.f);
    mod.mountAimAt(m, 100.f, 20.f);
    CHECK_EQ(m->state()->yaw, 45.f);
    CHECK_EQ(m->state()->pitch, 5.f);
}

static const char* kViewScript = R"SQ(
function testWeaponCppView() {
    local weapon = eve.Weapon()
    weapon.registerWeaponsFromJson("[{\"id\":\"pistol\",\"ammo\":{\"mag\":12}}]")
    local w = weapon.newWeapon("pistol")
    if (w == null) return false
    local all = eve.view(eve.WeaponEntity)
    foreach (e in all) {
        if (e.getId() == w.getId()) return true
    }
    return false
}
)SQ";

UnitSciptTest(WeaponViewScriptTest, kViewScript);
}  // namespace test_weapon_unit
#endif

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_advanced_carrier_contracts {
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

TEST_CASE("advancedCarrier.motionReplayAndValidation") {
    SUB_CASE("advancedCarrier.rotationDragAndSignedAcceleration") {
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
    };
    SUB_CASE("advancedCarrier.antiparallelHomingAndOrbitProviderContract") {
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
    };
    SUB_CASE("advancedCarrier.seededWanderSnapshotReplayAndCodec") {
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
    };
    SUB_CASE("advancedCarrier.distanceSpeedApexAndLifetimeTriggers") {
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
    };
    SUB_CASE("advancedCarrier.bounceContactAndInvalidSnapshotRollback") {
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
    };
    SUB_CASE("advancedCarrier.predictiveLeadAndStoppedSpeedRecovery") {
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
    };
    SUB_CASE("advancedCarrier.intervalBudgetRecipeMutationAndExpiryBoundaries") {
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
    };
    SUB_CASE("advancedCarrier.curvesRemainBoundedAcrossManyPeriods") {
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
    };
    SUB_CASE("advancedCarrier.roughBounceReplayAndSnapshotVersionValidation") {
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
    };
    SUB_CASE("advancedCarrier.invalidOperatorAndTriggerCompositionIsRejected") {
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
    };
    SUB_CASE("advancedCarrier.triggerChronologyAndBouncePathRespectContact") {
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
    };
    SUB_CASE("advancedCarrier.customMotionAtomicityReplayAndReentrancy") {
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
    };
    SUB_CASE("advancedCarrier.customMotionCollisionAndClampedLifetime") {
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
    };
}
}  // namespace test_advanced_carrier_contracts

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include "common/Diagnostic.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/CombatCarrier.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_continuous_solver_contracts {
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

TEST_CASE("carrierSolver.continuousCollisionContract") {
    SUB_CASE("carrierSolver.remainderMultipleBouncesAndFramePartition") {
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
    };
    SUB_CASE("carrierSolver.intervalDistanceAndExpirySurviveBounce") {
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
    };
    SUB_CASE("carrierSolver.budgetsAndLaterProviderFailureRollbackWholeFrame") {
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
    };
    SUB_CASE("carrierSolver.substepBoundsSnapshotAndCallbackRemainder") {
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
    };
    SUB_CASE("carrierSolver.zeroProgressAndMalformedContactsAreBounded") {
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
    };
    SUB_CASE("carrierSolver.thresholdTimesAndDeferredChildren") {
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
    };
    SUB_CASE("carrierSolver.predictiveSamplesOncePerOuterUpdate") {
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
    };
    SUB_CASE("carrierSolver.pierceDeduplicatesAcrossSubstepsAndRestore") {
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
        bad.slots[h.value().slot].state->piercedTargets.push_back(
            bad.slots[h.value().slot].state->piercedTargets.front());
        REQUIRE(!runtime.restore(bad).ok());
    };
    SUB_CASE("carrierSolver.impactSpeedCrossingAndLaterCallbackFailure") {
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
    };
    SUB_CASE("carrierSolver.optionalCollisionAndWorldSweep") {
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
    };
}
}  // namespace test_continuous_solver_contracts
