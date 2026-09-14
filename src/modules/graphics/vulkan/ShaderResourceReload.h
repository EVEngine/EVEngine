#pragma once
#include <cstdint>
#include <span>
#include "common/Result.h"
namespace eve::graphics::vulkan {
struct GpuShader;
/** @brief Immutable uploaded instance count; render-thread read, no retained borrow. */
std::uint32_t meshResourceInstanceCount(const GpuShader& shader);
/** @brief Query whether two live shader image slots share immutable GPU storage.
 * @ownership Borrows both shaders for this render-thread call only; invalid slots return false.
 */
bool         isMeshResourceImageShared(const GpuShader& first, std::uint32_t firstSlot, const GpuShader& second,
                                       std::uint32_t secondSlot);
Result<void> validateMeshResourceShaderReload(const GpuShader& shader, std::span<const uint32_t> vertex,
                                              std::span<const uint32_t> fragment);
}  // namespace eve::graphics::vulkan
