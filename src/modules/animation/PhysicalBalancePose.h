#pragma once
#include "common/Export.h"

#include "animation/AnimPose.h"
#include "common/Result.h"
#include "common/Time.h"

#include <vector>

namespace eve::animation {

class AnimSkeleton;

/**
 * @brief Authored-pose overlay that wobbles from world impulses and recovers balance.
 * @details Animation remains the target. A planar inverted pendulum on the balance
 * bone leans from contact impulses; recovery PD plus a gravity term makes the
 * character actively fight the lean. Optional per-bone recoil springs back to the
 * authored local rotation. Physics is not referenced: callers inject impulse values
 * from World3D contacts or gameplay.
 *
 * Determinism: identical skeleton, target pose, impulses and SimulationStep
 * sequences produce the same pose within float rounding. Not bit-exact across
 * compilers.
 *
 * Script type: `PhysicalBalancePose`.
 * @ownership Value-like runtime object; the borrowed skeleton is not owned.
 * @thread Owner thread only; no callbacks or reentrancy.
 */
class EVENGINE_API_WORLD PhysicalBalancePose {
public:
    /**
     * @brief Construct for a borrowed skeleton.
     * @param skeleton Hierarchy used for world COM and bone indexing; must outlive this object.
     * @throws eve::Exception when skeleton is null.
     */
    explicit PhysicalBalancePose(AnimSkeleton* skeleton);
    /** @brief Physical balance pose. */
    ~PhysicalBalancePose() = default;

    PhysicalBalancePose(const PhysicalBalancePose&)            = delete;
    PhysicalBalancePose& operator=(const PhysicalBalancePose&) = delete;

    /**
     * @brief Borrowed skeleton.
     * @ownership Borrowed; ownership remains with the animation source.
     * @lifetime Valid until this object or the skeleton is destroyed.
     */
    [[nodiscard]] AnimSkeleton* getSkeleton() const;

    /**
     * @brief Bone whose world XZ is the support point (typically pelvis/root).
     * @return Applied, or InvalidArgument when the index is out of range.
     */
    [[nodiscard]] eve::Result<void> setSupportBone(int boneIndex);
    [[nodiscard]] int               getSupportBone() const;

    /**
     * @brief Bone that receives whole-body lean (typically spine/chest).
     * @return Applied, or InvalidArgument when the index is out of range.
     */
    [[nodiscard]] eve::Result<void> setBalanceBone(int boneIndex);
    [[nodiscard]] int               getBalanceBone() const;

    /**
     * @brief Mass used for center-of-mass; zero excludes the bone.
     * @return Applied, InvalidArgument for a bad index or non-finite/negative mass.
     */
    [[nodiscard]] eve::Result<void> setBoneMass(int boneIndex, float mass);
    [[nodiscard]] float             getBoneMass(int boneIndex) const;

    /**
     * @brief Recovery frequency (Hz) and damping ζ for the balance pendulum.
     * @details Requires (2π f)² > g / height so the character can win against gravity.
     * ζ in (0, 1) wobbles while returning upright; ζ ≥ 1 settles without overshoot.
     */
    [[nodiscard]] eve::Result<void> setRecovery(float frequencyHz, float dampingZeta);
    [[nodiscard]] float             getRecoveryFrequency() const;
    [[nodiscard]] float             getRecoveryDamping() const;

    /**
     * @brief Local-bone recoil frequency and damping toward the authored rotation.
     */
    [[nodiscard]] eve::Result<void> setRecoil(float frequencyHz, float dampingZeta);
    [[nodiscard]] float             getRecoilFrequency() const;
    [[nodiscard]] float             getRecoilDamping() const;

    /** @brief Gravity magnitude (m/s², Y-up). Zero disables the fall-away term. */
    [[nodiscard]] eve::Result<void> setGravity(float metersPerSecondSquared);
    [[nodiscard]] float             getGravity() const;

    /** @brief Pendulum length used as g/h (meters). */
    [[nodiscard]] eve::Result<void> setPendulumHeight(float meters);
    [[nodiscard]] float             getPendulumHeight() const;

    /** @brief Whole-body rotational inertia for lean (kg·m²). */
    [[nodiscard]] eve::Result<void> setInertia(float inertia);
    [[nodiscard]] float             getInertia() const;

    /** @brief Per-bone recoil inertia (kg·m²). */
    [[nodiscard]] eve::Result<void> setRecoilInertia(float inertia);
    [[nodiscard]] float             getRecoilInertia() const;

    /** @brief Clamp for lean angle in radians. */
    [[nodiscard]] eve::Result<void> setMaxLean(float radians);
    [[nodiscard]] float             getMaxLean() const;

    /**
     * @brief Copy the authored pose that recovery tracks.
     * @param target Borrowed for this call only; bone count must match the skeleton.
     * @return Applied, or InvalidArgument when target is null or the count mismatches.
     */
    [[nodiscard]] eve::Result<void> setTargetPose(const AnimPose* target);

    /** @brief Zero lean, recoil and pending impulses; pose snaps to the last target. */
    [[nodiscard]] eve::Result<void> snapToTarget();

    /**
     * @brief Queue a world-space linear impulse at a world point.
     * @details Torque about the support updates lean velocity; torque about the
     * hit bone updates that bone's recoil. Applied on the next advance.
     * @param boneIndex Hit bone; must be in range.
     * @return Applied, NoOp for a near-zero impulse, or InvalidArgument for bad input.
     */
    [[nodiscard]] eve::Result<void> applyImpulse(int boneIndex, float impulseX, float impulseY, float impulseZ,
                                                 float pointX, float pointY, float pointZ);

    /**
     * @brief Advance the pendulum and recoil, then write the overlay pose.
     * @return Applied, or InvalidArgument/Conflict for a bad step.
     */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Legacy seconds facade; forwards to advance() and ignores its Result. */
    void update(float dt);

    /**
     * @brief Overlay pose after the last successful advance (or snap).
     * @ownership Borrowed; this object retains ownership.
     * @lifetime Valid until the next mutating call or destruction.
     */
    [[nodiscard]] AnimPose* getPose();
    /**
     * @brief Last authored target, or bind pose before the first setTargetPose.
     * @ownership Borrowed; this object retains ownership.
     * @lifetime Valid until the next setTargetPose/snap or destruction.
     */
    [[nodiscard]] AnimPose* getTargetPose();

    [[nodiscard]] float getLeanX() const;
    [[nodiscard]] float getLeanZ() const;
    [[nodiscard]] float getLeanVelocityX() const;
    [[nodiscard]] float getLeanVelocityZ() const;
    [[nodiscard]] float getCenterOfMassX() const;
    [[nodiscard]] float getCenterOfMassY() const;
    [[nodiscard]] float getCenterOfMassZ() const;
    [[nodiscard]] float getSupportX() const;
    [[nodiscard]] float getSupportY() const;
    [[nodiscard]] float getSupportZ() const;

private:
    struct Recoil {
        float x = 0.f, y = 0.f, z = 0.f;
        float vx = 0.f, vy = 0.f, vz = 0.f;
        float pendingTx = 0.f, pendingTy = 0.f, pendingTz = 0.f;
    };

    void                    ensureBones();
    void                    resetDynamics();
    void                    writeOverlayPose();
    void                    refreshDiagnostics();
    void                    updateUnchecked(float dt);
    [[nodiscard]] bool      boneInRange(int boneIndex) const;
    [[nodiscard]] eve::Result<void> requireFinitePositive(const char* name, float value, bool allowZero);

    AnimSkeleton* skeleton_ = nullptr;
    AnimPose      pose_;
    AnimPose      target_;

    int   supportBone_ = 0;
    int   balanceBone_ = 0;
    float recoveryHz_  = 2.f;
    float recoveryZeta_ = 0.45f;
    float recoilHz_    = 4.f;
    float recoilZeta_  = 0.5f;
    float gravity_     = 9.81f;
    float pendulumHeight_ = 1.f;
    float inertia_     = 1.f;
    float recoilInertia_ = 0.15f;
    float maxLean_     = 0.7f;

    float leanX_ = 0.f, leanZ_ = 0.f;
    float leanVelX_ = 0.f, leanVelZ_ = 0.f;
    float pendingLeanTx_ = 0.f, pendingLeanTz_ = 0.f;

    float comX_ = 0.f, comY_ = 0.f, comZ_ = 0.f;
    float supportX_ = 0.f, supportY_ = 0.f, supportZ_ = 0.f;

    std::vector<float>  masses_;
    std::vector<Recoil> recoils_;
    bool                hasTarget_    = false;
    eve::SimulationTick lastTick_     = eve::SimulationTick::zero();
    bool                hasLastTick_  = false;
};

}  // namespace eve::animation
