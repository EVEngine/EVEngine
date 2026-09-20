#pragma once

#include "common/Result.h"
#include "graphics/Drawable.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct aiMesh;

namespace eve::graphics {

/**
 * @brief GPU mesh handle (+ optional CPU morph targets).
 *
 * Morph pipeline: initMorphBase / addMorphTarget* → setMorphWeight →
 * Graphics::bakeMeshMorph (uploads blended positions to the host-visible VBO).
 */
class Mesh : public Drawable {
public:
    int   indexCount = 0;
    void *gpuHandle  = nullptr;  // vulkan::GpuMesh*
    /** @brief Vertex count of the GPU buffer (morph CPU base may be empty). */
    int gpuVertexCount = 0;
    /** @brief True after joint and weight data has been uploaded for GPU skinning. */
    bool hasGpuSkinning() const { return gpuSkinned_; }
    /** @brief Number of matrices in the current skinning palette. */
    int getSkinPaletteCount() const { return static_cast<int>(skinPalette_.size() / 16u); }
    /** @brief Copy a dynamically sized column-major palette on the render thread.
     * Invalid/non-finite input leaves the previous palette unchanged. No borrowed
     * pointer survives this call. GPU uploads enforce device storage-buffer limits.
     * @return Success or InvalidArgument; allocation failure leaves state unchanged. */
    [[nodiscard]] Result<void> setSkinPalette(const float* matrices, int matrixCount);
    /** @brief Packed column-major matrix palette used by graphics backends. */
    const std::vector<float> &skinPalette() const { return skinPalette_; }
    /** @brief Mark whether the backend vertex stream contains skin attributes. */
    void markGpuSkinned(bool value) { gpuSkinned_ = value; }

    /** @brief Copy one packed ST channel; finite values/count are checked atomically.
     * Render-thread only, no callbacks. Input is borrowed only during this call.
     */
    [[nodiscard]] Result<void> setTexcoordSet(uint32_t set, std::span<const float> values);
    /** @brief Borrow a channel until mutation/destruction; empty means absent. Render-thread only. */
    std::span<const float> texcoordSet(uint32_t set) const;
    /** @brief Revision used to invalidate backend UV uploads after a channel changes. */
    uint64_t texcoordRevision() const { return texcoordRevision_; }
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

    /** @brief Retain imported UV/color/tangent streams for custom pipelines and baking. */
    void captureImportedAttributes(const ::aiMesh &mesh);
    /** @brief Atomically copy authored tangent and bitangent XYZ streams, or clear both with empty spans.
     *
     * @return Checked invalid-input/allocation failure leaves both streams unchanged.
     * @remarks Render-thread
     * only, no callbacks. Inputs are borrowed during the call;
     * mesh owns the copies until replacement or
     * destruction. Both streams must match vertex count
     * and contain finite, nonzero, nonparallel vectors. No
     * normalization or coordinate conversion.
     */
    [[nodiscard]] Result<void> setTangentFrame(std::span<const float> tangents, std::span<const float> bitangents);
    /** @brief Validate a frame against current vertex count without mutation or allocation on success.
     * @remarks
     * Render-thread only, synchronous borrowed spans, no callbacks.
     * @return Checked validity, including the
     * empty-frame clear operation.
     */
    [[nodiscard]] Result<void> validateTangentFrame(std::span<const float> tangents,
                                                    std::span<const float> bitangents) const;
    /** @brief Validate then take owning frame buffers without allocating on success.
     * @remarks Render-thread
     * only, no callbacks. Failure does not consume inputs or change mesh state.
     * Success transfers ownership;
     * previous buffers are returned in the moved-from input vectors.
     * @return Checked validity. Useful after
     * prevalidation for coupled geometry updates.
     */
    [[nodiscard]] Result<void> adoptTangentFrame(std::vector<float> &&tangents, std::vector<float> &&bitangents);
    /** @brief Validate an optional per-vertex motion-highlight stream without mutation.
     * @remarks Render-thread
     * only; synchronous borrowed input, no callbacks or retained references.
     * Empty clears the stream; otherwise
     * one finite nonnegative scalar is required per vertex.
     * @return Checked validity; no allocation on success.

     */
    [[nodiscard]] Result<void> validateMotionHighlights(std::span<const float> values) const;
    /** @brief Validate and transfer an owning motion-highlight stream without allocating on success.
     * @remarks
     * Render-thread only, no callbacks. Failure preserves both mesh and input;
     * success swaps old storage into
     * input. Data lives until replacement or mesh destruction.
     * @return Checked validity, including empty-stream
     * clearing.
     */
    [[nodiscard]] Result<void> adoptMotionHighlights(std::vector<float> &&values);
    /** @brief Borrow motion-highlight scalars until replacement or mesh destruction.
     * @remarks Render-thread
     * only, no callbacks; never retain the view across mesh mutation.
     * @return Read-only scalar view, empty when
     * absent.
     */
    [[nodiscard]] std::span<const float> motionHighlights() const { return motionHighlights_; }
    /** @brief Validate optional interleaved vegetation variation/occlusion pairs without mutation.
     * @remarks
     * Render-thread only; values are borrowed for this call. Empty clears the stream;
     * otherwise two finite unit
     * values are required per vertex.
     * @return Checked validity; no allocation or callbacks.
     */
    [[nodiscard]] Result<void> validateVegetationFactors(std::span<const float> values) const;
    /** @brief Validate and take ownership of interleaved vegetation variation/occlusion pairs.
     * @remarks
     * Render-thread only, no callbacks. Failure preserves mesh and input. Success swaps
     * previous storage into
     * the moved-from vector, so the operation cannot allocate after validation.
     * @return Checked validity,
     * including the empty-stream clear operation.
     */
    [[nodiscard]] Result<void> adoptVegetationFactors(std::vector<float> &&values);
    /** @brief Borrow interleaved variation/occlusion pairs until mesh mutation or destruction. */
    [[nodiscard]] std::span<const float> vegetationFactors() const { return vegetationFactors_; }
    /** @brief Validate optional TVE rest-deformation data without mutation.
     * Nine floats per vertex are pivot
     * XYZ, bending, branch, flutter,
     * variation, bounds height, and bounds radius. Masks are unit values and

     * * bounds are finite nonnegative world units. Empty clears the stream.
     * @remarks Render-thread only; the
     * input is borrowed synchronously and no callbacks run.
     */
    [[nodiscard]] Result<void> validateVegetationDeformationFactors(std::span<const float> values) const;
    /** @brief Validate and atomically take ownership of a TVE rest-deformation stream.
     * Failure preserves the
     * mesh and input. Success swaps prior storage into
     * the moved-from vector without allocating after
     * validation.
     */
    [[nodiscard]] Result<void> adoptVegetationDeformationFactors(std::vector<float> &&values);
    /** @brief Borrow TVE rest-deformation data until mesh mutation or destruction. */
    [[nodiscard]] std::span<const float> vegetationDeformationFactors() const { return vegetationDeformationFactors_; }
    int getUvChannelCount() const { return static_cast<int>(importedUvs_.size()); }
    int getColorChannelCount() const { return static_cast<int>(importedColors_.size()); }
    bool hasImportedTangents() const { return !importedTangents_.empty(); }
    const std::vector<float> &importedUv(int channel) const;
    const std::vector<float> &importedColor(int channel) const;
    const std::vector<float> &importedTangents() const { return importedTangents_; }
    const std::vector<float> &importedBitangents() const { return importedBitangents_; }

private:
    std::map<uint32_t, std::vector<float>> texcoords_;
    uint64_t                               texcoordRevision_ = 0;

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
    std::vector<std::vector<float>> importedUvs_;     // xy packed per channel
    std::vector<std::vector<float>> importedColors_;  // rgba packed per channel
    std::vector<float> importedTangents_;             // xyz packed
    std::vector<float> importedBitangents_;            // xyz packed
    std::vector<float>                     motionHighlights_;
    std::vector<float>                     vegetationFactors_;
    std::vector<float>                     vegetationDeformationFactors_;
    bool gpuSkinned_ = false;
    std::vector<float> skinPalette_;
};

}  // namespace eve::graphics
