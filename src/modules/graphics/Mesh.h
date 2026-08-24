#pragma once

#include "graphics/Drawable.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

/**
 * @brief GPU mesh handle (+ optional CPU morph targets).
 *
 * Morph pipeline: initMorphBase / addMorphTarget* → setMorphWeight →
 * Graphics::bakeMeshMorph (uploads blended positions to the host-visible VBO).
 */
class Mesh : public Drawable {
public:
    static constexpr int kMaxSkinBones = 128;
    int   indexCount = 0;
    void *gpuHandle  = nullptr;  // vulkan::GpuMesh*
    /** @brief Vertex count of the GPU buffer (morph CPU base may be empty). */
    int gpuVertexCount = 0;
    /** @brief True after joint and weight data has been uploaded for GPU skinning. */
    bool hasGpuSkinning() const { return gpuSkinned_; }
    /** @brief Number of matrices in the current skinning palette. */
    int getSkinPaletteCount() const { return static_cast<int>(skinPalette_.size() / 16u); }
    /** @brief Replace the per-draw column-major GPU skinning palette. */
    bool setSkinPalette(const float *matrices, int matrixCount);
    /** @brief Packed column-major matrix palette used by graphics backends. */
    const std::vector<float> &skinPalette() const { return skinPalette_; }
    /** @brief Mark whether the backend vertex stream contains skin attributes. */
    void markGpuSkinned(bool value) { gpuSkinned_ = value; }

    /**
     * @brief Model-space bounding sphere used for view/cascade frustum culling.
     * Computed from vertex positions at upload time (see computeBounds).
     * boundsRadius <= 0 means unknown — callers must treat the mesh as
     * always visible (no culling).
     */
    float boundsCx     = 0.f;
    float boundsCy     = 0.f;
    float boundsCz     = 0.f;
    float boundsRadius = 0.f;

    /** @brief True when a valid bounding sphere is available for culling. */
    bool hasBounds() const { return boundsRadius > 0.f; }

    /** @brief Compute the bounding sphere (centroid + max radius) from positions. */
    void computeBounds(const float *posXYZ, int vertexCount);

    void draw(Graphics * /*gfx*/, const glm::mat4 & /*matrix*/) const override {}
    /**
     * @brief Screen-space black proxy for volumetric occlusion.
     * Interprets matrix as 2D affine (translation + scale) and fills a solid rect —
     * callers should pass a pixel-space placement (e.g. projected AABB).
     */
    void drawOcclusion(Graphics *gfx, const glm::mat4 &matrix) const override;

    // ---- morph targets (CPU) ----
    void clearMorphData();
    /** @brief Capture base pose (xyz packed). Optional normals/uvs (same vertex count). */
    void initMorphBase(int vertexCount, const float *posXYZ, const float *nrmXYZ = nullptr,
                       const float *uvST = nullptr);
    /** Delta morph: target = base + delta * weight. */
    bool addMorphTarget(const std::string &name, const float *deltaPosXYZ);
    /** @brief Absolute morph (Assimp aiAnimMesh style): stored as delta from base. */
    bool addMorphTargetAbsolute(const std::string &name, const float *absPosXYZ);

    int getVertexCount() const;
    int getMorphCount() const;
    std::string getMorphName(int index) const;
    bool hasMorph(const std::string &name) const;
    bool setMorphWeight(const std::string &name, float weight);
    float getMorphWeight(const std::string &name) const;
    void clearMorphWeights();
    bool isMorphDirty() const { return morphDirty_; }
    void markMorphClean() { morphDirty_ = false; }
    bool hasMorphData() const { return !basePos_.empty(); }

    /** @brief Bake current weights into outPos / outNrm (xyz packed). */
    void computeMorphedPositions(std::vector<float> &outPos, std::vector<float> &outNrm) const;

    const std::vector<float> &baseUv() const { return baseUv_; }

private:
    struct MorphTarget {
        std::string name;
        std::vector<float> deltaPos;  // xyz packed, size = vertexCount*3
    };

    std::vector<float> basePos_;
    std::vector<float> baseNrm_;
    std::vector<float> baseUv_;
    std::vector<MorphTarget> morphs_;
    std::unordered_map<std::string, float> morphWeights_;
    bool morphDirty_ = false;
    bool gpuSkinned_ = false;
    std::vector<float> skinPalette_;
};

}  // namespace eve::graphics
