#include <memory>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"

using namespace eve::physics;
namespace {
struct ProbeState {
    float x         = 0.f;
    int   destroyed = 0;
    bool  alive     = true;
};
template <class T>
[[maybe_unused]] eve::Result<T> unsupported() {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                          "injected provider rejection", "physics.backend.probe"));
}
#define PROBE_UNSUPPORTED(type, name, arguments, qualifiers) \
    eve::Result<type> name arguments qualifiers override { return unsupported<type>(); }
class BodyProbe final : public IBodyBackend3D {
public:
    PROBE_UNSUPPORTED(std::unique_ptr<IShapeBackend3D>, createShape, (const ShapeGeometry3D&, float, float, float), )
    void bindOwner(Body3D&) noexcept override {}
    explicit BodyProbe(ProbeState& state) : state_(state) {}
    ~BodyProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void              invalidate() noexcept override { state_.alive = false; }
    eve::Result<void> setPosition(float x, float, float) override {
        state_.x = x;
        return eve::Result<void>::success();
    }
    eve::Result<float> getX() const override { return eve::Result<float>::success(state_.x); }
    PROBE_UNSUPPORTED(float, getY, (), const)
    PROBE_UNSUPPORTED(float, getZ, (), const)
    PROBE_UNSUPPORTED(void, setRotation, (float, float, float, float), )
    PROBE_UNSUPPORTED(float, getRotX, (), const)
    PROBE_UNSUPPORTED(float, getRotY, (), const)
    PROBE_UNSUPPORTED(float, getRotZ, (), const)
    PROBE_UNSUPPORTED(float, getRotW, (), const)
    PROBE_UNSUPPORTED(void, setLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMass, (), const)
    PROBE_UNSUPPORTED(void, setAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(float, getAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalPoint, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, worldToLocalPointOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, localToWorldVector, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, localToWorldVectorOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, worldToLocalVector, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getLocalPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(PhysicsVector3D, getLocalPointVelocityOwned, (float, float, float), const)
    PROBE_UNSUPPORTED(std::vector<float>, getWorldPointVelocity, (float, float, float), const)
    PROBE_UNSUPPORTED(void, applyForce, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyForceAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLinearImpulseAt, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForce, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalForceToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalTorque, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulse, (float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalLinearImpulseToCenter, (float, float, float), )
    PROBE_UNSUPPORTED(void, applyLocalAngularImpulse, (float, float, float), )
    PROBE_UNSUPPORTED(void, setTargetTransform, (float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setLinearDamping, (float), )
    PROBE_UNSUPPORTED(float, getLinearDamping, (), const)
    PROBE_UNSUPPORTED(void, setAngularDamping, (float), )
    PROBE_UNSUPPORTED(float, getAngularDamping, (), const)
    PROBE_UNSUPPORTED(void, setGravityScale, (float), )
    PROBE_UNSUPPORTED(float, getGravityScale, (), const)
    PROBE_UNSUPPORTED(void, setSleepEnabled, (bool), )
    PROBE_UNSUPPORTED(bool, isSleepEnabled, (), const)
    PROBE_UNSUPPORTED(void, setSleepThreshold, (float), )
    PROBE_UNSUPPORTED(float, getSleepThreshold, (), const)
    PROBE_UNSUPPORTED(void, setMotionLocks, (bool, bool, bool, bool, bool, bool), )
    PROBE_UNSUPPORTED(bool, isLinearXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isLinearZLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularXLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularYLocked, (), const)
    PROBE_UNSUPPORTED(bool, isAngularZLocked, (), const)
    PROBE_UNSUPPORTED(void, setMassProperties, (float, float, float, float, float, float, float, float, float, float), )
    PROBE_UNSUPPORTED(void, resetMassProperties, (), )
    PROBE_UNSUPPORTED(float, getInertiaXX, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaZZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXY, (), const)
    PROBE_UNSUPPORTED(float, getInertiaXZ, (), const)
    PROBE_UNSUPPORTED(float, getInertiaYZ, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterX, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterY, (), const)
    PROBE_UNSUPPORTED(float, getLocalCenterZ, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterX, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterY, (), const)
    PROBE_UNSUPPORTED(float, getWorldCenterZ, (), const)
    PROBE_UNSUPPORTED(void, setType, (const std::string&), )
    PROBE_UNSUPPORTED(std::string, getType, (), const)
    PROBE_UNSUPPORTED(void, setFixedRotation, (bool), )
    PROBE_UNSUPPORTED(bool, isFixedRotation, (), const)
    PROBE_UNSUPPORTED(void, setActive, (bool), )
    PROBE_UNSUPPORTED(bool, isActive, (), const)
    PROBE_UNSUPPORTED(void, setBullet, (bool), )
    PROBE_UNSUPPORTED(bool, isBullet, (), const)
    PROBE_UNSUPPORTED(void, setAwake, (bool), )
    PROBE_UNSUPPORTED(bool, isAwake, (), const)
private:
    ProbeState& state_;
};
class JointProbe final : public IJointBackend3D {
public:
    void bindOwner(Joint3D&) noexcept override {}
    explicit JointProbe(ProbeState& state) : state_(state) {}
    ~JointProbe() override { destroy(); }
    bool isValid() const noexcept override { return state_.alive; }
    void destroy() noexcept override {
        if (state_.alive) ++state_.destroyed;
        state_.alive = false;
    }
    void                       invalidate() noexcept override { state_.alive = false; }
    eve::Result<JointFrames3D> localFrames() const override {
        JointFrames3D frames;
        frames.bodyA[0] = state_.x;
        return eve::Result<JointFrames3D>::success(frames);
    }
    PROBE_UNSUPPORTED(void, setCollideConnected, (bool), )
    PROBE_UNSUPPORTED(bool, getCollideConnected, (), const)
    PROBE_UNSUPPORTED(void, setForceThreshold, (float), )
    PROBE_UNSUPPORTED(float, getForceThreshold, (), const)
    PROBE_UNSUPPORTED(void, setTorqueThreshold, (float), )
    PROBE_UNSUPPORTED(float, getTorqueThreshold, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintForceZ, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueX, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueY, (), const)
    PROBE_UNSUPPORTED(float, getConstraintTorqueZ, (), const)
    PROBE_UNSUPPORTED(float, getLinearSeparation, (), const)
    PROBE_UNSUPPORTED(float, getAngularSeparation, (), const)
    PROBE_UNSUPPORTED(void, setDistanceLength, (float), )
    PROBE_UNSUPPORTED(float, getDistanceLength, (), const)
    PROBE_UNSUPPORTED(float, getDistanceCurrentLength, (), const)
    PROBE_UNSUPPORTED(void, setDistanceSpring, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setDistanceMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setRevoluteMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getRevoluteAngle, (), const)
    PROBE_UNSUPPORTED(float, getRevoluteMotorTorque, (), const)
    PROBE_UNSUPPORTED(void, setPrismaticSpring, (bool, float, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setPrismaticMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getPrismaticTranslation, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticSpeed, (), const)
    PROBE_UNSUPPORTED(float, getPrismaticMotorForce, (), const)
    PROBE_UNSUPPORTED(void, setSphericalConeLimit, (bool, float), )
    PROBE_UNSUPPORTED(void, setSphericalTwistLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setSphericalMotor, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(float, getSphericalConeAngle, (), const)
    PROBE_UNSUPPORTED(float, getSphericalTwistAngle, (), const)
    PROBE_UNSUPPORTED(void, setWheelSuspension, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSuspensionLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSpinMotor, (bool, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteering, (bool, float, float, float, float), )
    PROBE_UNSUPPORTED(void, setWheelSteeringLimits, (bool, float, float), )
    PROBE_UNSUPPORTED(float, getWheelSpinSpeed, (), const)
    PROBE_UNSUPPORTED(float, getWheelSpinTorque, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringAngle, (), const)
    PROBE_UNSUPPORTED(float, getWheelSteeringTorque, (), const)
    PROBE_UNSUPPORTED(void, setWeldLinearSpring, (float, float), )
    PROBE_UNSUPPORTED(void, setWeldAngularSpring, (float, float), )
    PROBE_UNSUPPORTED(float, getWeldLinearHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldLinearDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularHertz, (), const)
    PROBE_UNSUPPORTED(float, getWeldAngularDampingRatio, (), const)
    PROBE_UNSUPPORTED(void, setMotorLinearVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularVelocity, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorVelocityLimits, (float, float), )
    PROBE_UNSUPPORTED(void, setMotorLinearSpring, (float, float, float), )
    PROBE_UNSUPPORTED(void, setMotorAngularSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorLinearVelocityZ, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityX, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityY, (), const)
    PROBE_UNSUPPORTED(float, getMotorAngularVelocityZ, (), const)
    PROBE_UNSUPPORTED(void, setParallelSpring, (float, float, float), )
    PROBE_UNSUPPORTED(float, getParallelHertz, (), const)
    PROBE_UNSUPPORTED(float, getParallelDampingRatio, (), const)
    PROBE_UNSUPPORTED(float, getParallelMaxTorque, (), const)
private:
    ProbeState& state_;
};
class ShapeProbe final : public IShapeBackend3D {
public:
    bool              alive = true;
    bool              isValid() const noexcept override { return alive; }
    void              destroy() noexcept override { alive = false; }
    void              invalidate() noexcept override { alive = false; }
    void              bindOwner(Shape3D&) noexcept override {}
    eve::Result<bool> isSensor() const override { return eve::Result<bool>::success(false); }
    PROBE_UNSUPPORTED(ShapePlane3D, oneWayPlane, (const ShapeGeometry3D&, PhysicsVector3D, float), const)
    PROBE_UNSUPPORTED(void, rebuild, (const ShapeGeometry3D&, ShapeBehavior3D, std::span<const SurfaceMaterial3D>), )
    PROBE_UNSUPPORTED(float, density, (), const)
    PROBE_UNSUPPORTED(void, setDensity, (float, bool), )
    PROBE_UNSUPPORTED(float, friction, (), const)
    PROBE_UNSUPPORTED(void, setFriction, (float), )
    PROBE_UNSUPPORTED(float, restitution, (), const)
    PROBE_UNSUPPORTED(void, setRestitution, (float), )
    PROBE_UNSUPPORTED(SurfaceMaterial3D, material, (), const)
    PROBE_UNSUPPORTED(void, setMaterial, (SurfaceMaterial3D), )
    PROBE_UNSUPPORTED(ShapeFilter3D, filter, (), const)
    PROBE_UNSUPPORTED(void, setFilter, (ShapeFilter3D, bool), )
    PROBE_UNSUPPORTED(void, enableHitEvents, (bool), )
    PROBE_UNSUPPORTED(void, enablePreSolveEvents, (bool), )
    PROBE_UNSUPPORTED(int, meshMaterialCount, (), const)
    PROBE_UNSUPPORTED(SurfaceMaterial3D, meshMaterial, (int), const)
    PROBE_UNSUPPORTED(void, setMeshMaterial, (SurfaceMaterial3D, int), )
    PROBE_UNSUPPORTED(bool, testPoint, (float, float, float), const)
};

[[maybe_unused]] void checkPositionContract(Body3D& body) {
    for (float x : {7.f, -3.f, 0.f}) {
        body.setPosition(x, 0.f, 0.f);
        CHECK(body.getX() == x);
    }
    body.destroy();
    CHECK(!body.isValid());
    body.destroy();
    CHECK(!body.isValid());
}
#undef PROBE_UNSUPPORTED
}  // namespace

TEST_CASE("physics.backend.bodyDispatchDoesNotRequireNativeBody") {
    World3D    world(0.f, 0.f, 0.f, false);
    ProbeState state;
    {
        Body3D body(&world, std::make_unique<BodyProbe>(state), 123, PhysicsBodyHandle::invalid());
        body.setPosition(7.f, 8.f, 9.f);
        CHECK(body.getX() == 7.f);
        bool rejected = false;
        try {
            body.applyForce(1.f, 2.f, 3.f);
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(body.getX() == 7.f);
        auto result = body.localToWorldPointOwned(1.f, 2.f, 3.f);
        CHECK(!result.ok());
        REQUIRE(result.error() != nullptr);
        CHECK(result.error()->code() == eve::DiagnosticCode::Unsupported);
        body.destroy();
        CHECK(!body.isValid());
        CHECK(state.destroyed == 1);
        CHECK(body.getRotW() == 1.f);
    }
    CHECK(state.destroyed == 1);
}

TEST_CASE("physics.backend.jointDispatchDoesNotRequireNativeConstraint") {
    World3D    world(0.f, 0.f, 0.f, false);
    ProbeState state;
    state.x = 4.f;
    {
        Joint3D joint(&world, nullptr, nullptr, std::make_unique<JointProbe>(state), PhysicsJointHandle::invalid(),
                      Joint3D::Kind::Distance, 123);
        auto    frames = joint.localFrames();
        REQUIRE(frames.ok());
        CHECK(frames.value().bodyA[0] == 4.f);
        bool rejected = false;
        try {
            joint.setDistanceLength(2.f);
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
        joint.destroy();
        CHECK(!joint.isValid());
        CHECK(state.destroyed == 1);
    }
    CHECK(state.destroyed == 1);
}

TEST_CASE("physics.backend.parentDestructionInvalidatesBodyAndJoint") {
    auto                     world = std::make_unique<World3D>(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D>  a(world->newBody("static", 0.f, 0.f, 0.f));
    std::unique_ptr<Body3D>  b(world->newBody("dynamic", 1.f, 0.f, 0.f));
    std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f));
    world.reset();
    CHECK(!a->isValid());
    CHECK(!b->isValid());
    CHECK(!joint->isValid());
    auto position = b->localToWorldPointOwned(0.f, 0.f, 0.f);
    CHECK(!position.ok());
    REQUIRE(position.error() != nullptr);
    CHECK(position.error()->code() == eve::DiagnosticCode::StaleHandle);
    auto frames = joint->localFrames();
    CHECK(!frames.ok());
    REQUIRE(frames.error() != nullptr);
    CHECK(frames.error()->code() == eve::DiagnosticCode::StaleHandle);
}


TEST_CASE("physics.backend.sharedBodyPositionAndLifetimeContract") {
    World3D                 world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> native(world.newBody("dynamic", 0.f, 0.f, 0.f));
    checkPositionContract(*native);
    ProbeState state;
    Body3D     probe(nullptr, std::make_unique<BodyProbe>(state), 1, PhysicsBodyHandle::invalid());
    checkPositionContract(probe);
    CHECK(state.destroyed == 1);
}


TEST_CASE("physics.backend.failedMeshCookPreservesGeometryAndMaterials") {
    World3D                  world(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D>  body(world.newBody("static", 0.f, 0.f, 0.f));
    const std::vector<float> vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f, -1.f, 0.f, 1.f};
    std::unique_ptr<Shape3D> shape(body->newTriangleMeshShape(vertices, {0, 1, 2, 0, 2, 3}));
    shape->setTriangleMeshMaterialIndices({0, 1});
    bool rejected = false;
    try {
        shape->setTriangleMeshData(vertices, {0, 1, 99});
    } catch (const eve::Exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(shape->getTriangleMeshMaterialCount() == 2);
    CHECK(shape->getTriangleMeshMaterialIndex(1) == 1);
    CHECK(shape->isValid());
    shape->setTriangleMeshData(vertices, {0, 1, 2});
    CHECK(shape->getTriangleMeshMaterialCount() == 1);
    CHECK(shape->getTriangleMeshMaterialIndex(0) == 0);
}
