#pragma once

#include "animation/AnimPose.h"

namespace eve::animation {

class AnimClip;
class AnimSkeleton;

/**
 * @brief Single-clip (or cross-fading) 3D animation player.
 * Script type: `AnimPlayer`.
 */
class AnimPlayer {
public:
    explicit AnimPlayer(AnimSkeleton *skeleton);
    ~AnimPlayer() = default;

    AnimPlayer(const AnimPlayer &)            = delete;
    AnimPlayer &operator=(const AnimPlayer &) = delete;

    AnimSkeleton *getSkeleton() const { return skeleton_; }

    void play(AnimClip *clip);
    /** @brief Cross-fade to clip over blendSeconds (keeps sampling previous until done). */
    void crossFade(AnimClip *clip, float blendSeconds);

    void stop();
    void pause();
    void resume();

    void  setSpeed(float speed);
    float getSpeed() const { return speed_; }
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setLoop(bool loop) { loopOverride_ = loop; hasLoopOverride_ = true; }
    bool  getLoop() const;

    bool isPlaying() const { return playing_ && clip_ != nullptr; }
    bool isPaused() const { return paused_; }

    AnimClip *getClip() const { return clip_; }
    AnimPose *getPose();

    /** @brief Advance playback and sample into internal pose. */
    void update(float dt);

private:
    bool effectiveLoop() const;

    AnimSkeleton *skeleton_         = nullptr;
    AnimClip     *clip_             = nullptr;
    AnimClip     *prevClip_         = nullptr;
    AnimPose      pose_;
    AnimPose      prevPose_;
    float         time_             = 0.f;
    float         prevTime_         = 0.f;
    float         speed_            = 1.f;
    float         blendDuration_    = 0.f;
    float         blendElapsed_     = 0.f;
    bool          blending_         = false;
    bool          playing_          = false;
    bool          paused_           = false;
    bool          hasLoopOverride_  = false;
    bool          loopOverride_     = true;
};

}  // namespace eve::animation
