// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "cases": {
    "script-binding": [
      "physics.forces.scriptModelsProjectResults",
      "physics.forces.scriptFieldSetsOwnValuesAndBatchRights",
      "physics.forces.scriptWingAssembliesControlBothDimensions"
    ]
  },
  "module": "physics"
}
*/
// clang-format on

#include <cmath>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("physics.forces.scriptModelsProjectResults") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    table.addClass<eve::physics::Body>("Body", std::function<eve::physics::Body*()>([] { return nullptr; }), true);
    table.addClass<eve::physics::Body3D>("Body3D", std::function<eve::physics::Body3D*()>([] { return nullptr; }),
                                         true);
    eve::physics::exposeForceBindings(table);
    vm.run(vm.compileSource(R"(
        local p = eve.ForceVector(); p.x = 2.0;
        local a = eve.ForceVector(); a.y = 5.0;
        local region = eve.FieldRegion(); region.radius = 10.0;
        local field = eve.DirectionalField(); field.region = region; field.acceleration = a;
        local d = eve.sampleDirectionalForce(field, p, 2.0);
        assert(d.ok && d.value.y == 10.0);
        assert(!eve.sampleDirectionalForce(field, p, -1.0).ok);
        local radial = eve.RadialField(); radial.strength = -10.0;
        local r = eve.sampleRadialForce(radial, p, 2.0);
        assert(r.ok && r.value.x > 0.0);
        radial.maxAcceleration = 0.5; radial.maxForce = 0.25;
        local capped = eve.sampleRadialForce(radial, p, 2.0);
        assert(capped.ok && capped.value.x > 0.249 && capped.value.x < 0.251);
        radial.maxForce = -1.0; assert(!eve.sampleRadialForce(radial, p, 2.0).ok);
        local constant = eve.DirectionalForceField(); constant.force = a;
        local cf = eve.sampleDirectionalConstantForce(constant, p);
        assert(cf.ok && cf.value.y == 5.0);
        assert(!eve.sampleDirectionalConstantForce(field, p).ok);
        region.kind = "box"; region.halfExtents = a; constant.region = region;
        assert(!eve.sampleDirectionalConstantForce(constant, p).ok);
        local extent = eve.ForceVector(); extent.x = 3.0; extent.y = 4.0; extent.z = 5.0;
        region.halfExtents = extent; region.edgeWidth = 2.0; constant.region = region;
        local box = eve.sampleDirectionalConstantForce(constant, p);
        assert(box.ok && box.value.y == 2.5);
        region.kind = "invalid"; constant.region = region;
        assert(!eve.sampleDirectionalConstantForce(constant, p).ok);
        local origin=eve.ForceVector();
        local circle=eve.sampleCircleImmersion2D(origin,1.0,0.5,0.0);
        assert(circle.ok && circle.value.fraction==0.5 && circle.value.centroid.y<0.0);
        local rect=eve.sampleRectangleImmersion2D(origin,2.0,4.0,0.5,0.0);
        assert(rect.ok && rect.value.volume==2.0 && rect.value.centroid.y==-1.0);
        local sphere=eve.sampleSphereImmersion3D(origin,2.0,0.0);
        assert(sphere.ok && sphere.value.fraction==0.5 && sphere.value.centroid.y==-0.75);
        local size=eve.ForceVector(); size.x=2.0; size.y=4.0; size.z=6.0;
        local box=eve.sampleBoxImmersion3D(origin,size,0.0);
        assert(box.ok && box.value.volume==24.0);
        assert(!eve.sampleCircleImmersion2D(origin,1.0,0.0,0.0).ok);
        assert(!eve.sampleBoxImmersion3D(origin,field,0.0).ok);
        local buoyancy = eve.BuoyancyModel(); buoyancy.displacedVolume = 0.1;
        local b = eve.sampleBuoyancyForce(buoyancy); assert(b.ok && b.value.y > 900.0);
        local damping = eve.DampingModel(); damping.linear = 10.0;
        local drag = eve.sampleDampingForce(damping, p, 1.0, 0.01);
        assert(drag.ok && drag.value.x < 0.0);
        local angular=eve.AngularDampingModel(); angular.linear=10.0;
        local torque=eve.sampleAngularDamping2D(angular,4.0,2.0,0.1);
        assert(torque.ok && torque.value<0.0 && 4.0+torque.value*0.1/2.0>=0.0);
        local inertia=eve.ForceVector(); inertia.x=1.0; inertia.y=2.0; inertia.z=3.0;
        local torque3=eve.sampleAngularDamping3D(angular,p,inertia,0.1);
        assert(torque3.ok && torque3.value.x<0.0);
        assert(!eve.sampleAngularDamping3D(damping,p,inertia,0.1).ok);
        assert(!eve.sampleAngularDamping2D(angular,4.0,0.0,0.1).ok);
        local wing = eve.WingModel(); p.x = 20.0; p.y = -2.0;
        local w = eve.sampleWingForce(wing, p); assert(w.ok && w.value.force.y > 0.0);
        local friction = eve.FrictionModel(); local zero = eve.ForceVector();
        local f = eve.sampleFrictionForce(friction, zero, p, 100.0);
        assert(f.ok && f.value.x == -20.0);
        local tire = eve.TireModel(); local input = eve.TireInput(); input.driveTorque = 10.0;
        local t = eve.stepTire(tire, input, 0.01); assert(t.ok && t.value.angularVelocity > 0.0);
        assert(input.angularVelocity == 0.0);
        input.normalLoad=1000.0; input.longitudinalInverseMass=0.01; input.lateralInverseMass=0.01;
        local driven=eve.stepTire(tire,input,0.02); assert(driven.ok && driven.value.longitudinalForce>0.0);
        input.brakeTorque=100.0;
        local held=eve.stepTire(tire,input,0.02); assert(held.ok && held.value.angularVelocity==0.0 && held.value.resistanceTorque<0.0);
        input.longitudinalInverseMass=-1.0; assert(!eve.stepTire(tire,input,0.02).ok);
        input.longitudinalInverseMass=0.01;
        assert(!eve.stepTire(tire, input, 0.0).ok);
        assert(!eve.sampleWingForce(null, p).ok);
        assert(!eve.sampleWingForce(p, p).ok);
        assert(!eve.sampleWingForce(wing, wing).ok);
        assert(!eve.sampleDirectionalForce(radial, p, 1.0).ok);
        assert(!eve.applyForceSI2D(wing, 0.0, 0.0, 0.0, 0.0).ok);
        assert(!eve.applyForceSI2D(null, 0.0, 0.0, 0.0, 0.0).ok);
        assert(!eve.applyForceSI3D(null, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0).ok);
    )"));
}

TEST_CASE("physics.forces.scriptFieldSetsOwnValuesAndBatchRights") {
    using namespace eve::physics;
    World                    w2(0, 0, false, 30);
    World3D                  w3(0, 0, 0, false);
    std::unique_ptr<Body>    b2(w2.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> f2(b2->newCircleFixture(30));
    std::unique_ptr<Body3D>  b3(w3.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> f3(b3->newSphereShape(1));
    ssq::VM                  vm(1024);
    auto                     table = vm.addTable("eve");
    table.addClass<World>("World", std::function<World*()>([] { return nullptr; }), true);
    table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
    table.addClass<Body>("Body", std::function<Body*()>([] { return nullptr; }), true);
    table.addClass<Body3D>("Body3D", std::function<Body3D*()>([] { return nullptr; }), true);
    exposeForceBindings(table);
    vm.addFunc("world2", [&] { return &w2; });
    vm.addFunc("world3", [&] { return &w3; });
    vm.addFunc("body2", [&] { return b2.get(); });
    vm.addFunc("body3", [&] { return b3.get(); });
    vm.run(vm.compileSource(R"(
        local builder=eve.FieldSetBuilder();
        local model=eve.DirectionalField(); local accel=eve.ForceVector(); accel.x=2.0; model.acceleration=accel;
        assert(eve.addDirectionalField(builder,model,"9223372036854775808","0").ok);
        assert(!eve.addDirectionalField(builder,model,"18446744073709551616","0").ok);
        assert(!eve.addDirectionalField(builder,model,1,"0").ok);
        assert(!eve.addDirectionalField(builder,model,"1","-1").ok);
        assert(!eve.addDirectionalField(model,model,"1","0").ok);
        local bad=eve.RadialField(); bad.softening=-1.0;
        assert(!eve.addRadialField(builder,bad,"1","0").ok);
        local constant=eve.DirectionalForceField(); local force=eve.ForceVector(); force.y=3.0; constant.force=force;
        assert(eve.addDirectionalConstantField(builder,constant,"9223372036854775808","0").ok);
        local radial=eve.RadialField(); radial.strength=2.0; radial.softening=1.0;
        local region=eve.FieldRegion(); local center=eve.ForceVector(); center.x=1.0; region.center=center; radial.region=region;
        assert(eve.addRadialField(builder,radial,"9223372036854775808","0").ok);
        local result=eve.buildFieldSet(builder); assert(result.ok);
        fieldSet <- result.value;
        // The immutable set keeps copied values after the caller changes models and builder.
        accel.x=100.0; model.acceleration=accel;
        assert(eve.addDirectionalField(builder,model,"9223372036854775808","0").ok);
        local targets2=eve.FieldTargets();
        targets3 <- eve.FieldTargets();
        assert(eve.addFieldTarget2D(targets2,body2(),"9223372036854775808").ok);
        assert(eve.addFieldTarget3D(targets3,body3(),"9223372036854775808").ok);
        assert(!eve.addFieldTarget2D(targets2,body3(),"1").ok);
        assert(!eve.addFieldTarget3D(targets3,body3(),"-1").ok);
        local p2=eve.prepareFieldForces2D(fieldSet,world2(),targets2); assert(p2.ok);
        local p3=eve.prepareFieldForces3D(fieldSet,world3(),targets3); assert(p3.ok);
        assert(!eve.commitForceBatch3D(p2.value,world3(),"1","10000000").ok);
        local c2=eve.commitForceBatch2D(p2.value,world2(),"1","10000000"); assert(c2.ok);
        receipt2 <- c2.value;
        assert(eve.forceReceiptStatus(receipt2).value=="pending");
        assert(!eve.commitForceBatch2D(p2.value,world2(),"1","10000000").ok);
        assert(!eve.commitForceBatch3D(p3.value,world3(),1,"10000000").ok);
        assert(!eve.commitForceBatch3D(p3.value,world3(),"1","0").ok);
        assert(!eve.commitForceBatch3D(p3.value,world3(),"1","50000001").ok);
        local c3=eve.commitForceBatch3D(p3.value,world3(),"1","10000000"); assert(c3.ok);
        receipt3 <- c3.value;
        assert(!eve.forceReceiptStatus(model).ok);
        assert(!eve.stepPhysicsWorld3D(world3(),"1","20000000",4).ok);
        assert(!eve.stepPhysicsWorld2D(world2(),"2","10000000",8,3).ok);
        assert(!eve.stepPhysicsWorld3D(world3(),"1","10000000",0).ok);
        assert(eve.forceReceiptStatus(receipt3).value=="pending");
        assert(!eve.prepareFieldForces2D(fieldSet,world3(),targets2).ok);
        assert(!eve.commitForceBatch3D(model,world3(),"1","10000000").ok);
        pending <- eve.prepareFieldForces3D(fieldSet,world3(),targets3).value;
    )"));
    vm.run(vm.compileSource(R"(
        assert(eve.stepPhysicsWorld2D(world2(),"1","10000000",8,3).ok);
        assert(eve.stepPhysicsWorld3D(world3(),"1","10000000",4).ok);
        assert(eve.forceReceiptStatus(receipt2).value=="applied");
        assert(eve.forceReceiptStatus(receipt3).value=="applied");
        local batch=eve.prepareFieldForces3D(fieldSet,world3(),targets3); assert(batch.ok);
        cancelledReceipt <- eve.commitForceBatch3D(batch.value,world3(),"2","10000000").value;
    )"));
    REQUIRE(std::abs(b2->getLinearVelocityX() / 30 - .0270710678) < 1e-5);
    REQUIRE(std::abs(b3->getLinearVelocityX() - .0270710678) < 1e-5);
    REQUIRE(std::abs(b2->getLinearVelocityY() / 30 - 3 * .01 / b2->getMass()) < 1e-5);
    REQUIRE(std::abs(b3->getLinearVelocityY() - 3 * .01 / b3->getMass()) < 1e-5);
    b3->destroy();
    vm.run(vm.compileSource(R"(
        assert(!eve.commitForceBatch3D(pending,world3(),"1","10000000").ok);
        assert(!eve.prepareFieldForces3D(fieldSet,world3(),targets3).ok);
    )"));
    w3.destroy();
    vm.run(vm.compileSource(R"(
        assert(!eve.commitForceBatch3D(pending,world3(),"1","10000000").ok);
        assert(eve.forceReceiptStatus(cancelledReceipt).value=="cancelled");
        assert(eve.forceReceiptStatus(receipt3).value=="applied");
        pending=null; targets3=null; fieldSet=null; collectgarbage();
    )"));
}

TEST_CASE("physics.forces.scriptWingAssembliesControlBothDimensions") {
    using namespace eve::physics;
    World                    w2(0, 0, false, 30);
    World3D                  w3(0, 0, 0, false);
    std::unique_ptr<Body>    b2(w2.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> f2(b2->newCircleFixture(30));
    std::unique_ptr<Body3D>  b3(w3.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> s3(b3->newSphereShape(1));
    b2->setLinearVelocity(600, 0);
    b3->setLinearVelocity(20, 0, 0);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    table.addClass<World>("World", std::function<World*()>([] { return nullptr; }), true);
    table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
    table.addClass<Body>("Body", std::function<Body*()>([] { return nullptr; }), true);
    table.addClass<Body3D>("Body3D", std::function<Body3D*()>([] { return nullptr; }), true);
    exposeForceBindings(table);
    vm.addFunc("w2", [&] { return &w2; });
    vm.addFunc("w3", [&] { return &w3; });
    vm.addFunc("b2", [&] { return b2.get(); });
    vm.addFunc("b3", [&] { return b3.get(); });
    vm.run(vm.compileSource(R"(
        local builder=eve.WingAssemblyBuilder();
        local model=eve.WingModel(); model.area=0.1;
        local center=eve.ForceVector(); center.x=1.0;
        assert(eve.addWingSurface(builder,model,center).ok);
        model.area=0.0;
        assert(!eve.addWingSurface(builder,model,center).ok);
        local built=eve.buildWingAssembly(builder); assert(built.ok);
        assembly <- built.value;
        controls <- eve.WingControls();
        assert(eve.addWingDeflection(controls,0.15).ok);
        assert(!eve.setWingDeflection(controls,-1,0.0).ok);
        assert(!eve.setWingDeflection(controls,1,0.0).ok);
        wind <- eve.ForceVector();
        local p2=eve.prepareWingForces2D(assembly,w2(),b2(),controls,wind); assert(p2.ok);
        local p3=eve.prepareWingForces3D(assembly,w3(),b3(),controls,wind); assert(p3.ok);
        assert(!eve.prepareWingForces2D(assembly,w2(),b3(),controls,wind).ok);
        assert(!eve.prepareWingForces3D(model,w3(),b3(),controls,wind).ok);
        assert(eve.commitForceBatch2D(p2.value,w2(),"1","1000000").ok);
        assert(eve.commitForceBatch3D(p3.value,w3(),"1","1000000").ok);
    )"));
    REQUIRE(w2.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
    REQUIRE(w3.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
    CHECK(b2->getLinearVelocityY() > 0);
    CHECK(b2->getAngularVelocity() > 0);
    CHECK(b3->getLinearVelocityY() > 0);
    CHECK(b3->getAngularVelocityZ() > 0);
    const float vy = b3->getLinearVelocityY();
    vm.run(vm.compileSource(R"(
        assert(eve.setWingDeflection(controls,0,-0.15).ok);
        local p=eve.prepareWingForces3D(assembly,w3(),b3(),controls,wind); assert(p.ok);
        assert(eve.commitForceBatch3D(p.value,w3(),"2","1000000").ok);
    )"));
    REQUIRE(w3.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(1000000)}).ok());
    CHECK(b3->getLinearVelocityY() < vy);
    vm.run(vm.compileSource(R"(
        pendingWing <- eve.prepareWingForces3D(assembly,w3(),b3(),controls,wind).value;
    )"));
    b3->destroy();
    vm.run(vm.compileSource(R"(
        assert(!eve.commitForceBatch3D(pendingWing,w3(),"1","1000000").ok);
    )"));
}
