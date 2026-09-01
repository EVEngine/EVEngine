#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "vehicle/Vehicle.h"

#include <cmath>
#include <string>

using namespace eve::vehicle;

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
    CHECK_GT(mod.getHeading(v), h0 + 10.f);
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

    mod.setInput(v, 0.f, 1.f, 0.f);
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
    CHECK_GT(mod.getY(v), 5.f);
    CHECK_GT(mod.getHeight(v), 0.15f);
    CHECK_LT(mod.getHeight(v), 2.5f);
}
