#pragma once
#include "common/Export.h"
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <vector>
#include "common/Result.h"

namespace eve::graphics {
struct VegetationVertex;
/** @brief Immediate adapter input for TVE 12.x packed mesh streams, before UV truncation.
 * This is an owning CPU value, not a persistent native file format. UV0 and UV1
 * must retain all four source components; Unity UV4 is texcoord3 here.
 */
struct TvePackedVertex {
    glm::vec3 position{0.f}, normal{0.f, 1.f, 0.f};
    glm::vec4 color{0.f, 1.f, 0.f, 0.f};
    glm::vec4 texcoord0{0.f}, texcoord1{0.f}, texcoord3{0.f};
};
/** @brief Decode TVE 12.x color/motion/bounds/detail/pivot packing to native rest vertices.
 * Borrows input only during this call; worker-safe and reentrant, no callbacks/resources.
 * Encoded pairs must be exact unsigned 22-bit integers. Mask channels must be [0,1].
 * @return Owning vertices or InvalidArgument/Failed; failure publishes no partial stream.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<std::vector<VegetationVertex>> decodeTveVegetationVertices(
    std::span<const TvePackedVertex> vertices);
}  // namespace eve::graphics
