#pragma once

#include <span>
#include <vkbuilder/binding_validation.hpp>
#include "graphics/ShaderResources.h"

namespace eve::graphics::vulkan {

/** @brief Owned shader ABI, complete descriptor write contract and actual set-1 layout. */
struct MeshResourceBindingPlan {
    vkb::ShaderBindingContract                  shader;
    vkb::ShaderBindingContract                  writes;
    std::vector<vk::DescriptorSetLayoutBinding> layout;
};

/** @brief Prepare bindings before GPU allocation; worker-safe, no callbacks.
 * @param vertex Borrowed custom vertex SPIR-V; empty selects the engine vertex with no set 1.
 * @param fragment Borrowed fragment SPIR-V used to create the candidate pipeline.
 * @param inputs Borrowed resource snapshot, retained only for this call.
 * @param previous Borrowed ABI of the uploaded bytes; empty for resource replacement.
 * @return Owning candidate plan or diagnostic; failure preserves the live program.
 * @cost Reflects custom stages once per replacement or reload, never per draw.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<MeshResourceBindingPlan> prepareMeshResourceBindings(
    std::span<const uint32_t> vertex, std::span<const uint32_t> fragment, const ShaderResourceInputs& inputs,
    const vkb::ShaderBindingContract& previous);

/** @brief Convert borrowed VKBuilder diagnostics to owning engine Result details. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> meshBindingValidationResult(const vkb::BindingValidation& validation,
                                                                             DiagnosticCode                code);

}  // namespace eve::graphics::vulkan
