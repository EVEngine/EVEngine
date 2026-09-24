#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::hair {

/**
 * @brief One spatial cluster of strand curves (UE cluster-culling analogue).
 * @ownership Owned by `ClusterGrid`.
 */
struct HairCluster {
    glm::vec3 aabbMin{0.f};
    glm::vec3 aabbMax{0.f};
    /** @brief Curve indices into the source `StrandsDatas`. */
    std::vector<uint32_t> curveIndices;
};

/**
 * @brief Uniform grid over strand roots for CPU frustum culling.
 *
 * Clusters are built from root positions hashed into cells of `cellSize`.
 * Each cluster AABB covers all points of the curves assigned to that cell.
 *
 * @thread Affine to the caller; not synchronized.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_BACKENDS ClusterGrid {
public:
    /**
     * @brief Build clusters from validated strands.
     * @param cellSize World-space cell edge length; must be positive and finite.
     */
    [[nodiscard]] Result<void> build(const StrandsDatas &strands, float cellSize);

    void clear();

    [[nodiscard]] size_t clusterCount() const { return clusters_.size(); }
    [[nodiscard]] const HairCluster *clusterAt(size_t index) const;

    /**
     * @brief Frustum-cull clusters against a column-major view-projection matrix.
     * @param viewProj16 Exactly 16 floats in glm::mat4 memory order.
     * @return Visible cluster indices (stable ascending), or InvalidArgument.
     */
    [[nodiscard]] Result<std::vector<uint32_t>> cullClusters(const float *viewProj16) const;

    /**
     * @brief Collect unique curve indices from the given clusters (sorted ascending).
     */
    [[nodiscard]] Result<std::vector<uint32_t>>
    collectCurveIndices(const std::vector<uint32_t> &clusterIndices) const;

    /** @brief Overall AABB of all clusters; empty grid → zero box at origin. */
    void computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const;

private:
    std::vector<HairCluster> clusters_;
};

/**
 * @brief Copy a subset of curves into a new `StrandsDatas` (preserves point order).
 * @param curveIndices Must be in-range; duplicates are ignored after first keep.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<StrandsDatas> filterStrandsByCurves(
    const StrandsDatas &src, const std::vector<uint32_t> &curveIndices);

}  // namespace eve::graphics::hair
