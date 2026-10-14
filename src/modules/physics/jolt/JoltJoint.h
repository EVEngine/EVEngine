#pragma once
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include "physics/backend/JointBackend3D.h"
#include "physics/jolt/JoltDistanceConstraint.h"
#include "physics/jolt/JoltInternal.h"
namespace eve::physics::jolt {
struct JointState {
    std::weak_ptr<State>             world;
    std::weak_ptr<BodyState>         a, b;
    JPH::Ref<JPH::TwoBodyConstraint> native;
    JPH::Ref<JPH::SixDOFConstraint>  velocity;
    JPH::Vec3     worldLinearVelocity = JPH::Vec3::sZero(), worldAngularVelocity = JPH::Vec3::sZero();
    float         maxVelocityForce = 0, maxVelocityTorque = 0;
    JointFrames3D frames;
    JointKind3D   kind;
    Joint3D*      owner = nullptr;
    bool          alive = true, collide = false;
    float         length = 0, forceThreshold = FLT_MAX, torqueThreshold = FLT_MAX;
};
/** @brief Sole facade provider; parent invalidation removes the native constraint before either body dies. */
class JointBackend final : public IJointBackend3D {
public:
    std::shared_ptr<JointState> joint;
    explicit JointBackend(std::shared_ptr<JointState> value) : joint(std::move(value)) {}
    ~JointBackend() override;
    void                       bindOwner(Joint3D&) noexcept override;
    bool                       isValid() const noexcept override;
    void                       destroy() noexcept override;
    void                       invalidate() noexcept override;
    eve::Result<JointFrames3D> localFrames() const override;
    eve::Result<void>          setCollideConnected(bool collide) override;
    eve::Result<bool>          getCollideConnected() const override;
    eve::Result<void>          setForceThreshold(float threshold) override;
    eve::Result<float>         getForceThreshold() const override;
    eve::Result<void>          setTorqueThreshold(float threshold) override;
    eve::Result<float>         getTorqueThreshold() const override;
    eve::Result<float>         getConstraintForceX() const override;
    eve::Result<float>         getConstraintForceY() const override;
    eve::Result<float>         getConstraintForceZ() const override;
    eve::Result<float>         getConstraintTorqueX() const override;
    eve::Result<float>         getConstraintTorqueY() const override;
    eve::Result<float>         getConstraintTorqueZ() const override;
    eve::Result<float>         getLinearSeparation() const override;
    eve::Result<float>         getAngularSeparation() const override;
    eve::Result<void>          setDistanceLength(float length) override;
    eve::Result<float>         getDistanceLength() const override;
    eve::Result<float>         getDistanceCurrentLength() const override;
    eve::Result<void>          setDistanceSpring(bool enabled, float hertz, float dampingRatio) override;
    eve::Result<void>          setDistanceLimits(bool enabled, float minimum, float maximum) override;
    eve::Result<void>          setDistanceMotor(bool enabled, float speed, float maxForce) override;
    eve::Result<void>  setRevoluteSpring(bool enabled, float targetAngle, float hertz, float dampingRatio) override;
    eve::Result<void>  setRevoluteLimits(bool enabled, float lower, float upper) override;
    eve::Result<void>  setRevoluteMotor(bool enabled, float speed, float maxTorque) override;
    eve::Result<float> getRevoluteAngle() const override;
    eve::Result<float> getRevoluteMotorTorque() const override;
    eve::Result<void>  setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                          float dampingRatio) override;
    eve::Result<void>  setPrismaticLimits(bool enabled, float lower, float upper) override;
    eve::Result<void>  setPrismaticMotor(bool enabled, float speed, float maxForce) override;
    eve::Result<float> getPrismaticTranslation() const override;
    eve::Result<float> getPrismaticSpeed() const override;
    eve::Result<float> getPrismaticMotorForce() const override;
    eve::Result<void>  setSphericalConeLimit(bool enabled, float angle) override;
    eve::Result<void>  setSphericalTwistLimits(bool enabled, float lower, float upper) override;
    eve::Result<void>  setSphericalMotor(bool enabled, float velocityX, float velocityY, float velocityZ,
                                         float maxTorque) override;
    eve::Result<float> getSphericalConeAngle() const override;
    eve::Result<float> getSphericalTwistAngle() const override;
    eve::Result<void>  setWheelSuspension(bool enabled, float hertz, float dampingRatio) override;
    eve::Result<void>  setWheelSuspensionLimits(bool enabled, float lower, float upper) override;
    eve::Result<void>  setWheelSpinMotor(bool enabled, float speed, float maxTorque) override;
    eve::Result<void>  setWheelSteering(bool enabled, float targetAngle, float hertz, float dampingRatio,
                                        float maxTorque) override;
    eve::Result<void>  setWheelSteeringLimits(bool enabled, float lower, float upper) override;
    eve::Result<float> getWheelSpinSpeed() const override;
    eve::Result<float> getWheelSpinTorque() const override;
    eve::Result<float> getWheelSteeringAngle() const override;
    eve::Result<float> getWheelSteeringTorque() const override;
    eve::Result<void>  setWeldLinearSpring(float hertz, float dampingRatio) override;
    eve::Result<void>  setWeldAngularSpring(float hertz, float dampingRatio) override;
    eve::Result<float> getWeldLinearHertz() const override;
    eve::Result<float> getWeldLinearDampingRatio() const override;
    eve::Result<float> getWeldAngularHertz() const override;
    eve::Result<float> getWeldAngularDampingRatio() const override;
    eve::Result<void>  setMotorLinearVelocity(float vx, float vy, float vz) override;
    eve::Result<void>  setMotorAngularVelocity(float wx, float wy, float wz) override;
    eve::Result<void>  setMotorVelocityLimits(float maxForce, float maxTorque) override;
    eve::Result<void>  setMotorLinearSpring(float hertz, float dampingRatio, float maxForce) override;
    eve::Result<void>  setMotorAngularSpring(float hertz, float dampingRatio, float maxTorque) override;
    eve::Result<float> getMotorLinearVelocityX() const override;
    eve::Result<float> getMotorLinearVelocityY() const override;
    eve::Result<float> getMotorLinearVelocityZ() const override;
    eve::Result<float> getMotorAngularVelocityX() const override;
    eve::Result<float> getMotorAngularVelocityY() const override;
    eve::Result<float> getMotorAngularVelocityZ() const override;
    eve::Result<void>  setParallelSpring(float hertz, float dampingRatio, float maxTorque) override;
    eve::Result<float> getParallelHertz() const override;
    eve::Result<float> getParallelDampingRatio() const override;
    eve::Result<float> getParallelMaxTorque() const override;

private:
    [[nodiscard]] eve::Result<void> wakeBodies() const;
    [[nodiscard]] eve::Result<void> requireKind(JointKind3D) const;
    JPH::SixDOFConstraint&          six() const;
    JPH::SixDOFConstraint&          drive() const;
    JPH::RMat44                     frame(bool second) const;
    JPH::Vec3                       force(bool torque) const;
};
/** @brief Counts suppression references for one native pair; sequence numbers prevent reuse aliases. */
JPH::uint64 pairKey(JPH::BodyID, JPH::BodyID);
/** @brief Recomputes constraint COM offsets after a successful native shape replacement. */
void notifyJointShapeChanged(State&, JPH::BodyID, JPH::Vec3Arg);
}  // namespace eve::physics::jolt
