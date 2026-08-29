#pragma once

#include "animation/AnimMath.h"
#include "common/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/**
 * @brief Settings and diagnostics for offline skeletal animation retargeting.
 *
 * Explicit bone mappings take precedence over automatic matching. Automatic
 * matching first tries the exact name, then a normalized name with namespaces,
 * punctuation and case removed (for example `mixamorig:Hips` matches `hips`).
 * Script type: `AnimRetargetProfile`.
 */
class AnimRetargetProfile {
public:
    /** @brief Map one source bone to one target bone; replaces an existing mapping for that target. */
    void addBoneMapping(const std::string& sourceBone, const std::string& targetBone);
    /** @brief Remove all explicit bone mappings. */
    void clearBoneMappings();
    /** @brief Enable exact-then-normalized automatic bone-name matching. */
    void setNormalizedNameMatching(bool enabled) { normalizedNameMatching_ = enabled; }
    /** @brief Return whether normalized automatic bone-name matching is enabled. */
    bool getNormalizedNameMatching() const { return normalizedNameMatching_; }

    /** @brief Select the source and target pelvis/root used for proportional translation. */
    void setRootBones(const std::string& sourceBone, const std::string& targetBone);
    /** @brief Enable automatic root translation scaling from skeleton extents. */
    void setAutoRootScale(bool enabled) { autoRootScale_ = enabled; }
    /** @brief Return whether automatic root scaling is enabled. */
    bool getAutoRootScale() const { return autoRootScale_; }
    /** @brief Set additional horizontal and vertical multipliers for root translation. */
    void setRootTranslationScale(float horizontal, float vertical);
    /** @brief Return the additional horizontal root translation multiplier. */
    float getRootHorizontalScale() const { return rootHorizontalScale_; }
    /** @brief Return the additional vertical root translation multiplier. */
    float getRootVerticalScale() const { return rootVerticalScale_; }
    /** @brief Choose skeleton-space Bind correction instead of legacy local-space correction. */
    void setUseSkeletonSpaceRotation(bool enabled) { skeletonSpaceRotation_ = enabled; }
    /** @brief Return whether skeleton-space rotation correction is enabled. */
    bool getUseSkeletonSpaceRotation() const { return skeletonSpaceRotation_; }

    /** @brief Number of target bones mapped by the most recent retarget operation. */
    int getMatchedBoneCount() const { return matchedBoneCount_; }
    /** @brief Number of target bones left at bind pose by the most recent operation. */
    int getUnmatchedBoneCount() const { return static_cast<int>(unmatchedTargetBones_.size()); }
    /** @brief Name of an unmatched target bone, or empty for an invalid index. */
    std::string getUnmatchedTargetBone(int index) const;

private:
    friend class AnimClip;
    struct Mapping {
        std::string source, target;
    };
    std::vector<Mapping>     mappings_;
    std::string              sourceRoot_;
    std::string              targetRoot_;
    bool                     normalizedNameMatching_ = true;
    bool                     autoRootScale_          = true;
    bool                     skeletonSpaceRotation_  = true;
    float                    rootHorizontalScale_    = 1.f;
    float                    rootVerticalScale_      = 1.f;
    int                      matchedBoneCount_       = 0;
    std::vector<std::string> unmatchedTargetBones_;
};

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
    /** @brief Whether this clip contains at least one event with the stable semantic name. */
    [[nodiscard]] bool hasEvent(std::string_view name) const noexcept;
    /**
     * @brief Validates that every required semantic notify exists before gameplay registration.
     * @param
     * requiredNames Stable notify names such as contact.left_hand or land.
     * @return Applied when the contract is
     * complete, otherwise a structured rejection.
     * @thread Owner thread; this call is read-only and does not
     * retain the input span.
     * @reentrancy Does not invoke callbacks or scripts.
     */
    [[nodiscard]] eve::Result<void> validateNotifyContract(const std::vector<std::string>& requiredNames) const;

    /** @brief Add a named locomotion sync marker at clip-local time. */
    void addSyncMarker(float time, const std::string& name);
    /** @brief Replace and re-sort one sync marker. @return False for an invalid index. */
    bool setSyncMarker(int index, float time, const std::string& name);
    /** @brief Delete one sync marker. @return False for an invalid index. */
    bool removeSyncMarker(int index);
    /** @brief Return the number of locomotion sync markers. */
    int getSyncMarkerCount() const { return static_cast<int>(syncMarkers_.size()); }
    /** @brief Return a sync marker's time, or 0 for an invalid index. */
    float getSyncMarkerTime(int index) const;
    /** @brief Return a sync marker's name, or empty for an invalid index. */
    std::string getSyncMarkerName(int index) const;
    /** @brief Whether this clip and target share a usable ordered marker interval. */
    bool hasCompatibleSyncMarkers(const AnimClip* target) const;
    /** @brief Map local time into target marker space, falling back to normalized duration. */
    float mapSyncTimeTo(float time, const AnimClip* target) const;

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
    int compress(float positionError = 0.001f, float rotationErrorDegrees = 0.1f, float scaleError = 0.001f);

    /**
     * @brief Bake this clip onto a target skeleton by matching bone names and preserving bind-pose deltas.
     * Translation motion is scaled by the corresponding target/source bind-bone length ratio.
     * @return A new script-owned clip.
     */
    AnimClip* retarget(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton) const;

    /**
     * @brief Retarget using an Avatar-like mapping/profile and update its diagnostics.
     * Skeleton-space rotation preserves motion when source and target local bone axes differ.
     * The output is baked at this clip's sample rate so parent-space corrections remain stable.
     * @return A new script-owned clip.
     */
    AnimClip* retargetWithProfile(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton,
                                  AnimRetargetProfile* profile) const;

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
    struct SyncMarker {
        float       t = 0.f;
        std::string name;
    };

    void         ensureBone(int boneIndex);
    TransformTRS sampleBone(int boneIndex, float time, const TransformTRS& fallback) const;
    void         sampleClamped(float time, AnimPose* out, const AnimSkeleton* skeleton) const;

    static void sampleVec3(const std::vector<Vec3Key>& keys, float time, float& x, float& y, float& z, bool& ok);
    static void sampleQuat(const std::vector<QuatKey>& keys, float time, float& x, float& y, float& z, float& w,
                           bool& ok);

    std::string              name_;
    float                    duration_   = 0.f;
    bool                     loop_       = true;
    float                    sampleRate_ = 30.f;
    std::vector<BoneTrack>   tracks_;
    std::vector<EventMarker> events_;
    std::vector<SyncMarker>  syncMarkers_;
};

}  // namespace eve::animation
