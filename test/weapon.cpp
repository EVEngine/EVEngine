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
    void        fire(WeaponEntity&, const FireRequest&) override { ++fires; }
    void        update(WeaponEntity&, float) override {}
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

TEST_CASE("weapon.factory.newWeapon") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    const int before = countWeaponView();

    WeaponEntity* w = mod.newWeapon("cannon");
    REQUIRE(w != nullptr);
    CHECK_EQ(w->state()->magAmmo, 1);
    CHECK_EQ(w->state()->reserveAmmo, 40);
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
    CHECK_EQ(w->state()->magAmmo, 29);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("fire"));

    CHECK(!mod.fireAt(w, 100.f, 0.f, 0.f));  // 冷却中
    mod.update(0.1f);
    CHECK(!mod.fireAt(w, 100.f, 0.f, 0.f));
    mod.update(0.15f);  // 累计 0.25s > cooldown 0.2s
    CHECK(mod.fireAt(w, 100.f, 0.f, 0.f));
    CHECK_EQ(w->state()->magAmmo, 28);
}

TEST_CASE("weapon.fire.emptyAndReload") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("cannon");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->magAmmo, 0);
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(0), std::string("fire"));
    CHECK_EQ(mod.getEventType(1), std::string("empty"));

    mod.clearEvents();
    mod.startReload(w);
    CHECK(w->state()->reloading);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("reload_start"));

    mod.update(3.0f);
    CHECK(w->state()->reloading);
    mod.update(3.0f);  // 累计 6s = reloadTime
    CHECK(!w->state()->reloading);
    CHECK_EQ(w->state()->magAmmo, 1);
    CHECK_EQ(w->state()->reserveAmmo, 39);
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(1), std::string("reload_end"));
}

TEST_CASE("weapon.fire.noReserveCannotReload") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("noreload");
    REQUIRE(w != nullptr);
    for (int i = 0; i < 5; ++i) CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->magAmmo, 0);
    mod.clearEvents();
    mod.startReload(w);
    CHECK(!w->state()->reloading);
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
    CHECK_EQ(w->state()->magAmmo, 0);
    mod.update(4.0f);  // cooldown 4.0s 结束
    CHECK(w->state()->reloading);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("reload_start"));

    mod.update(2.0f);
    CHECK(w->state()->reloading);
    mod.update(4.0f);  // 累计 6s = reloadTime
    CHECK(!w->state()->reloading);
    CHECK_EQ(w->state()->magAmmo, 1);
    CHECK_EQ(w->state()->reserveAmmo, 39);
    CHECK_EQ(mod.getEventType(1), std::string("reload_end"));

    // 无备弹的武器不会被自动装填反复触发事件。
    mod.clearEvents();
    WeaponEntity* nr = mod.newWeapon("noreload");
    REQUIRE(nr != nullptr);
    for (int i = 0; i < 5; ++i) CHECK(mod.fireAt(nr, 0.f, 0.f, 0.f));
    mod.update(2.0f);
    CHECK(!nr->state()->reloading);
    CHECK_EQ(mod.getEventCount(), 0);
}

TEST_CASE("weapon.fire.burst") {
    Weapon mod;
    mod.registerWeaponsFromJson(kDefs);
    WeaponEntity* w = mod.newWeapon("smg");
    REQUIRE(w != nullptr);
    mod.clearEvents();

    CHECK(mod.fireAt(w, 0.f, 0.f, 0.f));
    CHECK_EQ(w->state()->magAmmo, 29);
    mod.update(0.07f);  // 续发 2
    mod.update(0.07f);  // 续发 3
    CHECK_EQ(w->state()->magAmmo, 27);
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
