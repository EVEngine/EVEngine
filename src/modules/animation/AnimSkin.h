#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::model3d {
class ModelData;
}

namespace eve::animation {

class AnimSkeleton;
class AnimPose;

/**
 * CPU linear-blend skinning binding for one mesh against an AnimSkeleton.
 *
 * Built from Assimp skin data on ModelData (bone names, inverse-bind matrices,
 * per-vertex weights). Call skinPositions() after AnimPose::computeWorld().
 *
 * Script type: `AnimSkin`.
 */
class AnimSkin {
public:
    static constexpr int kMaxInfluences = 4;

    AnimSkin() = default;
    ~AnimSkin() = default;

    AnimSkin(const AnimSkin &)            = delete;
    AnimSkin &operator=(const AnimSkin &) = delete;

    /**
     * Build skin binding for meshIndex on model, mapping bone names onto skeleton.
     * Throws if the mesh has no bones or no bone names match the skeleton.
     * Returned pointer is owned by the caller / script GC.
     */
    static AnimSkin *fromModel(const model3d::ModelData *model, int meshIndex,
                               const AnimSkeleton *skeleton);

    int getVertexCount() const { return vertexCount_; }
    int getBoneCount() const { return static_cast<int>(skeletonBone_.size()); }
    int getInfluenceCount() const { return kMaxInfluences; }

    /** Skeleton bone index used by skin joint i (-1 if unused). */
    int getSkeletonBone(int skinBoneIndex) const;
    std::string getSkinBoneName(int skinBoneIndex) const;

    /** Inverse-bind matrix element (column-major, 0..15) for skin joint i. */
    float getInverseBindElement(int skinBoneIndex, int elementIndex) const;

    /**
     * Linear-blend skin bind-pose positions into outPosXYZ (vertexCount*3 floats).
     * pose must already have computeWorld(skeleton) applied and match the skeleton
     * used at fromModel time.
     */
    void skinPositions(const AnimPose *pose, float *outPosXYZ) const;

    /**
     * Convenience: skin into a vector sized vertexCount*3.
     * Returns false if pose is null or bone count mismatches.
     */
    bool skinPositionsTo(const AnimPose *pose, std::vector<float> &outPosXYZ) const;

    /** Bind-pose (unskinned) position component for vertex v (0..vertexCount-1). */
    float getBindPositionX(int vertexIndex) const;
    float getBindPositionY(int vertexIndex) const;
    float getBindPositionZ(int vertexIndex) const;

    /** Influence slot i (0..3) for vertex: skeleton bone index or -1. */
    int   getVertexBone(int vertexIndex, int influenceIndex) const;
    float getVertexWeight(int vertexIndex, int influenceIndex) const;

private:
    struct Influence {
        int   bone   = -1;  // index into skeletonBone_ / inverseBind_
        float weight = 0.f;
    };

    void requireVertex(int vertexIndex) const;
    void requireSkinBone(int skinBoneIndex) const;

    int                      vertexCount_ = 0;
    std::vector<float>       bindPos_;       // xyz packed
    std::vector<Influence>   influences_;    // vertexCount_ * kMaxInfluences
    std::vector<int>         skeletonBone_;  // skin joint → skeleton bone
    std::vector<std::string> skinBoneNames_;
    std::vector<Mat4>        inverseBind_;
};

}  // namespace eve::animation
