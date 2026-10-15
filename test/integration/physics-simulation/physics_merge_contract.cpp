#include <memory>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Joint2D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceBatch.h"
#include "physics/mechanics/Coupling3D.h"
#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::physics;
namespace {
SnapshotHashProvider mergeHash() {
    return [](std::string_view input) -> Result<ContentId> {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : input) hash = (hash ^ c) * 1099511628211ull;
        ContentId::Bytes bytes{};
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
        return Result<ContentId>::success(ContentId(bytes));
    };
}
}  // namespace
TEST_CASE("physics.merge.planarFactoriesShareIdentityAndSnapshot") {
    World                    world(0, 0, false, 30);
    std::unique_ptr<Body>    frame(world.newBody("static", 0, 0));
    std::unique_ptr<Body>    a(world.newBody("dynamic", 30, 0)), b(world.newBody("dynamic", 60, 0));
    std::unique_ptr<Joint2D> pixel(world.newRevoluteJoint(frame.get(), a.get(), 30, 0));
    Joint2DSettings          settings;
    settings.anchorA = {2, 0};
    auto made        = Joint2D::create(world, *frame, *b, settings);
    REQUIRE(made.ok());
    auto si = std::move(made).takeValue();
    CHECK(pixel->getId() != si->getId());
    CHECK(pixel->runtimeHandle() != si->runtimeHandle());
    CHECK(world.findJoint(si->runtimeHandle()) == si.get());
    CHECK(world.findJoint(pixel->runtimeHandle()) == pixel.get());
    std::unique_ptr<Joint2D> gear(world.newGearJoint(pixel.get(), si.get(), 2));
    std::unique_ptr<Joint2D> motor(world.newMotorJoint(frame.get(), a.get()));
    motor->setMotorLinearOffset(45, 15);
    motor->setMotorAngularOffset(.2f);
    motor->setMotorLimits(60, 4);
    const auto hash     = mergeHash();
    auto       snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    const auto retired = si->runtimeHandle();
    REQUIRE(world.restore(snapshot.value(), hash).ok());
    CHECK(world.findJoint(retired) == nullptr);
    CHECK(!pixel->isValid());
    CHECK(!si->isValid());
    CHECK(!gear->isValid());
    auto again = world.snapshot(hash);
    REQUIRE(again.ok());
    CHECK(again.value().payload == snapshot.value().payload);
}
TEST_CASE("physics.merge.spatialAddedJointKindsRetainParameters") {
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("static", 0, 0, 0)), b(world.newBody("dynamic", 0, 1, 0));
    std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
    std::unique_ptr<Joint3D> weld(world.newWeldJoint(a.get(), b.get(), 0, 1, 0));
    std::unique_ptr<Joint3D> motor(world.newMotorJoint(a.get(), b.get()));
    std::unique_ptr<Joint3D> parallel(world.newParallelJoint(a.get(), b.get(), 0, 1, 0));
    std::unique_ptr<Joint3D> filter(world.newFilterJoint(a.get(), b.get()));
    weld->setWeldLinearSpring(3, .4f);
    weld->setWeldAngularSpring(5, .6f);
    motor->setMotorLinearVelocity(1, 2, 3);
    motor->setMotorAngularVelocity(.2f, .3f, .4f);
    motor->setMotorVelocityLimits(10, 20);
    motor->setMotorLinearSpring(4, .5f, 30);
    motor->setMotorAngularSpring(5, .6f, 40);
    parallel->setParallelSpring(2, .3f, 50);
    const auto hash     = mergeHash();
    auto       snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    auto restored = world.restore(snapshot.value(), hash);
    REQUIRE(restored.ok(), "{}", restored.status().describe());
    auto again = world.snapshot(hash);
    REQUIRE(again.ok());
    CHECK(again.value().payload == snapshot.value().payload);
}
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("physics.merge.joltKeepsPortableSnapshotsAndAcceptsForces") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("dynamic", 0, 0, 0)), b(world->newBody("dynamic", 2, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newBoxShape(1, 1, 1)), sb(b->newBoxShape(1, 1, 1));
    auto                     link = PhysicsLink::fromBody(*a);
    REQUIRE(link.ok());
    ForceLoad load{link.value(), {10, 0, 0}, {0, 0, 0}, {}};
    auto      batch = ForceBatch::prepare(*world, std::span<const ForceLoad>(&load, 1));
    REQUIRE(batch.ok());
    auto duration = Duration::fromSeconds(.01);
    REQUIRE(duration.ok());
    SimulationStep step{SimulationTick(1), duration.value()};
    auto           receipt = batch.value().commit(*world, step);
    REQUIRE(receipt.ok());
    REQUIRE(world->step(step, {}).ok());
    CHECK(a->getLinearVelocityX() > 0);
    const auto hash     = mergeHash();
    auto       snapshot = world->snapshot(hash);
    REQUIRE(snapshot.ok());
    REQUIRE(world->restore(snapshot.value(), hash).ok());
    CHECK(!a->isValid());
    auto again = world->snapshot(hash);
    REQUIRE(again.ok());
    CHECK(again.value().payload == snapshot.value().payload);
    auto coupling = Coupling3D::createPulley(*world, *world->findBodyById(1), *world->findBodyById(2), {});
    REQUIRE(!coupling.ok());
    CHECK(coupling.error()->code() == DiagnosticCode::Unsupported);
}
#endif

#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("physics.merge.joltRayTiresRejectWithoutPublishingState") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.81f, 0, false);
    REQUIRE(created.ok());
    auto                       world = std::move(created).takeValue();
    ecs::Table                 table;
    vehicle::VehicleDefinition definition;
    auto*                      entity = vehicle::VehicleEntity::create(table);
    entity->definition()->def         = &definition;
    REQUIRE(vehicle::VehiclePhysics::attach3D(entity, world.get(), 1) == vehicle::VehiclePhysicsStatus::Applied);
    std::unique_ptr<Body3D>  body(world->findBodyById(1));
    std::unique_ptr<Shape3D> shape(world->findShapeById(1));
    auto                     submitted = vehicle::VehiclePhysics::submitWheelStep(
        *entity, SimulationStep{SimulationTick{1}, Duration::fromNanoseconds(10000000)});
    CHECK(!submitted.ok());
    CHECK(submitted.error()->code() == DiagnosticCode::Unsupported);
    CHECK(entity->suspension()->wheels.empty());
    CHECK(body->getLinearVelocityY() == 0);
    CHECK(vehicle::VehiclePhysics::detach(entity) == vehicle::VehiclePhysicsStatus::Applied);
    ecs::DestroyEntity(entity);
}
#endif
