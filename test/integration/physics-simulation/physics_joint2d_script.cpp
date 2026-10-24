#include <Box2D/Box2D.h>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/World.h"
#include "physics/mechanics/Joint2DBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.joint2d.scriptResultAndOwningRelease") {
    using namespace eve::physics;
    World                    world(0, 0, false, 30);
    std::unique_ptr<Body>    ground(world.newBody("static", 0, 0));
    std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> fixture(body->newCircleFixture(15));
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World>("World", std::function<World*()>([] { return nullptr; }), true);
        table.addClass<Body>("Body", std::function<Body*()>([] { return nullptr; }), true);
        exposeJoint2DBindings(table);
        vm.addFunc("world", [&] { return &world; });
        vm.addFunc("ground", [&] { return ground.get(); });
        vm.addFunc("body", [&] { return body.get(); });
        vm.addFunc("advance", [&] { world.update(0.01f); });
        vm.run(vm.compileSource(R"(
            local settings = eve.Joint2DSettings();
            local created = eve.createJoint2D(world(), ground(), body(), settings);
            assert(created.ok && created.hasValue);
            saved <- created.value;
            assert(saved.isValid());
            assert(saved.getId() > 0);
            assert(saved.setMotor(true, 1.0, 10.0).ok);
            assert(saved.setLimits(true, -0.5, 0.5).ok);
            advance();
            local sample = saved.sample(0.01);
            assert(sample.ok && sample.value.coordinate > 0.0);
            assert(!saved.setLimits(true, 2.0, 1.0).ok);
            assert(!eve.createJoint2D(null, ground(), body(), settings).ok);
            assert(!eve.createJoint2D(settings, ground(), body(), settings).ok);
            assert(!eve.createJoint2D(world(), settings, body(), settings).ok);
            assert(!eve.createGearJoint2D(world(), settings, saved, 1.0).ok);
            assert(!saved.sample.call(settings, 0.01).ok);
            assert(!eve.createGearJoint2D(world(), saved, saved, 1.0).ok);
        )"));
        REQUIRE_EQ(world.raw()->GetJointCount(), 1);
    }
    REQUIRE_EQ(world.raw()->GetJointCount(), 0);
    REQUIRE(body->isValid());
}
