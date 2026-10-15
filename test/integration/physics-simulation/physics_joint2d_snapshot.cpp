#include <Box2D/Box2D.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Snapshot.h"
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/PhysicsLink.h"
#include "physics/World.h"
#include "physics/mechanics/Joint2D.h"
#include "physics/mechanics/Joint2DBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
using namespace eve;
using namespace eve::physics;
SnapshotHashProvider digest() {
    return [](std::string_view input) -> Result<ContentId> {
        ContentId::Bytes bytes{};
        std::uint64_t    hash = 14695981039346656037ull;
        for (unsigned char c : input) hash = (hash ^ c) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
        return Result<ContentId>::success(ContentId(bytes));
    };
}
struct Rig {
    World                    world{0, 0, false, 30};
    std::unique_ptr<Body>    ground{world.newBody("static", 0, 0)};
    std::unique_ptr<Body>    a{world.newBody("dynamic", -60, 0)}, b{world.newBody("dynamic", 60, 0)};
    std::unique_ptr<Fixture> fa{a->newCircleFixture(15)}, fb{b->newCircleFixture(15)};
    std::unique_ptr<Joint2D> make(Body& first, Body& second, Joint2DSettings s) {
        auto j = Joint2D::create(world, first, second, s);
        REQUIRE(j.ok());
        return std::move(j).takeValue();
    }
};
SnapshotEnvelope capture(World& w) {
    auto value = w.snapshot(digest());
    REQUIRE(value.ok(), "{}", value.status().describe());
    return std::move(value).takeValue();
}
SnapshotEnvelope reseal(const SnapshotEnvelope& original, Value payload, std::uint64_t version = 3) {
    auto value = makeSnapshotEnvelope(original.type, original.schema, SchemaVersion(version), original.instanceId,
                                      original.revision, original.tick, std::move(payload), digest());
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}
float coordinate(const Joint2D& j) {
    auto sampled = j.sample(1.f / 120);
    REQUIRE(sampled.ok());
    return sampled.value().coordinate;
}
}  // namespace

TEST_CASE("physics.joint2d.snapshotAllKindsPreserveParametersAndInvalidateLinks") {
    for (auto kind : {Joint2DKind::Revolute, Joint2DKind::Prismatic, Joint2DKind::Distance, Joint2DKind::Pulley,
                      Joint2DKind::Wheel, Joint2DKind::Weld}) {
        Rig r;
        r.a->setAngle(0.3f);
        r.b->setAngle(-0.2f);
        Joint2DSettings s;
        s.kind             = kind;
        s.anchorA          = {-2, 0};
        s.anchorB          = {2, 0};
        s.groundA          = {-2, 4};
        s.groundB          = {2, 4};
        s.length           = 4;
        s.ratio            = 1.75f;
        s.springHertz      = 3;
        s.dampingRatio     = 0.4f;
        s.axis             = {1, 2};
        s.collideConnected = true;
        auto joint         = r.make(*r.a, *r.b, s);
        if (kind == Joint2DKind::Revolute || kind == Joint2DKind::Prismatic || kind == Joint2DKind::Wheel)
            REQUIRE(joint->setMotor(true, 1.2f, 7.5f).ok());
        if (kind == Joint2DKind::Revolute || kind == Joint2DKind::Prismatic)
            REQUIRE(joint->setLimits(true, -0.7f, 0.9f).ok());
        const int  id      = joint->getId();
        const auto oldLink = PhysicsJointLink::fromJoint(*joint);
        REQUIRE(oldLink.ok());
        const auto saved = capture(r.world);
        REQUIRE_EQ(saved.schemaVersion.value(), std::uint64_t{3});
        auto restored = r.world.restore(saved, digest());
        REQUIRE(restored.ok(), "{}", restored.status().describe());
        CHECK(!joint->isValid());
        CHECK(!oldLink.value().resolve(r.world).ok());
        auto rebuilt = r.world.resolveJoint2D(id);
        REQUIRE(rebuilt.ok());
        CHECK(rebuilt.value().get().kind() == kind);
        CHECK(capture(r.world).payload == saved.payload);
        // Repeated restoration cannot reuse a published runtime link.
        auto rebuiltLink = PhysicsJointLink::fromJoint(rebuilt.value().get());
        REQUIRE(rebuiltLink.ok());
        REQUIRE(r.world.restore(saved, digest()).ok());
        CHECK(!rebuiltLink.value().resolve(r.world).ok());
        CHECK(capture(r.world).payload == saved.payload);
    }
}

TEST_CASE("physics.joint2d.snapshotRestoredMotorActuallyDrivesAndHonorsLimit") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA       = {-2, 0};
    auto      joint = r.make(*r.ground, *r.a, s);
    const int id    = joint->getId();
    REQUIRE(joint->setMotor(true, 2, 100).ok());
    REQUIRE(joint->setLimits(true, -0.4f, 0.4f).ok());
    const auto saved = capture(r.world);
    REQUIRE(r.world.restore(saved, digest()).ok());
    for (int i = 0; i < 120; ++i) r.world.update(1.f / 120);
    auto rebuilt = r.world.resolveJoint2D(id);
    REQUIRE(rebuilt.ok());
    const float angle = coordinate(rebuilt.value().get());
    CHECK((angle > 0.35f && angle < 0.45f));
}

TEST_CASE("physics.joint2d.snapshotGearPreservesOriginalPhaseAndDependencyTeardown") {
    for (auto secondKind : {Joint2DKind::Revolute, Joint2DKind::Prismatic}) {
        Rig             r;
        Joint2DSettings s;
        s.anchorA = {-2, 0};
        auto a    = r.make(*r.ground, *r.a, s);
        s.kind    = secondKind;
        s.anchorA = {2, 0};
        auto b    = r.make(*r.ground, *r.b, s);
        r.a->setAngle(0.4f);
        if (secondKind == Joint2DKind::Revolute)
            r.b->setAngle(-0.25f);
        else
            r.b->setPosition(75, 0);
        auto gearResult = Joint2D::createGear(r.world, *a, *b, -2.f);
        REQUIRE(gearResult.ok());
        auto        gear  = std::move(gearResult).takeValue();
        const float phase = coordinate(*a) - 2 * coordinate(*b);
        const int   aId = a->getId(), bId = b->getId(), gearId = gear->getId();
        // Save out of equilibrium. Rebuilding at the saved pose would silently change the gear phase.
        r.a->setAngle(0.9f);
        auto  saved   = capture(r.world);
        auto  payload = saved.payload;
        auto& list    = *payload.find("joints")->getIf<Value::Array>();
        std::reverse(list.begin(), list.end());
        auto reordered = reseal(saved, std::move(payload));
        auto restored  = r.world.restore(reordered, digest());
        REQUIRE(restored.ok(), "{}", restored.status().describe());
        CHECK(capture(r.world).payload == saved.payload);
        auto ra = r.world.resolveJoint2D(aId), rb = r.world.resolveJoint2D(bId), rg = r.world.resolveJoint2D(gearId);
        REQUIRE(ra.ok());
        REQUIRE(rb.ok());
        REQUIRE(rg.ok());
        for (int i = 0; i < 120; ++i) r.world.update(1.f / 120);
        CHECK(std::abs(coordinate(ra.value().get()) - 2 * coordinate(rb.value().get()) - phase) < 0.015f);
        auto link = PhysicsJointLink::fromJoint(rg.value().get());
        REQUIRE(link.ok());
        REQUIRE(ra.value().get().destroyChecked().ok());
        CHECK(!rg.value().get().isValid());
        CHECK(!link.value().resolve(r.world).ok());
        CHECK(rb.value().get().isValid());
    }
}

TEST_CASE("physics.joint2d.snapshotRejectsInvalidTopologyAtomically") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA = {-2, 0};
    auto a    = r.make(*r.ground, *r.a, s);
    s.anchorA = {2, 0};
    auto b    = r.make(*r.ground, *r.b, s);
    auto gear = Joint2D::createGear(r.world, *a, *b, 2);
    REQUIRE(gear.ok());
    const auto saved = capture(r.world);
    auto       link  = PhysicsJointLink::fromJoint(*a);
    REQUIRE(link.ok());
    for (int fault = 0; fault < 15; ++fault) {
        auto  payload = saved.payload;
        auto& joints  = *payload.find("joints")->getIf<Value::Array>();
        switch (fault) {
            case 0: joints[1].set("id", *joints[0].find("id")); break;
            case 1: joints[0].set("bodyA", Value("99999")); break;
            case 2: joints[0].set("future", Value(true)); break;
            case 3: joints[0].find("data")->set("maximum", Value(-1.f)); break;
            case 4: joints[2].find("data")->set("first", *joints[2].find("id")); break;
            case 5: joints[2].find("data")->set("ratio", Value(0.f)); break;
            case 6: joints[2].find("data")->find("referencePoses")->getIf<Value::Array>()->at(6) = Value(4.f); break;
            case 7: joints[2].find("data")->set("future", Value(true)); break;
            case 8: joints[0].set("id", Value("bad-id")); break;
            case 9: joints[0].set("kind", Value(true)); break;
            case 10: joints[2].find("data")->set("first", Value(true)); break;
            case 11: joints[2].find("data")->set("second", Value("bad-id")); break;
            case 12: payload.find("bodies")->getIf<Value::Array>()->at(0).set("id", Value("2147483647")); break;
            case 13:
                payload.find("bodies")
                    ->getIf<Value::Array>()
                    ->at(1)
                    .find("fixtures")
                    ->getIf<Value::Array>()
                    ->at(0)
                    .set("type", Value(true));
                break;
            case 14:
                payload.find("bodies")
                    ->getIf<Value::Array>()
                    ->at(1)
                    .find("fixtures")
                    ->getIf<Value::Array>()
                    ->at(0)
                    .set("radius", Value("bad-radius"));
                break;
        }
        auto invalid  = reseal(saved, std::move(payload));
        auto restored = r.world.restore(invalid, digest());
        CHECK(!restored.ok());
        CHECK(a->isValid());
        CHECK(b->isValid());
        CHECK(gear.value()->isValid());
        CHECK(link.value().resolve(r.world).ok());
        CHECK(capture(r.world).payload == saved.payload);
    }
}

TEST_CASE("physics.joint2d.snapshotMigratesV2AndRejectsV1AndUnmanagedJoints") {
    Rig  r;
    auto saved   = capture(r.world);
    auto payload = saved.payload;
    payload.getIf<Value::Object>()->erase("joints");
    payload.getIf<Value::Object>()->erase("allowSleep");
    for (auto& body : *payload.find("bodies")->getIf<Value::Array>()) {
        body.getIf<Value::Object>()->erase("dynamics");
        for (auto& fixture : *body.find("fixtures")->getIf<Value::Array>())
            fixture.getIf<Value::Object>()->erase("material");
    }
    auto legacy = reseal(saved, payload, 2);
    REQUIRE(r.world.restore(legacy, digest()).ok());
    CHECK(capture(r.world).payload == saved.payload);
    auto obsolete = reseal(saved, payload, 1);
    auto rejected = r.world.restore(obsolete, digest());
    REQUIRE(!rejected.ok());
    CHECK(rejected.error()->code() == DiagnosticCode::UnknownVersion);
    // A direct backend joint must not disappear silently from a snapshot.
    b2BodyDef bodyDef;
    bodyDef.type              = b2_dynamicBody;
    auto*              first  = r.world.raw()->CreateBody(&bodyDef);
    auto*              second = r.world.raw()->CreateBody(&bodyDef);
    b2DistanceJointDef jointDef;
    jointDef.bodyA    = first;
    jointDef.bodyB    = second;
    auto* rawJoint    = r.world.raw()->CreateJoint(&jointDef);
    auto  unsupported = r.world.snapshot(digest());
    REQUIRE(!unsupported.ok());
    CHECK(unsupported.error()->code() == DiagnosticCode::Unsupported);
    r.world.raw()->DestroyJoint(rawJoint);
    r.world.raw()->DestroyBody(first);
    r.world.raw()->DestroyBody(second);
}

TEST_CASE("physics.joint2d.snapshotRejectsSolverCallbackReentrancy") {
    Rig        r;
    const auto saved = capture(r.world);
    struct Listener : b2ContactListener {
        World&                  world;
        const SnapshotEnvelope& saved;
        bool                    called = false, captureRejected = false, restoreRejected = false;
        Listener(World& w, const SnapshotEnvelope& snapshot) : world(w), saved(snapshot) {}
        void BeginContact(b2Contact*) override {
            called          = true;
            auto captured   = world.snapshot(digest());
            auto restored   = world.restore(saved, digest());
            captureRejected = !captured.ok() && captured.error()->code() == DiagnosticCode::PreconditionViolation;
            restoreRejected = !restored.ok() && restored.error()->code() == DiagnosticCode::PreconditionViolation;
        }
    } listener(r.world, saved);
    r.world.raw()->SetContactListener(&listener);
    r.b->setPosition(-60, 0);
    r.world.update(1.f / 120);
    r.world.raw()->SetContactListener(nullptr);
    CHECK(listener.called);
    CHECK(listener.captureRejected);
    CHECK(listener.restoreRejected);
    CHECK(r.a->isValid());
    CHECK(r.b->isValid());
}

TEST_CASE("physics.joint2d.snapshotScriptReferencesObserveRestoreAndWorldLifetime") {
    Rig             r;
    Joint2DSettings s;
    s.anchorA        = {-2, 0};
    auto joint       = r.make(*r.ground, *r.a, s);
    s.anchorA        = {2, 0};
    auto       other = r.make(*r.ground, *r.b, s);
    const auto saved = capture(r.world);
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World>("World", std::function<World*()>([] { return nullptr; }), true);
        table.addClass<Body>("Body", std::function<Body*()>([] { return nullptr; }), true);
        exposeJoint2DBindings(table);
        vm.addFunc("world", [&] { return &r.world; });
        vm.addFunc("jointId", [&] { return joint->getId(); });
        vm.addFunc("otherId", [&] { return other->getId(); });
        vm.addFunc("restore", [&] {
            auto result = r.world.restore(saved, digest());
            REQUIRE(result.ok());
        });
        vm.run(vm.compileSource(R"(
            local found = eve.resolveJoint2D(world(), jointId());
            assert(found.ok && found.hasValue);
            observed <- found.value;
            assert(observed.isValid() && observed.getId() == jointId());
            restore();
            assert(!observed.isValid());
            assert(!observed.sample(0.01).ok);
            assert(!observed.setMotor(true, 1.0, 10.0).ok);
            observed = eve.resolveJoint2D(world(), jointId()).value;
            assert(observed.isValid());
            assert(observed.setMotor(true, 1.0, 10.0).ok);
            local other = eve.resolveJoint2D(world(), otherId()).value;
            local gear = eve.createGearJoint2D(world(), observed, other, 2.0);
            assert(gear.ok);
            assert(gear.value.destroy().ok);
            assert(gear.value.destroy().ok);
            assert(!eve.resolveJoint2D(world(), 99999).ok);
            assert(!eve.resolveJoint2D(null, jointId()).ok);
        )"));
        CHECK_EQ(r.world.raw()->GetJointCount(), 2);
    }
    // Collecting a reference is not equivalent to releasing an owning factory result.
    CHECK_EQ(r.world.raw()->GetJointCount(), 2);
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World>("World", std::function<World*()>([] { return nullptr; }), true);
        table.addClass<Body>("Body", std::function<Body*()>([] { return nullptr; }), true);
        exposeJoint2DBindings(table);
        vm.addFunc("world", [&] { return &r.world; });
        vm.addFunc("jointId", [&] { return joint->getId(); });
        vm.addFunc("destroyWorld", [&] { r.world.destroy(); });
        vm.run(vm.compileSource(R"(
            local observed = eve.resolveJoint2D(world(), jointId()).value;
            destroyWorld();
            assert(!observed.isValid());
            assert(!observed.sample(0.01).ok);
            assert(!observed.destroy().ok);
        )"));
    }
}

TEST_CASE("physics.snapshot2d.materialAndDynamicsPreserveMassAndTrajectory") {
    Rig r;
    r.world.setGravity(0, 90);
    r.world.raw()->SetAllowSleeping(true);
    r.a->raw()->SetSleepingAllowed(false);
    r.a->raw()->SetLinearDamping(0.4f);
    r.a->raw()->SetAngularDamping(0.7f);
    r.a->raw()->SetGravityScale(-0.5f);
    r.a->raw()->SetLinearVelocity({3, 1});
    r.a->raw()->SetAngularVelocity(2);
    r.fa->setDensity(3.5f);
    r.fa->setFriction(0.8f);
    r.fa->setRestitution(0.6f);
    r.fa->setSensor(true);
    r.fa->setCategoryBits(32768);
    r.fa->setMaskBits(4660);
    r.fa->setGroupIndex(-123);
    r.fa->setTag("mechanical-wheel");
    const int   id    = r.a->getId();
    const float mass  = r.a->getMass();
    const auto  saved = capture(r.world);
    for (int i = 0; i < 60; ++i) r.world.update(1.f / 120);
    const b2Vec2 expectedPosition = r.a->raw()->GetPosition(), expectedVelocity = r.a->raw()->GetLinearVelocity();
    const float  expectedAngle = r.a->raw()->GetAngle();
    REQUIRE(r.world.restore(saved, digest()).ok());
    CHECK(capture(r.world).payload == saved.payload);
    b2Body* rebuilt = nullptr;
    for (auto* candidate = r.world.raw()->GetBodyList(); candidate; candidate = candidate->GetNext())
        if (static_cast<Body*>(candidate->GetUserData())->getId() == id) rebuilt = candidate;
    REQUIRE(rebuilt != nullptr);
    CHECK_EQ(rebuilt->GetMass(), mass);
    const auto* fixture = rebuilt->GetFixtureList();
    REQUIRE(fixture != nullptr);
    CHECK_EQ(fixture->GetFilterData().categoryBits, 32768);
    CHECK_EQ(fixture->GetFilterData().maskBits, 4660);
    CHECK_EQ(fixture->GetFilterData().groupIndex, -123);
    CHECK(fixture->IsSensor());
    CHECK_EQ(static_cast<Fixture*>(fixture->GetUserData())->getTag(), "mechanical-wheel");
    for (int i = 0; i < 60; ++i) r.world.update(1.f / 120);
    CHECK((rebuilt->GetPosition() - expectedPosition).Length() < 1e-5f);
    CHECK((rebuilt->GetLinearVelocity() - expectedVelocity).Length() < 1e-5f);
    CHECK(std::abs(rebuilt->GetAngle() - expectedAngle) < 1e-5f);
}

TEST_CASE("physics.snapshot2d.invalidMaterialAndDynamicsPreserveLiveWorld") {
    Rig        r;
    const auto saved  = capture(r.world);
    const auto handle = r.a->runtimeHandle();
    for (int fault = 0; fault < 11; ++fault) {
        auto  payload  = saved.payload;
        auto& body     = payload.find("bodies")->getIf<Value::Array>()->at(1);
        auto* material = body.find("fixtures")->getIf<Value::Array>()->at(0).find("material");
        switch (fault) {
            case 0: material->set("density", Value(-1.f)); break;
            case 1: material->set("friction", Value(true)); break;
            case 2: material->set("restitution", Value(-0.1f)); break;
            case 3: material->set("mask", Value("65536")); break;
            case 4: material->set("group", Value("-32769")); break;
            case 5: material->set("tag", Value(true)); break;
            case 6: material->set("future", Value(true)); break;
            case 7: body.find("dynamics")->set("linearDamping", Value(-1.f)); break;
            case 8: body.find("dynamics")->set("allowSleep", Value("bad")); break;
            case 9: payload.set("allowSleep", Value(1.f)); break;
            case 10:
                material->set("density", Value(1e38f));
                body.find("fixtures")->getIf<Value::Array>()->at(0).set("radius", Value(2.f));
                break;
        }
        auto invalid  = reseal(saved, std::move(payload));
        auto restored = r.world.restore(invalid, digest());
        CHECK(!restored.ok());
        CHECK(r.world.findBody(handle) == r.a.get());
        CHECK(capture(r.world).payload == saved.payload);
    }
}
