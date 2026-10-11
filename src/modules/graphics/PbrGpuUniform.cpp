#include "graphics/PbrGpuUniform.h"

#include <algorithm>

namespace eve::graphics::detail {

PbrUniformGpu buildPbrUniformGpu(const PbrUniformBuildInputs& inputs) {
    const auto& s = inputs.surface;
    PbrUniformGpu u;
    u.mvp = inputs.mvp;
    u.model = inputs.model;
    u.view = inputs.view;
    u.camera = glm::vec4(inputs.camera, s.anisotropyRotation);
    u.tint = inputs.tint;
    u.ambient = glm::vec4(glm::vec3(inputs.ambient), s.anisotropyStrength);
    u.material = {inputs.metallic, inputs.roughness, inputs.surfaceCode,
                  std::clamp(inputs.alphaCutoff + s.vegetationColor.globalAlphaThresholdOffset, 0.f, 1.f)};
    u.emissive = {s.emissive[0], s.emissive[1], s.emissive[2], s.emissiveStrength};
    u.specular = {s.specularColor[0], s.specularColor[1], s.specularColor[2], s.specularFactor};
    u.coat = {s.clearcoatFactor, s.clearcoatRoughness, s.clearcoatNormalScale, s.ior};
    u.colorMaskSecondary = {s.colorMaskSecondary[0], s.colorMaskSecondary[1], s.colorMaskSecondary[2],
                            float(s.normalMode)};
    u.colorMaskParams = {s.colorMaskEnabled ? 1.f : 0.f, s.colorMaskMin, s.colorMaskMax,
                         s.albedoTextureStrength};
    const auto& trans = s.translucency;
    u.translucencyColor = {trans.color[0], trans.color[1], trans.color[2], trans.intensity};
    u.translucencyParams = {trans.strength, trans.normalDistortion, trans.scattering, trans.direct};
    u.translucencyLighting = {trans.ambient, trans.shadow, trans.maskAmount,
                              trans.globalIntensity * trans.overlay};
    u.translucencyMask = {trans.maskMinimum, trans.maskMaximum, 0.f, 0.f};
    u.motionHighlightColor = {s.motionHighlightColor[0], s.motionHighlightColor[1], s.motionHighlightColor[2],
                              s.vegetationColor.overlaySubsurface};
    const auto& color = s.vegetationColor;
    u.vegetationOverlay = {color.overlayColor[0], color.overlayColor[1], color.overlayColor[2], color.overlay};
    u.vegetationWetness = {color.wetness, color.overlayNormalScale, color.wetnessNormalScale,
                           color.overlaySmoothness};
    u.vegetationStageParams = {color.overlayVariation, color.overlayProjection, color.vertexOcclusionAlpha,
                               color.wetnessContrast};
    u.vegetationFieldColor = {color.fieldColor[0], color.fieldColor[1], color.fieldColor[2], color.fieldColor[3]};
    u.vegetationColorParams = {color.colorsCoverage, color.colorsIntensity, color.colorsMask, color.colorsVariation};
    u.vegetationOcclusionParams = {color.vertexOcclusionMinimum, color.vertexOcclusionMaximum, 0.f, 0.f};
    u.vegetationOcclusionColor = {color.vertexOcclusionColor[0], color.vertexOcclusionColor[1],
                                  color.vertexOcclusionColor[2], float(color.backfaceNormalMode)};
    u.vegetationGlobalMasks = {color.globalColorMaskMinimum, color.globalColorMaskMaximum,
                               color.globalOverlayMaskMinimum, color.globalOverlayMaskMaximum};
    u.vegetationInfo.z = color.invertVertexOcclusion ? 1u : 0u;
    u.vegetationInfo.w = color.invertVertexOcclusionColors ? 1u : 0u;

    const auto& d = s.vegetationDetail;
    u.detailUv = {d.uvScale[0], d.uvScale[1], d.uvOffset[0], d.uvOffset[1]};
    u.detailColor = {d.color[0], d.color[1], d.color[2], d.normalBlendValue};
    u.detailColorTwo = {d.colorTwo[0], d.colorTwo[1], d.colorTwo[2], d.colorTwo[3]};
    u.detailValues = {d.value, d.normalValue, d.albedoValue, d.metallicValue};
    u.detailMaterial = {d.occlusionValue, d.smoothnessValue, d.blendMinimum, d.blendMaximum};
    u.detailMasks = {d.maskMinimum, d.maskMaximum, d.meshMinimum, d.meshMaximum};
    u.detailInfo = {d.uvMode, d.colorMode, d.blendMode, d.alphaMode};
    std::uint32_t detailPresent = 0;
    for (std::size_t index = 0; index < d.textures.size(); ++index)
        if (d.textures[index].texture) detailPresent |= 1u << index;
    u.detailInfo2 = {d.maskMode, d.meshMode, d.inverseUvScale ? 1u : 0u, detailPresent};

    const auto fillField = [](const auto& field, glm::vec4& coords,
                              glm::vec4& fallback, glm::vec4& usage0, glm::vec4& usage1,
                              glm::vec4& usage2) {
        coords = {field.coords[0], field.coords[1], field.coords[2], field.coords[3]};
        fallback = {field.fallback[0], field.fallback[1], field.fallback[2], field.fallback[3]};
        usage0 = {field.usage[0], field.usage[1], field.usage[2], field.usage[3]};
        usage1 = {field.usage[4], field.usage[5], field.usage[6], field.usage[7]};
        usage2 = {field.usage[8], 0.f, 0.f, 0.f};
    };
    const auto& extras = s.vegetationExtras;
    fillField(extras, u.extrasCoords, u.extrasFallback, u.extrasUsage0, u.extrasUsage1, u.extrasUsage2);
    u.extrasInfo = {extras.layer, extras.usePivotPosition ? 1u : 0u, extras.texture ? 1u : 0u,
                    s.vegetationAlpha.enabled ? 1u : 0u};
    u.extrasUsage2.y = s.vegetationAlpha.global;
    u.extrasUsage2.z = s.vegetationAlpha.variation;
    u.extrasUsage2.w = s.vegetationAlpha.detailFade ? 1.f : 0.f;
    u.vegetationAlphaFade = {s.vegetationAlpha.glancing, s.vegetationAlpha.camera,
                             s.vegetationAlpha.constant, s.vegetationAlpha.noiseTiling};
    u.vegetationAlphaCamera = {s.vegetationAlpha.cameraFadeMin, s.vegetationAlpha.cameraFadeMax, 0.f, 0.f};
    u.vegetationEmission = {s.vegetationEmission.minimum, s.vegetationEmission.maximum,
                            s.vegetationEmission.phase,
                            s.vegetationEmission.enabled ? s.vegetationEmission.global : -1.f};
    u.vegetationGradientOne = {s.vegetationGradient.colorOne[0], s.vegetationGradient.colorOne[1],
                               s.vegetationGradient.colorOne[2],
                               s.vegetationGradient.enabled ? s.vegetationGradient.minimum : -1.f};
    u.vegetationGradientTwo = {s.vegetationGradient.colorTwo[0], s.vegetationGradient.colorTwo[1],
                               s.vegetationGradient.colorTwo[2], s.vegetationGradient.maximum};

    const auto& colors = s.vegetationColors;
    fillField(colors, u.colorsCoords, u.colorsFallback, u.colorsUsage0, u.colorsUsage1, u.colorsUsage2);
    u.colorsInfo = {colors.layer, colors.usePivotPosition ? 1u : 0u, colors.texture ? 1u : 0u, 0u};
    const auto& vertex = s.vegetationVertex;
    fillField(vertex, u.vertexCoords, u.vertexFallback, u.vertexUsage0, u.vertexUsage1, u.vertexUsage2);
    u.vertexSize = {vertex.globalSize, vertex.sizeFadeStart, vertex.sizeFadeEnd, vertex.distanceFadeBias};
    u.vertexInfo = {vertex.layer, std::uint32_t(vertex.source), 0u, 0u};
    const auto& motion = s.vegetationMotion;
    fillField(motion, u.motionCoords, u.motionFallback, u.motionUsage0, u.motionUsage1, u.motionUsage2);
    u.motionGlobal0 = {motion.globalDirection[0], motion.globalDirection[1], motion.worldOrigin[0],
                       motion.worldOrigin[1]};
    u.motionGlobal1 = {motion.worldOrigin[2], motion.dynamicMode, motion.rigidity, motion.facing};
    u.motionTime = {float(motion.time), 0.f, 0.f, 0.f};
    u.motionBending = {motion.bending, motion.bendingSpeed, motion.bendingScale, motion.bendingVariation};
    u.motionBranch = {motion.branch, motion.rolling, motion.branchSpeed, motion.branchScale};
    u.motionBranch2 = {motion.branchVariation, motion.globalBending, motion.globalBranch, motion.globalFlutter};
    u.motionFlutter = {motion.flutter, motion.flutterSpeed, motion.flutterScale, motion.flutterVariation};
    u.motionControl = {motion.noiseTiling, motion.interaction, motion.interactionMask, motion.fadeDistance};
    u.motionPerspective = {motion.perspectivePush, motion.perspectiveNoise, motion.perspectiveAngle, 0.f};
    u.motionInfo = {motion.layer, std::uint32_t(motion.mode), 0u, 0u};

    u.misc = {s.normalScale, s.occlusionStrength, s.unlit ? 1.f : 0.f, float(inputs.skinCount)};
    for (int index = 0; index < std::min(inputs.lighting.count, 8); ++index)
        u.lights[index] = inputs.lighting.lights[index];
    u.lights[0].color.w = inputs.environmentIntensity;
    for (std::size_t index = 0; index < s.textures.size(); ++index) {
        const auto& binding = s.textures[index];
        u.uvTransform[index] = {binding.offset[0], binding.offset[1], binding.scale[0], binding.scale[1]};
        u.uvInfo[index] = {binding.rotation, UINT32_MAX, binding.texture ? 1.f : 0.f,
                           binding.srgbDecode ? 1.f : 0.f};
    }
    return u;
}

}  // namespace eve::graphics::detail
