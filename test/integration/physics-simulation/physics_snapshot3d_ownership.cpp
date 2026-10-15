#include <box3d/box3d.h>
#include <cmath>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>
#include "common/Snapshot.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/mechanics/Coupling3D.h"
#include "physics/mechanics/Coupling3DBindings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
eve::SnapshotHashProvider ownershipHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (unsigned char byte : input) hash = (hash ^ byte) * 1099511628211ull;
        for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(hash >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}
}  // namespace
TEST_CASE("physics.snapshot3d.candidateOwnershipCommitAndRollback") {
    using namespace eve::physics;
    const auto               hash = ownershipHash();
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 2, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
    std::unique_ptr<Joint3D> joint(world.newDistanceJoint(ground.get(), body.get(), 0, 0, 0, 0, 2, 0, 2));
    auto                     captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    const auto handle = body->runtimeHandle();
    for (int pass = 0; pass < 3; ++pass) {
        auto  payload = captured.value().payload;
        auto* joints  = payload.find("joints")->getIf<eve::Value::Array>();
        REQUIRE(joints != nullptr);
        // Valid first joint is allocated before the unsupported second definition fails.
        auto invalid = joints->front();
        invalid.set("id", eve::Value("999"));
        invalid.set("kind", eve::Value("unsupported"));
        joints->push_back(std::move(invalid));
        auto envelope = eve::makeSnapshotEnvelope(
            captured.value().type, captured.value().schema, captured.value().schemaVersion, captured.value().instanceId,
            captured.value().revision, captured.value().tick, std::move(payload), hash);
        REQUIRE(envelope.ok());
        auto rejected = world.restore(envelope.value(), hash);
        CHECK(!rejected.ok());
        CHECK(world.findBody(handle) == body.get());
        CHECK(joint->isValid());
    }
    for (int pass = 0; pass < 3; ++pass) REQUIRE(world.restore(captured.value(), hash).ok());
    CHECK(!body->isValid());
    CHECK(!shape->isValid());
    CHECK(!joint->isValid());
    CHECK(world.findBody(handle) == nullptr);
    auto roundtrip = world.snapshot(hash);
    REQUIRE(roundtrip.ok());
    CHECK_EQ(roundtrip.value().payload.find("bodies")->arraySize(), 2u);
    CHECK_EQ(roundtrip.value().payload.find("shapes")->arraySize(), 1u);
    CHECK_EQ(roundtrip.value().payload.find("joints")->arraySize(), 1u);
}

TEST_CASE("physics.snapshot3d.preservesIndependentJointFrames") {
    using namespace eve::physics;
    auto                     hash = ownershipHash();
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Body3D>  b(world.newBody("dynamic", 0, 2, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.5f));
    std::unique_ptr<Joint3D> joint(world.newRevoluteJoint(a.get(), b.get(), 0, 1, 0, 0, 0, 1));
    // Separate attachment frames retain twist phase, including noncoincident anchors.
    b3Transform fa{{.25f, 1, 0}, {{0, 0, .6f}, .8f}};
    b3Transform fb{{-.25f, -1, .1f}, {{0, .6f, 0}, .8f}};
    b3Joint_SetLocalFrameA(eve::physics::box3d::Access::jointId(*joint), fa);
    b3Joint_SetLocalFrameB(eve::physics::box3d::Access::jointId(*joint), fb);
    auto snapshot = world.snapshot(hash);
    REQUIRE(snapshot.ok());
    const auto oldHandle = b->runtimeHandle();
    for (int failure = 0; failure < 6; ++failure) {
        auto  payload = snapshot.value().payload;
        auto& item    = payload.find("joints")->getIf<eve::Value::Array>()->front();
        if (failure == 0) item.set("bodyAId", eve::Value("invalid"));
        if (failure == 1) item.set("id", eve::Value("2147483647"));
        if (failure == 2) item.set("kind", eve::Value(17));
        if (failure == 3) item.set("bodyBId", eve::Value("4294967297"));
        if (failure == 4) item.set("localFrames", eve::Value(eve::Value::Array{}));
        if (failure == 5) {
            auto* frame = item.find("localFrames")->getIf<eve::Value::Array>();
            (*frame)[6] = eve::Value(0.0);
        }
        auto envelope = eve::makeSnapshotEnvelope(
            snapshot.value().type, snapshot.value().schema, snapshot.value().schemaVersion, snapshot.value().instanceId,
            snapshot.value().revision, snapshot.value().tick, std::move(payload), hash);
        REQUIRE(envelope.ok());
        CHECK(!world.restore(envelope.value(), hash).ok());
        CHECK(world.findBody(oldHandle) == b.get());
        CHECK(joint->isValid());
    }
    REQUIRE(world.restore(snapshot.value(), hash).ok());
    auto restored = world.snapshot(hash);
    REQUIRE(restored.ok());
    const auto* expected = snapshot.value().payload.find("joints")->at(0).find("localFrames");
    const auto* actual   = restored.value().payload.find("joints")->at(0).find("localFrames");
    REQUIRE(expected != nullptr);
    REQUIRE(actual != nullptr);
    for (std::size_t i = 0; i < 14; ++i) CHECK_EQ(actual->at(i).asDouble(), expected->at(i).asDouble());
}

TEST_CASE("physics.snapshot3d.jointParametersRoundTripAllKinds") {
    using namespace eve::physics;
    const auto hash = ownershipHash();
    for (int kind = 0; kind < 5; ++kind) {
        World3D                  world(0, 0, 0, false);
        std::unique_ptr<Body3D>  a(world.newBody("static", 0, 0, 0));
        std::unique_ptr<Body3D>  b(world.newBody("dynamic", 0, 2, 0));
        std::unique_ptr<Shape3D> shape(b->newSphereShape(.5f));
        std::unique_ptr<Joint3D> joint;
        if (kind == 0) joint.reset(world.newDistanceJoint(a.get(), b.get(), 0, 0, 0, 0, 2, 0, 3));
        if (kind == 1) joint.reset(world.newRevoluteJoint(a.get(), b.get(), 0, 1, 0, 0, 0, 1));
        if (kind == 2) joint.reset(world.newPrismaticJoint(a.get(), b.get(), 0, 1, 0, 0, 1, 0));
        if (kind == 3) joint.reset(world.newSphericalJoint(a.get(), b.get(), 0, 1, 0, 0, 0, 1));
        if (kind == 4) joint.reset(world.newWheelJoint(a.get(), b.get(), 0, 1, 0, 0, 1, 0, 0, 0, 1));
        const auto raw = eve::physics::box3d::Access::jointId(*joint);
        b3Joint_SetCollideConnected(raw, true);
        b3Joint_SetConstraintTuning(raw, 25, .7f);
        b3Joint_SetForceThreshold(raw, 100);
        b3Joint_SetTorqueThreshold(raw, 40);
        switch (kind) {
            case 0:
                b3DistanceJoint_EnableMotor(raw, true);
                b3DistanceJoint_SetMotorSpeed(raw, 1.5f);
                b3DistanceJoint_SetMaxMotorForce(raw, 17);
                b3DistanceJoint_EnableSpring(raw, true);
                b3DistanceJoint_SetSpringHertz(raw, 3);
                b3DistanceJoint_SetSpringDampingRatio(raw, .4f);
                b3DistanceJoint_SetSpringForceRange(raw, -8, 12);
                b3DistanceJoint_EnableLimit(raw, true);
                b3DistanceJoint_SetLengthRange(raw, 1, 4);
                break;
            case 1:
                b3RevoluteJoint_EnableMotor(raw, true);
                b3RevoluteJoint_SetMotorSpeed(raw, 1.5f);
                b3RevoluteJoint_SetMaxMotorTorque(raw, 17);
                b3RevoluteJoint_EnableSpring(raw, true);
                b3RevoluteJoint_SetTargetAngle(raw, .4f);
                b3RevoluteJoint_SetSpringHertz(raw, 3);
                b3RevoluteJoint_SetSpringDampingRatio(raw, .4f);
                b3RevoluteJoint_EnableLimit(raw, true);
                b3RevoluteJoint_SetLimits(raw, -.7f, .9f);
                break;
            case 2:
                b3PrismaticJoint_EnableMotor(raw, true);
                b3PrismaticJoint_SetMotorSpeed(raw, -1.5f);
                b3PrismaticJoint_SetMaxMotorForce(raw, 17);
                b3PrismaticJoint_EnableSpring(raw, true);
                b3PrismaticJoint_SetTargetTranslation(raw, .4f);
                b3PrismaticJoint_SetSpringHertz(raw, 3);
                b3PrismaticJoint_SetSpringDampingRatio(raw, .4f);
                b3PrismaticJoint_EnableLimit(raw, true);
                b3PrismaticJoint_SetLimits(raw, -.7f, .9f);
                break;
            case 3:
                b3SphericalJoint_EnableMotor(raw, true);
                b3SphericalJoint_SetMotorVelocity(raw, {1, 2, 3});
                b3SphericalJoint_SetMaxMotorTorque(raw, 17);
                b3SphericalJoint_EnableSpring(raw, true);
                b3SphericalJoint_SetTargetRotation(raw, {{0, .6f, 0}, .8f});
                b3SphericalJoint_SetSpringHertz(raw, 3);
                b3SphericalJoint_SetSpringDampingRatio(raw, .4f);
                b3SphericalJoint_EnableConeLimit(raw, true);
                b3SphericalJoint_SetConeLimit(raw, .5f);
                b3SphericalJoint_EnableTwistLimit(raw, true);
                b3SphericalJoint_SetTwistLimits(raw, -.7f, .9f);
                break;
            case 4:
                b3WheelJoint_EnableSuspension(raw, true);
                b3WheelJoint_SetSuspensionHertz(raw, 3);
                b3WheelJoint_SetSuspensionDampingRatio(raw, .4f);
                b3WheelJoint_EnableSuspensionLimit(raw, true);
                b3WheelJoint_SetSuspensionLimits(raw, -.7f, .9f);
                b3WheelJoint_EnableSpinMotor(raw, true);
                b3WheelJoint_SetSpinMotorSpeed(raw, 1.5f);
                b3WheelJoint_SetMaxSpinTorque(raw, 17);
                b3WheelJoint_EnableSteering(raw, true);
                b3WheelJoint_SetSteeringHertz(raw, 4);
                b3WheelJoint_SetSteeringDampingRatio(raw, .6f);
                b3WheelJoint_SetMaxSteeringTorque(raw, 12);
                b3WheelJoint_EnableSteeringLimit(raw, true);
                b3WheelJoint_SetSteeringLimits(raw, -.3f, .8f);
                b3WheelJoint_SetTargetSteeringAngle(raw, .4f);
                break;
        }
        auto saved = world.snapshot(hash);
        REQUIRE(saved.ok());
        CHECK_EQ(saved.value().schemaVersion.value(), 3u);
        const auto previousHandle = b->runtimeHandle();
        for (int failure = 0; failure < 4; ++failure) {
            auto  payload    = saved.value().payload;
            auto& item       = payload.find("joints")->getIf<eve::Value::Array>()->front();
            auto* parameters = item.find("parameters");
            if (failure == 0) parameters->set("unknown", eve::Value(true));
            if (failure == 1) parameters->set("forceThreshold", eve::Value(eve::Value::Array{eve::Value(-1.0)}));
            if (failure == 2) parameters->set("collideConnected", eve::Value(1));
            if (failure == 3)
                parameters->set("constraintTuning", eve::Value(eve::Value::Array{eve::Value(1e100), eve::Value(1.0)}));
            auto invalid = eve::makeSnapshotEnvelope(
                saved.value().type, saved.value().schema, saved.value().schemaVersion, saved.value().instanceId,
                saved.value().revision, saved.value().tick, std::move(payload), hash);
            REQUIRE(invalid.ok());
            CHECK(!world.restore(invalid.value(), hash).ok());
            CHECK(world.findBody(previousHandle) == b.get());
            CHECK(joint->isValid());
        }

        auto applied = world.restore(saved.value(), hash);
        REQUIRE(applied.ok(), "kind {}: {}", kind, applied.status().describe());
        auto restored = world.snapshot(hash);
        REQUIRE(restored.ok());
        CHECK(*saved.value().payload.find("joints") == *restored.value().payload.find("joints"));
        // Legacy v2 has no parameter object and deliberately migrates constructor defaults.
        auto legacyPayload = saved.value().payload;
        legacyPayload.getIf<eve::Value::Object>()->erase("couplings");
        for (auto& shape : *legacyPayload.find("shapes")->getIf<eve::Value::Array>())
            shape.getIf<eve::Value::Object>()->erase("localTransform");
        for (auto& shape : *legacyPayload.find("shapes")->getIf<eve::Value::Array>())
            shape.getIf<eve::Value::Object>()->erase("contact");
        legacyPayload.getIf<eve::Value::Object>()->erase("allowSleep");
        for (auto& body : *legacyPayload.find("bodies")->getIf<eve::Value::Array>())
            body.getIf<eve::Value::Object>()->erase("dynamics");
        legacyPayload.find("joints")->getIf<eve::Value::Array>()->front().getIf<eve::Value::Object>()->erase(
            "parameters");
        auto legacy = eve::makeSnapshotEnvelope(saved.value().type, saved.value().schema, eve::SchemaVersion(2),
                                                saved.value().instanceId, saved.value().revision, saved.value().tick,
                                                std::move(legacyPayload), hash);
        REQUIRE(legacy.ok());
        REQUIRE(world.restore(legacy.value(), hash).ok());
        auto migrated = world.snapshot(hash);
        REQUIRE(migrated.ok());
        CHECK(migrated.value().payload.find("joints")->at(0).find("parameters") != nullptr);
    }
}

TEST_CASE("physics.snapshot3d.motorTrajectorySurvivesRestore") {
    using namespace eve::physics;
    const auto               hash = ownershipHash();
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Body3D>  b(world.newBody("dynamic", 0, 2, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.5f));
    std::unique_ptr<Joint3D> joint(world.newRevoluteJoint(a.get(), b.get(), 0, 2, 0, 0, 0, 1));
    b3RevoluteJoint_EnableMotor(eve::physics::box3d::Access::jointId(*joint), true);
    b3RevoluteJoint_SetMotorSpeed(eve::physics::box3d::Access::jointId(*joint), 2);
    b3RevoluteJoint_SetMaxMotorTorque(eve::physics::box3d::Access::jointId(*joint), 5);
    const int id    = b->getId();
    auto      saved = world.snapshot(hash);
    REQUIRE(saved.ok());
    for (int i = 0; i < 60; ++i) world.update(.01f);
    const auto rotation = b3Body_GetTransform(eve::physics::box3d::Access::bodyId(*b)).q;
    const auto speed    = b->getAngularVelocityZ();
    CHECK(rotation.v.z > .1f);
    REQUIRE(world.restore(saved.value(), hash).ok());
    for (int i = 0; i < 60; ++i) world.update(.01f);
    auto* restored = world.findBodyById(id);
    REQUIRE(restored != nullptr);
    const auto restoredRotation = b3Body_GetTransform(eve::physics::box3d::Access::bodyId(*restored)).q;
    CHECK(std::abs(restoredRotation.v.z - rotation.v.z) < 1e-5);
    CHECK(std::abs(restoredRotation.s - rotation.s) < 1e-5);
    CHECK(std::abs(restored->getAngularVelocityZ() - speed) < 1e-5);
}

namespace {
struct RestorePhaseProbe {
    eve::physics::World3D*       world = nullptr;
    const eve::SnapshotEnvelope* saved = nullptr;
    eve::SnapshotHashProvider    hash;
    bool                         snapshotRejected = false, restoreRejected = false;
};
class ReentrantRestoreBackend final : public eve::physics::ISimulationBackend {
public:
    ReentrantRestoreBackend(void* state, RestorePhaseProbe& probe)
        : probe_(probe),
          delegate_(eve::physics::detail::makeCallbackSimulationBackend(
              state, &advance, eve::physics::SimulationBackendKind::MockAccelerator,
              eve::physics::SimulationDeterminism::ToleranceBounded)) {}
    eve::Result<void> step(const eve::SimulationStep& step, const eve::physics::SimulationSettings& settings) override {
        auto captured           = probe_.world->snapshot(probe_.hash);
        probe_.snapshotRejected = !captured.ok();
        auto restored           = probe_.world->restore(*probe_.saved, probe_.hash);
        probe_.restoreRejected  = !restored.ok();
        return delegate_->step(step, settings);
    }
    eve::physics::SimulationObservation observation() const noexcept override { return delegate_->observation(); }
    eve::physics::SimulationBackendKind kind() const noexcept override { return delegate_->kind(); }
    eve::physics::SimulationDeterminism determinism() const noexcept override { return delegate_->determinism(); }

private:
    static void advance(void* state, const eve::SimulationStep& step,
                        const eve::physics::SimulationSettings& settings) noexcept {
        b3World_Step(*static_cast<b3WorldId*>(state), static_cast<float>(step.delta.seconds()), settings.subStepCount);
    }
    RestorePhaseProbe&                                probe_;
    std::unique_ptr<eve::physics::ISimulationBackend> delegate_;
};
class ReentrantRestoreProvider final : public eve::physics::IAcceleratorBackendProvider {
public:
    RestorePhaseProbe probe;
    bool              supports(eve::physics::SimulationBackendDomain domain) const noexcept override {
        return domain == eve::physics::SimulationBackendDomain::World3D;
    }
    eve::Result<std::unique_ptr<eve::physics::ISimulationBackend>> create(eve::physics::SimulationBackendDomain,
                                                                          void* state) override {
        return eve::Result<std::unique_ptr<eve::physics::ISimulationBackend>>::success(
            std::make_unique<ReentrantRestoreBackend>(state, probe));
    }
};
}  // namespace
TEST_CASE("physics.snapshot3d.rejectsReentrantSnapshotAndRestore") {
    using namespace eve::physics;
    ReentrantRestoreProvider provider;
    auto                     registered = AcceleratorBackendProviderRegistration::registerProvider(provider);
    REQUIRE(registered.ok());
    auto                     registration = std::move(registered).takeValue();
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
    const auto               handle = body->runtimeHandle();
    provider.probe.world            = &world;
    provider.probe.hash             = ownershipHash();
    auto saved                      = world.snapshot(provider.probe.hash);
    REQUIRE(saved.ok());
    provider.probe.saved = &saved.value();
    body->setLinearVelocity(1, 0, 0);
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(provider.probe.snapshotRejected);
    CHECK(provider.probe.restoreRejected);
    CHECK(world.findBody(handle) == body.get());
    CHECK(body->getX() > 0);
    CHECK_EQ(world.simulationObservation().stepCount, 1u);
    CHECK(world.snapshot(provider.probe.hash).ok());
    // This provider does not support observation restore: failure happens after candidate links are prepared.
    auto refused = world.restore(saved.value(), provider.probe.hash);
    CHECK(!refused.ok());
    CHECK(world.findBody(handle) == body.get());
    CHECK_EQ(world.simulationObservation().stepCount, 1u);
    std::unique_ptr<Body3D> next(world.newBody("dynamic", 3, 0, 0));
    CHECK_EQ(next->runtimeHandle().index(), handle.index() + 1);
}

TEST_CASE("physics.snapshot3d.transmissionPhaseTopologyAndTrajectory") {
    using namespace eve::physics;
    const auto hash = ownershipHash();
    for (bool gear : {true, false}) {
        World3D                     world(0, 0, 0, false);
        std::unique_ptr<Body3D>     ground(world.newBody("static", 0, 0, 0));
        std::unique_ptr<Body3D>     a(world.newBody("dynamic", -2, 0, 0));
        std::unique_ptr<Body3D>     b(world.newBody("dynamic", 2, 0, 0));
        std::unique_ptr<Shape3D>    sa(a->newSphereShape(.3f)), sb(b->newSphereShape(.3f));
        std::unique_ptr<Joint3D>    ja, jb;
        std::unique_ptr<Coupling3D> coupling;
        if (gear) {
            ja.reset(world.newRevoluteJoint(ground.get(), a.get(), -2, 0, 0, 0, 0, 1));
            jb.reset(world.newRevoluteJoint(ground.get(), b.get(), 2, 0, 0, 0, 0, 1));
            GearCoupling3DSettings settings;
            settings.ratio = 2;
            auto created   = Coupling3D::createGear(world, *ja, *jb, settings);
            REQUIRE(created.ok());
            coupling = std::move(created).takeValue();
            a->setAngularVelocity(0, 0, 10);
        } else {
            PulleyCoupling3DSettings settings;
            settings.groundA = {-2, 4, 0};
            settings.groundB = {2, 4, 0};
            settings.length  = 8;
            auto created     = Coupling3D::createPulley(world, *a, *b, settings);
            REQUIRE(created.ok());
            coupling = std::move(created).takeValue();
            a->setLinearVelocity(0, -1, 0);
        }
        auto createExtra = [&]() -> eve::Result<std::unique_ptr<Coupling3D>> {
            if (gear) {
                GearCoupling3DSettings settings;
                settings.ratio = 2;
                return Coupling3D::createGear(world, *ja, *jb, settings);
            }
            PulleyCoupling3DSettings settings;
            settings.groundA = {-2, 4, 0};
            settings.groundB = {2, 4, 0};
            settings.length  = 8;
            return Coupling3D::createPulley(world, *a, *b, settings);
        };
        auto temporary = createExtra();
        REQUIRE(temporary.ok());
        std::move(temporary).takeValue().reset();
        auto extraResult = createExtra();
        REQUIRE(extraResult.ok());
        auto extra = std::move(extraResult).takeValue();
        REQUIRE(extra->setEnabled(false).ok());
        CHECK_EQ(extra->getId(), 3);
        for (int i = 0; i < 400; ++i) world.update(.01f);
        auto phase = coupling->sample();
        REQUIRE(phase.ok());
        if (gear) CHECK(std::abs(phase.value().coordinateA) > 6.3);
        const auto oldHandle = a->runtimeHandle();
        const int  bodyId = a->getId(), couplingId = coupling->getId();
        auto       saved = world.snapshot(hash);
        REQUIRE(saved.ok());
        for (int failure = 0; failure < 4; ++failure) {
            auto  payload = saved.value().payload;
            auto& entries = *payload.find("couplings")->getIf<eve::Value::Array>();
            if (failure == 0) entries.back().set("sourceA", eve::Value("99999"));
            if (failure == 1) entries.push_back(entries.front());
            if (failure == 2) entries.front().set("unknown", eve::Value(true));
            if (failure == 3) entries.front().set("ratio", eve::Value(0.0));
            auto bad = eve::makeSnapshotEnvelope(saved.value().type, saved.value().schema, saved.value().schemaVersion,
                                                 saved.value().instanceId, saved.value().revision, saved.value().tick,
                                                 std::move(payload), hash);
            REQUIRE(bad.ok());
            CHECK(!world.restore(bad.value(), hash).ok());
            CHECK(coupling->isValid());
            CHECK(world.findBody(oldHandle) == a.get());
        }
        for (int i = 0; i < 60; ++i) world.update(.01f);
        auto expected = coupling->sample();
        REQUIRE(expected.ok());
        const auto expectedY = a->getY();
        REQUIRE(world.restore(saved.value(), hash).ok());
        CHECK(!coupling->isValid());
        CHECK(!extra->isValid());
        CHECK(world.findBody(oldHandle) == nullptr);
        auto resolved = world.resolveCoupling3D(couplingId);
        REQUIRE(resolved.ok());
        auto* rebuilt  = &resolved.value().get();
        auto  restored = rebuilt->sample();
        REQUIRE(restored.ok());
        CHECK(std::abs(restored.value().coordinateA - phase.value().coordinateA) < 1e-6);
        CHECK(std::abs(restored.value().error - phase.value().error) < 1e-6);
        auto roundtrip = world.snapshot(hash);
        REQUIRE(roundtrip.ok());
        CHECK(*roundtrip.value().payload.find("couplings") == *saved.value().payload.find("couplings"));
        for (int i = 0; i < 60; ++i) world.update(.01f);
        auto advanced = rebuilt->sample();
        REQUIRE(advanced.ok());
        CHECK(std::abs(advanced.value().coordinateA - expected.value().coordinateA) < 1e-4);
        CHECK(std::abs(world.findBodyById(bodyId)->getY() - expectedY) < 1e-4);
        world.findBodyById(bodyId)->destroy();
        CHECK(!world.resolveCoupling3D(couplingId).ok());
    }
}

TEST_CASE("physics.snapshot3d.scriptReferencesDoNotRetargetAfterRestore") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -1, 0, 0)), b(world.newBody("dynamic", 1, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(.3f)), sb(b->newSphereShape(.3f));
    auto                     created = Coupling3D::createPulley(world, *a, *b, PulleyCoupling3DSettings{});
    REQUIRE(created.ok());
    auto       owned = std::move(created).takeValue();
    const int  id    = owned->getId();
    const auto hash  = ownershipHash();
    auto       saved = world.snapshot(hash);
    REQUIRE(saved.ok());
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
        exposeCoupling3DBindings(table);
        vm.addFunc("world", [&] { return &world; });
        vm.addFunc("identity", [&] { return id; });
        vm.run(vm.compileSource(R"(
            local found = eve.resolveCoupling3D(world(),identity());
            assert(found.ok && found.hasValue);
            oldReference <- found.value;
            assert(oldReference.isValid() && oldReference.getId() == identity());
            assert(oldReference.setEnabled(false).ok);
            assert(!oldReference.sample().value.active);
            assert(oldReference.setEnabled(true).ok);
            local wrong = eve.CouplingPoint3D();
            assert(!oldReference.sample.call(wrong).ok);
            assert(!oldReference.setEnabled.call(wrong,true).ok);
            assert(!oldReference.destroy.call(wrong).ok);
            assert(!eve.resolveCoupling3D(wrong,identity()).ok);
            assert(!eve.resolveCoupling3D(world(),9999).ok);
        )"));
        REQUIRE(world.restore(saved.value(), hash).ok());
        vm.run(vm.compileSource(R"(
            assert(!oldReference.isValid());
            assert(!oldReference.sample().ok && !oldReference.destroy().ok);
            current <- eve.resolveCoupling3D(world(),identity()).value;
            assert(current.isValid());
        )"));
        // A second restore frees a world-owned wrapper while its observer remains in the VM.
        REQUIRE(world.restore(saved.value(), hash).ok());
        vm.run(vm.compileSource(R"(
            assert(!current.isValid() && !current.setEnabled(false).ok);
            current = eve.resolveCoupling3D(world(),identity()).value;
            assert(current.isValid() && current.sample().ok);
        )"));
    }
    // Collecting all observers must leave the world-owned solver constraint alive.
    REQUIRE(world.resolveCoupling3D(id).ok());
    {
        ssq::VM vm(1024);
        auto    table = vm.addTable("eve");
        table.addClass<World3D>("World3D", std::function<World3D*()>([] { return nullptr; }), true);
        exposeCoupling3DBindings(table);
        vm.addFunc("world", [&] { return &world; });
        vm.addFunc("identity", [&] { return id; });
        vm.run(vm.compileSource(R"(
            reference <- eve.resolveCoupling3D(world(),identity()).value;
            assert(reference.destroy().ok);
            assert(!reference.isValid() && !reference.destroy().ok);
            assert(!eve.resolveCoupling3D(world(),identity()).ok);
        )"));
        REQUIRE(world.restore(saved.value(), hash).ok());
        vm.run(vm.compileSource("reference = eve.resolveCoupling3D(world(),identity()).value;"));
        world.destroy();
        vm.run(vm.compileSource("assert(!reference.isValid() && !reference.sample().ok);"));
    }
}

TEST_CASE("physics.snapshot3d.dampingGravityAndSleepPreserveTrajectory") {
    using namespace eve::physics;
    const auto               hash = ownershipHash();
    World3D                  world(0, -10, 0, true);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 4, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
    b3Body_SetLinearDamping(eve::physics::box3d::Access::bodyId(*body), 2);
    b3Body_SetAngularDamping(eve::physics::box3d::Access::bodyId(*body), .5f);
    b3Body_SetGravityScale(eve::physics::box3d::Access::bodyId(*body), .4f);
    b3Body_SetSleepThreshold(eve::physics::box3d::Access::bodyId(*body), .03f);
    b3Body_EnableSleep(eve::physics::box3d::Access::bodyId(*body), false);
    body->setLinearVelocity(2, 1, 0);
    body->setAngularVelocity(0, 0, 3);
    const auto handle = body->runtimeHandle();
    const int  id     = body->getId();
    auto       saved  = world.snapshot(hash);
    REQUIRE(saved.ok());
    for (int failure = 0; failure < 7; ++failure) {
        auto  payload  = saved.value().payload;
        auto* dynamics = payload.find("bodies")->getIf<eve::Value::Array>()->front().find("dynamics");
        if (failure == 0) dynamics->set("linearDamping", eve::Value(-1.0));
        if (failure == 1) dynamics->set("angularDamping", eve::Value(-1.0));
        if (failure == 2) dynamics->set("sleepThreshold", eve::Value(-1.0));
        if (failure == 3) dynamics->set("allowSleep", eve::Value(1));
        if (failure == 4) dynamics->set("unknown", eve::Value(1));
        if (failure == 5) dynamics->set("gravityScale", eve::Value(1e100));
        if (failure == 6) payload.set("allowSleep", eve::Value(1));
        auto invalid = eve::makeSnapshotEnvelope(saved.value().type, saved.value().schema, saved.value().schemaVersion,
                                                 saved.value().instanceId, saved.value().revision, saved.value().tick,
                                                 std::move(payload), hash);
        REQUIRE(invalid.ok());
        CHECK(!world.restore(invalid.value(), hash).ok());
        CHECK(world.findBody(handle) == body.get());
        auto unchanged = world.snapshot(hash);
        REQUIRE(unchanged.ok());
        CHECK(unchanged.value().payload == saved.value().payload);
    }
    for (int i = 0; i < 120; ++i) world.update(.01f);
    const float x = body->getX(), y = body->getY(), vy = body->getLinearVelocityY(), wz = body->getAngularVelocityZ();
    REQUIRE(world.restore(saved.value(), hash).ok());
    auto roundtrip = world.snapshot(hash);
    REQUIRE(roundtrip.ok());
    CHECK(roundtrip.value().payload == saved.value().payload);
    auto* restored = world.findBodyById(id);
    REQUIRE(restored != nullptr);
    CHECK(b3World_IsSleepingEnabled(eve::physics::box3d::Access::worldId(world)));
    CHECK(!b3Body_IsSleepEnabled(eve::physics::box3d::Access::bodyId(*restored)));
    CHECK_EQ(b3Body_GetSleepThreshold(eve::physics::box3d::Access::bodyId(*restored)), .03f);
    for (int i = 0; i < 120; ++i) world.update(.01f);
    CHECK(std::abs(restored->getX() - x) < 1e-5);
    CHECK(std::abs(restored->getY() - y) < 1e-5);
    CHECK(std::abs(restored->getLinearVelocityY() - vy) < 1e-5);
    CHECK(std::abs(restored->getAngularVelocityZ() - wz) < 1e-5);
}

TEST_CASE("physics.snapshot3d.localShapeTransformPreservesCenterOfMass") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 2, 3));
    shape->setLocalTransform(2, 3, 4, 0, 0, .6f, .8f);
    body->setLinearVelocity(1, 2, 3);
    body->setAngularVelocity(0, 0, 2);
    const int  id      = body->getId();
    const auto center  = b3Body_GetWorldCenterOfMass(eve::physics::box3d::Access::bodyId(*body));
    const auto inertia = b3Body_GetWorldInverseRotationalInertia(eve::physics::box3d::Access::bodyId(*body));
    auto       hash    = ownershipHash();
    auto       saved   = world.snapshot(hash);
    REQUIRE(saved.ok());
    for (int failure = 0; failure < 6; ++failure) {
        auto  payload = saved.value().payload;
        auto& item    = payload.find("shapes")->getIf<eve::Value::Array>()->front();
        if (failure == 0) item.set("localTransform", eve::Value(eve::Value::Array{}));
        if (failure == 1)
            item.set("localTransform",
                     eve::Value(eve::Value::Array{eve::Value(0), eve::Value(0), eve::Value(0), eve::Value(0),
                                                  eve::Value(0), eve::Value(0), eve::Value(0)}));
        if (failure == 2) item.find("localTransform")->getIf<eve::Value::Array>()->front() = eve::Value(1e100);
        if (failure == 3) item.set("unknown", eve::Value(0));
        if (failure == 4) payload.find("bodies")->getIf<eve::Value::Array>()->front().set("qx", eve::Value(1e30));
        if (failure == 5) payload.find("bodies")->getIf<eve::Value::Array>()->front().set("qw", eve::Value(0.0));
        auto invalid = eve::makeSnapshotEnvelope(saved.value().type, saved.value().schema, saved.value().schemaVersion,
                                                 saved.value().instanceId, saved.value().revision, saved.value().tick,
                                                 std::move(payload), hash);
        REQUIRE(invalid.ok());
        CHECK(!world.restore(invalid.value(), hash).ok());
        CHECK(body->isValid());
        CHECK(shape->isValid());
    }
    REQUIRE(world.restore(saved.value(), hash).ok());
    auto* restored = world.findBodyById(id);
    REQUIRE(restored != nullptr);
    auto next = world.snapshot(hash);
    REQUIRE(next.ok());
    CHECK(next.value().payload == saved.value().payload);
    const auto nextCenter = b3Body_GetWorldCenterOfMass(eve::physics::box3d::Access::bodyId(*restored));
    CHECK_EQ(nextCenter.x, center.x);
    CHECK_EQ(nextCenter.y, center.y);
    CHECK_EQ(nextCenter.z, center.z);
    const auto nextInertia = b3Body_GetWorldInverseRotationalInertia(eve::physics::box3d::Access::bodyId(*restored));
    CHECK(std::abs(nextInertia.cx.x - inertia.cx.x) < 1e-6);
    CHECK(std::abs(nextInertia.cy.y - inertia.cy.y) < 1e-6);
    CHECK(std::abs(nextInertia.cz.z - inertia.cz.z) < 1e-6);
}

TEST_CASE("physics.snapshot3d.contactEventsUseRestoredSparseShapeIds") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 1.5f, 0, 0));
    std::unique_ptr<Shape3D> first(ground->newSphereShape(1));
    std::unique_ptr<Shape3D> removed(ground->newSphereShape(.1f));
    removed.reset();
    std::unique_ptr<Shape3D> second(body->newSphereShape(1));
    const int                firstId = first->getId(), secondId = second->getId();
    REQUIRE_EQ(firstId, 1);
    REQUIRE_EQ(secondId, 3);
    auto hash  = ownershipHash();
    auto saved = world.snapshot(hash);
    REQUIRE(saved.ok());
    REQUIRE(world.restore(saved.value(), hash).ok());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE_EQ(world.getBeginContactCount(), 1);
    const auto a = world.getBeginContactShapeAId(0), b = world.getBeginContactShapeBId(0);
    CHECK(((a == firstId && b == secondId) || (a == secondId && b == firstId)));
}

TEST_CASE("physics.snapshot3d.contactMaterialDensityAndFilterRoundTrip") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(.7f));
    shape->setDensity(3.5f);
    shape->setTag(-73);
    shape->setExplosionScale(.4f);
    shape->setOneWay(0, 1, 0, .2f, .03f, -.1f);
    b3SurfaceMaterial material{.8f, .3f, .07f, {1, 2, -3}, UINT64_C(0xfedcba9876543210), 0xaabbccdd};
    b3Shape_SetSurfaceMaterial(eve::physics::box3d::Access::shapeId(*shape), material);
    b3Shape_SetFilter(eve::physics::box3d::Access::shapeId(*shape),
                      {UINT64_C(0x8000000000000001), UINT64_C(0xf000000000000001), -42}, false);
    b3Shape_EnableSensorEvents(eve::physics::box3d::Access::shapeId(*shape), false);
    b3Shape_EnableContactEvents(eve::physics::box3d::Access::shapeId(*shape), false);
    shape->setHitEventsEnabled(true);
    const auto mass   = b3Body_GetMass(eve::physics::box3d::Access::bodyId(*body));
    const int  bodyId = body->getId();
    auto       hash   = ownershipHash();
    auto       saved  = world.snapshot(hash);
    REQUIRE(saved.ok());
    REQUIRE(world.restore(saved.value(), hash).ok());
    auto* restored = world.findBodyById(bodyId);
    REQUIRE(restored != nullptr);
    CHECK_EQ(b3Body_GetMass(eve::physics::box3d::Access::bodyId(*restored)), mass);
    b3ShapeId raw{};
    REQUIRE_EQ(b3Body_GetShapes(eve::physics::box3d::Access::bodyId(*restored), &raw, 1), 1);
    CHECK_EQ(b3Shape_GetDensity(raw), 3.5f);
    CHECK_EQ(b3Shape_GetSurfaceMaterial(raw).userMaterialId, material.userMaterialId);
    CHECK_EQ(b3Shape_GetFilter(raw).categoryBits, UINT64_C(0x8000000000000001));
    CHECK_EQ(b3Shape_GetFilter(raw).groupIndex, -42);
    CHECK(!b3Shape_AreContactEventsEnabled(raw));
    CHECK(b3Shape_AreHitEventsEnabled(raw));
    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    auto before = saved.value().payload.find("shapes")->toJson();
    REQUIRE(before.ok());
    auto after = captured.value().payload.find("shapes")->toJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
}

TEST_CASE("physics.snapshot3d.invalidContactRestoresNothing") {
    using namespace eve::physics;
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> first(body->newSphereShape(.5f));
    std::unique_ptr<Shape3D> second(body->newSphereShape(.7f));
    auto                     hash  = ownershipHash();
    auto                     saved = world.snapshot(hash);
    REQUIRE(saved.ok());
    const auto handle = body->runtimeHandle();
    for (int failure = 0; failure < 8; ++failure) {
        auto  payload = saved.value().payload;
        auto& contact = *payload.find("shapes")->getIf<eve::Value::Array>()->back().find("contact");
        if (failure == 0) contact.set("density", eve::Value(-1.0));
        if (failure == 1) contact.set("density", eve::Value(1e38));
        if (failure == 2) contact.set("category", eve::Value("18446744073709551616"));
        if (failure == 3) contact.set("group", eve::Value("2147483648"));
        if (failure == 4) contact.set("unknown", eve::Value(true));
        if (failure == 5)
            contact.find("surface")->find("values")->getIf<eve::Value::Array>()->front() = eve::Value(-1.0);
        if (failure == 6) contact.find("oneWayParameters")->getIf<eve::Value::Array>()->at(1) = eve::Value(0.0);
        if (failure == 7) contact.set("sensor", eve::Value("true"));
        auto invalid = eve::makeSnapshotEnvelope(saved.value().type, saved.value().schema, saved.value().schemaVersion,
                                                 saved.value().instanceId, saved.value().revision, saved.value().tick,
                                                 std::move(payload), hash);
        REQUIRE(invalid.ok());
        CHECK(!world.restore(invalid.value(), hash).ok());
        CHECK(world.findBody(handle) == body.get());
        CHECK(first->isValid());
        CHECK(second->isValid());
    }
}

TEST_CASE("physics.snapshot3d.meshMaterialsAndSensorBehavior") {
    using namespace eve::physics;
    auto                     hash = ownershipHash();
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0, -5, 0));
    std::unique_ptr<Shape3D> mesh(
        ground->newTriangleMeshShape({-1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1}, {0, 2, 1, 0, 3, 2}));
    mesh->setTriangleMeshMaterialIndices({0, 2});
    for (int i = 0; i < 3; ++i) {
        auto m            = b3Shape_GetMeshSurfaceMaterial(eve::physics::box3d::Access::shapeId(*mesh), i);
        m.friction        = .2f + i * .15f;
        m.tangentVelocity = {float(i), 0, 0};
        m.userMaterialId  = UINT64_C(0x100000000) + i;
        m.customColor     = 0xaabbcc00 + i;
        b3Shape_SetMeshMaterial(eve::physics::box3d::Access::shapeId(*mesh), m, i);
    }
    std::unique_ptr<Body3D>  sensorBody(world.newBody("static", 0, 0, 0));
    std::unique_ptr<Shape3D> sensor(sensorBody->newSphereShape(1));
    sensor->setSensor(true);
    sensor->setTag(91);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", .5f, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
    const int                sensorId = sensor->getId();
    auto                     saved    = world.snapshot(hash);
    REQUIRE(saved.ok());
    REQUIRE(world.restore(saved.value(), hash).ok());
    auto captured = world.snapshot(hash);
    REQUIRE(captured.ok());
    auto before = saved.value().payload.find("shapes")->toJson();
    REQUIRE(before.ok());
    auto after = captured.value().payload.find("shapes")->toJson();
    REQUIRE(after.ok());
    CHECK_EQ(before.value(), after.value());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK_EQ(world.getBeginContactCount(), 0);
    REQUIRE_EQ(world.getBeginTriggerCount(), 1);
    CHECK_EQ(world.getBeginTriggerSensorShapeId(0), sensorId);
}
