#pragma once
#include "common/Export.h"


#include "animation/AnimMath.h"

#include <vector>

namespace eve::animation {

class AnimSkeleton;

/**
 * @brief Evaluated local (and optional world) pose for an AnimSkeleton.
 * Script type: `AnimPose`.
 */
class EVENGINE_API_WORLD AnimPose {
public:
    /** @brief Anim pose. */
    AnimPose() = default;
    /** @brief Anim pose. */
    explicit AnimPose(int boneCount);
    /** @brief Anim pose. */
    ~AnimPose() = default;

    AnimPose(const AnimPose &)            = delete;
    AnimPose &operator=(const AnimPose &) = delete;
    /** @brief Anim pose. */
    AnimPose(AnimPose &&) noexcept        = default;
    /** @brief Operator =. */
    AnimPose &operator=(AnimPose &&) noexcept = default;

    /** @brief Resize. */
    void resize(int boneCount);
    /** @brief Returns the bone count. */
    int  getBoneCount() const { return static_cast<int>(locals_.size()); }

    /** @brief Copies from. */
    void copyFrom(const AnimPose *other);
    /** @brief Blend from. */
    void blendFrom(const AnimPose *a, const AnimPose *b, float t);

    /** @brief Sets the local position. */
    void setLocalPosition(int boneIndex, float x, float y, float z);
    /** @brief Sets the local rotation. */
    void setLocalRotation(int boneIndex, float x, float y, float z, float w);
    /** @brief Sets the local scale. */
    void setLocalScale(int boneIndex, float x, float y, float z);

    /** @brief Returns the local position x. */
    float getLocalPositionX(int boneIndex) const;
    /** @brief Returns the local position y. */
    float getLocalPositionY(int boneIndex) const;
    /** @brief Returns the local position z. */
    float getLocalPositionZ(int boneIndex) const;
    /** @brief Returns the local rotation x. */
    float getLocalRotationX(int boneIndex) const;
    /** @brief Returns the local rotation y. */
    float getLocalRotationY(int boneIndex) const;
    /** @brief Returns the local rotation z. */
    float getLocalRotationZ(int boneIndex) const;
    /** @brief Returns the local rotation w. */
    float getLocalRotationW(int boneIndex) const;
    /** @brief Returns the local scale x. */
    float getLocalScaleX(int boneIndex) const;
    /** @brief Returns the local scale y. */
    float getLocalScaleY(int boneIndex) const;
    /** @brief Returns the local scale z. */
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
     * @brief Analytically solve a root-mid-tip chain toward a world-space target.
     * @details Preserves segment lengths and the current bend plane, with a deterministic
     * plane for a straight chain. Unreachable targets clamp to the chain's reach;
     * full-weight reachable targets are exact up to float rounding for rigid/uniformly
     * scaled chains. Blends solved local rotations once, then updates world transforms.
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
     * False for an invalid chain or zero-length segment, otherwise true.
     * Nonfinite targets/weights throw before changing local transforms.
     */
    bool solveTwoBoneIK(const AnimSkeleton* skeleton, int rootBone, int midBone, int tipBone, float targetX,
                        float targetY, float targetZ, float weight = 1.f);

    /** @brief Returns the world position x. */
    float getWorldPositionX(int boneIndex) const;
    /** @brief Returns the world position y. */
    float getWorldPositionY(int boneIndex) const;
    /** @brief Returns the world position z. */
    float getWorldPositionZ(int boneIndex) const;
    /** @brief Returns the world rotation x. */
    float getWorldRotationX(int boneIndex) const;
    /** @brief Returns the world rotation y. */
    float getWorldRotationY(int boneIndex) const;
    /** @brief Returns the world rotation z. */
    float getWorldRotationZ(int boneIndex) const;
    /** @brief Returns the world rotation w. */
    float getWorldRotationW(int boneIndex) const;

    /**
     * @brief Column-major 4x4 world matrix for boneIndex after computeWorld().
     * elementIndex in [0, 15]. Used by CPU skinning (AnimSkin).
     */
    float getWorldMatrixElement(int boneIndex, int elementIndex) const;
    /** @brief Write 16 floats (column-major) into out16 (must not be null). */
    void  getWorldMatrix(int boneIndex, float *out16) const;

    /** @brief Local. */
    TransformTRS       &local(int boneIndex);
    /** @brief Local. */
    const TransformTRS &local(int boneIndex) const;
    /** @brief World. */
    const TransformTRS &world(int boneIndex) const;

private:
    void requireBone(int boneIndex) const;
    bool solveTwoBoneIKPoleImpl(const AnimSkeleton* skeleton, int rootBone, int midBone, int tipBone, float targetX,
                                float targetY, float targetZ, float poleX, float poleY, float poleZ, float weight);

    std::vector<TransformTRS> locals_;
    std::vector<TransformTRS> worlds_;
};

}  // namespace eve::animation
