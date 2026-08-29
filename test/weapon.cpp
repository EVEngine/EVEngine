#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponLogic.h"

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

int countWeaponView() {
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
    CHECK_EQ(svc.spawns, 4);             // 一次触发 4 粒弹丸
    CHECK_EQ(mod.getEventPellets(0), 4); // 事件记录弹丸数
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
    CHECK_EQ(mod.getSpread(w), 4.f);           // 1 + 3
    CHECK_EQ(mod.getEventSpread(0), 4.f);
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getSpread(w), 7.f);
    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(mod.getSpread(w), 10.f);          // 上限 spreadMax

    mod.update(1.0f);                          // recover 5 → 10-5=5
    CHECK_EQ(mod.getSpread(w), 5.f);
    mod.update(1.0f);
    CHECK_EQ(mod.getSpread(w), 1.f);           // 回稳到 spreadMin
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

TEST_CASE_FIXTURE(WeaponViewScriptTest, "weapon.script.viewSeesNewWeapon") {
    CHECK(vm.callFunc(vm.findFunc("testWeaponCppView"), vm).toBool());
}