#include "graphics/vulkan/ShaderBindingValidation.h"

#include <algorithm>
#include <string>
#include "graphics/ShaderResourceValidation.h"
#if defined(EVENGINE_SPIRV_ASSET_VALIDATION)
#include <vkbuilder/shader_reflection.hpp>
#endif

namespace eve::graphics::vulkan {

Result<void> meshBindingValidationResult(const vkb::BindingValidation& validation, DiagnosticCode code) {
    if (validation) return Result<void>::success();
    std::vector<Diagnostic> diagnostics;
    for (const auto& issue : validation.diagnostics) {
        const auto set = std::to_string(issue.set), binding = std::to_string(issue.binding);
        diagnostics.push_back(Diagnostic::error(code, "Shader resource binding mismatch",
                                                "shader.resources.set[" + set + "].binding[" + binding + "]",
                                                {{"set", set},
                                                 {"binding", binding},
                                                 {"stages", vk::to_string(issue.stages)},
                                                 {"expected", issue.expected},
                                                 {"actual", issue.actual}},
                                                "graphics.vulkan.shader_bindings"));
    }
    const auto statusCode = Status::failure(diagnostics.front()).code();
    return Result<void>::failure(Status{statusCode, std::move(diagnostics)});
}

Result<MeshResourceBindingPlan> prepareMeshResourceBindings(std::span<const uint32_t>         vertex,
                                                            std::span<const uint32_t>         fragment,
                                                            const ShaderResourceInputs&       inputs,
                                                            const vkb::ShaderBindingContract& previous) {
    auto admitted = detail::validateResourceShaderStages(vertex, fragment, inputs);
    if (!admitted) return Result<MeshResourceBindingPlan>::failure(admitted.status());
#if defined(EVENGINE_SPIRV_ASSET_VALIDATION)
    MeshResourceBindingPlan plan;
    plan.shader = previous;
    // Engine-owned set 0 has a separately validated Frame-prefix ABI. Legal
    // stage-specific prefixes do not have identical reflected struct sizes.
    for (bool isVertex : {true, false}) {
        const auto words = isVertex ? vertex : fragment;
        if (words.empty()) continue;
        vkb::ShaderBindingContract stage;
        auto                       reflected =
            meshBindingValidationResult(vkb::reflectShaderBindings(stage, {words.begin(), words.end()},
                                                                   isVertex ? vk::ShaderStageFlagBits::eVertex
                                                                            : vk::ShaderStageFlagBits::eFragment),
                                        DiagnosticCode::Unsupported);
        if (!reflected) return Result<MeshResourceBindingPlan>::failure(reflected.status());
        std::vector<vkb::ShaderBinding> resources;
        for (const auto& binding : stage.bindings())
            if (binding.set == 1) resources.push_back(binding);
        auto merged = meshBindingValidationResult(plan.shader.merge(resources), DiagnosticCode::Conflict);
        if (!merged) return Result<MeshResourceBindingPlan>::failure(merged.status());
    }
    plan.writes                            = plan.shader;
    const auto                      stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    std::vector<vkb::ShaderBinding> unused;
    auto                            addHostBinding = [&](vkb::ShaderBinding host) {
        plan.layout.emplace_back(host.binding, host.type, host.count, host.stages);
        // Initialize unused host slots too, allowing a later reload to use them.
        if (std::none_of(plan.shader.bindings().begin(), plan.shader.bindings().end(),
                                                    [&](const auto& shader) { return shader.binding == host.binding; }))
            unused.push_back(std::move(host));
    };
    for (const auto& image : inputs.images) {
        vkb::ShaderBinding host;
        host.set      = 1;
        host.binding  = image.binding;
        host.type     = vk::DescriptorType::eCombinedImageSampler;
        host.stages   = stages;
        host.viewType = image.dimension == ShaderImageDimension::Cube      ? vk::ImageViewType::eCube
                        : image.dimension == ShaderImageDimension::Array2D ? vk::ImageViewType::e2DArray
                        : image.dimension == ShaderImageDimension::Image3D ? vk::ImageViewType::e3D
                                                                           : vk::ImageViewType::e2D;
        addHostBinding(std::move(host));
    }
    if (!inputs.constants.empty()) {
        vkb::ShaderBinding host;
        host.set     = 1;
        host.binding = 32;
        host.stages  = stages;
        addHostBinding(std::move(host));
    }
    if (!inputs.instanceMatrices.empty()) {
        vkb::ShaderBinding host;
        host.set     = 1;
        host.binding = 33;
        host.type    = vk::DescriptorType::eStorageBuffer;
        host.stages  = vk::ShaderStageFlagBits::eVertex;
        addHostBinding(std::move(host));
    }
    auto complete = meshBindingValidationResult(plan.writes.merge(unused), DiagnosticCode::Conflict);
    if (!complete) return Result<MeshResourceBindingPlan>::failure(complete.status());
    auto layout = meshBindingValidationResult(plan.writes.validateLayout(1, plan.layout), DiagnosticCode::TypeMismatch);
    if (!layout) return Result<MeshResourceBindingPlan>::failure(layout.status());
    return Result<MeshResourceBindingPlan>::success(std::move(plan));
#else
    (void)previous;
    return Result<MeshResourceBindingPlan>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "SPIR-V validation provider is unavailable", "shader.resources"));
#endif
}

}  // namespace eve::graphics::vulkan
