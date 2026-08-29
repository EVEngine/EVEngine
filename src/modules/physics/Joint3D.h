#pragma once

#include "physics/PhysicsHandles.h"

#include <box3d/id.h>

#include <string>

namespace eve::physics {

class Body3D;
class World3D;

/** @brief Script-facing Box3D joint owned by a World3D. */
class Joint3D {
public:
    /** @brief Supported joint geometry. */
    enum class Kind { Distance, Revolute, Prismatic, Spherical, Wheel };

    /** @brief Internal wrapper constructor; use World3D::new*Joint. */
    Joint3D(World3D *world, Body3D *bodyA, Body3D *bodyB, b3JointId jointId, PhysicsJointHandle runtimeHandle,
            Kind kind, int id);
    ~Joint3D();

    Joint3D(const Joint3D &) = delete;
    Joint3D &operator=(const Joint3D &) = delete;

    /** @brief Stable world-local identifier. */
    int getId() const { return id_; }
    /** @brief Process-local generation-qualified handle owned by World3D. */
    [[nodiscard]] PhysicsJointHandle runtimeHandle() const noexcept { return runtimeHandle_; }
    /** @brief Joint kind: "distance", "revolute", "prismatic", "spherical", or "wheel". */
    std::string getKind() const;
    /** @brief Stable ID of attached body A, or -1 after invalidation. */
    int getBodyAId() const;
    /** @brief Stable ID of attached body B, or -1 after invalidation. */
    int getBodyBId() const;
    /** @brief Owning world, or null after joint destruction. */
    [[nodiscard]] World3D *getWorld() const noexcept { return world_; }
    /** @brief Enables or disables collision between the attached bodies. */
    void setCollideConnected(bool collide);
    /** @brief Whether attached bodies may collide. */
    bool getCollideConnected() const;
    /** @brief Sets the force threshold that emits a joint-stress event, in newtons. */
    void setForceThreshold(float threshold);
    /** @brief Current force event threshold in newtons. */
    float getForceThreshold() const;
    /** @brief Sets the torque threshold that emits a joint-stress event, in newton-metres. */
    void setTorqueThreshold(float threshold);
    /** @brief Current torque event threshold in newton-metres. */
    float getTorqueThreshold() const;

    /** @brief Current world-space constraint force X in newtons. */
    float getConstraintForceX() const;
    /** @brief Current world-space constraint force Y in newtons. */
    float getConstraintForceY() const;
    /** @brief Current world-space constraint force Z in newtons. */
    float getConstraintForceZ() const;
    /** @brief Current world-space constraint torque X in newton-metres. */
    float getConstraintTorqueX() const;
    /** @brief Current world-space constraint torque Y in newton-metres. */
    float getConstraintTorqueY() const;
    /** @brief Current world-space constraint torque Z in newton-metres. */
    float getConstraintTorqueZ() const;
    /** @brief Current inadmissible linear separation error in metres. */
    float getLinearSeparation() const;
    /** @brief Current inadmissible angular separation error in radians. */
    float getAngularSeparation() const;

    /** @brief Changes distance-joint rest length in metres. */
    void setDistanceLength(float length);
    /** @brief Distance-joint rest length in metres. */
    float getDistanceLength() const;
    /** @brief Current anchor-to-anchor distance in metres. */
    float getDistanceCurrentLength() const;
    /** @brief Configures a distance spring; hertz and damping must be non-negative. */
    void setDistanceSpring(bool enabled, float hertz, float dampingRatio);
    /** @brief Configures optional distance limits in metres. */
    void setDistanceLimits(bool enabled, float minimum, float maximum);
    /** @brief Configures the distance motor in metres/second and newtons. */
    void setDistanceMotor(bool enabled, float speed, float maxForce);

    /** @brief Configures a revolute angular spring in radians. */
    void setRevoluteSpring(bool enabled, float targetAngle, float hertz, float dampingRatio);
    /** @brief Configures revolute angular limits in radians. */
    void setRevoluteLimits(bool enabled, float lower, float upper);
    /** @brief Configures the revolute motor in radians/second and newton-metres. */
    void setRevoluteMotor(bool enabled, float speed, float maxTorque);
    /** @brief Current revolute angle in radians. */
    float getRevoluteAngle() const;
    /** @brief Current revolute motor torque in newton-metres. */
    float getRevoluteMotorTorque() const;

    /** @brief Configures a prismatic linear spring and target translation. */
    void setPrismaticSpring(bool enabled, float targetTranslation, float hertz,
                            float dampingRatio);
    /** @brief Configures prismatic translation limits in metres. */
    void setPrismaticLimits(bool enabled, float lower, float upper);
    /** @brief Configures a prismatic motor in metres/second and newtons. */
    void setPrismaticMotor(bool enabled, float speed, float maxForce);
    /** @brief Current prismatic translation in metres. */
    float getPrismaticTranslation() const;
    /** @brief Current prismatic translation speed in metres/second. */
    float getPrismaticSpeed() const;
    /** @brief Current prismatic motor force in newtons. */
    float getPrismaticMotorForce() const;

    /** @brief Enables a spherical cone limit in radians, within [0, pi]. */
    void setSphericalConeLimit(bool enabled, float angle);
    /** @brief Enables spherical twist limits in radians. */
    void setSphericalTwistLimits(bool enabled, float lower, float upper);
    /** @brief Configures spherical motor velocity and maximum torque. */
    void setSphericalMotor(bool enabled, float velocityX, float velocityY, float velocityZ,
                           float maxTorque);
    /** @brief Current spherical cone angle in radians. */
    float getSphericalConeAngle() const;
    /** @brief Current spherical twist angle in radians. */
    float getSphericalTwistAngle() const;

    /** @brief Configures wheel suspension spring stiffness and damping. */
    void setWheelSuspension(bool enabled, float hertz, float dampingRatio);
    /** @brief Configures wheel suspension travel limits in metres. */
    void setWheelSuspensionLimits(bool enabled, float lower, float upper);
    /** @brief Configures wheel spin motor speed and maximum torque. */
    void setWheelSpinMotor(bool enabled, float speed, float maxTorque);
    /** @brief Configures steering target, stiffness, damping and maximum torque. */
    void setWheelSteering(bool enabled, float targetAngle, float hertz, float dampingRatio,
                          float maxTorque);
    /** @brief Configures steering angular limits in radians. */
    void setWheelSteeringLimits(bool enabled, float lower, float upper);
    /** @brief Current wheel spin speed in radians/second. */
    float getWheelSpinSpeed() const;
    /** @brief Current wheel spin motor torque in newton-metres. */
    float getWheelSpinTorque() const;
    /** @brief Current wheel steering angle in radians. */
    float getWheelSteeringAngle() const;
    /** @brief Current steering torque in newton-metres. */
    float getWheelSteeringTorque() const;

    /** @brief Destroys the backend joint and invalidates this wrapper. */
    void destroy();
    /** @brief Whether the backend joint still exists. */
    bool isValid() const;
    /** @brief Internal invalidation used by body/world destruction. */
    void invalidate();
    /** @brief Internal raw Box3D identifier. */
    b3JointId raw() const { return jointId_; }

private:
    friend class Body3D;
    friend class World3D;
    void requireKind(Kind expected, const char *operation) const;

    World3D *world_ = nullptr;
    Body3D *bodyA_ = nullptr;
    Body3D *bodyB_ = nullptr;
    b3JointId jointId_{};
    PhysicsJointHandle runtimeHandle_ = PhysicsJointHandle::invalid();
    Kind kind_ = Kind::Distance;
    int id_ = 0;
};

}  // namespace eve::physics
