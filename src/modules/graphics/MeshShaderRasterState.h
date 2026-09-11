#pragma once
#include <cstdint>
#include "common/Result.h"

namespace eve::graphics {
/** @brief Depth comparison for an ordinary custom mesh pipeline. */
enum class MeshDepthCompare { Less, LessEqual, Always };
/**
 * @brief Value snapshot of custom mesh rasterization state, independent of blending/culling.
 * @details Bias uses the backend's native depth-buffer units and slope factor.
 * Color mask bits 0..3 select R,G,B,A. This is runtime state, not a persistent format.
 */
struct MeshShaderRasterState {
    MeshDepthCompare depthCompare      = MeshDepthCompare::Less;
    float            depthBiasConstant = 0.f;
    float            depthBiasSlope    = 0.f;
    std::uint8_t     colorWriteMask    = 15;
};
/** @brief Validate a borrowed value without touching graphics state; safe on any thread. */
[[nodiscard]] Result<void> validateMeshShaderRasterState(const MeshShaderRasterState& state);
}  // namespace eve::graphics
