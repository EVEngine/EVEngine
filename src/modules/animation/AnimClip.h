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
    /** @brief Replace and re-sort one position key. @return False when the bone or key index is invalid. */
    bool setPositionKey(int boneIndex, int keyIndex, float time, float x, float y, float z);
    /** @brief Replace and re-sort one rotation key. @return False when the bone or key index is invalid. */
    bool setRotationKey(int boneIndex, int keyIndex, float time, float x, float y, float z, float w);
    /** @brief Replace and re-sort one scale key. @return False when the bone or key index is invalid. */
    bool setScaleKey(int boneIndex, int keyIndex, float time, float x, float y, float z);
    /** @brief Delete one position key. @return False when the bone or key index is invalid. */
    bool removePositionKey(int boneIndex, int keyIndex);
    /** @brief Delete one rotation key. @return False when the bone or key index is invalid. */
    bool removeRotationKey(int boneIndex, int keyIndex);
    /** @brief Delete one scale key. @return False when the bone or key index is invalid. */
    bool removeScaleKey(int boneIndex, int keyIndex);
    /** @brief Clear every transform channel for one bone. @return False when the bone index is invalid. */
    bool clearTrack(int boneIndex);
    /** @brief Return the number of allocated bone tracks, including empty tracks. */
    int getTrackCount() const { return static_cast<int>(tracks_.size()); }
    /** @brief Add a named event marker at clip-local time. Events are kept time-sorted. */
    void addEvent(float time, const std::string& name, const std::string& payload = "");
    /** @brief Replace and re-sort one event marker. @return False when the event index is invalid. */
    bool setEvent(int index, float time, const std::string& name, const std::string& payload = "");
    /** @brief Delete one event marker. @return False when the event index is invalid. */
    bool removeEvent(int index);
    /** @brief Return the number of event markers. */
    int getEventCount() const { return static_cast<int>(events_.size()); }
    /** @brief Return an event marker's time, or 0 for an invalid index. */
    float getEventTime(int index) const;
    /** @brief Return an event marker's name, or empty for an invalid index. */
    std::string getEventName(int index) const;
    /** @brief Return an event marker's optional payload. */
    std::string getEventPayload(int index) const;

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
     * @brief Remove interpolation-redundant keys while keeping sampled error below the supplied tolerances.
     * @param positionError Maximum local translation error in engine units.
     * @param rotationErrorDegrees Maximum local angular error in degrees.
     * @param scaleError Maximum local scale-vector error.
     * @return Number of removed keys.
     */
    int compress(float positionError = 0.001f, float rotationErrorDegrees = 0.1f,
                 float scaleError = 0.001f);

    /**
     * @brief Bake this clip onto a target skeleton by matching bone names and preserving bind-pose deltas.
     * Translation motion is scaled by the corresponding target/source bind-bone length ratio.
     * @return A new script-owned clip.
     */
    AnimClip* retarget(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton) const;

    /**
     * @brief Sample local pose at time (seconds). If skeleton non-null, missing tracks
     * fall back to bind pose; otherwise identity.
     */
    void sample(float time, AnimPose* out, const AnimSkeleton* skeleton = nullptr) const;
    /** @brief Sample only bones enabled for lodLevel; culled tracks use target bind pose. */
    void sampleLod(float time, AnimPose* out, const AnimSkeleton* skeleton, int lodLevel) const;

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
    struct EventMarker {
        float       t = 0.f;
        std::string name;
        std::string payload;
    };

    void         ensureBone(int boneIndex);
    TransformTRS sampleBone(int boneIndex, float time, const TransformTRS& fallback) const;
    void         sampleClamped(float time, AnimPose* out, const AnimSkeleton* skeleton) const;

    static void sampleVec3(const std::vector<Vec3Key>& keys, float time, float& x, float& y, float& z, bool& ok);
    static void sampleQuat(const std::vector<QuatKey>& keys, float time, float& x, float& y, float& z, float& w,
                           bool& ok);

    std::string             name_;
    float                   duration_   = 0.f;
    bool                    loop_       = true;
    float                   sampleRate_ = 30.f;
    std::vector<BoneTrack>  tracks_;
    std::vector<EventMarker> events_;
};

}  // namespace eve::animation
