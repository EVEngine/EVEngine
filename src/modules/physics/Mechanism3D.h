#pragma once
#include "common/Export.h"

#include <string>

namespace eve::physics {

class Body3D;
class Joint3D;
class World3D;

/**
 * @brief Composed mechanical assembly owned by a World3D.
 *
 * Builds crank-slider, shaft, and ratchet mechanisms from Joint3D primitives.
 * Ratchet one-way locking is applied automatically during World3D::step.
 *
 * @ownership World3D owns the mechanism and the joints it creates. Borrowed
 *            Body3D inputs are never retained as caller ownership.
 * @lifetime Valid until Mechanism3D::destroy(), World3D::destroy(), or any
 *           dependent body/joint invalidation.
 * @thread Call on the owning physics thread.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_WORLD Mechanism3D {
public:
    /** @brief Assembly kind. */
    enum class Kind { CrankSlider, Shaft, Ratchet };

    Mechanism3D(const Mechanism3D &) = delete;
    Mechanism3D &operator=(const Mechanism3D &) = delete;

    /** @brief Stable world-local identifier. */
    int getId() const { return id_; }
    /** @brief Kind name: "crankSlider", "shaft", or "ratchet". */
    std::string getKind() const;
    /** @brief Owning world, or null after invalidation. */
    World3D *getWorld() const { return world_; }
    /** @brief Whether the assembly and its primary joints are still live. */
    bool isValid() const;

    /**
     * @brief Primary revolute (crank / shaft / ratchet wheel).
     * @return Borrowed joint owned by the parent World3D, or null after invalidation.
     * @ownership World3D owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism3D::destroy(), Joint3D::destroy(), or World3D::destroy().
     */
    Joint3D *getDriveJoint() const { return drive_; }
    /**
     * @brief Crank-to-rod revolute, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World3D, or null when unused/invalid.
     * @ownership World3D owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism3D::destroy(), Joint3D::destroy(), or World3D::destroy().
     */
    Joint3D *getCrankPinJoint() const { return crankPin_; }
    /**
     * @brief Rod-to-slider revolute, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World3D, or null when unused/invalid.
     * @ownership World3D owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism3D::destroy(), Joint3D::destroy(), or World3D::destroy().
     */
    Joint3D *getSliderPinJoint() const { return sliderPin_; }
    /**
     * @brief Frame-to-slider prismatic, or null for shaft/ratchet.
     * @return Borrowed joint owned by the parent World3D, or null when unused/invalid.
     * @ownership World3D owns the joint; this mechanism only borrows it.
     * @lifetime Valid until Mechanism3D::destroy(), Joint3D::destroy(), or World3D::destroy().
     */
    Joint3D *getSliderJoint() const { return slider_; }

    /** @brief Current drive revolute angle in radians (shaft/crank/ratchet). */
    float getDriveAngle() const;
    /** @brief Current slider translation in metres (crank-slider only). */
    float getSliderTranslation() const;
    /** @brief Relative spin speed along the drive axis in radians/second. */
    float getSpinSpeed() const;

    /**
     * @brief Enables a continuous drive motor on the primary revolute.
     * @param speed Target speed in radians/second (sign is absolute; ratchet
     *              remaps it onto the freewheel direction).
     * @param maxTorque Maximum motor torque in newton-metres.
     */
    void setDrive(float speed, float maxTorque);
    /** @brief Disables the continuous drive motor. */
    void clearDrive();
    /** @brief Whether a continuous drive motor is configured. */
    bool isDriveEnabled() const { return driveEnabled_; }

    /**
     * @brief Sets ratchet freewheel direction.
     * @param direction +1 or -1 along the joint axis; other values throw.
     */
    void setRatchetDirection(int direction);
    /** @brief Current ratchet freewheel direction (+1 or -1). */
    int getRatchetDirection() const { return ratchetDirection_; }
    /**
     * @brief Sets reverse-lock torque for ratchet engagement.
     * @param torque Maximum opposing torque in newton-metres.
     */
    void setRatchetEngagementTorque(float torque);
    /** @brief Current ratchet engagement torque in newton-metres. */
    float getRatchetEngagementTorque() const { return engagementTorque_; }

    /** @brief Destroys owned joints and invalidates this wrapper. */
    void destroy();
    /** @brief Internal invalidation used by world teardown. */
    void invalidate();
    /** @brief Internal: applies ratchet lock / drive before a solver step. */
    void syncBeforeStep();

    ~Mechanism3D();

private:
    friend class World3D;

    Mechanism3D(World3D *world, Kind kind, int id, Joint3D *drive, Joint3D *crankPin, Joint3D *sliderPin,
                Joint3D *slider, float axisX, float axisY, float axisZ, int ratchetDirection,
                float engagementTorque);

    void requireKind(Kind expected, const char *operation) const;
    float axisSpinSpeed() const;

    World3D *world_ = nullptr;
    Kind kind_ = Kind::Shaft;
    int id_ = 0;
    Joint3D *drive_ = nullptr;
    Joint3D *crankPin_ = nullptr;
    Joint3D *sliderPin_ = nullptr;
    Joint3D *slider_ = nullptr;
    float axisX_ = 0.f, axisY_ = 0.f, axisZ_ = 1.f;
    bool driveEnabled_ = false;
    float driveSpeed_ = 0.f;
    float driveMaxTorque_ = 0.f;
    int ratchetDirection_ = 1;
    float engagementTorque_ = 0.f;
};

}  // namespace eve::physics
