#pragma once

#include "animation/AnimPose.h"

#include <string>
#include <vector>

namespace eve::animation {

class AnimClip;
class AnimSkeleton;

/**
 * Baked motion-matching feature database from one or more AnimClips.
 * Feature layout per frame:
 *   [0..1]   root velocity xz
 *   [2..7]   trajectory pos xz at +0.33/+0.66/+1.0s (character space)
 *   [8..10]  trajectory facing dir xz + yaw sin/cos packed as (fx,fz) for +1.0s
 *   [11..]   selected joint world positions (xyz each)
 * Script type: `MotionDatabase`.
 */
class MotionDatabase {
public:
    explicit MotionDatabase(AnimSkeleton *skeleton);
    ~MotionDatabase() = default;

    MotionDatabase(const MotionDatabase &)            = delete;
    MotionDatabase &operator=(const MotionDatabase &) = delete;

    AnimSkeleton *getSkeleton() const { return skeleton_; }

    /** Include bone world position in pose features (by index). */
    void addFeatureBone(int boneIndex);
    void addFeatureBoneByName(const std::string &name);

    /**
     * Bone used for trajectory / velocity features (default 0).
     * Mixamo clips typically want hips (`mixamorig:Hips`).
     */
    void setRootBone(int boneIndex);
    int  getRootBone() const { return rootBone_; }
    void setRootBoneByName(const std::string &name);

    void addClip(AnimClip *clip);
    int  getClipCount() const { return static_cast<int>(clips_.size()); }

    /** Bake all clips into searchable frames. Call after addClip / feature bones. */
    void bake();
    bool isBaked() const { return baked_; }

    int getFrameCount() const { return static_cast<int>(frames_.size()); }
    int getFeatureSize() const { return featureSize_; }

    float getFrameTime(int frameIndex) const;
    int   getFrameClipIndex(int frameIndex) const;
    AnimClip *getClip(int clipIndex) const;

    int getFeatureBoneCount() const { return static_cast<int>(featureBones_.size()); }
    int getFeatureBone(int index) const;

    /** Copy feature vector into out[0..featureSize). */
    void getFeature(int frameIndex, float *out, int outCount) const;

    struct Frame {
        int                clipIndex = 0;
        float              time      = 0.f;
        std::vector<float> feature;
        // Cached root for trajectory reconstruction helpers.
        float rootX = 0.f, rootZ = 0.f, rootYaw = 0.f;
        float velX = 0.f, velZ = 0.f;
    };

    const Frame &frameAt(int index) const;

private:
    void        requireBaked() const;
    void        computeFeatureSize();
    void        extractFeature(AnimClip *clip, float time, float dtSample, std::vector<float> &out,
                               float &rootX, float &rootZ, float &rootYaw, float &velX,
                               float &velZ) const;
    static float yawFromQuat(float x, float y, float z, float w);

    AnimSkeleton              *skeleton_ = nullptr;
    std::vector<AnimClip *>    clips_;
    std::vector<int>           featureBones_;
    std::vector<Frame>         frames_;
    int                        featureSize_ = 0;
    int                        rootBone_    = 0;
    bool                       baked_       = false;
    mutable AnimPose           scratchPose_;
};

}  // namespace eve::animation
