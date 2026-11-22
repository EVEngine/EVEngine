#pragma once
#include <cstdint>
#include <span>
#include <vkbuilder/binding_validation.hpp>
#include "common/Result.h"
namespace eve::graphics::vulkan {
struct GpuShader;
/** @brief Immutable uploaded instance count; render-thread read, no retained borrow. */
std::uint32_t meshResourceInstanceCount(const GpuShader& shader);
/** @brief Query whether two live shader image slots share immutable GPU storage.
 * @ownership Borrows both shaders for this render-thread call only; invalid slots return false.
 */
EVENGINE_API_BACKENDS bool isMeshResourceImageShared(const GpuShader& first, std::uint32_t firstSlot,
                                                     const GpuShader& second, std::uint32_t secondSlot);
/** @brief Prepare a reload against uploaded ABI and real bindings; render-thread only.
 * @ownership Borrows shader/stages for this call; returns an owning candidate contract.
 * @cost Reflects custom stages once per reload, before preparing GPU pipelines.
 */
[[nodiscard]] Result<vkb::ShaderBindingContract> prepareMeshResourceShaderReload(const GpuShader&          shader,
                                                                                 std::span<const uint32_t> vertex,
                                                                                 std::span<const uint32_t> fragment);
/** @brief Publish prepared ABI after all replacement pipelines succeed; render-thread only. */
void commitMeshResourceShaderReload(GpuShader& shader, vkb::ShaderBindingContract contract) noexcept;
}  // namespace eve::graphics::vulkan
