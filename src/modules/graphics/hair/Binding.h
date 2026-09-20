#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::hair {

/**
 * @brief Borrowed triangle mesh used as a scalp / skin bind target.
 *
 * Positions are xyz-packed. Indices are triangle lists (3 × triangleCount).
 * Pointers are borrowed only for the duration of the call that receives them.
 *
 * @ownership Caller retains ownership; Binding copies only discrete attach data.
 */
struct SkinTriMesh {
    const float *posXYZ = nullptr;
    int vertexCount = 0;
    const uint32_t *indices = nullptr;
    int indexCount = 0;
};

/**
 * @brief How bound strands follow a deformed skin triangle.
 *
 * - Rigid: translate the whole curve by (skinnedRoot − restRoot).
 * - Offset: also rotate the rest offset by the rest→deformed triangle normal.
 *
 * Neither mode is a full skinned RBF (UE optional path); see design doc Phase 4.
 */
enum class BindingDeformMode : uint8_t { Rigid = 0, Offset = 1 };

/**
 * @brief One strand root projected onto a skin triangle (UE binding entry analogue).
 * @ownership Owned by `GroomBinding`.
 */
struct RootAttach {
    uint32_t triangleIndex = 0;
    /** @brief Barycentric weights (u,v,w) with u+v+w ≈ 1. */
    glm::vec3 barycentric{1.f, 0.f, 0.f};
    /** @brief Strand root position at bind time (rest pose). */
    glm::vec3 restRoot{0.f};
    /** @brief Rest triangle geometric normal (unit). */
    glm::vec3 restNormal{0.f, 1.f, 0.f};
};

/**
 * @brief Root-to-skin binding table (UE `UGroomBindingAsset` analogue, CPU-only).
 *
 * Built once against a rest mesh; applied each frame (or on demand) against a
 * deformed mesh that shares topology. Does not depend on the animation module —
 * callers supply packed positions.
 *
 * @thread Affine to the caller; not synchronized.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_BACKENDS GroomBinding {
public:
    /**
     * @brief Project every strand root onto the closest rest-mesh triangle.
     * @ownership Overwrites this binding; strands remain caller-owned.
     */
    [[nodiscard]] Result<void> build(const StrandsDatas &strands, const SkinTriMesh &restMesh);

    void clear();

    [[nodiscard]] size_t rootCount() const { return roots_.size(); }
    [[nodiscard]] const RootAttach *rootAt(size_t index) const;

    /**
     * @brief Deform rest strands using this binding and a deformed skin mesh.
     * @return A new `StrandsDatas` (same curve topology); rest strands unchanged.
     */
    [[nodiscard]] Result<StrandsDatas> deform(const StrandsDatas &restStrands,
                                             const SkinTriMesh &deformedMesh,
                                             BindingDeformMode mode = BindingDeformMode::Rigid) const;

private:
    std::vector<RootAttach> roots_;
};

}  // namespace eve::graphics::hair
