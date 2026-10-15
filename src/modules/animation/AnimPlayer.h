#pragma once
#include "common/Export.h"


#include "animation/AnimPose.h"
#include "animation/AnimPoseSource.h"
#include "animation/RootMotionPolicy.h"
#include "common/Result.h"
#include "common/Time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::animation {

/** @brief Deterministic built-in curve used by cross-fade pose interpolation. */
enum class AnimBlendCurve : std::uint8_t {
    Linear,
    EaseInOut,
};

class AnimClip;
class AnimSkeleton;

/**
 * @brief Single-clip (or cross-fading) 3D animation player.
 * Script type: `AnimPlayer`.
 */
class EVENGINE_API_WORLD AnimPlayer : public IAnimPoseSource {
public:
    /** @brief Anim player. */
    explicit AnimPlayer(AnimSkeleton* skeleton);
    /** @brief Anim player. */
    ~AnimPlayer() override = default;

    AnimPlayer(const AnimPlayer&)            = delete;
    AnimPlayer& operator=(const AnimPlayer&) = delete;

    /** @brief Borrowed pointer accessor.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimSkeleton* getSkeleton() const override { return skeleton_; }

    /** @brief Play. */
    void play(AnimClip* clip);
    /** @brief Cross-fade to clip over blendSeconds (keeps sampling previous until done). */
    void crossFade(AnimClip* clip, float blendSeconds);
    /** @brief Select the curve used by subsequent and active cross-fades. */
    void setBlendCurve(AnimBlendCurve curve) noexcept { blendCurve_ = curve; }

    /** @brief Stops . */
    void stop();
    /** @brief Pause. */
    void pause();
    /** @brief Resume. */
    void resume();

    /** @brief Sets the speed. */
    void  setSpeed(float speed);
    /** @brief Returns the speed. */
    float getSpeed() const { return speed_; }
    /** @brief Sets the time. */
    void  setTime(float seconds);
    /** @brief Returns the time. */
    float getTime() const { return time_; }
    /** @brief Sets the loop. */
    void  setLoop(bool loop) {
        loopOverride_    = loop;
        hasLoopOverride_ = true;
    }
    /** @brief Returns the loop. */
    bool getLoop() const;

    /** @brief True when playing. */
    bool isPlaying() const { return playing_ && clip_ != nullptr; }
    /** @brief True when paused. */
    bool isPaused() const { return paused_; }

    /** @brief Borrow the selected clip, or null if none is selected.
     * The caller owns the clip and must keep it alive during playback and this borrow.
     * Playback changes can replace the selected clip; no ownership is transferred.
     * @thread Owner thread only, outside advance; no callbacks or reentrancy.
     */
    AnimClip* getClip() const { return clip_; }
    /** @brief Borrowed pointer accessor.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    AnimPose* getPose() override;
    /** @brief Select the bone whose per-frame motion is extracted (default 0). */
    void  setRootMotionBone(int boneIndex);
    /** @brief Returns the root motion bone. */
    int   getRootMotionBone() const { return rootMotionBone_; }

    /**
     * @brief Replace the root-motion filter/bake/apply policy used by subsequent updates.
     * @return Applied on success; InvalidArgument leaves the previous policy unchanged.
     * @thread Owner thread only, outside advance; no callbacks or reentrancy.
     */
    [[nodiscard]] eve::Result<void> setRootMotionPolicy(const RootMotionPolicy& policy);
    /** @brief Copy of the active root-motion policy. */
    [[nodiscard]] RootMotionPolicy getRootMotionPolicy() const { return rootMotionPolicy_; }
    /**
     * @brief Set CharacterFacing yaw in radians without replacing the rest of the policy.
     * @return Applied on success; InvalidArgument when yaw is non-finite.
     */
    [[nodiscard]] eve::Result<void> setRootMotionCharacterYaw(float yawRadians);

    /** @brief Controller-facing translation X after policy filtering (BoneLocal or CharacterFacing). */
    float getRootMotionX() const { return rootMotion_.px; }
    /** @brief Returns the root motion y. */
    float getRootMotionY() const { return rootMotion_.py; }
    /** @brief Returns the root motion z. */
    float getRootMotionZ() const { return rootMotion_.pz; }
    /** @brief Returns the root motion rotation x. */
    float getRootMotionRotationX() const { return rootMotion_.qx; }
    /** @brief Returns the root motion rotation y. */
    float getRootMotionRotationY() const { return rootMotion_.qy; }
    /** @brief Returns the root motion rotation z. */
    float getRootMotionRotationZ() const { return rootMotion_.qz; }
    /** @brief Returns the root motion rotation w. */
    float getRootMotionRotationW() const { return rootMotion_.qw; }
    /** @brief Pop the oldest notify crossed since the previous update. */
    std::string consumeEvent();
    /** @brief Limit pose evaluation frequency for animation LOD; 0 updates every call. */
    void  setUpdateRate(float hz);
    /** @brief Returns the update rate. */
    float getUpdateRate() const { return updateRate_; }

    /** @brief Number of events crossed by the most recent play/update call. */
    int getEventCount() const override { return static_cast<int>(events_.size()); }
    /** @brief Name of a dispatched event, or empty for invalid index. */
    std::string getEventName(int index) const override;
    /** @brief Payload of a dispatched event, or empty for invalid index. */
    std::string getEventPayload(int index) const override;
    /** @brief Clear currently dispatched events. update() also clears them at frame start. */
    void clearEvents() { events_.clear(); }

    /** @brief Advance playback and sample into internal pose. */
    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep& step) override;

    /** @brief Last scheduler tick consumed by the checked playback API. */
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept override { return lastTick_; }
    /** @brief Whether the player has consumed at least one checked step. */
    [[nodiscard]] bool hasCurrentTick() const noexcept override { return hasLastTick_; }

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
    AnimBlendCurve           blendCurve_      = AnimBlendCurve::Linear;
    bool                     playing_         = false;
    bool                     paused_          = false;
    bool                     hasLoopOverride_ = false;
    bool                     loopOverride_    = true;
    int                      rootMotionBone_  = 0;
    RootMotionPolicy         rootMotionPolicy_{};
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
