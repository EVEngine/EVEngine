#include "fluids/FluidSurfaceRenderer.h"

#include <algorithm>

namespace eve::fluids {

glm::vec4 FluidSurfaceRenderer::blendFluidColor(FluidBlendFactor factor, const glm::vec4& source,
                                                const glm::vec4& destination) {
    switch (factor) {
        case FluidBlendFactor::Zero: return glm::vec4(0.f);
        case FluidBlendFactor::One: return glm::vec4(1.f);
        case FluidBlendFactor::DestinationColor: return destination;
        case FluidBlendFactor::SourceColor: return source;
        case FluidBlendFactor::OneMinusDestinationColor: return glm::vec4(1.f) - destination;
        case FluidBlendFactor::SourceAlpha: return glm::vec4(source.a);
        case FluidBlendFactor::OneMinusSourceColor: return glm::vec4(1.f) - source;
        case FluidBlendFactor::DestinationAlpha: return glm::vec4(destination.a);
        case FluidBlendFactor::OneMinusDestinationAlpha: return glm::vec4(1.f - destination.a);
        case FluidBlendFactor::SourceAlphaSaturate: {
            const float saturated = std::min(source.a, 1.f - destination.a);
            return glm::vec4(saturated, saturated, saturated, 1.f);
        }
        case FluidBlendFactor::OneMinusSourceAlpha: return glm::vec4(1.f - source.a);
    }
    return glm::vec4(0.f);
}

Result<void> FluidSurfaceRenderer::configureSurfaceBlend(int source, int destination) {
    if (source < int(FluidBlendFactor::Zero) || source > int(FluidBlendFactor::OneMinusSourceAlpha) ||
        destination < int(FluidBlendFactor::Zero) || destination > int(FluidBlendFactor::OneMinusSourceAlpha))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Fluid surface blend factors must be Unity BlendMode values in [0,10]",
            "fluids.surface.blend"));
    surfaceBlendSource_      = static_cast<FluidBlendFactor>(source);
    surfaceBlendDestination_ = static_cast<FluidBlendFactor>(destination);
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeConfiguredSurfaceBlend(std::span<const uint8_t> sceneColor) {
    const size_t expected = size_t(params_.width) * size_t(params_.height) * 4u;
    if (!auxiliaryCurrent_ || residentColorCurrent_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Current fluid frame has no host-visible surface color",
                                                       "fluids.surface.blend"));
    if (sceneColor.size() != expected)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Surface blend requires a matching RGBA8 scene image",
                                                       "fluids.surface.blend"));

    for (size_t offset = 0; offset < expected; offset += 4u) {
        glm::vec4 source, destination;
        for (size_t channel = 0; channel < 4u; ++channel) {
            const auto component = static_cast<glm::vec4::length_type>(channel);
            source[component]      = float(color_[offset + channel]) / 255.f;
            destination[component] = float(sceneColor[offset + channel]) / 255.f;
        }
        const glm::vec4 blended =
            glm::clamp(source * blendFluidColor(surfaceBlendSource_, source, destination) +
                           destination * blendFluidColor(surfaceBlendDestination_, source, destination),
                       glm::vec4(0.f), glm::vec4(1.f));
        for (size_t channel = 0; channel < 4u; ++channel)
            color_[offset + channel] =
                uint8_t(blended[static_cast<glm::vec4::length_type>(channel)] * 255.f + .5f);
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureParticleBlend(int source, int destination, bool depthWrite) {
    if (source < int(FluidBlendFactor::Zero) || source > int(FluidBlendFactor::OneMinusSourceAlpha) ||
        destination < int(FluidBlendFactor::Zero) || destination > int(FluidBlendFactor::OneMinusSourceAlpha))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Fluid particle blend factors must be Unity BlendMode values in [0,10]",
            "fluids.surface.particleBlend"));
    particleBlendSource_      = static_cast<FluidBlendFactor>(source);
    particleBlendDestination_ = static_cast<FluidBlendFactor>(destination);
    particleDepthWrite_       = depthWrite;
    particleBlendConfigured_  = true;
    return Result<void>::success();
}

}  // namespace eve::fluids
