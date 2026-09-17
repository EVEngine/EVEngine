#pragma once
#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "common/Result.h"

namespace eve::graphics {
/** @brief Value-owned subset of a resource shader's static instances.
 * @details Bounds are in the coordinate system produced by the instance matrices,
 * before the renderable model transform. No resource pointers are stored here.
 * Zero distance disables horizontal-distance culling. Runtime-only draw metadata;
 * callers reconstruct it from their own versioned asset after reload.
 */
struct MeshInstanceRange {
    std::uint32_t        first = 0, count = 0;
    std::array<float, 3> minimum{}, maximum{};
    float                maximumHorizontalDistance = 0;
};
/** @brief Worker-safe validation of value-owned range and culling metadata. */
[[nodiscard]] Result<void> validateMeshInstanceRange(const MeshInstanceRange& range);
/** @brief Test transformed bounds against Vulkan clip planes and horizontal distance.
 * @param range Previously validated value; read-only, not retained.
 * @param model Finite affine instance-to-world transform.
 * @param viewProjection Camera projection in zero-to-one clip depth.
 * @param eye World-space camera origin.
 * @return Whether any of the supplied bounds can be visible. Worker-safe.
 */
bool meshInstanceRangeVisible(const MeshInstanceRange& range, const glm::mat4& model, const glm::mat4& viewProjection,
                              const glm::vec3& eye);
}  // namespace eve::graphics
