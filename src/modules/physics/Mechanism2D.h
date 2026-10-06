#pragma once
#include "common/Export.h"

#include <string>

namespace eve::physics {

class Body;
class Joint2D;
class World;

/**
 * @brief Composed 2D mechanical assembly owned by a World.
 *
 * Builds crank-slider, shaft, and ratchet mechanisms from Joint2D primitives.
 * Ratchet one-way locking is applied automatically during World::step.
 *
 * @ownership World owns the mechanism and the joints it creates. Borrowed Body
 *            inputs are never retained as caller ownership.
 * @lifetime Valid until Mechanism2D::destroy(), World::destroy(), or dependent
 *           body/joint invalidation.
 * @thread Call on the owning physics thread.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_WORLD Mechanism2D {
public:
    /** @brief Assembly kind. */
    enum class Kind { CrankSlider, Shaft, Ratchet };

    Mechanism2D(const Mechanism2D &) = delete;
    Mechanism2D &operator=(const Mechanism2D &) = delete;

    /** @brief Stable world-local identifier. */
    int getId() const { return id_; }
    /** @brief Kind name: "crankSlider", "shaft", or "ratchet". */
    std::string getKind() const;
    /**
     * @brief Owning world, or null after invalidation.
     * @return Borrowed world pointer.
     * @ownership World owns this mechanism; the returned pointer is borrowed.
     * @lifetime Valid until World::destroy() or this mechanism's invalidation.
     */
    World *getWorld() const { return world_; }
    /** @brief Whether the assembly and its primary joints are still live. */
    bool isValid() const;

    /**
     * @brief Primary revolute (crank / shaft / ratchet wheel).
     * @return Borrowed joint owned by the parent World, or null after invalidation.
     * @ownership World owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism2D::destroy(), Joint2D::destroy(), or World::destroy().
     */
    Joint2D *getDriveJoint() const { return drive_; }
    /**
     * @brief Crank-to-rod revolute, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World, or null when unused/invalid.
     * @ownership World owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism2D::destroy(), Joint2D::destroy(), or World::destroy().
     */
    Joint2D *getCrankPinJoint() const { return crankPin_; }
    /**
     * @brief Rod-to-slider revolute, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World, or null when unused/invalid.
     * @ownership World owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism2D::destroy(), Joint2D::destroy(), or World::destroy().
     */
    Joint2D *getSliderPinJoint() const { return sliderPin_; }
    /**
     * @brief Frame-to-slider prismatic, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World, or null when unused/invalid.
     * @ownership World owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism2D::destroy(), Joint2D::destroy(), or World::destroy().
     */
    Joint2D *getSliderJoint() const { return slider_; }

    /** @brief Current drive revolute angle in radians. */
    float getDriveAngle() const;
    /** @brief Current slider translation in pixels (crank-slider only). */
    float getSliderTranslation() const;
    /** @brief Relative spin speed in radians/second. */
    float getSpinSpeed() const;

    /**
     * @brief Enables a continuous drive motor on the primary revolute.
     * @param speed Target speed in radians/second (ratchet remaps onto freewheel direction).
     * @param maxTorque Maximum motor torque in newton-metres.
     */
    void setDrive(float speed, float maxTorque);
    /** @brief Disables the continuous drive motor. */
    void clearDrive();
    /** @brief Whether a continuous drive motor is configured. */
    bool isDriveEnabled() const { return driveEnabled_; }

    /** @brief Sets ratchet freewheel direction (+1 or -1). */
    void setRatchetDirection(int direction);
    /** @brief Current ratchet freewheel direction (+1 or -1). */
    int getRatchetDirection() const { return ratchetDirection_; }
    /** @brief Sets reverse-lock torque for ratchet engagement. */
    void setRatchetEngagementTorque(float torque);
    /** @brief Current ratchet engagement torque in newton-metres. */
    float getRatchetEngagementTorque() const { return engagementTorque_; }

    /** @brief Destroys owned joints and invalidates this wrapper. */
    void destroy();
    /** @brief Internal invalidation used by world teardown. */
    void invalidate();
    /** @brief Internal: applies ratchet lock / drive before a solver step. */
    void syncBeforeStep();

    ~Mechanism2D();

private:
    friend class World;

    Mechanism2D(World *world, Kind kind, int id, Joint2D *drive, Joint2D *crankPin, Joint2D *sliderPin,
                Joint2D *slider, int ratchetDirection, float engagementTorque);

    void requireKind(Kind expected, const char *operation) const;
    float axisSpinSpeed() const;

    World *world_ = nullptr;
    Kind kind_ = Kind::Shaft;
    int id_ = 0;
    Joint2D *drive_ = nullptr;
    Joint2D *crankPin_ = nullptr;
    Joint2D *sliderPin_ = nullptr;
    Joint2D *slider_ = nullptr;
    bool driveEnabled_ = false;
    float driveSpeed_ = 0.f;
    float driveMaxTorque_ = 0.f;
    int ratchetDirection_ = 1;
    float engagementTorque_ = 0.f;
};

}  // namespace eve::physics
