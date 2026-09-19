#include "graphics/webgpu/PbrVariantPlan.h"

#include <bit>
#include <tuple>
#include <utility>
#include <vector>

namespace eve::graphics::webgpu {
namespace {
using SamplerKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

SamplerKey samplerKey(const PbrTextureBinding& binding) {
    return {binding.wrapS, binding.wrapT, binding.minFilter, binding.magFilter};
}

std::uint32_t addSampler(std::vector<std::pair<SamplerKey, std::uint32_t>>& samplers,
                         const SamplerKey& key, std::uint32_t resource) {
    for (const auto& [existing, representative] : samplers)
        if (existing == key) return representative;
    samplers.emplace_back(key, resource);
    return resource;
}
}  // namespace

Result<PbrVariantPlan> planPbrVariant(const PbrSurface& surface, bool environment, bool shadows,
                                      const PbrVariantLimits& limits) {
    auto valid = validatePbrSurface(surface);
    if (!valid) return Result<PbrVariantPlan>::failure(valid.status());

    PbrVariantPlan plan;
    for (std::uint32_t slot = 0; slot < std::uint32_t(PbrTextureSlot::Count); ++slot)
        if (surface.textures[slot].texture) plan.canonicalTextureMask |= 1u << slot;

    if (surface.vegetationDetail.value > 0.f) {
        // The TVE detail stage always evaluates albedo and normal through neutral
        // fallbacks. Its packed mask is reachable only when authored.
        plan.detailTextureMask = 0b011u;
        if (surface.vegetationDetail.textures[2].texture) plan.detailTextureMask |= 0b100u;
    }
    if (environment) plan.fragmentFlags |= PbrEnvironment;
    if (shadows) plan.fragmentFlags |= PbrShadows;
    if (surface.vegetationExtras.texture) plan.fragmentFlags |= PbrExtras;
    if (surface.vegetationColors.texture) plan.fragmentFlags |= PbrColors;
    if (surface.vegetationAlpha.enabled && surface.vegetationAlpha.noise) plan.fragmentFlags |= PbrFadeNoise;

    if (surface.vegetationVertex.source == PbrVegetationDeformationSource::GpuFields &&
        surface.vegetationVertex.texture)
        plan.vertexFlags |= PbrVertexField;
    if (surface.vegetationMotion.mode == PbrVegetationMotionMode::Object) {
        if (surface.vegetationMotion.texture) plan.vertexFlags |= PbrMotionField;
        if (surface.vegetationMotion.noise) plan.vertexFlags |= PbrMotionNoise;
    }

    plan.fragmentTextures = std::popcount(plan.canonicalTextureMask) + std::popcount(plan.detailTextureMask) +
                            std::popcount(plan.fragmentFlags);
    std::vector<std::pair<SamplerKey, std::uint32_t>> fragmentSamplers;
    for (std::uint32_t slot = 0; slot < std::uint32_t(PbrTextureSlot::Count); ++slot)
        if ((plan.canonicalTextureMask & (1u << slot)) != 0u) {
            plan.canonicalSamplerRepresentatives[slot] =
                addSampler(fragmentSamplers, samplerKey(surface.textures[slot]), slot);
        }
    for (std::uint32_t slot = 0; slot < surface.vegetationDetail.textures.size(); ++slot)
        if ((plan.detailTextureMask & (1u << slot)) != 0u) {
            const auto resource = std::uint32_t(PbrTextureSlot::Count) + slot;
            plan.detailSamplerRepresentatives[slot] =
                addSampler(fragmentSamplers, samplerKey(surface.vegetationDetail.textures[slot]), resource);
        }
    // These fixed-function resources currently have distinct backend sampler
    // contracts; a later layout may merge them only after proving equivalence.
    plan.fragmentSamplers = std::uint32_t(fragmentSamplers.size()) + std::popcount(plan.fragmentFlags);
    plan.vertexTextures   = std::popcount(plan.vertexFlags);
    plan.vertexSamplers   = plan.vertexTextures;

    if (plan.fragmentTextures > limits.sampledTexturesPerStage ||
        plan.fragmentSamplers > limits.samplersPerStage || plan.vertexTextures > limits.sampledTexturesPerStage ||
        plan.vertexSamplers > limits.samplersPerStage)
        return Result<PbrVariantPlan>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "WebGPU PBR material exceeds per-stage texture or sampler limits"));
    return Result<PbrVariantPlan>::success(plan);
}

}  // namespace eve::graphics::webgpu
