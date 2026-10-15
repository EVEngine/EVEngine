#pragma once
#include "common/Export.h"

#include "animation/AnimControlMath.h"
#include "animation/AnimPose.h"
#include "common/Time.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimSkeleton;

/**
 * @brief Control-theory procedural pose driver: tracks a target AnimPose with
 * per-channel second-order / spring / PD dynamics (same laws as ControlAnim).
 * Script type: `ControlPose`.
 *
 * Position and scale use independent scalar channels. Rotations use shortest-path
 * quaternion components with renormalization after each step.
 */
class EVENGINE_API_WORLD ControlPose {
public:
    /** @brief Control pose. */
    explicit ControlPose(AnimSkeleton *skeleton);
    /** @brief Control pose. */
    ~ControlPose() = default;

    ControlPose(const ControlPose &)            = delete;
    ControlPose &operator=(const ControlPose &) = delete;

    /** @brief Returns the skeleton. */
    AnimSkeleton *getSkeleton() const { return skeleton_; }

    /** @brief Sets the frequency. */
    void  setFrequency(float frequencyHz);
    /** @brief Returns the frequency. */
    float getFrequency() const { return frequencyHz_; }
    /** @brief Sets the damping. */
    void  setDamping(float dampingZeta);
    /** @brief Returns the damping. */
    float getDamping() const { return dampingZeta_; }
    /** @brief Sets the response. */
    void  setResponse(float response);
    /** @brief Returns the response. */
    float getResponse() const { return response_; }

    /** @brief Sets the integrator. */
    void        setIntegrator(const std::string &kind);
    /** @brief Returns the integrator. */
    std::string getIntegrator() const;

    /** @brief Per-bone blend weight in [0,1]; 1 = full dynamics, 0 = hard snap to target. */
    void  setBoneWeight(int boneIndex, float weight);
    /** @brief Returns the bone weight. */
    float getBoneWeight(int boneIndex) const;

    /** @brief Copy target pose. Channels without prior state snap; existing state keeps momentum. */
    void setTargetPose(const AnimPose *target);
    /** @brief Snap current state to the last target without changing the target. */
    void snapToTarget();

    /** @brief Returns the pose. */
    AnimPose *getPose();
    /** @brief Returns the target pose. */
    AnimPose *getTargetPose();

    /** @brief Advance pose dynamics by one scheduler-owned deterministic step. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);
    /** @brief Legacy seconds facade; explicitly forwards to advance(). */
    void update(float dt);

private:
    enum class Integrator { SecondOrder, Spring, Pd };

    struct ScalarState {
        float y  = 0.f;
        float yd = 0.f;
        float x  = 0.f;
        float xp = 0.f;
        bool  hasPrev = false;
    };

    struct BoneState {
        ScalarState px, py, pz;
        ScalarState qx, qy, qz, qw;
        ScalarState sx, sy, sz;
        float weight = 1.f;
    };

    void refreshCoeffs();
    void ensureBones(int count);
    void stepScalar(float dt, float xd, ScalarState &s);
    void writePoseFromState();
    void readTargetIntoState(const AnimPose *target, bool snap);

    AnimSkeleton *skeleton_ = nullptr;
    AnimPose      pose_;
    AnimPose      target_;

    float frequencyHz_ = 3.f;
    float dampingZeta_ = 1.f;
    float response_    = 1.f;
    Integrator integrator_ = Integrator::SecondOrder;
    SecondOrderCoeffs coeffs_{};
    float kp_ = 0.f;
    float kd_ = 0.f;

    std::vector<BoneState> bones_;
    bool hasTarget_ = false;
    eve::SimulationTick    lastTick_    = eve::SimulationTick::zero();
    bool                   hasLastTick_ = false;

    void updateUnchecked(float dt);
};

}  // namespace eve::animation
