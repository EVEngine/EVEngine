#pragma once
#include "common/Export.h"

#include "physics/PhysicsHandles.h"

#include <string>

class b2Joint;

namespace eve::physics {

class Body;
class World;

/**
 * @brief Script-facing Box2D joint owned by a World.
 *
 * Anchors, lengths and linear motor quantities use pixel space (same as Body).
 * Angles and angular motors use radians.
 */
class EVENGINE_API_WORLD Joint2D {
public:
    /** @brief Supported joint geometry.
     * @remarks Kind codes: 0 Distance, 1 Revolute, 2 Prismatic, 3 Weld, 4 Wheel,
     *          5 Motor, 6 Gear.
     */
    enum class Kind { Distance, Revolute, Prismatic, Weld, Wheel, Motor, Gear };

    /** @brief Internal wrapper constructor; use World::new*Joint. */
    Joint2D(World *world, Body *bodyA, Body *bodyB, b2Joint *joint, PhysicsJointHandle runtimeHandle,
            Kind kind, int id);
    ~Joint2D();

    Joint2D(const Joint2D &) = delete;
    Joint2D &operator=(const Joint2D &) = delete;

    /** @brief Stable world-local identifier. */
    int getId() const { return id_; }
    /** @brief Process-local generation-qualified handle owned by World. */
    PhysicsJointHandle runtimeHandle() const { return runtimeHandle_; }
    /** @brief Joint kind string for script/debug. */
    std::string getKind() const;
    /** @brief Stable ID of attached body A, or -1 after invalidation. */
    int getBodyAId() const;
    /** @brief Stable ID of attached body B, or -1 after invalidation. */
    int getBodyBId() const;
    /**
     * @brief Owning world, or null after joint destruction.
     * @return Borrowed world pointer.
     * @ownership World owns this joint; the returned world pointer is borrowed.
     * @lifetime Valid until World::destroy() or this joint's invalidation.
     */
    World *getWorld() const { return world_; }
    /** @brief Enables or disables collision between the attached bodies. */
    void setCollideConnected(bool collide);
    /** @brief Whether attached bodies may collide. */
    bool getCollideConnected() const;

    /** @brief Changes distance-joint rest length in pixels. */
    void setDistanceLength(float lengthPixels);
    /** @brief Distance-joint rest length in pixels. */
    float getDistanceLength() const;
    /** @brief Configures distance spring frequency (Hz) and damping ratio. */
    void setDistanceSpring(float frequencyHz, float dampingRatio);
    /** @brief Distance spring frequency in Hertz. */
    float getDistanceFrequency() const;
    /** @brief Distance spring damping ratio. */
    float getDistanceDampingRatio() const;

    /** @brief Configures revolute angular limits in radians. */
    void setRevoluteLimits(bool enabled, float lower, float upper);
    /** @brief Configures the revolute motor in radians/second and newton-metres. */
    void setRevoluteMotor(bool enabled, float speed, float maxTorque);
    /** @brief Current revolute angle in radians. */
    float getRevoluteAngle() const;
    /** @brief Current revolute speed in radians/second. */
    float getRevoluteSpeed() const;
    /** @brief Current revolute motor torque in newton-metres. */
    float getRevoluteMotorTorque() const;

    /** @brief Configures prismatic translation limits in pixels. */
    void setPrismaticLimits(bool enabled, float lowerPixels, float upperPixels);
    /** @brief Configures a prismatic motor in pixels/second and pixel-force units. */
    void setPrismaticMotor(bool enabled, float speedPixels, float maxForcePixels);
    /** @brief Current prismatic translation in pixels. */
    float getPrismaticTranslation() const;
    /** @brief Current prismatic speed in pixels/second. */
    float getPrismaticSpeed() const;

    /** @brief Configures weld rotational spring frequency and damping. */
    void setWeldSpring(float frequencyHz, float dampingRatio);
    /** @brief Weld spring frequency in Hertz. */
    float getWeldFrequency() const;
    /** @brief Weld spring damping ratio. */
    float getWeldDampingRatio() const;

    /** @brief Configures wheel suspension spring. */
    void setWheelSpring(float frequencyHz, float dampingRatio);
    /** @brief Configures wheel spin motor in radians/second and newton-metres. */
    void setWheelMotor(bool enabled, float speed, float maxTorque);
    /** @brief Current wheel translation along the suspension axis in pixels. */
    float getWheelTranslation() const;
    /** @brief Current wheel spin speed in radians/second. */
    float getWheelSpeed() const;

    /** @brief Sets motor-joint linear offset in pixels (bodyA frame). */
    void setMotorLinearOffset(float xPixels, float yPixels);
    /** @brief Sets motor-joint angular offset in radians. */
    void setMotorAngularOffset(float radians);
    /** @brief Sets motor max force (pixel-force) and torque (N·m). */
    void setMotorLimits(float maxForcePixels, float maxTorque);
    /** @brief Motor linear offset X in pixels. */
    float getMotorLinearOffsetX() const;
    /** @brief Motor linear offset Y in pixels. */
    float getMotorLinearOffsetY() const;
    /** @brief Motor angular offset in radians. */
    float getMotorAngularOffset() const;

    /** @brief Changes gear ratio. */
    void setGearRatio(float ratio);
    /** @brief Current gear ratio. */
    float getGearRatio() const;
    /**
     * @brief First joint linked by a gear, or null.
     * @return Borrowed joint owned by World.
     * @ownership World owns the joint; this wrapper only borrows it.
     * @lifetime Valid until that joint or this gear is destroyed.
     */
    Joint2D *getGearJoint1() const { return gearJoint1_; }
    /**
     * @brief Second joint linked by a gear, or null.
     * @return Borrowed joint owned by World.
     * @ownership World owns the joint; this wrapper only borrows it.
     * @lifetime Valid until that joint or this gear is destroyed.
     */
    Joint2D *getGearJoint2() const { return gearJoint2_; }

    /** @brief Destroys the backend joint and invalidates this wrapper. */
    void destroy();
    /** @brief Whether the backend joint still exists. */
    bool isValid() const;
    /** @brief Internal invalidation used by body/world destruction. */
    void invalidate();
    /** @brief Internal raw Box2D joint. */
    b2Joint *raw() const { return joint_; }

private:
    friend class Body;
    friend class World;
    friend class Mechanism2D;
    void requireKind(Kind expected, const char *operation) const;
    void setGearMembers(Joint2D *joint1, Joint2D *joint2);

    World *world_ = nullptr;
    Body *bodyA_ = nullptr;
    Body *bodyB_ = nullptr;
    b2Joint *joint_ = nullptr;
    PhysicsJointHandle runtimeHandle_ = PhysicsJointHandle::invalid();
    Kind kind_ = Kind::Distance;
    int id_ = 0;
    Joint2D *gearJoint1_ = nullptr;
    Joint2D *gearJoint2_ = nullptr;
};

}  // namespace eve::physics
