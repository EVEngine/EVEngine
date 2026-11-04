// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"physics"}
#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/WorldSphereSweep.h"
#include "physics/WorldSphereSweepScript.h"
#include "weapon/WorldHitProbe.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace eve;
using namespace eve::weapon;
Duration      seconds(double value) { return Duration::fromSeconds(value).value(); }
CarrierRecipe recipe() {
    CarrierRecipe spec;
    spec.id                = *LogicalId::parse("composition:bolt");
    spec.speed             = 10;
    spec.lifetime          = seconds(2);
    spec.physicsWorldSweep = true;
    spec.motionOps         = {{CarrierMotionOpKind::IntegrateLinear}};
    spec.triggers          = {{CarrierTriggerKind::OnHit}};
    spec.impacts           = {{CarrierImpactKind::EmitHit, CarrierTriggerKind::OnHit}};
    return spec;
}
void realContract(physics::RigidBackend3D backend, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    using namespace eve::physics;
    auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto world   = std::move(created).takeValue();
    auto body    = std::unique_ptr<Body3D>(world->newBody("dynamic", 2, 0, 0));
    auto shape   = std::unique_ptr<Shape3D>(body->newBoxShape(0.2f, 4, 2, 1, 0.3f, 0));
    auto session = worldSphereSweepFactory().bind(world->queryToken(), {0.1f});
    REQUIRE(session.ok());
    WorldHitProbe        probe(std::move(session).takeValue());
    CombatCarrierRuntime runtime;
    REQUIRE(runtime.spawn(recipe(), {{}, {1, 0, 0}}).ok());
    auto frame = runtime.update(seconds(0.4), nullptr, nullptr, nullptr, &probe);
    REQUIRE(frame.ok());
    REQUIRE(frame.value().events.size() == 1);
    const auto contact = *frame.value().events[0].worldContact;
    REQUIRE(contact.worldEpoch == world->queryEpoch());
    REQUIRE(frame.value().events[0].target.id == 0);
    REQUIRE(std::abs(body->getLinearVelocityX()) < 1e-6f);
    const auto mass = body->getMass();
    REQUIRE(mass > 0);
    REQUIRE(world->applySweepContactImpulse(contact, {mass * 2, 0, 0}, {2, 0, 0}).ok());
    REQUIRE(std::abs(body->getLinearVelocityX() - 2) < 1e-4f);
    auto stale = contact;
    ++stale.worldEpoch;
    REQUIRE(!world->applySweepContactImpulse(stale, {mass, 0, 0}, {2, 0, 0}).ok());
    World3D other(0, 0, 0, false);
    REQUIRE(!other.applySweepContactImpulse(contact, {mass, 0, 0}, {2, 0, 0}).ok());
    REQUIRE(!world->applySweepContactImpulse(contact, {std::numeric_limits<float>::infinity(), 0, 0}, {}).ok());
    REQUIRE(!world->applySweepContactImpulse(contact, {}, {0, std::numeric_limits<float>::quiet_NaN(), 0}).ok());
    REQUIRE(std::abs(body->getLinearVelocityX() - 2) < 1e-4f);
    world->destroyBody(body.get());
    auto replacement = std::unique_ptr<Body3D>(world->newBody("dynamic", 2, 0, 0));
    REQUIRE(!world->applySweepContactImpulse(contact, {mass, 0, 0}, {2, 0, 0}).ok());
    world->destroy();
    REQUIRE(!world->applySweepContactImpulse(contact, {mass, 0, 0}, {2, 0, 0}).ok());
}
}  // namespace

TEST_CASE("carrierComposition.explicitPhysicsImpulseBox3D") {
    realContract(eve::physics::RigidBackend3D::Box3D, _ZEROERR_TEST_CONTEXT);
}
#if EVENGINE_HAS_JOLT
TEST_CASE("carrierComposition.explicitPhysicsImpulseJolt") {
    realContract(eve::physics::RigidBackend3D::Jolt, _ZEROERR_TEST_CONTEXT);
}
#endif

TEST_CASE("carrierComposition.scriptImpulseIsExplicitAndValidated") {
    using namespace eve;
    using namespace eve::physics;
    World3D world(0, 0, 0, false);
    auto    body  = std::unique_ptr<Body3D>(world.newBody("dynamic", 2, 0, 0));
    auto    shape = std::unique_ptr<Shape3D>(body->newBoxShape(0.2f, 4, 2, 1, 0.3f, 0));
    auto    hits  = world.sweepSphereOwned(0, 0, 0, 4, 0, 0, {0.1f});
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 1);
    const auto hit = hits.value()[0];
    ssq::VM    vm(1024);
    auto       table = vm.addTable("eve");
    auto       cls   = table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
    cls.addFunc("applySweepContactImpulse",
                [](World3D* self, ssq::Object event, float ix, float iy, float iz, float px, float py, float pz) {
                    return projectSweepContactImpulse(self, event, {ix, iy, iz}, {px, py, pz});
                });
    vm.addFunc("world", [&] { return &world; });
    vm.addFunc("mass", [&] { return body->getMass(); });
    vm.addFunc("velocity", [&] { return body->getLinearVelocityX(); });
    vm.addFunc("destroyBody", [&] { world.destroyBody(body.get()); });
    vm.addFunc("contact", [&] {
        Value::Object event;
        event.emplace("worldIndex", Value(static_cast<std::int64_t>(hit.identity.world & 0xffffffffu)));
        event.emplace("worldGeneration", Value(static_cast<std::int64_t>(hit.identity.world >> 32)));
        event.emplace("bodyIndex", Value(static_cast<std::int64_t>(hit.identity.body & 0xffffffffu)));
        event.emplace("bodyGeneration", Value(static_cast<std::int64_t>(hit.identity.body >> 32)));
        event.emplace("bodyId", Value(static_cast<std::int64_t>(hit.bodyId)));
        event.emplace("worldEpoch", Value(static_cast<std::int64_t>(hit.worldEpoch)));
        return script::projectStatusResult(vm.getHandle(), Status::success(), Value(std::move(event)));
    });
    vm.run(vm.compileSource(R"(
        local event = contact().value;
        assert(velocity() == 0.0);
        assert(!world().applySweepContactImpulse({}, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
        assert(!world().applySweepContactImpulse(42, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
        event.bodyGeneration = -1;
        assert(!world().applySweepContactImpulse(event, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
        event = contact().value;
        event.worldEpoch++;
        assert(!world().applySweepContactImpulse(event, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
        assert(velocity() == 0.0);
        event = contact().value;
        assert(world().applySweepContactImpulse(event, mass()*2.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
        assert(velocity() > 1.999 && velocity() < 2.001);
        destroyBody();
        assert(!world().applySweepContactImpulse(event, 1.0, 0.0, 0.0, 2.0, 0.0, 0.0).ok);
    )"));
}
