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


// ---------------------------------------------------------------------------
// v2：近战 / 阶段机 / 资源 / 手持位
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// P0：多弹丸 / 散布 bloom / 后坐 / 射击模式 / 开镜 / 伤害类型 / 弹药池
// ---------------------------------------------------------------------------


static const char* kViewScript = R"SQ(
[[maybe_unused]] function testWeaponCppView() {
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
