#include <Box2D/Box2D.h>
#include <cmath>
#include <limits>
#include <memory>
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/PhysicsLink.h"
#include "physics/World.h"
#include "physics/mechanics/Joint2D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::physics;
namespace {
struct Rig {
    World                    world{0, 0, false, 30};
    std::unique_ptr<Body>    ground{world.newBody("static", 0, 0)};
    std::unique_ptr<Body>    a{world.newBody("dynamic", -60, 0)};
    std::unique_ptr<Body>    b{world.newBody("dynamic", 60, 0)};
    std::unique_ptr<Fixture> fa{a->newCircleFixture(15)};
    std::unique_ptr<Fixture> fb{b->newCircleFixture(15)};
    void                     step(int count = 120) {
        for (int i = 0; i < count; ++i) world.update(1.f / 120);
    }
};
std::unique_ptr<Joint2D> create(Rig& rig, Body& a, Body& b, const Joint2DSettings& s) {
    auto result = Joint2D::create(rig.world, a, b, s);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
float coordinate(const Joint2D& j) {
    auto result = j.sample(1.f / 120);
    REQUIRE(result.ok());
    return result.value().coordinate;
}
}  // namespace
TEST_CASE("physics.joint2d.revoluteMotorLimitsAndBodyTeardown") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA  = {-2, 0};
    auto joint = create(r, *r.ground, *r.a, s);
    REQUIRE(joint->setMotor(true, 2, 100).ok());
    REQUIRE(joint->setLimits(true, -0.4f, 0.4f).ok());
    r.step();
    CHECK((coordinate(*joint) > 0.35f && coordinate(*joint) < 0.45f));
    const auto handle = joint->runtimeHandle();
    r.a.reset();
    CHECK(!joint->isValid());
    CHECK(joint->runtimeHandle().isInvalid());
    CHECK(!handle.isInvalid());
    CHECK(!joint->setMotor(true, 1, 1).ok());
    CHECK(joint->destroyChecked().ok());
}
TEST_CASE("physics.joint2d.prismaticDistanceWheelAndWeld") {
    for (auto kind : {Joint2DKind::Prismatic, Joint2DKind::Distance, Joint2DKind::Wheel, Joint2DKind::Weld}) {
        Rig             r;
        Joint2DSettings s;
        s.kind     = kind;
        s.anchorA  = {0, 0};
        s.anchorB  = {-2, 0};
        s.length   = 2;
        auto joint = create(r, *r.ground, *r.a, s);
        if (kind == Joint2DKind::Prismatic) {
            REQUIRE(joint->setLimits(true, 0, 0.5f).ok());
            REQUIRE(joint->setMotor(true, 1, 100).ok());
        } else if (kind == Joint2DKind::Wheel) {
            REQUIRE(joint->setMotor(true, 1, 100).ok());
        }
        r.a->applyLinearImpulse(0, 30);
        r.step();
        if (kind == Joint2DKind::Distance) CHECK(std::abs(coordinate(*joint) - 2) < 0.01);
        if (kind == Joint2DKind::Prismatic) CHECK(std::abs(coordinate(*joint) - 0.5) < 0.01);
        if (kind == Joint2DKind::Weld) CHECK(std::abs(r.a->getY()) < 0.1);
        if (kind == Joint2DKind::Wheel) CHECK_GT(std::abs(r.a->getAngularVelocity()), 0.1);
        REQUIRE(joint->destroyChecked().ok());
        CHECK_EQ(r.world.raw()->GetJointCount(), 0);
    }
}
TEST_CASE("physics.joint2d.pulleyConservesWeightedLength") {
    Rig             r;
    Joint2DSettings s;
    s.kind     = Joint2DKind::Pulley;
    s.anchorA  = {-2, 0};
    s.anchorB  = {2, 0};
    s.groundA  = {-2, 3};
    s.groundB  = {2, 3};
    s.ratio    = 2;
    auto joint = create(r, *r.a, *r.b, s);
    r.a->setLinearVelocity(0, -30);
    r.step();
    auto state = joint->sample(1.f / 120);
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().coordinate + 2 * state.value().secondaryCoordinate - 9) < 0.02);
    CHECK_GT(r.b->getY(), 0);
    CHECK(!joint->setMotor(true, 1, 1).ok());
}
TEST_CASE("physics.joint2d.gearRatioAndSourceDestruction") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA   = {-2, 0};
    auto first  = create(r, *r.ground, *r.a, s);
    s.anchorA   = {2, 0};
    auto second = create(r, *r.ground, *r.b, s);
    auto result = Joint2D::createGear(r.world, *first, *second, 2);
    REQUIRE(result.ok());
    auto gear = std::move(result).takeValue();
    REQUIRE(first->setMotor(true, 2, 100).ok());
    r.step();
    CHECK_GT(coordinate(*first), 1);
    CHECK(std::abs(coordinate(*first) + 2 * coordinate(*second)) < 0.01);
    REQUIRE(first->destroyChecked().ok());
    CHECK(!gear->isValid());
    CHECK(second->isValid());
    CHECK_EQ(r.world.raw()->GetJointCount(), 1);
}
TEST_CASE("physics.joint2d.gearFirstAndWorldFirstDestruction") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA   = {-2, 0};
    auto first  = create(r, *r.ground, *r.a, s);
    s.anchorA   = {2, 0};
    auto second = create(r, *r.ground, *r.b, s);
    auto result = Joint2D::createGear(r.world, *first, *second, -1);
    REQUIRE(result.ok());
    auto gear = std::move(result).takeValue();
    gear.reset();
    CHECK((first->isValid() && second->isValid()));
    r.world.destroy();
    CHECK((!first->isValid() && !second->isValid()));
    CHECK(!first->sample(0.01f).ok());
}
TEST_CASE("physics.joint2d.invalidInputDoesNotCreateConstraints") {
    Rig             r, other;
    Joint2DSettings s;
    s.kind = Joint2DKind::Prismatic;
    s.axis = {};
    CHECK(!Joint2D::create(r.world, *r.ground, *r.a, s).ok());
    s.axis = {1, 0};
    CHECK(!Joint2D::create(r.world, *r.ground, *other.a, s).ok());
    CHECK(!Joint2D::create(r.world, *r.a, *r.a, s).ok());
    s.kind = Joint2DKind::Gear;
    CHECK(!Joint2D::create(r.world, *r.a, *r.b, s).ok());
    s.kind  = Joint2DKind::Pulley;
    s.ratio = 1e-12f;
    CHECK(!Joint2D::create(r.world, *r.a, *r.b, s).ok());
    CHECK_EQ(r.world.raw()->GetJointCount(), 0);
    s           = {};
    auto first  = create(r, *r.ground, *r.a, s);
    auto second = create(r, *r.ground, *r.b, s);
    r.b->setAngle(2.f);
    CHECK(!Joint2D::createGear(r.world, *first, *second, std::numeric_limits<float>::max()).ok());
    CHECK_EQ(r.world.raw()->GetJointCount(), 2);
}

TEST_CASE("physics.joint2d.linkRejectsForeignWorldAndDestroyedJoint") {
    Rig             r, other;
    Joint2DSettings s;
    s.anchorA   = {-2, 0};
    auto joint  = create(r, *r.ground, *r.a, s);
    auto linked = PhysicsJointLink::fromJoint(*joint);
    REQUIRE(linked.ok());
    const auto link     = linked.value();
    auto       resolved = link.resolve(r.world);
    REQUIRE(resolved.ok());
    CHECK_EQ(&resolved.value().get(), joint.get());
    CHECK(!link.resolve(other.world).ok());
    REQUIRE(joint->destroyChecked().ok());
    CHECK(!link.resolve(r.world).ok());
    auto replacement = create(r, *r.ground, *r.a, s);
    CHECK(!link.resolve(r.world).ok());
    CHECK(replacement->runtimeHandle() != link.joint);
}

TEST_CASE("physics.joint2d.bodyDestructionInvalidatesDependentGear") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA   = {-2, 0};
    auto first  = create(r, *r.ground, *r.a, s);
    s.anchorA   = {2, 0};
    auto second = create(r, *r.ground, *r.b, s);
    auto made   = Joint2D::createGear(r.world, *first, *second, 2);
    REQUIRE(made.ok());
    auto gear = std::move(made).takeValue();
    r.a->destroy();
    CHECK(!first->isValid());
    CHECK(!gear->isValid());
    CHECK(second->isValid());
    CHECK_EQ(r.world.raw()->GetJointCount(), 1);
}

TEST_CASE("physics.joint2d.lockedWorldRejectsMutation") {
    Rig             r;
    Joint2DSettings settings;
    settings.anchorA = {-2, 0};
    auto joint       = create(r, *r.ground, *r.a, settings);
    struct Listener : b2ContactListener {
        Rig&     rig;
        Joint2D& joint;
        bool     called = false, createRejected = false, destroyRejected = false, motorRejected = false;
        Listener(Rig& r, Joint2D& j) : rig(r), joint(j) {}
        void BeginContact(b2Contact*) override {
            called          = true;
            auto created    = Joint2D::create(rig.world, *rig.ground, *rig.b, {});
            createRejected  = !created.ok();
            destroyRejected = !joint.destroyChecked().ok();
            motorRejected   = !joint.setMotor(true, 1, 10).ok();
        }
    } listener(r, *joint);
    r.world.raw()->SetContactListener(&listener);
    r.b->setPosition(-60, 0);
    r.step(1);
    r.world.raw()->SetContactListener(nullptr);
    CHECK(listener.called);
    CHECK(listener.createRejected);
    CHECK(listener.destroyRejected);
    CHECK(listener.motorRejected);
    CHECK(joint->isValid());
    CHECK_EQ(r.world.raw()->GetJointCount(), 1);
}
