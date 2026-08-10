#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/**
 * Keyframed skeletal animation clip (local TRS tracks per bone).
 * Script type: `AnimClip`.
 */
class AnimClip {
public:
    explicit AnimClip(std::string name = "");
    ~AnimClip() = default;

    AnimClip(const AnimClip &)            = delete;
    AnimClip &operator=(const AnimClip &) = delete;

    void        setName(const std::string &name) { name_ = name; }
    std::string getName() const { return name_; }

    void  setDuration(float seconds);
    float getDuration() const { return duration_; }

    void setLoop(bool loop) { loop_ = loop; }
    bool getLoop() const { return loop_; }

    /** Sample rate hint used by MotionDatabase baking (Hz). */
    void  setSampleRate(float hz);
    float getSampleRate() const { return sampleRate_; }

    void addPositionKey(int boneIndex, float time, float x, float y, float z);
    void addRotationKey(int boneIndex, float time, float x, float y, float z, float w);
    void addScaleKey(int boneIndex, float time, float x, float y, float z);

    int getPositionKeyCount(int boneIndex) const;
    int getRotationKeyCount(int boneIndex) const;
    int getScaleKeyCount(int boneIndex) const;

    /**
     * Sample local pose at time (seconds). If skeleton non-null, missing tracks
     * fall back to bind pose; otherwise identity.
     */
    void sample(float time, AnimPose *out, const AnimSkeleton *skeleton = nullptr) const;

    /** Wrap or clamp time according to loop flag. */
    float wrapTime(float time) const;

private:
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

    void       ensureBone(int boneIndex);
    TransformTRS sampleBone(int boneIndex, float time, const TransformTRS &fallback) const;

    static void sampleVec3(const std::vector<Vec3Key> &keys, float time, float &x, float &y,
                           float &z, bool &ok);
    static void sampleQuat(const std::vector<QuatKey> &keys, float time, float &x, float &y,
                           float &z, float &w, bool &ok);

    std::string             name_;
    float                   duration_   = 0.f;
    bool                    loop_       = true;
    float                   sampleRate_ = 30.f;
    std::vector<BoneTrack>  tracks_;
};

}  // namespace eve::animation
