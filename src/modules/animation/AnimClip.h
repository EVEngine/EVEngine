#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/**
 * @brief Keyframed skeletal animation clip (local TRS tracks per bone).
 * Script type: `AnimClip`.
 */
class AnimClip {
public:
    explicit AnimClip(std::string name = "");
    ~AnimClip();

    AnimClip(const AnimClip&)            = delete;
    AnimClip& operator=(const AnimClip&) = delete;

    void        setName(const std::string& name) { name_ = name; }
    std::string getName() const { return name_; }

    void  setDuration(float seconds);
    float getDuration() const { return duration_; }

    void setLoop(bool loop) { loop_ = loop; }
    bool getLoop() const { return loop_; }

    /** @brief Sample rate hint used by MotionDatabase baking (Hz). */
    void  setSampleRate(float hz);
    float getSampleRate() const { return sampleRate_; }

    void addPositionKey(int boneIndex, float time, float x, float y, float z);
    void addRotationKey(int boneIndex, float time, float x, float y, float z, float w);
    void addScaleKey(int boneIndex, float time, float x, float y, float z);
    /** @brief Add a named gameplay notify at clip-local time. */
    void        addEvent(float time, const std::string& name);
    int         getEventCount() const { return static_cast<int>(events_.size()); }
    float       getEventTime(int eventIndex) const;
    std::string getEventName(int eventIndex) const;

    int getPositionKeyCount(int boneIndex) const;
    int getRotationKeyCount(int boneIndex) const;
    int getScaleKeyCount(int boneIndex) const;

    float getPositionKeyTime(int boneIndex, int keyIndex) const;
    float getPositionKeyX(int boneIndex, int keyIndex) const;
    float getPositionKeyY(int boneIndex, int keyIndex) const;
    float getPositionKeyZ(int boneIndex, int keyIndex) const;

    float getRotationKeyTime(int boneIndex, int keyIndex) const;
    float getRotationKeyX(int boneIndex, int keyIndex) const;
    float getRotationKeyY(int boneIndex, int keyIndex) const;
    float getRotationKeyZ(int boneIndex, int keyIndex) const;
    float getRotationKeyW(int boneIndex, int keyIndex) const;

    float getScaleKeyTime(int boneIndex, int keyIndex) const;
    float getScaleKeyX(int boneIndex, int keyIndex) const;
    float getScaleKeyY(int boneIndex, int keyIndex) const;
    float getScaleKeyZ(int boneIndex, int keyIndex) const;

    /**
     * @brief Bake planar root motion onto an existing position track (or create one).
     * For each position key at time t: x += speedX * t, z += speedZ * t.
     * Useful when source clips are Mixamo in-place locomotion.
     */
    void applyPlanarRootMotion(int boneIndex, float speedX, float speedZ);

    /**
     * @brief Sample local pose at time (seconds). If skeleton non-null, missing tracks
     * fall back to bind pose; otherwise identity.
     */
    void sample(float time, AnimPose* out, const AnimSkeleton* skeleton = nullptr) const;

    /** @brief Wrap or clamp time according to loop flag. */
    float wrapTime(float time) const;

    /**
     * @brief Replace this clip's content with `other`'s (payload moved, not
     *        copied). Used by hot reload so registered clip instances keep
     *        their identity while their content is refreshed. `other` is left
     *        drained and is destroyed by the caller.
     */
    void adopt(AnimClip& other);
    /** @brief Collect notifies crossed by forward playback, including loop wrap. */
    void collectEvents(float previousTime, float currentTime, bool loop, std::vector<std::string>& out) const;

private:
    friend class AnimPlayer;
    struct Vec3Key {
        float t = 0.f, x = 0.f, y = 0.f, z = 0.f;
    };
    struct QuatKey {
        float t = 0.f, x = 0.f, y = 0.f, z = 0.f, w = 1.f;
    };
    struct BoneTrack {
        std::vector<Vec3Key> positions;
        std::vector<QuatKey> rotations;
        std::vector<Vec3Key> scales;
    };
    struct EventKey {
        float       t = 0.f;
        std::string name;
    };

    void         ensureBone(int boneIndex);
    TransformTRS sampleBone(int boneIndex, float time, const TransformTRS& fallback) const;
    void         sampleClamped(float time, AnimPose* out, const AnimSkeleton* skeleton) const;

    static void sampleVec3(const std::vector<Vec3Key>& keys, float time, float& x, float& y, float& z, bool& ok);
    static void sampleQuat(const std::vector<QuatKey>& keys, float time, float& x, float& y, float& z, float& w,
                           bool& ok);

    std::string            name_;
    float                  duration_   = 0.f;
    bool                   loop_       = true;
    float                  sampleRate_ = 30.f;
    std::vector<BoneTrack> tracks_;
    std::vector<EventKey>  events_;
};

}  // namespace eve::animation
