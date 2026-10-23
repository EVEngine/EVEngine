#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "physics/PhysicsHandles.h"

#include <array>
#include <memory>

class b2Joint;
namespace eve::physics {
class World;
class Body;

/** @brief Closed set of planar mechanical constraints. Gear couples two existing joints. */
enum class Joint2DKind { Revolute, Prismatic, Distance, Pulley, Gear, Wheel, Weld, Motor };

/** @brief Owning world-space point/direction in SI (meters), not pixel coordinates. */
struct JointPoint2D {
    float x = 0, y = 0;
};

/**
 * @brief Validated-at-creation joint parameters; all lengths are meters, angles radians.
 * @details anchorA/B are body attachment points in world space; revolute/prismatic/wheel/weld
 * use anchorA as their common world anchor. axis is normalized after validation.
 * Distance length must be positive. Pulley ground anchors are fixed world points and ratio > 0.
 * springHertz/dampingRatio configure distance/wheel/weld softness (zero Hertz is rigid).
 */
struct Joint2DSettings {
    Joint2DKind  kind = Joint2DKind::Revolute;
    JointPoint2D anchorA, anchorB;
    JointPoint2D axis{1, 0};
    JointPoint2D groundA{-1, 1}, groundB{1, 1};
    float        length = 1, ratio = 1;
    float        springHertz = 0, dampingRatio = 0;
    bool         collideConnected = false;
};

/** @brief Owning solver observation. Coordinate is angle/radians or translation/meters by kind. */
struct Joint2DState {
    Joint2DKind  kind = Joint2DKind::Revolute;
    JointPoint2D anchorA, anchorB, reactionForce;
    float        reactionTorque = 0;
    float        coordinate     = 0;  ///< Distance length, revolute angle, prismatic/wheel translation, or gear ratio.
    float        secondaryCoordinate = 0;  ///< Pulley segment B; coordinate is pulley segment A.
};

/**
 * @brief Owning planar joint wrapper observing World-owned backend state.
 * @ownership Caller/VM owns factory wrappers; World owns restored wrappers and all solver joints.
 * @lifetime Body/world/source-joint destruction or restore invalidates observers; never retain a raw backend pointer.
 * @thread All methods and destruction are simulation-owner-thread only, outside solver callbacks/step.
 * @reentrancy No callbacks; no locks or reentrant world mutation. Inputs are borrowed only during calls.
 * @details Gear source identities use generation handles. Source-first destroys dependent gears;
 * gear-first leaves sources intact. No state is reconstructed from pointer addresses.
 */
class EVENGINE_API_WORLD Joint2D {
public:
    using Kind = Joint2DKind;
    /** @brief Joint kind string for script/debug. */
    std::string getKind() const;
    /** @brief Stable ID of attached body A, or -1 after invalidation. */
    int getBodyAId() const;
    /** @brief Stable ID of attached body B, or -1 after invalidation. */
    int getBodyBId() const;
    /**
     * @brief Owning world, or null after joint destruction.
     * @return Borrowed world pointer.
     * @ownership The returned world pointer is borrowed; wrapper ownership follows the factory contract.
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
    Joint2D* getGearJoint1() const;
    /**
     * @brief Second joint linked by a gear, or null.
     * @return Borrowed joint owned by World.
     * @ownership World owns the joint; this wrapper only borrows it.
     * @lifetime Valid until that joint or this gear is destroyed.
     */
    Joint2D* getGearJoint2() const;


    /** @brief Internal borrowed solver joint; valid only while this wrapper is valid. */
    b2Joint *raw() const { return joint_; }
    /** @brief Legacy throwing destruction; delegates to destroyChecked(). */
    void destroy();
    /** @brief Release this joint and dependent gears on the unlocked simulation owner thread. */
    ~Joint2D();
    Joint2D(const Joint2D&)            = delete;
    Joint2D& operator=(const Joint2D&) = delete;

    /** @brief Create a constraint; rejects invalid input, foreign/dead bodies, Gear kind and locked worlds atomically.
     * @return Owning wrapper or InvalidArgument/PreconditionViolation; caller owns the successful value.
     */
    [[nodiscard]] static Result<std::unique_ptr<Joint2D>> create(World& world, Body& a, Body& b,
                                                                 const Joint2DSettings& settings);
    /** @brief Couple two revolute/prismatic joints in the same world with coordinate1 + ratio*coordinate2 constant.
     * @param ratio Finite nonzero signed transmission ratio; mixed rotation/translation has length-dependent units.
     * @return Owning gear or structured failure. Source joints must have distinct dynamic body-B endpoints.
     */
    [[nodiscard]] static Result<std::unique_ptr<Joint2D>> createGear(World& world, Joint2D& first, Joint2D& second,
                                                                     float ratio);
    /** @brief True until wrapper release or a body/world/source-joint is destroyed. */
    [[nodiscard]] bool isValid() const noexcept;
    /** @brief Process-local generation handle; invalid after destruction, never serialized. */
    [[nodiscard]] PhysicsJointHandle runtimeHandle() const noexcept { return handle_; }
    /** @brief World-local persistent topology id, retained after invalidation; not a runtime handle. */
    [[nodiscard]] int getId() const noexcept { return id_; }
    /** @brief Constraint kind, unchanged after invalidation. */
    [[nodiscard]] Joint2DKind kind() const noexcept { return kind_; }
    /** @brief Set revolute/prismatic/wheel motor; speed rad/s for revolute/wheel, m/s for prismatic.
     * @param maximumForce Nonnegative maximum Nm for revolute/wheel, N for prismatic.
     * @return Success, StaleHandle, Unsupported kind or invalid input; failures do not mutate.
     */
    [[nodiscard]] Result<void> setMotor(bool enabled, float speed, float maximumForce);
    /** @brief Set revolute angular or prismatic linear limits; lower <= upper.
     * @return Success or structured failure before mutation. Other kinds return Unsupported.
     */
    [[nodiscard]] Result<void> setLimits(bool enabled, float lower, float upper);
    /** @brief Read owning SI observation; dt must be the positive duration of the last completed solver step.
     * @return Solver observation or invalid input/stale diagnostic; never retains world references.
     */
    [[nodiscard]] Result<Joint2DState> sample(float dt) const;
    /** @brief Remove this constraint and dependent gears; repeat destruction is successful.
     * @return PreconditionViolation when called during a locked solver step; no mutation then.
     */
    [[nodiscard]] Result<void> destroyChecked();

private:
    friend class Body;
    friend class Mechanism2D;
    void requireKind(Kind expected, const char *operation) const;
    friend class World;
    friend struct WorldSnapshotAccess;
    friend struct PhysicsJointLink;
    Joint2D(World& world, b2Joint& joint, Joint2DKind kind, PhysicsJointHandle handle);
    void release();
    void invalidate() noexcept;
    int  id_ = 0;
    // Immutable constructor inputs for backend constants without public getters.
    std::array<float, 12> gearReferencePoses_{};
    JointPoint2D          pulleyLocalA_, pulleyLocalB_;
    World*                world_ = nullptr;
    b2Joint*              joint_ = nullptr;
    Joint2DKind           kind_;
    PhysicsJointHandle    handle_;
    PhysicsJointHandle    firstSource_  = PhysicsJointHandle::invalid();
    PhysicsJointHandle    secondSource_ = PhysicsJointHandle::invalid();
};
}  // namespace eve::physics
