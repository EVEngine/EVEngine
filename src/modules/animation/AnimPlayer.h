#pragma once

#include "animation/AnimPose.h"
#include "common/Time.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;

/**
 * @brief Single-clip (or cross-fading) 3D animation player.
 * Script type: `AnimPlayer`.
 */
class AnimPlayer {
public:
    explicit AnimPlayer(AnimSkeleton* skeleton);
    ~AnimPlayer() = default;

    AnimPlayer(const AnimPlayer&)            = delete;
    AnimPlayer& operator=(const AnimPlayer&) = delete;

    AnimSkeleton* getSkeleton() const { return skeleton_; }

    void play(AnimClip* clip);
    /** @brief Cross-fade to clip over blendSeconds (keeps sampling previous until done). */
    void crossFade(AnimClip* clip, float blendSeconds);

    void stop();
    void pause();
    void resume();

    void  setSpeed(float speed);
    float getSpeed() const { return speed_; }
    void  setTime(float seconds);
    float getTime() const { return time_; }
    void  setLoop(bool loop) {
        loopOverride_    = loop;
        hasLoopOverride_ = true;
    }
    bool getLoop() const;

    bool isPlaying() const { return playing_ && clip_ != nullptr; }
    bool isPaused() const { return paused_; }

    AnimClip* getClip() const { return clip_; }
    AnimPose* getPose();
    /** @brief Select the bone whose per-frame motion is extracted (default 0). */
    void  setRootMotionBone(int boneIndex);
    int   getRootMotionBone() const { return rootMotionBone_; }
    float getRootMotionX() const { return rootMotion_.px; }
    float getRootMotionY() const { return rootMotion_.py; }
    float getRootMotionZ() const { return rootMotion_.pz; }
    float getRootMotionRotationX() const { return rootMotion_.qx; }
    float getRootMotionRotationY() const { return rootMotion_.qy; }
    float getRootMotionRotationZ() const { return rootMotion_.qz; }
    float getRootMotionRotationW() const { return rootMotion_.qw; }
    /** @brief Pop the oldest notify crossed since the previous update. */
    std::string consumeEvent();
    /** @brief Limit pose evaluation frequency for animation LOD; 0 updates every call. */
    void  setUpdateRate(float hz);
    float getUpdateRate() const { return updateRate_; }

    /** @brief Number of events crossed by the most recent play/update call. */
    int getEventCount() const { return static_cast<int>(events_.size()); }
    /** @brief Name of a dispatched event, or empty for invalid index. */
    std::string getEventName(int index) const;
    /** @brief Payload of a dispatched event, or empty for invalid index. */
    std::string getEventPayload(int index) const;
    /** @brief Clear currently dispatched events. update() also clears them at frame start. */
    void clearEvents() { events_.clear(); }

    /** @brief Advance playback and sample into internal pose. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step);

    /** @brief Last scheduler tick consumed by the checked playback API. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }
    /** @brief Whether the player has consumed at least one checked step. */
    [[nodiscard]] bool hasCurrentTick() const noexcept { return hasLastTick_; }

    /** @brief Legacy seconds facade retained for scripts and old callers. */
    void update(float dt);

private:
    bool effectiveLoop() const;
    void updateUnchecked(float dt);

    AnimSkeleton*            skeleton_ = nullptr;
    AnimClip*                clip_     = nullptr;
    AnimClip*                prevClip_ = nullptr;
    AnimPose                 pose_;
    AnimPose                 prevPose_;
    AnimPose                 sampledPose_;
    AnimPose                 rootPreviousPose_;
    AnimPose                 rootStartPose_;
    AnimPose                 rootEndPose_;
    float                    time_            = 0.f;
    float                    prevTime_        = 0.f;
    float                    speed_           = 1.f;
    float                    blendDuration_   = 0.f;
    float                    blendElapsed_    = 0.f;
    bool                     blending_        = false;
    bool                     playing_         = false;
    bool                     paused_          = false;
    bool                     hasLoopOverride_ = false;
    bool                     loopOverride_    = true;
    int                      rootMotionBone_  = 0;
    TransformTRS             rootMotion_;
    std::vector<std::string> pendingEvents_;
    float                    updateRate_        = 0.f;
    float                    updateAccumulator_ = 0.f;
    struct DispatchedEvent {
        std::string name;
        std::string payload;
    };
    std::vector<DispatchedEvent> events_;
    eve::SimulationTick          lastTick_    = eve::SimulationTick::zero();
    bool                         hasLastTick_ = false;

    void dispatchEvents(float oldTime, float newTime);
};

}  // namespace eve::animation
