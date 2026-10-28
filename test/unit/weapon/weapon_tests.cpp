// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "projectile_runtime.cpp",
    "spell_fragment_compiler.cpp",
    "weapon_infinite_reload.cpp",
    "weapon.cpp",
    "advanced_carrier.cpp",
    "continuous_solver.cpp"
  ]
}
*/
// clang-format on

#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/ECS.h"
#include "weapon/CarrierRecipeCodec.h"
#include "weapon/ProjectileRuntime.h"
#include "weapon/SpellFragmentCompiler.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponLogic.h"
#include "weapon/WeaponSystem.h"
#include "weapon/detail/NumericRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_projectile_runtime)
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

TEST_CASE("weapon.projectile.spec.motionAndFailureAtomicity") {
    SUB_CASE("half-second step moves linear five units and ballistic two-and-a-half units") {
        eve::weapon::ProjectileRuntime runtime;
        REQUIRE(runtime.configurePool(4).ok());
        auto linear    = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        auto ballistic = runtime.spawn(definition(eve::weapon::ProjectileMode::Ballistic), {{}, {0., 1., 0.}, {}});
        REQUIRE(linear.ok());
        REQUIRE(ballistic.ok());
        REQUIRE(runtime.update(eve::Duration::fromNanoseconds(500000000)).ok());
        auto moved   = runtime.find(linear.value());
        auto falling = runtime.find(ballistic.value());
        REQUIRE(moved.has_value());
        REQUIRE(falling.has_value());
        REQUIRE(std::fabs(moved->position.x - 5.) < 1e-9);
        REQUIRE(std::fabs(falling->position.y - 2.5) < 1e-9);
    };
    SUB_CASE("homing turns toward injected target but keeps speed") {
        eve::weapon::ProjectileRuntime runtime;
        ecs::EntityHandle              target;
        target.id         = 7;
        target.generation = 1;
        auto shot         = runtime.spawn(definition(eve::weapon::ProjectileMode::Homing), {{}, {1., 0., 0.}, target});
        REQUIRE(shot.ok());
        TargetProvider provider;
        provider.positions[target.id] = {0., 0., 10.};
        REQUIRE(runtime.update(eve::Duration::fromNanoseconds(500000000), &provider).ok());
        auto state = runtime.find(shot.value());
        REQUIRE(state.has_value());
        REQUIRE(state->velocity.x > 0.);
        REQUIRE(state->velocity.z > 0.);
        const double speed = std::hypot(state->velocity.x, state->velocity.z);
        REQUIRE(std::fabs(speed - 10.) < 1e-9);
    };
    SUB_CASE("provider failure aborts the whole update, including unrelated projectiles") {
        eve::weapon::ProjectileRuntime runtime;
        REQUIRE(runtime.configurePool(4).ok());
        ecs::EntityHandle target;
        target.id         = 42;
        target.generation = 7;
        auto linear       = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        auto homing       = runtime.spawn(definition(eve::weapon::ProjectileMode::Homing), {{}, {1., 0., 0.}, target});
        REQUIRE(linear.ok());
        REQUIRE(homing.ok());
        TargetProvider provider;
        provider.reject = true;
        REQUIRE(!runtime.update(eve::Duration::fromNanoseconds(500000000), &provider).ok());
        auto a = runtime.find(linear.value());
        auto b = runtime.find(homing.value());
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(a->position.x == 0.);
        REQUIRE(b->position.x == 0.);
    };
}

TEST_CASE("weapon.projectile.spec.poolLifetimeAndSnapshot") {
    SUB_CASE("exhausted pool rejects spawn; reused slot invalidates old generation") {
        eve::weapon::ProjectileRuntime runtime;
        REQUIRE(runtime.configurePool(1).ok());
        auto first = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        REQUIRE(first.ok());
        REQUIRE(!runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}}).ok());
        REQUIRE(runtime.release(first.value()).ok());
        auto second = runtime.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        REQUIRE(second.ok());
        REQUIRE(second.value().slot == first.value().slot);
        REQUIRE(second.value().generation != first.value().generation);
        REQUIRE(!runtime.release(first.value()).ok());
    };
    SUB_CASE("expiry reports release and removes the projectile") {
        eve::weapon::ProjectileRuntime runtime;
        auto                           shortLived = definition(eve::weapon::ProjectileMode::Linear);
        shortLived.lifetime                       = eve::Duration::fromNanoseconds(100);
        auto shot                                 = runtime.spawn(shortLived, {{}, {1., 0., 0.}, {}});
        REQUIRE(shot.ok());
        auto step = runtime.update(eve::Duration::fromNanoseconds(100));
        REQUIRE(step.ok());
        REQUIRE(step.value().released.size() == 1);
        REQUIRE(runtime.activeCount() == 0);
        REQUIRE(!runtime.find(shot.value()).has_value());
    };
    SUB_CASE("restore preserves trajectory and generation; bad coordinate cannot partially restore") {
        eve::weapon::ProjectileRuntime source;
        REQUIRE(source.configurePool(2).ok());
        auto shot = source.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        REQUIRE(shot.ok());
        REQUIRE(source.update(eve::Duration::fromNanoseconds(250000000)).ok());
        const auto                     image = source.snapshot();
        eve::weapon::ProjectileRuntime restored;
        REQUIRE(restored.restore(image).ok());
        REQUIRE(restored.capacity() == 2);
        auto state = restored.find(shot.value());
        REQUIRE(state.has_value());
        REQUIRE(std::fabs(state->position.x - 2.5) < 1e-9);
        REQUIRE(restored.release(shot.value()).ok());
        auto reused = restored.spawn(definition(eve::weapon::ProjectileMode::Linear), {{}, {1., 0., 0.}, {}});
        REQUIRE(reused.ok());
        REQUIRE(reused.value().slot == shot.value().slot);
        REQUIRE(reused.value().generation != shot.value().generation);
        auto invalid                       = image;
        invalid.slots[0].state->position.x = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(!restored.restore(invalid).ok());
        REQUIRE(restored.find(reused.value()).has_value());
    };
}
}  // namespace test_projectile_runtime_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_spell_fragment_compiler)
// spell_fragment_compiler.cpp
namespace test_spell_fragment_compiler_unit {
TEST_CASE("weapon.spell.spec.recipeCodec") {
    SUB_CASE("preset decodes and canonical full form preserves recipe identity and operation counts") {
        auto json = eve::Value::fromJson(R"({"id":"spell:bolt","lifetime":2,"speed":12,"damage":10,
            "element":"fire","homing":90,"pierce":2})");
        REQUIRE(json.ok());
        auto recipe = eve::weapon::decodeCarrierRecipe(json.value());
        REQUIRE(recipe.ok());
        REQUIRE(recipe.value().id.format() == "spell:bolt");
        REQUIRE(recipe.value().speed == 12.);
        REQUIRE(recipe.value().motionOps.size() >= 2);
        auto encoded = eve::weapon::encodeCarrierRecipe(recipe.value());
        REQUIRE(encoded.ok());
        auto again = eve::weapon::decodeCarrierRecipe(encoded.value());
        REQUIRE(again.ok());
        REQUIRE(again.value().id == recipe.value().id);
        REQUIRE(again.value().motionOps.size() == recipe.value().motionOps.size());
        REQUIRE(again.value().impacts.size() == recipe.value().impacts.size());
    };
    SUB_CASE("optional numeric fields reject wrong types instead of silently using defaults") {
        for (const auto* input : {R"({"id":"spell:a","lifetime":1,"speed":5,"gravity":"heavy"})",
                                  R"({"id":"spell:b","lifetime":1,"speed":5,
                                       "motion":[{"kind":"homing","maxTurnRateDegrees":"fast"},"linear"],
                                       "triggers":["onExpire"],"impacts":[{"kind":"release","on":"onExpire"}]})"}) {
            auto json = eve::Value::fromJson(input);
            REQUIRE(json.ok());
            REQUIRE(!eve::weapon::decodeCarrierRecipe(json.value()).ok());
        }
    };
}

TEST_CASE("weapon.spell.spec.fragmentCompilation") {
    SUB_CASE("modifiers before projectile compose volley, homing, pierce, and damage") {
        auto json = eve::Value::fromJson(R"([
            {"kind":"fan","count":3,"spread":30},{"kind":"homing","turnRate":120},
            {"kind":"pierce","count":2},{"kind":"damage","add":5},
            {"kind":"projectile","id":"spell:bolt","speed":10,"damage":8,"lifetime":2,"element":"arcane"}])");
        REQUIRE(json.ok());
        auto plan = eve::weapon::compileSpellFragments(json.value());
        REQUIRE(plan.ok());
        REQUIRE(plan.value().recipe.id.format() == "spell:bolt");
        REQUIRE(plan.value().volley.has_value());
        REQUIRE(plan.value().volley->count == 3);
        REQUIRE(plan.value().volley->pattern == eve::weapon::CarrierVolleyPattern::Fan);
        bool hit = false;
        for (const auto& impact : plan.value().recipe.impacts)
            if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) {
                REQUIRE(impact.damage == 13.);
                REQUIRE(impact.element == "arcane");
                hit = true;
            }
        REQUIRE(hit);
    };
    SUB_CASE("damage operations apply left to right") {
        for (const auto& [input, expected] : {std::pair{R"([{"kind":"damage","add":10},{"kind":"damage","multiply":2},
                              {"kind":"projectile","id":"spell:order","speed":1,"damage":5,"lifetime":1}])",
                                                        30.},
                                              std::pair{R"([{"kind":"damage","multiply":2},{"kind":"damage","add":10},
                              {"kind":"projectile","id":"spell:order","speed":1,"damage":5,"lifetime":1}])",
                                                        20.}}) {
            auto json = eve::Value::fromJson(input);
            REQUIRE(json.ok());
            auto plan = eve::weapon::compileSpellFragments(json.value());
            REQUIRE(plan.ok());
            bool found = false;
            for (const auto& impact : plan.value().recipe.impacts)
                if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) {
                    REQUIRE(impact.damage == expected);
                    found = true;
                }
            REQUIRE(found);
        }
    };
    SUB_CASE("projectile-local homing survives compilation") {
        auto json = eve::Value::fromJson(R"([{"kind":"projectile","id":"spell:seeker",
            "speed":10,"damage":1,"lifetime":2,"homing":45}])");
        REQUIRE(json.ok());
        auto plan = eve::weapon::compileSpellFragments(json.value());
        REQUIRE(plan.ok());
        bool found = false;
        for (const auto& op : plan.value().recipe.motionOps)
            if (op.kind == eve::weapon::CarrierMotionOpKind::SteerHoming) {
                REQUIRE(op.maxTurnRateDegrees == 45.);
                found = true;
            }
        REQUIRE(found);
    };
    SUB_CASE("modifier after projectile, malformed number, and full-form fragment fail") {
        for (const auto* input : {R"([{"kind":"projectile","id":"spell:a","speed":10,"damage":1,"lifetime":1},
                                     {"kind":"homing","turnRate":90}])",
                                  R"([{"kind":"projectile","id":"spell:b","speed":"fast","damage":1,"lifetime":1}])",
                                  R"([{"kind":"projectile","id":"spell:c","speed":1,"lifetime":1,
                                     "motion":["linear"],"triggers":["onExpire"],
                                     "impacts":[{"kind":"release","on":"onExpire"}]}])"}) {
            auto json = eve::Value::fromJson(input);
            REQUIRE(json.ok());
            REQUIRE(!eve::weapon::compileSpellFragments(json.value()).ok());
        }
    };
}

TEST_CASE("weapon.numeric.spec.optionalReads") {
    for (const auto* source : {"weapon.carrier.recipe", "weapon.spell.fragments"}) {
        auto missing = eve::weapon::detail::readOptionalDouble({}, "speed", "recipe.speed", source);
        REQUIRE(missing.ok());
        REQUIRE(!missing.value().has_value());
        auto number = eve::weapon::detail::readOptionalDouble({{"speed", eve::Value::number(1.5)}}, "speed",
                                                              "recipe.speed", source);
        REQUIRE(number.ok());
        REQUIRE(number.value().value() == 1.5);
        auto bad = eve::weapon::detail::readOptionalDouble({{"speed", eve::Value::string("fast")}}, "speed",
                                                           "recipe.speed", source);
        REQUIRE(!bad.ok());
        REQUIRE(bad.status().diagnostics().front().path() == "recipe.speed");
        auto integer =
            eve::weapon::detail::readOptionalInt({{"count", eve::Value::number(42.)}}, "count", "recipe.count", source);
        REQUIRE(integer.ok());
        REQUIRE(integer.value().value() == 42);
        REQUIRE(!eve::weapon::detail::readOptionalInt({{"count", eve::Value::number(42.5)}}, "count", "recipe.count",
                                                      source)
                     .ok());
    }
}
}  // namespace test_spell_fragment_compiler_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weapon_infinite_reload)
// weapon_infinite_reload.cpp
namespace test_weapon_infinite_reload_unit {
TEST_CASE("weapon.infiniteReserve.spec.repeatedReload") {
    ecs::Table          table;
    ecs::ScopedTable    scope(table);
    eve::weapon::Weapon weapons;
    REQUIRE(weapons.registerWeaponsFromJson(R"([{"id":"infinite","logic":"hitscan","cooldown":0,
        "ammo":{"mag":1,"reserve":-1,"reload":0.1}}])") == 1);
    auto* gun = weapons.newWeapon("infinite");
    REQUIRE(gun != nullptr);
    eve::weapon::AttackRequest attack;
    attack.hasTarget = true;
    attack.targetX   = 1.f;
    for (int cycle = 0; cycle < 4; ++cycle) {
        REQUIRE(eve::weapon::WeaponSystem::tryFire(*gun, attack));
        REQUIRE(gun->state()->resource.value == 0.f);
        eve::weapon::WeaponSystem::update(*gun, .11f);
        REQUIRE(gun->state()->resource.value == 1.f);
        REQUIRE(gun->state()->resource.reserve == 0);
        REQUIRE(!gun->state()->resource.reloading);
    }
}
}  // namespace test_weapon_infinite_reload_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_weapon)
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

TEST_CASE("weapon.spec.definitionFactoryAndModes") {
    SUB_CASE("valid definitions expose damage, range, and logic; malformed input publishes none") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        REQUIRE(weapons.hasWeaponDefinition("cannon"));
        REQUIRE(weapons.getWeaponDefinitionLogic("cannon") == "projectile");
        REQUIRE(weapons.getWeaponDefinitionDamage("cannon") == 320.f);
        REQUIRE(weapons.getWeaponDefinitionRange("cannon") == 1200.f);
        REQUIRE(weapons.registerWeaponsFromJson("not json") == 0);
        REQUIRE(weapons.registerWeaponsFromJson("[{\"name\":\"missing-id\"}]") == 0);
        REQUIRE(weapons.getWeaponDefinitionLogic("missing").empty());
    };
    SUB_CASE("new weapon initializes ammo and identity; unknown id allocates nothing") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        const int before = countWeaponView();
        auto*     cannon = weapons.newWeapon("cannon");
        REQUIRE(cannon != nullptr);
        REQUIRE(cannon->state()->resource.value == 1.f);
        REQUIRE(cannon->state()->resource.reserve == 40);
        REQUIRE(cannon->identity()->defId == "cannon");
        REQUIRE(weapons.newWeapon("missing") == nullptr);
        REQUIRE(countWeaponView() == before + 1);
    };
    SUB_CASE("kind names round trip, unknown name uses ranged, and melee has no ammo cost") {
        for (const auto& [kind, name] :
             {std::pair{WeaponKind::Melee, "melee"}, std::pair{WeaponKind::Ranged, "ranged"},
              std::pair{WeaponKind::Magic, "magic"}, std::pair{WeaponKind::Missile, "missile"}}) {
            REQUIRE(std::string_view(weaponKindName(kind)) == std::string_view(name));
            REQUIRE(weaponKindFromName(name) == kind);
        }
        REQUIRE(weaponKindFromName("nonsense") == WeaponKind::Ranged);
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(R"([{"id":"blade","kind":"melee","logic":"melee"}])") == 1);
        auto* blade = weapons.newWeapon("blade");
        REQUIRE(blade != nullptr);
        REQUIRE(blade->definition()->def->kind == WeaponKind::Melee);
        REQUIRE(blade->state()->resource.kind == ResourceKind::None);
        REQUIRE(blade->state()->resource.infinite);
    };
}

TEST_CASE("weapon.spec.rangedFireReloadAndBurst") {
    SUB_CASE("single fire spends one round, emits fire, and cooldown blocks until elapsed") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        auto* rifle = weapons.newWeapon("rifle");
        REQUIRE(rifle != nullptr);
        weapons.clearEvents();
        REQUIRE(weapons.fireAt(rifle, 100.f, 0.f, 0.f));
        REQUIRE(rifle->state()->resource.value == 29.f);
        REQUIRE(weapons.getEventCount() == 1);
        REQUIRE(weapons.getEventType(0) == "fire");
        REQUIRE(!weapons.fireAt(rifle, 100.f, 0.f, 0.f));
        weapons.update(.1f);
        REQUIRE(!weapons.fireAt(rifle, 100.f, 0.f, 0.f));
        weapons.update(.15f);
        REQUIRE(weapons.fireAt(rifle, 100.f, 0.f, 0.f));
        REQUIRE(rifle->state()->resource.value == 28.f);
    };
    SUB_CASE("empty cannon starts manual reload, completes at duration, and spends reserve") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        auto* cannon = weapons.newWeapon("cannon");
        REQUIRE(cannon != nullptr);
        weapons.clearEvents();
        REQUIRE(weapons.fireAt(cannon, 0.f, 0.f, 0.f));
        REQUIRE(cannon->state()->resource.value == 0.f);
        REQUIRE(weapons.getEventType(0) == "fire");
        REQUIRE(weapons.getEventType(1) == "empty");
        weapons.clearEvents();
        weapons.startReload(cannon);
        REQUIRE(cannon->state()->resource.reloading);
        REQUIRE(weapons.getEventType(0) == "reload_start");
        weapons.update(3.f);
        REQUIRE(cannon->state()->resource.reloading);
        weapons.update(3.f);
        REQUIRE(!cannon->state()->resource.reloading);
        REQUIRE(cannon->state()->resource.value == 1.f);
        REQUIRE(cannon->state()->resource.reserve == 39);
        REQUIRE(weapons.getEventType(1) == "reload_end");
    };
    SUB_CASE("automatic reload occurs only with reserve; burst fires three rounds") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        auto* cannon       = weapons.newWeapon("cannon");
        auto* emptyReserve = weapons.newWeapon("noreload");
        auto* smg          = weapons.newWeapon("smg");
        REQUIRE(cannon != nullptr);
        REQUIRE(emptyReserve != nullptr);
        REQUIRE(smg != nullptr);
        REQUIRE(weapons.fireAt(cannon, 0.f, 0.f, 0.f));
        weapons.update(4.f);
        REQUIRE(cannon->state()->resource.reloading);
        for (int i = 0; i < 5; ++i) REQUIRE(weapons.fireAt(emptyReserve, 0.f, 0.f, 0.f));
        weapons.startReload(emptyReserve);
        REQUIRE(!emptyReserve->state()->resource.reloading);
        weapons.clearEvents();
        REQUIRE(weapons.fireAt(smg, 0.f, 0.f, 0.f));
        weapons.update(.07f);
        weapons.update(.07f);
        REQUIRE(smg->state()->resource.value == 27.f);
        REQUIRE(weapons.getEventCount() >= 3);
    };
    SUB_CASE("infinite reserve replenishes multiple magazines without decrementing reserve") {
        ecs::Table       table;
        ecs::ScopedTable scope(table);
        Weapon           weapons;
        REQUIRE(weapons.registerWeaponsFromJson(R"([{"id":"infinite","logic":"hitscan","cooldown":0,
            "ammo":{"mag":1,"reserve":-1,"reload":0.1}}])") == 1);
        auto* gun = weapons.newWeapon("infinite");
        REQUIRE(gun != nullptr);
        AttackRequest request;
        request.hasTarget = true;
        request.targetX   = 1.f;
        for (int magazine = 0; magazine < 3; ++magazine) {
            REQUIRE(WeaponSystem::tryFire(*gun, request));
            REQUIRE(gun->state()->resource.value == 0.f);
            WeaponSystem::update(*gun, .11f);
            REQUIRE(gun->state()->resource.value == 1.f);
            REQUIRE(gun->state()->resource.reserve == 0);
        }
    };
}

TEST_CASE("weapon.spec.meleeStagesAndResource") {
    SUB_CASE("sword passes windup, active, recover, idle with one fire event") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kMeleeDefs) == 2);
        auto* sword = weapons.newWeapon("sword");
        REQUIRE(sword != nullptr);
        weapons.clearEvents();
        REQUIRE(weapons.attack(sword, 0.f, 0.f));
        REQUIRE(weapons.getStage(sword) == "windup");
        REQUIRE(weapons.getEventType(0) == "windup_start");
        REQUIRE(!weapons.canFire(sword));
        weapons.update(.3f);
        REQUIRE(weapons.getStage(sword) == "active");
        REQUIRE(weapons.getEventType(1) == "fire");
        REQUIRE(weapons.getEventArc(1) == 90.f);
        weapons.update(.2f);
        REQUIRE(weapons.getStage(sword) == "recover");
        REQUIRE(weapons.getEventCount() == 3);
        REQUIRE(weapons.getEventType(2) == "attack_end");
        weapons.update(.4f);
        REQUIRE(weapons.getStage(sword) == "idle");
        REQUIRE(weapons.canFire(sword));
        REQUIRE(sword->state()->resource.infinite);
    };
    SUB_CASE("stamina cost limits cleaver to three attacks from ten units") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kMeleeDefs) == 2);
        auto* cleaver = weapons.newWeapon("cleaver");
        REQUIRE(cleaver != nullptr);
        for (float expected : {7.f, 4.f, 1.f}) {
            REQUIRE(weapons.attack(cleaver, 0.f, 0.f));
            REQUIRE(cleaver->state()->resource.value == expected);
            weapons.update(.15f);
            weapons.update(.1f);
            weapons.update(.15f);
        }
        REQUIRE(!weapons.attack(cleaver, 0.f, 0.f));
        REQUIRE(cleaver->state()->resource.value == 1.f);
    };
    SUB_CASE("custom logic callbacks fire at stage boundaries") {
        HookLogic hook;
        Weapon::registerLogic(&hook);
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(R"([{"id":"dagger","kind":"melee","logic":"test.hook",
          "damage":8,"stages":{"windup":0.2,"active":0.3,"recover":0.1}}])") == 1);
        auto* dagger = weapons.newWeapon("dagger");
        REQUIRE(dagger != nullptr);
        REQUIRE(weapons.attack(dagger, 0.f, 0.f));
        REQUIRE(hook.begins == 1);
        weapons.update(.2f);
        REQUIRE(hook.fires == 1);
        weapons.update(.3f);
        REQUIRE(hook.channels >= 1);
        REQUIRE(hook.ends == 1);
    };
}

TEST_CASE("weapon.spec.servicesPoseAndAdvancedState") {
    SUB_CASE("projectile logic uses service; hitscan does not") {
        StubProjectileService service;
        eve::cap::provide<IProjectileService>(&service);
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        auto* cannon = weapons.newWeapon("cannon");
        auto* rifle  = weapons.newWeapon("rifle");
        REQUIRE(cannon != nullptr);
        REQUIRE(rifle != nullptr);
        REQUIRE(weapons.fireAt(cannon, 0.f, 0.f, 0.f));
        REQUIRE(service.spawns == 1);
        REQUIRE(weapons.fireAt(rifle, 0.f, 0.f, 0.f));
        REQUIRE(service.spawns == 1);
        eve::cap::revoke<IProjectileService>(&service);
    };
    SUB_CASE("rig holds a weapon and keeps authored pose") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kMeleeDefs) == 2);
        auto* sword = weapons.newWeapon("sword");
        auto* rig   = weapons.newRig("right_hand");
        REQUIRE(sword != nullptr);
        REQUIRE(rig != nullptr);
        REQUIRE(weapons.rigAttachWeapon(rig, sword));
        REQUIRE(weapons.rigGetWeapon(rig) == sword);
        weapons.rigSetPose(rig, .1f, .2f, .3f, 0.f, 0.f, 0.f);
        REQUIRE(rig->held()->posX == .1f);
        REQUIRE(rig->held()->posZ == .3f);
    };
    SUB_CASE("pellet count, typed hit, bloom, recoil, and aiming are observable") {
        StubProjectileService service;
        eve::cap::provide<IProjectileService>(&service);
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kP0Defs) > 0);
        auto* shotgun = weapons.newWeapon("shotgun");
        auto* typed   = weapons.newWeapon("typed");
        auto* bloom   = weapons.newWeapon("bloom");
        auto* recoil  = weapons.newWeapon("recoil");
        auto* aim     = weapons.newWeapon("aim");
        REQUIRE(shotgun != nullptr);
        REQUIRE(typed != nullptr);
        REQUIRE(bloom != nullptr);
        REQUIRE(recoil != nullptr);
        REQUIRE(aim != nullptr);
        weapons.clearEvents();
        REQUIRE(weapons.fireAt(shotgun, 0.f, 0.f, 0.f));
        REQUIRE(service.spawns == 4);
        REQUIRE(weapons.getEventPellets(0) == 4);
        weapons.clearEvents();
        REQUIRE(weapons.fireAt(typed, 0.f, 0.f, 0.f));
        REQUIRE(weapons.getEventDamageType(0) == "pierce");
        REQUIRE(weapons.getEventElement(0) == "fire");
        REQUIRE(weapons.fireAt(bloom, 0.f, 0.f, 0.f));
        REQUIRE(weapons.getSpread(bloom) == 4.f);
        REQUIRE(weapons.fireAt(recoil, 0.f, 0.f, 0.f));
        REQUIRE(weapons.getRecoilPitch(recoil) == 2.f);
        REQUIRE(weapons.getRecoilYaw(recoil) == -1.f);
        REQUIRE(weapons.getZoomFov(aim) == 40.f);
        REQUIRE(weapons.setAiming(aim, true));
        REQUIRE(weapons.isAiming(aim));
        REQUIRE(!weapons.setAiming(aim, true));
        eve::cap::revoke<IProjectileService>(&service);
    };
    SUB_CASE("fire selector rejects unavailable modes and mount clamps aim to limits") {
        Weapon weapons;
        REQUIRE(weapons.registerWeaponsFromJson(kDefs) == 4);
        REQUIRE(weapons.registerWeaponsFromJson(kP0Defs) > 0);
        auto* selector = weapons.newWeapon("selector");
        REQUIRE(selector != nullptr);
        REQUIRE(weapons.getFireMode(selector) == "auto");
        REQUIRE(weapons.setFireMode(selector, "burst"));
        REQUIRE(weapons.getFireMode(selector) == "burst");
        REQUIRE(!weapons.setFireMode(selector, "auto"));
        auto* mount  = weapons.newMount("turret");
        auto* cannon = weapons.newWeapon("cannon");
        REQUIRE(mount != nullptr);
        REQUIRE(cannon != nullptr);
        weapons.mountSetLimits(mount, -90.f, 90.f, -10.f, 20.f, 45.f, 0.f);
        REQUIRE(weapons.mountAttachWeapon(mount, cannon));
        weapons.mountAimAt(mount, 200.f, 50.f);
        REQUIRE(cannon->aim()->desiredYaw == 90.f);
        REQUIRE(cannon->aim()->desiredPitch == 20.f);
        weapons.update(1.f);
        REQUIRE(cannon->aim()->yaw == 45.f);
    };
}

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
