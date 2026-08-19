#pragma once

#include "animation/AnimControlMath.h"
#include "animation/AnimPose.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimSkeleton;

/**
 * Control-theory procedural pose driver: tracks a target AnimPose with
 * per-channel second-order / spring / PD dynamics (same laws as ControlAnim).
 * Script type: `ControlPose`.
 *
 * Position and scale use independent scalar channels. Rotations use shortest-path
 * quaternion components with renormalization after each step.
 */
class ControlPose {
public:
    explicit ControlPose(AnimSkeleton *skeleton);
    ~ControlPose() = default;

    ControlPose(const ControlPose &)            = delete;
    ControlPose &operator=(const ControlPose &) = delete;

    AnimSkeleton *getSkeleton() const { return skeleton_; }

    void  setFrequency(float frequencyHz);
    float getFrequency() const { return frequencyHz_; }
    void  setDamping(float dampingZeta);
    float getDamping() const { return dampingZeta_; }
    void  setResponse(float response);
    float getResponse() const { return response_; }

    void        setIntegrator(const std::string &kind);
    std::string getIntegrator() const;

    /** Per-bone blend weight in [0,1]; 1 = full dynamics, 0 = hard snap to target. */
    void  setBoneWeight(int boneIndex, float weight);
    float getBoneWeight(int boneIndex) const;

    /** Copy target pose. Channels without prior state snap; existing state keeps momentum. */
    void setTargetPose(const AnimPose *target);
    /** Snap current state to the last target without changing the target. */
    void snapToTarget();

    AnimPose *getPose();
    AnimPose *getTargetPose();

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
};

}  // namespace eve::animation
