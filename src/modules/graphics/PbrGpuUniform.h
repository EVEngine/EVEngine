#pragma once

#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "graphics/Light.h"
#include "graphics/PbrSurface.h"

namespace eve::graphics::detail {

/** @brief std140 texture-coordinate metadata shared by Vulkan and WebGPU PBR shaders. */
struct alignas(16) PbrUvInfoGpu {
    float         rotation = 0.f;
    std::uint32_t offset   = UINT32_MAX;
    float         present  = 0.f;
    float         srgb     = 0.f;
};

/** @brief Canonical std140 payload consumed by both generated PBR shader stages. */
struct alignas(16) PbrUniformGpu {
    glm::mat4  mvp{1.f}, model{1.f}, view{1.f};
    glm::vec4  camera{}, tint{}, ambient{}, material{};
    glm::vec4  emissive{}, specular{}, coat{}, misc{};
    Light3DGpu lights[8]{};
    glm::vec4  uvTransform[11]{};
    PbrUvInfoGpu uvInfo[11]{};
    glm::vec4  colorMaskSecondary{}, colorMaskParams{};
    glm::uvec4 tangentInfo{};
    glm::vec4  translucencyColor{}, translucencyParams{}, translucencyLighting{}, translucencyMask{};
    glm::vec4  motionHighlightColor{};
    glm::vec4  vegetationOverlay{}, vegetationWetness{};
    glm::vec4  vegetationStageParams{};
    glm::vec4  vegetationFieldColor{}, vegetationColorParams{}, vegetationOcclusionParams{};
    glm::vec4  vegetationOcclusionColor{};
    glm::vec4  vegetationGlobalMasks{};
    glm::uvec4 vegetationInfo{};
    glm::vec4  detailUv{}, detailColor{}, detailColorTwo{}, detailValues{};
    glm::vec4  detailMaterial{}, detailMasks{};
    glm::uvec4 detailInfo{}, detailInfo2{};
    glm::vec4  extrasCoords{}, extrasFallback{}, extrasUsage0{}, extrasUsage1{}, extrasUsage2{};
    glm::uvec4 extrasInfo{};
    glm::vec4  vegetationAlphaFade{}, vegetationAlphaCamera{}, vegetationEmission{};
    glm::vec4  vegetationGradientOne{}, vegetationGradientTwo{};
    glm::vec4  colorsCoords{}, colorsFallback{}, colorsUsage0{}, colorsUsage1{}, colorsUsage2{};
    glm::uvec4 colorsInfo{};
    glm::vec4  vertexCoords{}, vertexFallback{}, vertexUsage0{}, vertexUsage1{}, vertexUsage2{};
    glm::vec4  vertexSize{};
    glm::uvec4 vertexInfo{};
    glm::vec4  motionCoords{}, motionFallback{}, motionUsage0{}, motionUsage1{}, motionUsage2{};
    glm::vec4  motionGlobal0{}, motionGlobal1{}, motionTime{}, motionBending{}, motionBranch{}, motionBranch2{};
    glm::vec4  motionFlutter{}, motionControl{}, motionPerspective{};
    glm::uvec4 motionInfo{};
};

static_assert(sizeof(PbrUvInfoGpu) == 16);
static_assert(sizeof(PbrUniformGpu) == 1952);

/** @brief Backend-neutral state needed to build one immutable PBR uniform snapshot. */
struct PbrUniformBuildInputs {
    const PbrSurface& surface;
    glm::mat4         mvp{1.f};
    glm::mat4         model{1.f};
    glm::mat4         view{1.f};
    glm::vec3         camera{};
    glm::vec4         tint{1.f};
    glm::vec4         ambient{};
    const Lighting3DPack& lighting;
    float metallic = 0.f, roughness = 1.f, surfaceCode = 0.f, alphaCutoff = .5f;
    float environmentIntensity = 0.f;
    std::uint32_t skinCount = 0;
};

/** @brief Build all material and scene fields; mesh stream offsets remain disabled for the backend to fill. */
[[nodiscard]] PbrUniformGpu buildPbrUniformGpu(const PbrUniformBuildInputs& inputs);

}  // namespace eve::graphics::detail
