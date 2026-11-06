#pragma once
#include <cstdint>
#include <span>
#include "common/GpuResidentBufferView.h"
#include "common/Result.h"
namespace eve::graphics {
class Shader;
struct ShaderImageInput;
struct ShaderImageRegion;
}  // namespace eve::graphics
namespace eve::graphics::vulkan {
class Graphics;
struct GpuShader;
/** @brief Module-private synchronous update; all references are borrowed for this render-thread call only.
 * @pre Input and regions were validated and copied by ShaderImageUpdate::prepare.
 * @cost Linear staging upload plus GPU synchronization per call; no pipeline creation.
 */
[[nodiscard]] Result<void> updateMeshResourceImage(Graphics& graphics, Shader& shader, const ShaderImageInput& input,
                                                   std::span<const ShaderImageRegion> regions);
/** @brief Copy a completed same-device RGBA32F buffer into one uniquely owned shader image.
 * @ownership Borrows provider, shader and buffer view only through this synchronous call.
 * @cost Linear device copy and graphics queue synchronization; no staging allocation or pipeline rebuild.
 */
[[nodiscard]] Result<void> updateMeshResourceImageFromResident(Graphics& graphics, Shader& shader,
                                                               std::uint32_t binding, std::uint32_t width,
                                                               std::uint32_t                height,
                                                               const GpuResidentBufferView& source);
/** @brief Immutable uploaded instance count; render-thread read, no retained borrow. */
std::uint32_t meshResourceInstanceCount(const GpuShader& shader);
/** @brief Query whether two live shader image slots share immutable GPU storage.
 * @ownership Borrows both shaders for this render-thread call only; invalid slots return false.
 */
EVENGINE_API_BACKENDS bool isMeshResourceImageShared(const GpuShader& first, std::uint32_t firstSlot,
                                                     const GpuShader& second, std::uint32_t secondSlot);
/** @brief Validate mesh resource shader reload. */
Result<void> validateMeshResourceShaderReload(const GpuShader& shader, std::span<const uint32_t> vertex,
                                              std::span<const uint32_t> fragment);
}  // namespace eve::graphics::vulkan
