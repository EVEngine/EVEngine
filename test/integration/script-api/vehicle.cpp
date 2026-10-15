#include "vehicle/Vehicle.h"
#include <utility>
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleOrderQueueAdapter.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// vehicle.cpp
using namespace eve::vehicle;

namespace {

const char* kWeaponDefs = R"JSON(
[{"id":"cannon.125","logic":"hitscan","damage":320,"cooldown":4.0,
  "ammo":{"mag":1,"reserve":40,"reload":6.0}}]
)JSON";

const char* kVehicleDefs = R"JSON(
[{"id":"tank.t90","category":"tank","mobility":"kinematic",
  "maxSpeed":120,"accel":80,"turnRate":90,"radius":24,"maxHealth":600,
  "armorZones":[{"name":"front","mult":1.0}],
  "mounts":[{"name":"turret","weapon":"cannon.125","type":"turret",
             "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"auto"}]},
 {"id":"apc","category":"apc","mobility":"kinematic",
  "maxSpeed":200,"accel":120,"turnRate":120,"radius":20,"maxHealth":300}]
)JSON";

const char* kFpsDefs = R"JSON(
[{"id":"tank.fps","mobility":"kinematic","maxSpeed":120,"accel":80,"turnRate":90,
  "radius":24,"maxHealth":600,
  "armorZones":[{"name":"front","mult":1.0},{"name":"side","mult":0.5}],
  "mounts":[{"name":"turret","weapon":"cannon.125","type":"turret",
             "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"manual"}],
  "seats":[{"name":"driver","cameraMode":"first"},
           {"name":"gunner","cameraMode":"third","mountIndex":0}]}]
)JSON";

[[maybe_unused]] eve::weapon::Weapon* weaponMod() { return eve::ModuleManager::requireInstance<eve::weapon::Weapon>("Weapon"); }

[[maybe_unused]] int countVehicleView() {
    int  n    = 0;
    auto view = ecs::View<VehicleEntity, VehicleEntity::Identity>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

/** @brief 自定义移动模型：每帧向东瞬移（验证扩展点）。 */
class TeleportMobility : public IVehicleMobility {
public:
    const char* name() const override { return "test.teleport"; }
    void        update(VehicleEntity& v, float) override { v.motion()->x += 10.f; }
};

}  // namespace


static const char* kViewScript = R"SQ(
[[maybe_unused]] function testVehicleCppView() {
    local vehicle = eve.Vehicle()
    vehicle.registerVehiclesFromJson("[{\"id\":\"jeep\"}]")
    local v = vehicle.newVehicle("jeep", 5.0, 6.0)
    if (v == null) return false
    local all = eve.view(eve.VehicleEntity)
    foreach (e in all) {
        if (e.getId() == v.getId()) return true
    }
    return false
}
)SQ";

UnitSciptTest(VehicleViewScriptTest, kViewScript);

TEST_CASE_FIXTURE(VehicleViewScriptTest, "vehicle.script.viewSeesNewVehicle") {
    CHECK(vm.callFunc(vm.findFunc("testVehicleCppView"), vm).toBool());
}

static const char* kRtsFlowScript = R"SQ(
[[maybe_unused]] function testVehicleRtsFlow() {
    local weapon = eve.Weapon()
    weapon.registerWeaponsFromJson("[{\"id\":\"cannon\",\"logic\":\"hitscan\",\"cooldown\":1.0,\"ammo\":{\"mag\":5,\"reserve\":20,\"reload\":2.0}}]")
    local vehicle = eve.Vehicle()
    vehicle.registerVehiclesFromJson("[{\"id\":\"tank\",\"mobility\":\"kinematic\",\"maxSpeed\":100,\"accel\":80,\"turnRate\":90,\"maxHealth\":300,\"mounts\":[{\"name\":\"turret\",\"weapon\":\"cannon\",\"type\":\"turret\",\"rotSpeed\":90,\"aimMode\":\"auto\"}]}]")
    local v = vehicle.newVehicle("tank", 0.0, 0.0)
    if (v == null) return false

    vehicle.moveTo(v, 100.0, 0.0)
    for (local i = 0; i < 300 && !vehicle.isArrived(v); i += 1) vehicle.update(0.1)
    if (!vehicle.isArrived(v)) return false

    local m = vehicle.getMount(v, 0)
    if (m == null) return false
    local w = weapon.mountGetWeapon(m)
    if (w == null) return false
    vehicle.attack(v, 200.0, 0.0, 0)
    for (local i = 0; i < 20; i += 1) { vehicle.update(0.1); weapon.update(0.1) }
    if (!weapon.canFire(w)) return false   // 开火后进入冷却

    vehicle.applyDamage(v, 100.0, "", 0)
    return vehicle.getHealth(v) == 200.0
}
)SQ";

UnitSciptTest(VehicleRtsFlowTest, kRtsFlowScript);

TEST_CASE_FIXTURE(VehicleRtsFlowTest, "vehicle.script.rtsFlow") {
    CHECK(vm.callFunc(vm.findFunc("testVehicleRtsFlow"), vm).toBool());
}

// vehicle_orders_adapter.cpp
using eve::vehicle::VehicleOrder;
using eve::vehicle::VehicleOrderQueueAdapter;
using eve::vehicle::VehicleOrderType;

namespace {

[[maybe_unused]] VehicleOrder moveOrder() {
    VehicleOrder order;
    order.type = VehicleOrderType::Move;
    order.x    = 10.f;
    order.y    = 20.f;
    return order;
}

[[maybe_unused]] VehicleOrder attackOrder() {
    VehicleOrder order;
    order.type     = VehicleOrderType::Attack;
    order.x        = 30.f;
    order.y        = 40.f;
    order.targetId = 7;
    return order;
}

[[maybe_unused]] std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}

}  // namespace
