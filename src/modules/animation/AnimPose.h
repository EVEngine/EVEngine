#pragma once

#include "animation/AnimMath.h"

#include <vector>

namespace eve::animation {

class AnimSkeleton;

/**
 * @brief Evaluated local (and optional world) pose for an AnimSkeleton.
 * Script type: `AnimPose`.
 */
class AnimPose {
public:
    AnimPose() = default;
    explicit AnimPose(int boneCount);
    ~AnimPose() = default;

    AnimPose(const AnimPose &)            = delete;
    AnimPose &operator=(const AnimPose &) = delete;

    void resize(int boneCount);
    int  getBoneCount() const { return static_cast<int>(locals_.size()); }

    void copyFrom(const AnimPose *other);
    void blendFrom(const AnimPose *a, const AnimPose *b, float t);

    void setLocalPosition(int boneIndex, float x, float y, float z);
    void setLocalRotation(int boneIndex, float x, float y, float z, float w);
    void setLocalScale(int boneIndex, float x, float y, float z);

    float getLocalPositionX(int boneIndex) const;
    float getLocalPositionY(int boneIndex) const;
    float getLocalPositionZ(int boneIndex) const;
    float getLocalRotationX(int boneIndex) const;
    float getLocalRotationY(int boneIndex) const;
    float getLocalRotationZ(int boneIndex) const;
    float getLocalRotationW(int boneIndex) const;
    float getLocalScaleX(int boneIndex) const;
    float getLocalScaleY(int boneIndex) const;
    float getLocalScaleZ(int boneIndex) const;

    /**
     * @brief Compute world transforms from local pose + skeleton hierarchy.
     * World values readable via getWorld* after this call.
     */
    void computeWorld(const AnimSkeleton *skeleton);

    float getWorldPositionX(int boneIndex) const;
    float getWorldPositionY(int boneIndex) const;
    float getWorldPositionZ(int boneIndex) const;
    float getWorldRotationX(int boneIndex) const;
    float getWorldRotationY(int boneIndex) const;
    float getWorldRotationZ(int boneIndex) const;
    float getWorldRotationW(int boneIndex) const;

    /**
     * @brief Column-major 4x4 world matrix for boneIndex after computeWorld().
     * elementIndex in [0, 15]. Used by CPU skinning (AnimSkin).
     */
    float getWorldMatrixElement(int boneIndex, int elementIndex) const;
    /** @brief Write 16 floats (column-major) into out16 (must not be null). */
    void  getWorldMatrix(int boneIndex, float *out16) const;

    TransformTRS       &local(int boneIndex);
    const TransformTRS &local(int boneIndex) const;
    const TransformTRS &world(int boneIndex) const;

private:
    void requireBone(int boneIndex) const;

    std::vector<TransformTRS> locals_;
    std::vector<TransformTRS> worlds_;
};

}  // namespace eve::animation
