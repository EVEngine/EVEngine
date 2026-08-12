#pragma once

#include "common/Module.h"
#include "animation/Tween.h"

#include <string>
#include <vector>

namespace eve::model3d {
class ModelData;
}

namespace eve::animation {

class AnimSkeleton;
class AnimClip;
class AnimPose;
class AnimPlayer;
class AnimStateMachine;
class MotionDatabase;
class MotionMatcher;
class ControlAnim;
class ControlPose;
class AnimSkin;
class AnimTrail;

/**
 * Animation module — tween factory + 3D skeletal playback
 * (player / state machine / motion matching) + control-theory
 * procedural drivers + motion trails + per-frame pump.
 * Script: `anim <- eve.Animation();`
 *
 * Tweens own their property tracks; call `anim.update(dt)` (or `tween.update(dt)`)
 * each frame. Relative property changes use `Tween::setDelta` / `setDeltaAngle`.
 *
 * 3D: build `AnimSkeleton` + `AnimClip`, then drive with `AnimPlayer`,
 * `AnimStateMachine`, or `MotionMatcher` (+ `MotionDatabase`).
 * Procedural: `ControlAnim` (scalar second-order/PD/spring) and
 * `ControlPose` (pose tracking with the same control laws).
 * Trails: `AnimTrail` records samples and draws fading trajectories.
 */
class Animation : public Module {
public:
    Module_REG(Animation);
    Animation() = default;
    ~Animation() override;

    /** Create a tween (duration in seconds). Returned pointer is owned by script GC. */
    Tween *newTween(float duration = 1.f);

    /** 3D skeletal animation factories (script GC owns returned objects). */
    AnimSkeleton     *newSkeleton();
    AnimClip         *newClip(const std::string &name = "");
    AnimPose         *newPose(int boneCount = 0);
    AnimPlayer       *newPlayer(AnimSkeleton *skeleton);
    AnimStateMachine *newStateMachine(AnimSkeleton *skeleton);
    MotionDatabase   *newMotionDatabase(AnimSkeleton *skeleton);
    MotionMatcher    *newMotionMatcher(AnimSkeleton *skeleton, MotionDatabase *database);

    /**
     * Control-theory procedural animation (second-order / spring / PD).
     * frequencyHz: natural frequency f (Hz); dampingZeta: ζ; response: r.
     */
    ControlAnim *newControlAnim(float frequencyHz = 3.f, float dampingZeta = 1.f,
                                float response = 1.f);
    ControlPose *newControlPose(AnimSkeleton *skeleton);

    /**
     * Import skeleton/clip from Assimp-backed ModelData, or from compact `.eva`
     * fixtures (see AnimImporter).
     */
    AnimSkeleton *newSkeletonFromModel(eve::model3d::ModelData *model);
    AnimClip     *newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                   int animIndex = 0);
    AnimSkeleton *newSkeletonFromEvaFile(const std::string &path);
    AnimClip     *newClipFromEvaFile(const std::string &path);

    /**
     * CPU linear-blend skin binding for a skinned mesh on ModelData.
     * meshIndex selects the Assimp mesh; bone names must match the skeleton.
     */
    AnimSkin *newSkinFromModel(eve::model3d::ModelData *model, int meshIndex,
                               AnimSkeleton *skeleton);

    /**
     * Motion trail / afterimage (script GC owns returned object).
     * capacity: max retained samples (>= 2).
     */
    AnimTrail *newTrail(int capacity = 64);

    /** Advance all registered tweens. */
    void update(float dt);

    int getTweenCount() const { return static_cast<int>(tweens_.size()); }
    int getActiveCount() const;

    /** Drop finished/stopped entries from the registry (does not delete Tween objects). */
    void clearFinished();
    /** Detach all tweens from the registry (does not delete Tween objects). */
    void clearAll();

private:
    friend class Tween;

    void registerTween(Tween *t);
    void unregisterTween(Tween *t);

    std::vector<Tween *> tweens_;
};

}  // namespace eve::animation
