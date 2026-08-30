#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "vehicle/Vehicle.h"
#include "vehicle/VehicleMobility.h"
#include "weapon/Weapon.h"

#ifdef EVENGINE_HAS_PHYSICS
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"
#endif

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

eve::weapon::Weapon* weaponMod() { return eve::ModuleManager::requireInstance<eve::weapon::Weapon>("Weapon"); }

int countVehicleView() {
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

TEST_CASE("vehicle.defs.registerJson") {
    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson(kVehicleDefs), 2);
    CHECK(mod.hasVehicleDefinition("tank.t90"));
    CHECK_EQ(mod.getVehicleDefinitionMobility("tank.t90"), std::string("kinematic"));
    CHECK_EQ(mod.getVehicleDefinitionMaxHealth("tank.t90"), 600.f);
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"name\":\"no-id\"}]"), 0);
    CHECK_EQ(mod.registerVehiclesFromJson("not json"), 0);
    CHECK_EQ(mod.getVehicleDefinitionMobility("missing"), std::string{});
}

TEST_CASE("vehicle.factory.newVehicleWithMounts") {
    weaponMod()->registerWeaponsFromJson(kWeaponDefs);
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    const int before = countVehicleView();

    VehicleEntity* tank = mod.newVehicle("tank.t90", 100.f, 200.f, 0.f, "red");
    REQUIRE(tank != nullptr);
    CHECK_EQ(tank->identity()->defId, std::string("tank.t90"));
    CHECK_EQ(tank->identity()->faction, std::string("red"));
    CHECK_EQ(tank->health()->hp, 600.f);
    CHECK_EQ(mod.getX(tank), 100.f);
    CHECK_EQ(mod.getY(tank), 200.f);
    CHECK_EQ(mod.getMountCount(tank), 1);
    eve::weapon::WeaponMountEntity* m = mod.getMount(tank, 0);
    REQUIRE(m != nullptr);
    REQUIRE(weaponMod()->mountGetWeapon(m) != nullptr);

    VehicleEntity* apc = mod.newVehicle("apc", 0.f, 0.f);
    REQUIRE(apc != nullptr);
    CHECK_EQ(mod.getMountCount(apc), 0);
    CHECK_EQ(mod.newVehicle("missing", 0.f, 0.f), nullptr);
    CHECK_EQ(countVehicleView(), before + 2);
}

TEST_CASE("vehicle.orders.moveAndArrive") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    VehicleEntity* v = mod.newVehicle("apc", 100.f, 100.f, 0.f);
    REQUIRE(v != nullptr);
    mod.clearEvents();

    mod.moveTo(v, 300.f, 100.f);
    CHECK_EQ(mod.getCurrentOrderType(v), std::string("move"));
    for (int i = 0; i < 200 && !mod.isArrived(v); ++i) mod.update(0.1f);

    CHECK(mod.isArrived(v));
    CHECK_GT(mod.getX(v), 200.f);
    CHECK_LT(mod.getSpeed(v), 1.f);
    CHECK_EQ(mod.getEventCount(), 1);
    CHECK_EQ(mod.getEventType(0), std::string("order_completed"));
    CHECK_EQ(mod.getEventOrderType(0), std::string("move"));
    CHECK_EQ(mod.getCurrentOrderType(v), std::string("none"));
}

TEST_CASE("vehicle.orders.turnsTowardTarget") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    VehicleEntity* v = mod.newVehicle("apc", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    mod.moveTo(v, 0.f, 200.f);  // 目标正北
    for (int i = 0; i < 50; ++i) mod.update(0.1f);
    CHECK_GT(mod.getHeading(v), 30.f);  // 车头已向目标旋转
    for (int i = 0; i < 200 && !mod.isArrived(v); ++i) mod.update(0.1f);
    CHECK(mod.isArrived(v));
    CHECK_GT(mod.getY(v), 100.f);
}

TEST_CASE("vehicle.orders.stopDecelerates") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    VehicleEntity* v = mod.newVehicle("apc", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    mod.moveTo(v, 1000.f, 0.f);
    mod.update(1.0f);
    CHECK_GT(mod.getSpeed(v), 50.f);

    mod.stop(v);
    CHECK_EQ(mod.getCurrentOrderType(v), std::string("stop"));
    mod.update(1.0f);
    CHECK_LT(mod.getSpeed(v), 1.f);
    CHECK_EQ(mod.orderCount(v), 1);
}

TEST_CASE("vehicle.orders.adapterPreservesDomainCommandsAndReplaceSemantics") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    VehicleEntity* v = mod.newVehicle("apc", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);

    mod.moveTo(v, 100.f, 0.f);
    mod.attack(v, 200.f, 0.f, 7);
    CHECK_EQ(mod.orderCount(v), 2);
    REQUIRE(v->orders()->current == 0);
    CHECK_EQ(v->orders()->queue[0].type, VehicleOrderType::Move);
    CHECK_EQ(v->orders()->queue[1].type, VehicleOrderType::Attack);

    mod.moveTo(v, 300.f, 0.f);
    CHECK_EQ(mod.orderCount(v), 1);
    REQUIRE(v->orders()->current == 0);
    CHECK_EQ(v->orders()->queue[0].type, VehicleOrderType::Move);
}

TEST_CASE("vehicle.orders.attackAimsMount") {
    weaponMod()->registerWeaponsFromJson(kWeaponDefs);
    Vehicle mod;
    mod.registerVehiclesFromJson(kVehicleDefs);
    VehicleEntity* tank = mod.newVehicle("tank.t90", 0.f, 0.f, 0.f);
    REQUIRE(tank != nullptr);

    mod.attack(tank, 0.f, 100.f, 7);
    eve::weapon::WeaponMountEntity* m = mod.getMount(tank, 0);
    REQUIRE(m != nullptr);
    eve::weapon::WeaponEntity* w = weaponMod()->mountGetWeapon(m);
    REQUIRE(w != nullptr);

    for (int i = 0; i < 30; ++i) {
        mod.update(0.1f);
        weaponMod()->update(0.1f);
    }
    CHECK_GT(std::fabs(w->aim()->desiredYaw), 80.f);                 // 朝向目标（本地角）
    CHECK_LT(std::fabs(w->aim()->yaw - w->aim()->desiredYaw), 1.f);  // 已转到
    CHECK_EQ(mod.getCurrentOrderType(tank), std::string("attack"));
}

TEST_CASE("vehicle.mobility.customRegistration") {
    TeleportMobility mob;
    Vehicle::registerMobility(&mob);
    CHECK(Vehicle::getMobilityCount() >= 2);

    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"id\":\"hover\",\"mobility\":\"test.teleport\"}]"), 1);
    VehicleEntity* v = mod.newVehicle("hover", 0.f, 0.f);
    REQUIRE(v != nullptr);
    mod.update(0.1f);
    CHECK_EQ(mod.getX(v), 10.f);
}

#ifdef EVENGINE_HAS_PHYSICS

TEST_CASE("vehicle.physics2d.wheelDrivesAndSteers") {
    eve::physics::Physics ph;
    eve::physics::World*  world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"id\":\"car\",\"mobility\":\"wheel\",\"maxSpeed\":120,"
                                          "\"accel\":100,\"turnRate\":120,\"radius\":18}]"),
             1);
    VehicleEntity* v = mod.newVehicle("car", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics2D(v, world));
    CHECK(mod.hasPhysics(v));
    CHECK_EQ(mod.getPhysicsSpace(v), std::string("2d"));

    mod.setInput(v, 1.f, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(mod.getX(v), 30.f);
    CHECK_GT(mod.getSpeed(v), 60.f);

    mod.setInput(v, 0.7f, 1.f, 0.f);
    const float h0 = mod.getHeading(v);
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(mod.getHeading(v), h0 + 10.f);  // 车头转向
}

TEST_CASE("vehicle.physics2d.trackRotatesInPlace") {
    eve::physics::Physics ph;
    eve::physics::World*  world = ph.newWorld(0.f, 0.f, true);
    REQUIRE(world != nullptr);

    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson("[{\"id\":\"tank\",\"mobility\":\"track\",\"maxSpeed\":100,"
                                          "\"accel\":80,\"turnRate\":150,\"radius\":20}]"),
             1);
    VehicleEntity* v = mod.newVehicle("tank", 100.f, 100.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics2D(v, world));

    mod.setInput(v, 0.f, 1.f, 0.f);  // 原地转向
    for (int i = 0; i < 60; ++i) {
        mod.update(1.f / 60.f);
        world->update(1.f / 60.f);
    }
    CHECK_GT(std::fabs(mod.getHeading(v)), 40.f);
    CHECK_LT(std::fabs(mod.getX(v) - 100.f), 8.f);
    CHECK_LT(std::fabs(mod.getY(v) - 100.f), 8.f);
}

TEST_CASE("vehicle.physics3d.suspensionDrives") {
    eve::physics::Physics  ph;
    eve::physics::World3D* world3 = ph.newWorld3D(0.f, -9.8f, 0.f, true);
    REQUIRE(world3 != nullptr);
    eve::physics::Body3D* ground = world3->newBody("static", 0.f, -0.5f, 0.f);
    REQUIRE(ground != nullptr);
    ground->newBoxShape(60.f, 0.5f, 60.f, 1.f, 0.8f, 0.f);

    const char* suv = R"JSON(
[{"id":"suv","mobility":"suspension","maxSpeed":16,"turnRate":60,"radius":1.0,
  "suspension":{"maxTravel":0.25,"driveForce":3000,"lateralGrip":10,
    "wheels":[
      {"x":-0.9,"y":-0.35,"z":-1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":true,"steer":true},
      {"x":0.9,"y":-0.35,"z":-1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":true,"steer":true},
      {"x":-0.9,"y":-0.35,"z":1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":false,"steer":false},
      {"x":0.9,"y":-0.35,"z":1.2,"radius":0.3,"rest":0.35,"stiffness":30000,"damping":3000,"drive":false,"steer":false}]}}]
)JSON";
    Vehicle     mod;
    CHECK_EQ(mod.registerVehiclesFromJson(suv), 1);
    VehicleEntity* v = mod.newVehicle("suv", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.attachPhysics3D(v, world3, 2.0f));
    CHECK_EQ(mod.getPhysicsSpace(v), std::string("3d"));

    mod.setInput(v, 0.8f, 0.f, 0.f);
    for (int i = 0; i < 240; ++i) {
        mod.update(1.f / 60.f);
        world3->update(1.f / 60.f);
    }
    CHECK_GT(mod.getY(v), 5.f);         // 沿 +Z 前进
    CHECK_GT(mod.getHeight(v), 0.15f);  // 悬架托住车体，未穿透地面
    CHECK_LT(mod.getHeight(v), 2.5f);   // 也没有飞走
}

#endif  // EVENGINE_HAS_PHYSICS

TEST_CASE("vehicle.seats.enterExit") {
    Vehicle mod;
    CHECK_EQ(mod.registerVehiclesFromJson(kFpsDefs), 1);
    VehicleEntity* v = mod.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);

    CHECK_EQ(mod.getSeatCount(v), 2);
    CHECK_EQ(mod.getSeatName(v, 0), std::string("driver"));
    CHECK_EQ(mod.getSeatCameraMode(v, 1), std::string("third"));

    CHECK(mod.enterSeat(v, 0, 1));
    CHECK(mod.isSeatOccupied(v, 0));
    CHECK_EQ(mod.getSeatOccupant(v, 0), 1);
    CHECK(!mod.enterSeat(v, 0, 2));  // 已占用
    CHECK(mod.enterSeat(v, 1, 2));
    CHECK(!mod.enterSeat(v, 1, 3));  // 玩家2已在炮手座

    CHECK_EQ(mod.exitSeatByPlayer(v, 1), 0);
    CHECK(!mod.isSeatOccupied(v, 0));
    CHECK(mod.isSeatOccupied(v, 1));
}

TEST_CASE("vehicle.driver.seatControlsDrive") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = mod.newVehicle("tank.fps", 0.f, 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.enterSeat(v, 0, 7));

    mod.setPlayerControls(7, 1.f, 0.f, 0.f, false, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) mod.update(1.f / 60.f);
    CHECK_GT(mod.getX(v), 30.f);
    CHECK_GT(mod.getSpeed(v), 60.f);
}

TEST_CASE("vehicle.driver.gunnerAimsAndFires") {
    weaponMod()->registerWeaponsFromJson(kWeaponDefs);
    Vehicle mod;
    mod.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = mod.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);
    CHECK(mod.enterSeat(v, 1, 8));

    eve::weapon::WeaponMountEntity* m = mod.getSeatMount(v, 1);
    REQUIRE(m != nullptr);
    eve::weapon::WeaponEntity* w = weaponMod()->mountGetWeapon(m);
    REQUIRE(w != nullptr);

    weaponMod()->clearEvents();
    mod.setPlayerControls(8, 0.f, 0.f, 0.f, true, 90.f, 10.f);
    mod.update(0.1f);

    CHECK_EQ(w->aim()->desiredYaw, 90.f);
    CHECK_GT(weaponMod()->getEventCount(), 0);
    CHECK_EQ(weaponMod()->getEventType(0), std::string("fire"));
}

TEST_CASE("vehicle.damage.armorAndEvents") {
    Vehicle mod;
    mod.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = mod.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);
    mod.clearEvents();

    CHECK_EQ(mod.getHealth(v), 600.f);
    mod.applyDamage(v, 100.f, "side", 0);  // 100 * 0.5 = 50
    CHECK_EQ(mod.getHealth(v), 550.f);
    mod.applyDamage(v, 200.f, "front", 0);
    CHECK_EQ(mod.getHealth(v), 350.f);
    CHECK_EQ(mod.getEventCount(), 2);
    CHECK_EQ(mod.getEventType(0), std::string("damaged"));
    CHECK(!mod.isDestroyed(v));

    mod.applyDamage(v, 1000.f, "front", 0);
    CHECK_EQ(mod.getHealth(v), 0.f);
    CHECK(mod.isDestroyed(v));
    CHECK_EQ(mod.getEventType(mod.getEventCount() - 1), std::string("destroyed"));

    mod.applyDamage(v, 50.f, "front", 0);  // 已毁不再受伤
    CHECK_EQ(mod.getEventCount(), 3);
}

TEST_CASE("vehicle.damage.modifiers") {
    class DoubleDamage : public IVehicleDamageModifier {
    public:
        float modifyDamage(VehicleEntity&, float amount, const std::string&, int) override { return amount * 2.f; }
    };
    DoubleDamage dmg;
    eve::cap::addListener<IVehicleDamageModifier>(&dmg);

    Vehicle mod;
    mod.registerVehiclesFromJson(kFpsDefs);
    VehicleEntity* v = mod.newVehicle("tank.fps", 0.f, 0.f);
    REQUIRE(v != nullptr);
    mod.applyDamage(v, 100.f, "front", 0);  // 100 * 2 = 200
    CHECK_EQ(mod.getHealth(v), 400.f);

    eve::cap::removeListener<IVehicleDamageModifier>(&dmg);
}

static const char* kViewScript = R"SQ(
function testVehicleCppView() {
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
function testVehicleRtsFlow() {
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
