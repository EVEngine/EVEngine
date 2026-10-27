// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "cases": {
    "script-binding": [
      "physics.coupling3d.scriptOwnershipAndTypeSafety",
      "physics.coupling3d.scriptWorldFirstInvalidation"
    ]
  },
  "module": "physics"
}
*/
// clang-format on

#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/mechanics/Coupling3DBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.coupling3d.scriptOwnershipAndTypeSafety") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -2, 0, 0));
    std::unique_ptr<Body3D>  b(world.newBody("dynamic", 2, 0, 0));
    std::unique_ptr<Shape3D> fa(a->newSphereShape(.3f)), fb(b->newSphereShape(.3f));
    std::unique_ptr<Joint3D> ja(world.newRevoluteJoint(ground.get(), a.get(), -2, 0, 0, 0, 0, 1));
    std::unique_ptr<Joint3D> jb(world.newRevoluteJoint(ground.get(), b.get(), 2, 0, 0, 0, 0, 1));
    a->setAngularDamping(0);
    b->setAngularDamping(0);
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
        table.addClass<Body3D>("Body3D", std::function<Body3D*()>([] { return nullptr; }), true);
        table.addClass<Joint3D>("Joint3D", std::function<Joint3D*()>([] { return nullptr; }), true);
        exposeCoupling3DBindings(table);
        vm.addFunc("world", [&] { return &world; });
        vm.addFunc("a", [&] { return a.get(); });
        vm.addFunc("b", [&] { return b.get(); });
        vm.addFunc("ja", [&] { return ja.get(); });
        vm.addFunc("jb", [&] { return jb.get(); });
        vm.addFunc("advance", [&] {
            a->setAngularVelocity(0, 0, 2);
            world.update(.01f);
        });
        vm.run(vm.compileSource(R"(
            local s = eve.GearCoupling3DSettings(); s.ratio = 2.0;
            local result = eve.createGearCoupling3D(world(), ja(), jb(), s);
            assert(result.ok && result.hasValue);
            saved <- result.value;
            assert(saved.isValid() && saved.getId() > 0);
            assert(saved.setEnabled(false).ok && !saved.sample().value.active);
            assert(saved.setEnabled(true).ok);
            advance();
            local state = saved.sample();
            assert(state.ok && state.value.active && state.value.coordinateA > 0);
            assert(!eve.createGearCoupling3D(s, ja(), jb(), s).ok);
            assert(!eve.createGearCoupling3D(world(), a(), jb(), s).ok);
            assert(!eve.createGearCoupling3D(world(), ja(), jb(), a()).ok);
            assert(!saved.sample.call(s).ok);
            assert(!saved.setEnabled.call(s, true).ok);
            assert(!saved.destroy.call(s).ok);
            assert(!saved.isValid.call(s));
            local p = eve.PulleyCoupling3DSettings();
            p.groundA.x = -2.0; p.groundB.x = 2.0;
            local rope = eve.createPulleyCoupling3D(world(), a(), b(), p);
            assert(rope.ok && rope.value.isValid());
            assert(!eve.createPulleyCoupling3D(world(), ja(), b(), p).ok);
            assert(rope.value.destroy().ok && !rope.value.isValid());
            assert(!rope.value.sample().ok);
            assert(rope.value.destroy().ok);
        )"));
    }
    // Destroying the VM releases its owning transmission but keeps native source joints and bodies.
    REQUIRE(ja->isValid());
    REQUIRE(jb->isValid());
    a->setAngularVelocity(0, 0, 2);
    b->setAngularVelocity(0, 0, 0);
    world.update(.01f);
    CHECK_EQ(a->getAngularVelocityZ(), 2);
    CHECK_EQ(b->getAngularVelocityZ(), 0);
}

TEST_CASE("physics.coupling3d.scriptWorldFirstInvalidation") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -1, 0, 0));
    std::unique_ptr<Body3D>  b(world.newBody("dynamic", 1, 0, 0));
    std::unique_ptr<Shape3D> fa(a->newSphereShape(.3f)), fb(b->newSphereShape(.3f));
    ssq::VM                  vm(1024);
    auto                     table = vm.addTable("eve");
    table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
    table.addClass<Body3D>("Body3D", std::function<Body3D*()>([] { return nullptr; }), true);
    exposeCoupling3DBindings(table);
    vm.addFunc("world", [&] { return &world; });
    vm.addFunc("a", [&] { return a.get(); });
    vm.addFunc("b", [&] { return b.get(); });
    vm.run(vm.compileSource(R"(
        local s = eve.PulleyCoupling3DSettings();
        local made = eve.createPulleyCoupling3D(world(), a(), b(), s);
        assert(made.ok);
        saved <- made.value;
        identity <- saved.getId();
    )"));
    world.destroy();
    vm.run(vm.compileSource(R"(
        assert(!saved.isValid() && saved.getId() == identity);
        assert(!saved.sample().ok && !saved.setEnabled(true).ok);
        assert(saved.destroy().ok);
        assert(!eve.createPulleyCoupling3D(world(), a(), b(), eve.PulleyCoupling3DSettings()).ok);
    )"));
}
