#pragma once

#include <string_view>
#include <string>

#include "graphics/webgpu/PbrVariantPlan.h"

namespace eve::graphics::webgpu {

/** @brief Reproducibly generated full PBR WGSL sources before resource specialization. */
struct PbrVariantSources {
    std::string_view vertex;
    std::string_view fragment;
};

/** @brief Owning WGSL stages after unreachable texture operations and declarations are removed. */
struct PbrSpecializedSources {
    std::string vertex;
    std::string fragment;
};

/**
 * @brief Return immutable full PBR WGSL generated from the canonical GLSL implementation.
 * @return Process-lifetime views; callers must specialize resource declarations before pipeline creation.
 * @thread Worker-safe and reentrant.
 */
[[nodiscard]] PbrVariantSources pbrVariantBaseSources();

/**
 * @brief Specialize the generated WGSL for the resource reachability encoded by a checked plan.
 * @param plan Plan returned by planPbrVariant for the same immutable draw snapshot.
 * @return Owning stages whose static texture declarations match the reachable resource set.
 */
[[nodiscard]] PbrSpecializedSources specializePbrVariantSources(const PbrVariantPlan& plan);

}  // namespace eve::graphics::webgpu
