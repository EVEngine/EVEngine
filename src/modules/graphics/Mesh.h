#pragma once

#include "graphics/Drawable.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

/**
 * GPU mesh handle (+ optional CPU morph targets).
 *
 * Morph pipeline: initMorphBase / addMorphTarget* → setMorphWeight →
 * Graphics::bakeMeshMorph (uploads blended positions to the host-visible VBO).
 */
class Mesh : public Drawable {
public:
    int indexCount = 0;
    void *gpuHandle = nullptr;  // vulkan::GpuMesh*

    void draw(Graphics * /*gfx*/, const glm::mat4 & /*matrix*/) const override {}

    // ---- morph targets (CPU) ----
    void clearMorphData();
    /** Capture base pose (xyz packed). Optional normals/uvs (same vertex count). */
    void initMorphBase(int vertexCount, const float *posXYZ, const float *nrmXYZ = nullptr,
                       const float *uvST = nullptr);
    /** Delta morph: target = base + delta * weight. */
    bool addMorphTarget(const std::string &name, const float *deltaPosXYZ);
    /** Absolute morph (Assimp aiAnimMesh style): stored as delta from base. */
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

    /** Bake current weights into outPos / outNrm (xyz packed). */
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
};

}  // namespace eve::graphics
