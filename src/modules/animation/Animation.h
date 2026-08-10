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

/**
 * Animation module — tween factory + 3D skeletal playback
 * (player / state machine / motion matching) + per-frame pump.
 * Script: `anim <- eve.Animation();`
 *
 * Tweens own their property tracks; call `anim.update(dt)` (or `tween.update(dt)`)
 * each frame. Relative property changes use `Tween::setDelta` / `setDeltaAngle`.
 *
 * 3D: build `AnimSkeleton` + `AnimClip`, then drive with `AnimPlayer`,
 * `AnimStateMachine`, or `MotionMatcher` (+ `MotionDatabase`).
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
     * Import skeleton/clip from Assimp-backed ModelData, or from compact `.eva`
     * fixtures (see AnimImporter).
     */
    AnimSkeleton *newSkeletonFromModel(eve::model3d::ModelData *model);
    AnimClip     *newClipFromModel(eve::model3d::ModelData *model, AnimSkeleton *skeleton,
                                   int animIndex = 0);
    AnimSkeleton *newSkeletonFromEvaFile(const std::string &path);
    AnimClip     *newClipFromEvaFile(const std::string &path);

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
