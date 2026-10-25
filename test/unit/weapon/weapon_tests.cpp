// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "projectile_runtime.cpp",
    "spell_fragment_compiler.cpp",
    "weapon_infinite_reload.cpp",
    "weapon.cpp"
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
