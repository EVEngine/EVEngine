#pragma once

#include "animation/AnimMath.h"

#include <string>
#include <vector>

namespace eve::model3d {
class ModelData;
}

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::animation {

class AnimSkeleton;
class AnimPose;

/**
 * @brief CPU linear-blend skinning binding for one mesh against an AnimSkeleton.
 *
 * Built from Assimp skin data on ModelData (bone names, inverse-bind matrices,
 * per-vertex weights). Call skinPositions() after AnimPose::computeWorld().
 *
 * Script type: `AnimSkin`.
 */
class AnimSkin {
public:
    static constexpr int kMaxInfluences = 4;

    AnimSkin()  = default;
    ~AnimSkin() = default;

    AnimSkin(const AnimSkin&)            = delete;
    AnimSkin& operator=(const AnimSkin&) = delete;

    /**
     * @brief Build skin binding for meshIndex on model, mapping bone names onto skeleton.
     * Throws if the mesh has no bones or no bone names match the skeleton.
     * Returned pointer is owned by the caller / script GC.
     */
    static AnimSkin* fromModel(const model3d::ModelData* model, int meshIndex, const AnimSkeleton* skeleton);

    int getVertexCount() const { return vertexCount_; }
    int getBoneCount() const { return static_cast<int>(skeletonBone_.size()); }
    int getInfluenceCount() const { return kMaxInfluences; }

    /** @brief Skeleton bone index used by skin joint i (-1 if unused). */
    int         getSkeletonBone(int skinBoneIndex) const;
    std::string getSkinBoneName(int skinBoneIndex) const;

    /** @brief Inverse-bind matrix element (column-major, 0..15) for skin joint i. */
    float getInverseBindElement(int skinBoneIndex, int elementIndex) const;

    /**
     * @brief Build and cache the column-major skinning matrix palette for a pose.
     * The palette contains boneWorld * inverseBind and can be uploaded to a GPU
     * storage/uniform buffer without CPU vertex deformation.
     * @return False when pose is null, otherwise true.
     */
    bool updateMatrixPalette(const AnimPose* pose) const;
    /** @brief Cached palette element for one skin bone (elementIndex 0..15). */
    float getMatrixPaletteElement(int skinBoneIndex, int elementIndex) const;
    /** @brief Upload this skin's joint/weight vertex stream to an existing GPU mesh. */
    bool bindGpuMesh(graphics::Graphics* gfx, graphics::Mesh* mesh);
    /** @brief Update a bound GPU mesh's matrix palette from a world-computed pose. */
    bool updateGpuMesh(graphics::Mesh* mesh, const AnimPose* pose) const;

    /**
     * @brief Linear-blend skin bind-pose positions into outPosXYZ (vertexCount*3 floats).
     * pose must already have computeWorld(skeleton) applied and match the skeleton
     * used at fromModel time.
     */
    void skinPositions(const AnimPose* pose, float* outPosXYZ) const;

    /**
     * @brief Convenience: skin into a vector sized vertexCount*3.
     * Returns false if pose is null or bone count mismatches.
     */
    bool skinPositionsTo(const AnimPose* pose, std::vector<float>& outPosXYZ) const;

    /**
     * @brief Skin into an internal cache readable via getSkinnedPosition*.
     * Returns false if pose is null or there are no vertices.
     */
    bool updateSkinnedPositions(const AnimPose* pose);

    /** @brief True after a successful updateSkinnedPositions(). */
    bool hasSkinnedPositions() const { return skinnedValid_; }

    /** @brief Cached skinned position component (requires updateSkinnedPositions). */
    float getSkinnedPositionX(int vertexIndex) const;
    float getSkinnedPositionY(int vertexIndex) const;
    float getSkinnedPositionZ(int vertexIndex) const;

    /**
     * @brief Packed skinned positions (xyz, vertexCount*3) as a copy.
     * Empty when updateSkinnedPositions() has not succeeded yet.
     */
    std::vector<float> getSkinnedPositions() const;

    /**
     * @brief Skin the bind-pose normals captured at fromModel with the pose.
     * pose must already have computeWorld(skeleton) applied and match the
     * skeleton used at fromModel time. Returns false when the model mesh has
     * no normals or pose is null.
     */
    bool updateSkinnedNormals(const AnimPose* pose);

    /** @brief True after a successful updateSkinnedNormals(). */
    bool hasSkinnedNormals() const { return skinnedNrmValid_; }

    /** @brief Packed skinned normals (xyz, vertexCount*3) as a copy. */
    std::vector<float> getSkinnedNormals() const;

    /**
     * @brief Skin positions (and normals when available) and write the result
     * back to a GPU mesh in one call. pose must already have computeWorld()
     * applied. The mesh must be built from the same model/mesh this skin was
     * created from, with matching vertex count. Returns false when the
     * backend cannot update in place (e.g. WebGPU) or arguments are invalid.
     */
    bool applyToMesh(graphics::Graphics* gfx, graphics::Mesh* mesh, const AnimPose* pose);

    /** @brief Bind-pose (unskinned) position component for vertex v (0..vertexCount-1). */
    float getBindPositionX(int vertexIndex) const;
    float getBindPositionY(int vertexIndex) const;
    float getBindPositionZ(int vertexIndex) const;

    /** @brief Influence slot i (0..3) for vertex: skeleton bone index or -1. */
    int   getVertexBone(int vertexIndex, int influenceIndex) const;
    float getVertexWeight(int vertexIndex, int influenceIndex) const;

private:
    struct Influence {
        int   bone   = -1;  // index into skeletonBone_ / inverseBind_
        float weight = 0.f;
    };

    void requireVertex(int vertexIndex) const;
    void requireSkinBone(int skinBoneIndex) const;

    int                        vertexCount_ = 0;
    std::vector<float>         bindPos_;       // xyz packed
    std::vector<Influence>     influences_;    // vertexCount_ * kMaxInfluences
    std::vector<int>           skeletonBone_;  // skin joint → skeleton bone
    std::vector<std::string>   skinBoneNames_;
    std::vector<Mat4>          inverseBind_;
    std::vector<float>         bindNrm_;         // xyz packed, may be empty
    std::vector<float>         skinnedPos_;      // xyz packed cache
    std::vector<float>         skinnedNrm_;      // xyz packed cache
    mutable std::vector<Mat4>  skinMatrices_;    // reused per-frame palette scratch
    mutable std::vector<float> normalMatrices_;  // reused 3x3 palette scratch
    mutable bool               matrixPaletteValid_ = false;
    bool                       skinnedValid_    = false;
    bool                       skinnedNrmValid_ = false;
};

}  // namespace eve::animation
