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
    AnimPose(AnimPose &&) noexcept        = default;
    AnimPose &operator=(AnimPose &&) noexcept = default;

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
    void computeWorld(const AnimSkeleton* skeleton);

    /**
     * @brief Rotate a bone so its local +Z axis aims at a world-space target.
     * @param skeleton Skeleton
     * defining the hierarchy.
     * @param boneIndex Bone to rotate.
     * @param targetX World-space target X.

     * * @param targetY World-space target Y.
     * @param targetZ World-space target Z.
     * @param weight Blend
     * weight in [0, 1].
     * @return False when the target is coincident with the bone, otherwise true.
     */
    bool aimBone(const AnimSkeleton* skeleton, int boneIndex, float targetX, float targetY, float targetZ,
                 float weight = 1.f);

    /**
     * @brief Solve a root-mid-tip chain toward a world-space target using CCD.
     * @param skeleton Skeleton
     * defining the hierarchy.
     * @param rootBone Root joint index.
     * @param midBone Middle joint index.
     *
     * @param tipBone End-effector index.
     * @param targetX World-space target X.
     * @param targetY World-space
     * target Y.
     * @param targetZ World-space target Z.
     * @param weight Blend weight in [0, 1].
     * @return
     * False for an invalid chain or degenerate target, otherwise true.
     */
    bool solveTwoBoneIK(const AnimSkeleton* skeleton, int rootBone, int midBone, int tipBone, float targetX,
                        float targetY, float targetZ, float weight = 1.f);

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
