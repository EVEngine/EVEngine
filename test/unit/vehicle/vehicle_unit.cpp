// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "vehicle.cpp"
  ]
}
*/
// clang-format on

#include "common/Capability.h"
#include "common/ECS.h"
#include "vehicle/Vehicle.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleOrderQueueAdapter.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <utility>

using namespace eve::vehicle;

namespace {
constexpr auto weapons  = R"([{"id":"cannon","logic":"hitscan","damage":320,"cooldown":4,
  "ammo":{"mag":1,"reserve":40,"reload":6}}])";
constexpr auto vehicles = R"([
 {"id":"tank","category":"tank","mobility":"kinematic","maxSpeed":120,"accel":80,
  "turnRate":90,"radius":24,"maxHealth":600,
  "armorZones":[{"name":"front","mult":1},{"name":"side","mult":0.5}],
  "mounts":[{"name":"turret","weapon":"cannon","type":"turret",
    "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"auto"}]},
 {"id":"apc","category":"apc","mobility":"kinematic","maxSpeed":200,"accel":120,
  "turnRate":120,"radius":20,"maxHealth":300}])";
constexpr auto crewed   = R"([{"id":"crewed","mobility":"kinematic","maxSpeed":120,"accel":80,
  "turnRate":90,"maxHealth":600,
  "mounts":[{"name":"turret","weapon":"cannon","type":"turret",
    "limits":[-180,180,-8,20],"rotSpeed":45,"aimMode":"manual"}],
  "seats":[{"name":"driver","cameraMode":"first"},
           {"name":"gunner","cameraMode":"third","mountIndex":0}]}])";

eve::weapon::Weapon& weaponModule() {
    auto* module = eve::ModuleManager::requireInstance<eve::weapon::Weapon>("Weapon");
    REQUIRE(module != nullptr);
    return *module;
}

VehicleEntity& spawn(Vehicle& module, const char* definition, float x = 0.f, float y = 0.f) {
    auto* entity = module.newVehicle(definition, x, y);
    REQUIRE(entity != nullptr);
    return *entity;
}

VehicleOrder moveOrder() {
    VehicleOrder order;
    order.type = VehicleOrderType::Move;
    order.x    = 10.f;
    order.y    = 20.f;
    return order;
}

VehicleOrder attackOrder() {
    VehicleOrder order;
    order.type     = VehicleOrderType::Attack;
    order.x        = 30.f;
    order.y        = 40.f;
    order.targetId = 7;
    return order;
}

std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}

class Teleport final : public IVehicleMobility {
public:
    const char* name() const override { return "test.teleport"; }
    void        update(VehicleEntity& entity, float) override { entity.motion()->x += 10.f; }
};
}  // namespace

// Definitions and construction: registered JSON controls identity, health, position and mounts.
TEST_CASE("vehicle.definition.validAndMalformedInput") {
    Vehicle module;
    CHECK_EQ(module.registerVehiclesFromJson(vehicles), 2);
    CHECK(module.hasVehicleDefinition("tank"));
    CHECK_EQ(module.getVehicleDefinitionMobility("tank"), std::string("kinematic"));
    CHECK_EQ(module.getVehicleDefinitionMaxHealth("tank"), 600.f);
    CHECK_EQ(module.registerVehiclesFromJson("not json"), 0);
    CHECK_EQ(module.registerVehiclesFromJson(R"([{"name":"missing-id"}])"), 0);
    REQUIRE_EQ(module.getVehicleDefinitionMobility("missing"), std::string{});
}

TEST_CASE("vehicle.factory.knownDefinitionCreatesComponentsAndUnknownDoesNot") {
    weaponModule().registerWeaponsFromJson(weapons);
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& tank = spawn(module, "tank", 100.f, 200.f);
    CHECK_EQ(tank.identity()->defId, std::string("tank"));
    CHECK_EQ(tank.health()->hp, 600.f);
    CHECK_EQ(module.getX(&tank), 100.f);
    CHECK_EQ(module.getY(&tank), 200.f);
    CHECK_EQ(module.getMountCount(&tank), 1);
    auto* mount = module.getMount(&tank, 0);
    REQUIRE(mount != nullptr);
    CHECK(weaponModule().mountGetWeapon(mount) != nullptr);
    auto& apc = spawn(module, "apc");
    CHECK_EQ(module.getMountCount(&apc), 0);
    CHECK(module.newVehicle("missing", 0.f, 0.f) == nullptr);
}

// Order lifecycle: commands replace or queue and produce completion events after physical arrival.
TEST_CASE("vehicle.orders.moveTurnsArrivesAndEmitsCompletion") {
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& apc = spawn(module, "apc", 100.f, 100.f);
    module.clearEvents();
    module.moveTo(&apc, 100.f, 300.f);
    CHECK_EQ(module.getCurrentOrderType(&apc), std::string("move"));
    for (int i = 0; i < 300 && !module.isArrived(&apc); ++i) module.update(0.1f);
    CHECK(module.isArrived(&apc));
    CHECK_GT(module.getY(&apc), 200.f);
    CHECK_GT(module.getHeading(&apc), 30.f);
    CHECK_LT(module.getSpeed(&apc), 1.f);
    CHECK_EQ(module.getCurrentOrderType(&apc), std::string("none"));
    CHECK_EQ(module.getEventCount(), 1);
    CHECK_EQ(module.getEventType(0), std::string("order_completed"));
    CHECK_EQ(module.getEventOrderType(0), std::string("move"));
}

TEST_CASE("vehicle.orders.stopDeceleratesAndReplaceKeepsOneCommand") {
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& apc = spawn(module, "apc");
    module.moveTo(&apc, 1000.f, 0.f);
    module.update(1.f);
    CHECK_GT(module.getSpeed(&apc), 50.f);
    module.stop(&apc);
    CHECK_EQ(module.getCurrentOrderType(&apc), std::string("stop"));
    module.update(1.f);
    CHECK_LT(module.getSpeed(&apc), 1.f);
    CHECK_EQ(module.orderCount(&apc), 1);
    module.moveTo(&apc, 100.f, 0.f);
    module.attack(&apc, 200.f, 0.f, 7);
    CHECK_EQ(module.orderCount(&apc), 2);
    CHECK(apc.orders()->queue[0].type == VehicleOrderType::Move);
    CHECK(apc.orders()->queue[1].type == VehicleOrderType::Attack);
    module.moveTo(&apc, 300.f, 0.f);
    CHECK_EQ(module.orderCount(&apc), 1);
    CHECK(apc.orders()->queue[0].type == VehicleOrderType::Move);
}

TEST_CASE("vehicle.orders.attackTracksTargetWithAutoMount") {
    weaponModule().registerWeaponsFromJson(weapons);
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& tank  = spawn(module, "tank");
    auto* mount = module.getMount(&tank, 0);
    REQUIRE(mount != nullptr);
    auto* weapon = weaponModule().mountGetWeapon(mount);
    REQUIRE(weapon != nullptr);
    module.attack(&tank, 0.f, 100.f, 7);
    for (int i = 0; i < 30; ++i) {
        module.update(0.1f);
        weaponModule().update(0.1f);
    }
    // The hull also turns toward the target; mount yaw is relative to the hull.
    CHECK_LT(std::fabs(module.getHeading(&tank) + weapon->aim()->desiredYaw - 90.f), 1e-3f);
    CHECK_LT(std::fabs(weapon->aim()->yaw - weapon->aim()->desiredYaw), 1.f);
    CHECK_EQ(module.getCurrentOrderType(&tank), std::string("attack"));
}

// The adapter owns order lifecycle; legacy Orders fields are a refreshed projection.
TEST_CASE("vehicle.adapter.appendCompleteAndTimeoutAdvanceDomainPayload") {
    VehicleOrderQueueAdapter adapter;
    auto                     first  = takeId(adapter.append(moveOrder(), 1));
    auto                     second = takeId(adapter.append(attackOrder(), 5));
    CHECK(!first.empty());
    CHECK(!second.empty());
    REQUIRE(adapter.current() != nullptr);
    CHECK(adapter.current()->type == VehicleOrderType::Move);
    CHECK_EQ(adapter.activeOrQueuedCount(), 2);
    REQUIRE(adapter.completeCurrent().ok());
    REQUIRE(adapter.current() != nullptr);
    CHECK(adapter.current()->type == VehicleOrderType::Attack);
    adapter.clear();
    CHECK(adapter.current() == nullptr);
    takeId(adapter.append(moveOrder(), 0, 0.25));
    takeId(adapter.append(attackOrder()));
    adapter.update(0.25);
    REQUIRE(adapter.current() != nullptr);
    CHECK(adapter.current()->type == VehicleOrderType::Attack);
}

TEST_CASE("vehicle.adapter.interruptRequiresEqualOrHigherPriority") {
    VehicleOrderQueueAdapter adapter;
    takeId(adapter.append(moveOrder(), 20));
    CHECK(!adapter.interrupt(attackOrder(), 19).hasValue());
    REQUIRE(adapter.current() != nullptr);
    CHECK(adapter.current()->type == VehicleOrderType::Move);
    CHECK(!takeId(adapter.interrupt(attackOrder(), 20)).empty());
    REQUIRE(adapter.current() != nullptr);
    CHECK(adapter.current()->type == VehicleOrderType::Attack);
    CHECK_EQ(adapter.activeOrQueuedCount(), 1);
}

TEST_CASE("vehicle.adapter.replaceRefreshesLegacyProjection") {
    VehicleEntity::Orders    legacy;
    VehicleOrderQueueAdapter adapter;
    takeId(adapter.append(moveOrder()));
    takeId(adapter.append(attackOrder()));
    adapter.syncCompatibility(legacy);
    CHECK_EQ(legacy.queue.size(), size_t{2});
    CHECK_EQ(legacy.current, 0);
    takeId(adapter.replace(attackOrder()));
    adapter.syncCompatibility(legacy);
    CHECK_EQ(legacy.queue.size(), size_t{1});
    CHECK(legacy.queue.front().type == VehicleOrderType::Attack);
}

TEST_CASE("vehicle.adapter.copyHasIndependentLifecycleAndPayload") {
    VehicleOrderQueueAdapter source;
    auto                     first  = takeId(source.append(moveOrder(), 3));
    auto                     second = takeId(source.append(attackOrder(), 1));
    VehicleOrderQueueAdapter copy(source);
    CHECK_EQ(copy.activeOrQueuedCount(), 2);
    REQUIRE(copy.current() != nullptr);
    CHECK_EQ(copy.current()->x, 10.f);
    REQUIRE(source.cancel(first, "source-only").ok());
    REQUIRE(source.current() != nullptr);
    CHECK(source.current()->type == VehicleOrderType::Attack);
    REQUIRE(copy.current() != nullptr);
    CHECK(copy.current()->type == VehicleOrderType::Move);
    REQUIRE(copy.completeCurrent().ok());
    REQUIRE(copy.cancel(second, "copy-only").ok());
    CHECK(copy.current() == nullptr);
    REQUIRE(source.current() != nullptr);
    CHECK(source.current()->type == VehicleOrderType::Attack);
}

TEST_CASE("vehicle.orders.componentCopyClonesAdapterState") {
    VehicleEntity::Orders source;
    auto                  id = takeId(source.adapter->append(attackOrder(), 4, 2.0));
    source.adapter->syncCompatibility(source);
    VehicleEntity::Orders copy(source);
    REQUIRE(copy.adapter->current() != nullptr);
    CHECK_EQ(copy.adapter->current()->targetId, 7);
    REQUIRE(source.adapter->cancel(id, "source-only").ok());
    CHECK(source.adapter->current() == nullptr);
    REQUIRE(copy.adapter->current() != nullptr);
    VehicleEntity::Orders assigned;
    assigned = copy;
    REQUIRE(assigned.adapter->current() != nullptr);
    REQUIRE(assigned.adapter->cancel(id, "assigned-only").ok());
    CHECK(copy.adapter->current() != nullptr);
}

// Seat ownership and controls: the driver moves, the gunner aims and fires.
TEST_CASE("vehicle.seats.occupancyRejectsConflictsAndExitClearsOnlyOwner") {
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(crewed) == 1);
    auto& entity = spawn(module, "crewed");
    CHECK_EQ(module.getSeatCount(&entity), 2);
    CHECK_EQ(module.getSeatName(&entity, 0), std::string("driver"));
    CHECK_EQ(module.getSeatCameraMode(&entity, 1), std::string("third"));
    REQUIRE(module.enterSeat(&entity, 0, 1));
    CHECK(!module.enterSeat(&entity, 0, 2));
    REQUIRE(module.enterSeat(&entity, 1, 2));
    CHECK(!module.enterSeat(&entity, 1, 3));
    CHECK_EQ(module.getSeatOccupant(&entity, 0), 1);
    CHECK_EQ(module.exitSeatByPlayer(&entity, 1), 0);
    CHECK(!module.isSeatOccupied(&entity, 0));
    CHECK(module.isSeatOccupied(&entity, 1));
}

TEST_CASE("vehicle.seats.driverThrottleAndGunnerTriggerReachTheirSystems") {
    weaponModule().registerWeaponsFromJson(weapons);
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(crewed) == 1);
    auto& entity = spawn(module, "crewed");
    REQUIRE(module.enterSeat(&entity, 0, 7));
    REQUIRE(module.enterSeat(&entity, 1, 8));
    module.setPlayerControls(7, 1.f, 0.f, 0.f, false, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) module.update(1.f / 60.f);
    CHECK_GT(module.getX(&entity), 30.f);
    auto* mount = module.getSeatMount(&entity, 1);
    REQUIRE(mount != nullptr);
    auto* weapon = weaponModule().mountGetWeapon(mount);
    REQUIRE(weapon != nullptr);
    weaponModule().clearEvents();
    module.setPlayerControls(8, 0.f, 0.f, 0.f, true, 90.f, 10.f);
    module.update(0.1f);
    CHECK_EQ(weapon->aim()->desiredYaw, 90.f);
    CHECK_GT(weaponModule().getEventCount(), 0);
    CHECK_EQ(weaponModule().getEventType(0), std::string("fire"));
}

// Damage applies zone multiplier then listeners, emits events and stops at destruction.
TEST_CASE("vehicle.damage.armorZonesAndTerminalDestroyedState") {
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& tank = spawn(module, "tank");
    module.clearEvents();
    module.applyDamage(&tank, 100.f, "side", 0);
    CHECK_EQ(module.getHealth(&tank), 550.f);
    module.applyDamage(&tank, 200.f, "front", 0);
    CHECK_EQ(module.getHealth(&tank), 350.f);
    CHECK_EQ(module.getEventCount(), 2);
    CHECK_EQ(module.getEventType(0), std::string("damaged"));
    module.applyDamage(&tank, 1000.f, "front", 0);
    CHECK_EQ(module.getHealth(&tank), 0.f);
    CHECK(module.isDestroyed(&tank));
    CHECK_EQ(module.getEventType(module.getEventCount() - 1), std::string("destroyed"));
    module.applyDamage(&tank, 50.f, "front", 0);
    CHECK_EQ(module.getEventCount(), 3);
}

TEST_CASE("vehicle.damage.listenerCanTransformAmount") {
    class DoubleDamage final : public IVehicleDamageModifier {
    public:
        float modifyDamage(VehicleEntity&, float amount, const std::string&, int) override { return amount * 2.f; }
    } modifier;
    eve::cap::addListener<IVehicleDamageModifier>(&modifier);
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(vehicles) == 2);
    auto& tank = spawn(module, "tank");
    module.applyDamage(&tank, 100.f, "front", 0);
    CHECK_EQ(module.getHealth(&tank), 400.f);
    eve::cap::removeListener<IVehicleDamageModifier>(&modifier);
}

TEST_CASE("vehicle.mobility.registeredModelUpdatesMotion") {
    Teleport mobility;
    Vehicle::registerMobility(&mobility);
    Vehicle module;
    REQUIRE(module.registerVehiclesFromJson(R"([{"id":"hover","mobility":"test.teleport"}])") == 1);
    auto& hover = spawn(module, "hover");
    module.update(0.1f);
    CHECK_EQ(module.getX(&hover), 10.f);
}

#include <cmath>
#include "vehicle/VehicleDefinitionRuntime.h"

namespace test_vehicle_tire_definition_unit {
using namespace eve;
using namespace eve::vehicle;

TEST_CASE("physics.vehicleTires.definitionCodecAdmitsAndRejectsTireParameters") {
    definitions::Definition source;
    source.id   = "tires";
    source.json = R"({"suspension":{"wheelInertia":0.2,"tireFriction":0.7,"longitudinalStiffness":4000,
        "corneringStiffness":3000,"rollingResistance":0.02,"brakeTorque":80,"maxSteerAngle":0.5,"supportAcceleration2D":8}})";
    auto parsed = parseVehicleDefinition(source);
    REQUIRE(parsed.ok());
    const auto& config = parsed.value().suspension;
    CHECK(std::abs(config.wheelInertia - .2) < 1e-6);
    CHECK(std::abs(config.tireFriction - .7) < 1e-6);
    CHECK_EQ(config.longitudinalStiffness, 4000);
    CHECK_EQ(config.corneringStiffness, 3000);
    CHECK(std::abs(config.rollingResistance - .02) < 1e-6);
    CHECK_EQ(config.brakeTorque, 80);
    CHECK_EQ(config.maxSteerAngle, .5f);
    CHECK_EQ(config.supportAcceleration2D, 8);
    for (const char* invalid : {R"({"suspension":{"wheelInertia":0}})", R"({"suspension":{"tireFriction":-1}})",
                                R"({"suspension":{"maxSteerAngle":2}})", R"({"suspension":{"brakeTorque":"heavy"}})"}) {
        source.json = invalid;
        CHECK(!parseVehicleDefinition(source).ok());
    }
    source.json   = "{}";
    auto defaults = parseVehicleDefinition(source);
    REQUIRE(defaults.ok());
    CHECK(defaults.value().suspension.wheelInertia > 0);
}
}  // namespace test_vehicle_tire_definition_unit
