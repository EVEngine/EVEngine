#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "common/Result.h"
#include "graphics/PbrSurface.h"

namespace eve::graphics::webgpu {

enum PbrFragmentResourceFlag : std::uint32_t {
    PbrEnvironment = 1u << 0,
    PbrShadows     = 1u << 1,
    PbrExtras      = 1u << 2,
    PbrColors      = 1u << 3,
    PbrFadeNoise   = 1u << 4,
};

enum PbrVertexResourceFlag : std::uint32_t {
    PbrVertexField = 1u << 0,
    PbrMotionField = 1u << 1,
    PbrMotionNoise = 1u << 2,
};

/** @brief Device resource limits relevant to one extended PBR shader variant. */
struct PbrVariantLimits {
    std::uint32_t sampledTexturesPerStage = 17;
    std::uint32_t samplersPerStage        = 16;
};

/** @brief Stable resource-layout key and checked stage counts for a PBR snapshot. */
struct PbrVariantPlan {
    static constexpr std::uint32_t NoSampler = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t canonicalTextureMask = 0;
    std::uint32_t detailTextureMask    = 0;
    std::uint32_t fragmentFlags        = 0;
    std::uint32_t vertexFlags          = 0;
    std::uint32_t fragmentTextures     = 0;
    std::uint32_t fragmentSamplers     = 0;
    std::uint32_t vertexTextures       = 0;
    std::uint32_t vertexSamplers       = 0;
    /** Representative in the unified canonical [0,10], detail [11,13] sampler namespace. */
    std::array<std::uint32_t, 11> canonicalSamplerRepresentatives{
        NoSampler, NoSampler, NoSampler, NoSampler, NoSampler, NoSampler,
        NoSampler, NoSampler, NoSampler, NoSampler, NoSampler};
    std::array<std::uint32_t, 3> detailSamplerRepresentatives{NoSampler, NoSampler, NoSampler};

    bool operator==(const PbrVariantPlan&) const = default;
};

/**
 * @brief Plan only resources reachable for an immutable PBR draw snapshot.
 * @param surface Validated material snapshot; texture pointers are inspected for presence only.
 * @param environment True when the draw samples an environment cube.
 * @param shadows True when the draw samples the shadow array.
 * @param limits Captured WebGPU per-stage device limits.
 * @return A stable variant plan, or a structured Unsupported diagnostic before GPU mutation.
 * @thread Worker-safe and reentrant; no texture pointer is dereferenced.
 */
[[nodiscard]] Result<PbrVariantPlan> planPbrVariant(const PbrSurface& surface, bool environment, bool shadows,
                                                    const PbrVariantLimits& limits = {});

}  // namespace eve::graphics::webgpu
