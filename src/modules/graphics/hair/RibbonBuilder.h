#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::hair {

/**
 * @brief Triangle-list ribbon mesh expanded from strand curves.
 *
 * Layout matches `Graphics::newMeshFromArrays`: packed pos/nrm/uv. `nrmXYZ`
 * stores the strand tangent so the existing Kajiya-Kay hair shader can light it.
 */
struct RibbonMesh {
    std::vector<float> posXYZ;
    std::vector<float> nrmXYZ;
    std::vector<float> uvST;
    std::vector<uint32_t> indices;

    [[nodiscard]] int vertexCount() const { return int(posXYZ.size() / 3u); }
    [[nodiscard]] int indexCount() const { return int(indices.size()); }
};

struct RibbonParams {
    /** @brief World-space side hint used to extrude the ribbon (normalized internally). */
    glm::vec3 sideHint{1.f, 0.f, 0.f};
    /** @brief Multiplier applied to each point radius. */
    float widthScale = 1.f;
    /** @brief Skip strands thinner than this after scaling. */
    float minWidth = 1e-5f;
};

/**
 * @brief Expand validated strands into stable (non-view-facing) ribbons.
 *
 * Deterministic for unit tests. A later phase can rebuild per frame for true
 * camera-facing billboards.
 *
 * @param strands Must already be structurally valid (or validate() is called).
 */
[[nodiscard]] Result<RibbonMesh> buildRibbons(const StrandsDatas &strands,
                                              const RibbonParams &params = {});

}  // namespace eve::graphics::hair
