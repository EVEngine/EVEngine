#pragma once
#include <array>
#include "common/Result.h"

namespace eve::physics {
class Joint3D;
/** @brief Owning joint frames: local XYZ position followed by quaternion XYZW for each body. */
struct JointFrames3D {
    std::array<float, 7> bodyA{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    std::array<float, 7> bodyB{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f};
};
/** @brief Stable constraint kinds; numeric values are used by stress events. */
enum class JointKind3D { Distance, Revolute, Prismatic, Spherical, Wheel, Weld, Motor, Parallel, Filter };
/**
 * @brief Solver-independent control of one constraint, owned exclusively by its Joint3D facade.
 * @ownership The implementation owns the native constraint until destroy or parent invalidation.
 * @lifetime No native identifier escapes; invalidate detaches before parent body/world teardown.
 * @thread All calls run on the owning simulation thread, between steps.
 * @reentrancy Calls never invoke user callbacks. Setters validate all inputs before mutation.
 * @remarks Results report StaleHandle, InvalidArgument, or Unsupported without partial mutation.
 * Reads return owning values. Implementations must not emulate unsupported features silently.
 */
class IJointBackend3D {
public:
    /** @brief Binds its exclusive facade owner for internal attribution, on the simulation thread.
     * @ownership Borrowed owner remains alive until invalidate; no external callbacks are invoked.
     */
    virtual void bindOwner(Joint3D& owner) noexcept = 0;
    virtual ~IJointBackend3D()                      = default;
    /** @brief Whether the native constraint is still alive. */
    virtual bool isValid() const noexcept = 0;
    /** @brief Idempotently destroys the constraint; no failure or callbacks. */
    virtual void destroy() noexcept = 0;
    /** @brief Detaches before the parent destroys its constraints; no failure or callbacks. */
    virtual void invalidate() noexcept = 0;
    /** @brief localFrames; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<JointFrames3D> localFrames() const = 0;
    /** @brief setCollideConnected; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setCollideConnected(bool collide) = 0;
    /** @brief getCollideConnected; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<bool> getCollideConnected() const = 0;
    /** @brief setForceThreshold; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setForceThreshold(float threshold) = 0;
    /** @brief getForceThreshold; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getForceThreshold() const = 0;
    /** @brief setTorqueThreshold; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setTorqueThreshold(float threshold) = 0;
    /** @brief getTorqueThreshold; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getTorqueThreshold() const = 0;
    /** @brief getConstraintForceX; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintForceX() const = 0;
    /** @brief getConstraintForceY; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintForceY() const = 0;
    /** @brief getConstraintForceZ; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintForceZ() const = 0;
    /** @brief getConstraintTorqueX; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintTorqueX() const = 0;
    /** @brief getConstraintTorqueY; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintTorqueY() const = 0;
    /** @brief getConstraintTorqueZ; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getConstraintTorqueZ() const = 0;
    /** @brief getLinearSeparation; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getLinearSeparation() const = 0;
    /** @brief getAngularSeparation; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getAngularSeparation() const = 0;
    /** @brief setDistanceLength; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setDistanceLength(float length) = 0;
    /** @brief getDistanceLength; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getDistanceLength() const = 0;
    /** @brief getDistanceCurrentLength; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getDistanceCurrentLength() const = 0;
    /** @brief setDistanceSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setDistanceSpring(bool enabled, float hertz, float dampingRatio) = 0;
    /** @brief setDistanceLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setDistanceLimits(bool enabled, float minimum, float maximum) = 0;
    /** @brief setDistanceMotor; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setDistanceMotor(bool enabled, float speed, float maxForce) = 0;
    /** @brief setRevoluteSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setRevoluteSpring(bool enabled, float targetAngle, float hertz,
                                                              float dampingRatio) = 0;
    /** @brief setRevoluteLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setRevoluteLimits(bool enabled, float lower, float upper) = 0;
    /** @brief setRevoluteMotor; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setRevoluteMotor(bool enabled, float speed, float maxTorque) = 0;
    /** @brief getRevoluteAngle; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRevoluteAngle() const = 0;
    /** @brief getRevoluteMotorTorque; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getRevoluteMotorTorque() const = 0;
    /** @brief setPrismaticSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                                                               float dampingRatio) = 0;
    /** @brief setPrismaticLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setPrismaticLimits(bool enabled, float lower, float upper) = 0;
    /** @brief setPrismaticMotor; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setPrismaticMotor(bool enabled, float speed, float maxForce) = 0;
    /** @brief getPrismaticTranslation; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getPrismaticTranslation() const = 0;
    /** @brief getPrismaticSpeed; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getPrismaticSpeed() const = 0;
    /** @brief getPrismaticMotorForce; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getPrismaticMotorForce() const = 0;
    /** @brief setSphericalConeLimit; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setSphericalConeLimit(bool enabled, float angle) = 0;
    /** @brief setSphericalTwistLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setSphericalTwistLimits(bool enabled, float lower, float upper) = 0;
    /** @brief setSphericalMotor; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setSphericalMotor(bool enabled, float velocityX, float velocityY,
                                                              float velocityZ, float maxTorque) = 0;
    /** @brief getSphericalConeAngle; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getSphericalConeAngle() const = 0;
    /** @brief getSphericalTwistAngle; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getSphericalTwistAngle() const = 0;
    /** @brief setWheelSuspension; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWheelSuspension(bool enabled, float hertz, float dampingRatio) = 0;
    /** @brief setWheelSuspensionLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWheelSuspensionLimits(bool enabled, float lower, float upper) = 0;
    /** @brief setWheelSpinMotor; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWheelSpinMotor(bool enabled, float speed, float maxTorque) = 0;
    /** @brief setWheelSteering; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWheelSteering(bool enabled, float targetAngle, float hertz,
                                                             float dampingRatio, float maxTorque) = 0;
    /** @brief setWheelSteeringLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWheelSteeringLimits(bool enabled, float lower, float upper) = 0;
    /** @brief getWheelSpinSpeed; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWheelSpinSpeed() const = 0;
    /** @brief getWheelSpinTorque; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWheelSpinTorque() const = 0;
    /** @brief getWheelSteeringAngle; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWheelSteeringAngle() const = 0;
    /** @brief getWheelSteeringTorque; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWheelSteeringTorque() const = 0;
    /** @brief setWeldLinearSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWeldLinearSpring(float hertz, float dampingRatio) = 0;
    /** @brief setWeldAngularSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setWeldAngularSpring(float hertz, float dampingRatio) = 0;
    /** @brief getWeldLinearHertz; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWeldLinearHertz() const = 0;
    /** @brief getWeldLinearDampingRatio; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWeldLinearDampingRatio() const = 0;
    /** @brief getWeldAngularHertz; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWeldAngularHertz() const = 0;
    /** @brief getWeldAngularDampingRatio; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getWeldAngularDampingRatio() const = 0;
    /** @brief setMotorLinearVelocity; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotorLinearVelocity(float vx, float vy, float vz) = 0;
    /** @brief setMotorAngularVelocity; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotorAngularVelocity(float wx, float wy, float wz) = 0;
    /** @brief setMotorVelocityLimits; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotorVelocityLimits(float maxForce, float maxTorque) = 0;
    /** @brief setMotorLinearSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotorLinearSpring(float hertz, float dampingRatio, float maxForce) = 0;
    /** @brief setMotorAngularSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setMotorAngularSpring(float hertz, float dampingRatio, float maxTorque) = 0;
    /** @brief getMotorLinearVelocityX; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorLinearVelocityX() const = 0;
    /** @brief getMotorLinearVelocityY; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorLinearVelocityY() const = 0;
    /** @brief getMotorLinearVelocityZ; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorLinearVelocityZ() const = 0;
    /** @brief getMotorAngularVelocityX; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorAngularVelocityX() const = 0;
    /** @brief getMotorAngularVelocityY; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorAngularVelocityY() const = 0;
    /** @brief getMotorAngularVelocityZ; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getMotorAngularVelocityZ() const = 0;
    /** @brief setParallelSpring; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<void> setParallelSpring(float hertz, float dampingRatio, float maxTorque) = 0;
    /** @brief getParallelHertz; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getParallelHertz() const = 0;
    /** @brief getParallelDampingRatio; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getParallelDampingRatio() const = 0;
    /** @brief getParallelMaxTorque; see the class lifetime, validation and failure contract. */
    [[nodiscard]] virtual eve::Result<float> getParallelMaxTorque() const = 0;
};
}  // namespace eve::physics
