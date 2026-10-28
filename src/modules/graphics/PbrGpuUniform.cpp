#include "graphics/PbrGpuUniform.h"
#include <algorithm>
#include <cmath>
#include "graphics/PbrSurface.h"

// PbrGpuUniform.cpp
namespace eve::graphics::detail {

PbrUniformGpu buildPbrUniformGpu(const PbrUniformBuildInputs& inputs) {
    const auto&   s = inputs.surface;
    PbrUniformGpu u;
    u.mvp                  = inputs.mvp;
    u.model                = inputs.model;
    u.view                 = inputs.view;
    u.camera               = glm::vec4(inputs.camera, s.anisotropyRotation);
    u.tint                 = inputs.tint;
    u.ambient              = glm::vec4(glm::vec3(inputs.ambient), s.anisotropyStrength);
    u.material             = {inputs.metallic, inputs.roughness, inputs.surfaceCode,
                              std::clamp(inputs.alphaCutoff + s.vegetationColor.globalAlphaThresholdOffset, 0.f, 1.f)};
    u.emissive             = {s.emissive[0], s.emissive[1], s.emissive[2], s.emissiveStrength};
    u.specular             = {s.specularColor[0], s.specularColor[1], s.specularColor[2], s.specularFactor};
    u.coat                 = {s.clearcoatFactor, s.clearcoatRoughness, s.clearcoatNormalScale, s.ior};
    u.colorMaskSecondary   = {s.colorMaskSecondary[0], s.colorMaskSecondary[1], s.colorMaskSecondary[2],
                              float(s.normalMode)};
    u.colorMaskParams      = {s.colorMaskEnabled ? 1.f : 0.f, s.colorMaskMin, s.colorMaskMax, s.albedoTextureStrength};
    const auto& trans      = s.translucency;
    u.translucencyColor    = {trans.color[0], trans.color[1], trans.color[2], trans.intensity};
    u.translucencyParams   = {trans.strength, trans.normalDistortion, trans.scattering, trans.direct};
    u.translucencyLighting = {trans.ambient, trans.shadow, trans.maskAmount, trans.globalIntensity * trans.overlay};
    u.translucencyMask     = {trans.maskMinimum, trans.maskMaximum, 0.f, 0.f};
    u.motionHighlightColor = {s.motionHighlightColor[0], s.motionHighlightColor[1], s.motionHighlightColor[2],
                              s.vegetationColor.overlaySubsurface};
    const auto& color      = s.vegetationColor;
    u.vegetationOverlay    = {color.overlayColor[0], color.overlayColor[1], color.overlayColor[2], color.overlay};
    u.vegetationWetness = {color.wetness, color.overlayNormalScale, color.wetnessNormalScale, color.overlaySmoothness};
    u.vegetationStageParams = {color.overlayVariation, color.overlayProjection, color.vertexOcclusionAlpha,
                               color.wetnessContrast};
    u.vegetationFieldColor      = {color.fieldColor[0], color.fieldColor[1], color.fieldColor[2], color.fieldColor[3]};
    u.vegetationColorParams = {color.colorsCoverage, color.colorsIntensity, color.colorsMask, color.colorsVariation};
    u.vegetationOcclusionParams = {color.vertexOcclusionMinimum, color.vertexOcclusionMaximum, 0.f, 0.f};
    u.vegetationOcclusionColor  = {color.vertexOcclusionColor[0], color.vertexOcclusionColor[1],
                                   color.vertexOcclusionColor[2], float(color.backfaceNormalMode)};
    u.vegetationGlobalMasks     = {color.globalColorMaskMinimum, color.globalColorMaskMaximum,
                                   color.globalOverlayMaskMinimum, color.globalOverlayMaskMaximum};
    u.vegetationInfo.z          = color.invertVertexOcclusion ? 1u : 0u;
    u.vegetationInfo.w          = color.invertVertexOcclusionColors ? 1u : 0u;

    const auto& d               = s.vegetationDetail;
    u.detailUv                  = {d.uvScale[0], d.uvScale[1], d.uvOffset[0], d.uvOffset[1]};
    u.detailColor               = {d.color[0], d.color[1], d.color[2], d.normalBlendValue};
    u.detailColorTwo            = {d.colorTwo[0], d.colorTwo[1], d.colorTwo[2], d.colorTwo[3]};
    u.detailValues              = {d.value, d.normalValue, d.albedoValue, d.metallicValue};
    u.detailMaterial            = {d.occlusionValue, d.smoothnessValue, d.blendMinimum, d.blendMaximum};
    u.detailMasks               = {d.maskMinimum, d.maskMaximum, d.meshMinimum, d.meshMaximum};
    u.detailInfo                = {d.uvMode, d.colorMode, d.blendMode, d.alphaMode};
    std::uint32_t detailPresent = 0;
    for (std::size_t index = 0; index < d.textures.size(); ++index)
        if (d.textures[index].texture) detailPresent |= 1u << index;
    u.detailInfo2 = {d.maskMode, d.meshMode, d.inverseUvScale ? 1u : 0u, detailPresent};

    const auto fillField = [](const auto& field, glm::vec4& coords, glm::vec4& fallback, glm::vec4& usage0,
                              glm::vec4& usage1, glm::vec4& usage2) {
        coords   = {field.coords[0], field.coords[1], field.coords[2], field.coords[3]};
        fallback = {field.fallback[0], field.fallback[1], field.fallback[2], field.fallback[3]};
        usage0   = {field.usage[0], field.usage[1], field.usage[2], field.usage[3]};
        usage1   = {field.usage[4], field.usage[5], field.usage[6], field.usage[7]};
        usage2   = {field.usage[8], 0.f, 0.f, 0.f};
    };
    const auto& extras = s.vegetationExtras;
    fillField(extras, u.extrasCoords, u.extrasFallback, u.extrasUsage0, u.extrasUsage1, u.extrasUsage2);
    u.extrasInfo            = {extras.layer, extras.usePivotPosition ? 1u : 0u, extras.texture ? 1u : 0u,
                    s.vegetationAlpha.enabled ? 1u : 0u};
    u.extrasUsage2.y        = s.vegetationAlpha.global;
    u.extrasUsage2.z        = s.vegetationAlpha.variation;
    u.extrasUsage2.w        = s.vegetationAlpha.detailFade ? 1.f : 0.f;
    u.vegetationAlphaFade   = {s.vegetationAlpha.glancing, s.vegetationAlpha.camera, s.vegetationAlpha.constant,
                               s.vegetationAlpha.noiseTiling};
    u.vegetationAlphaCamera = {s.vegetationAlpha.cameraFadeMin, s.vegetationAlpha.cameraFadeMax, 0.f, 0.f};
    u.vegetationEmission    = {s.vegetationEmission.minimum, s.vegetationEmission.maximum, s.vegetationEmission.phase,
                            s.vegetationEmission.enabled ? s.vegetationEmission.global : -1.f};
    u.vegetationGradientOne = {s.vegetationGradient.colorOne[0], s.vegetationGradient.colorOne[1],
                               s.vegetationGradient.colorOne[2],
                               s.vegetationGradient.enabled ? s.vegetationGradient.minimum : -1.f};
    u.vegetationGradientTwo = {s.vegetationGradient.colorTwo[0], s.vegetationGradient.colorTwo[1],
                               s.vegetationGradient.colorTwo[2], s.vegetationGradient.maximum};

    const auto& colors = s.vegetationColors;
    fillField(colors, u.colorsCoords, u.colorsFallback, u.colorsUsage0, u.colorsUsage1, u.colorsUsage2);
    u.colorsInfo       = {colors.layer, colors.usePivotPosition ? 1u : 0u, colors.texture ? 1u : 0u, 0u};
    const auto& vertex = s.vegetationVertex;
    fillField(vertex, u.vertexCoords, u.vertexFallback, u.vertexUsage0, u.vertexUsage1, u.vertexUsage2);
    u.vertexSize       = {vertex.globalSize, vertex.sizeFadeStart, vertex.sizeFadeEnd, vertex.distanceFadeBias};
    u.vertexInfo       = {vertex.layer, std::uint32_t(vertex.source), 0u, 0u};
    const auto& motion = s.vegetationMotion;
    fillField(motion, u.motionCoords, u.motionFallback, u.motionUsage0, u.motionUsage1, u.motionUsage2);
    u.motionGlobal0     = {motion.globalDirection[0], motion.globalDirection[1], motion.worldOrigin[0],
                           motion.worldOrigin[1]};
    u.motionGlobal1     = {motion.worldOrigin[2], motion.dynamicMode, motion.rigidity, motion.facing};
    u.motionTime        = {float(motion.time), 0.f, 0.f, 0.f};
    u.motionBending     = {motion.bending, motion.bendingSpeed, motion.bendingScale, motion.bendingVariation};
    u.motionBranch      = {motion.branch, motion.rolling, motion.branchSpeed, motion.branchScale};
    u.motionBranch2     = {motion.branchVariation, motion.globalBending, motion.globalBranch, motion.globalFlutter};
    u.motionFlutter     = {motion.flutter, motion.flutterSpeed, motion.flutterScale, motion.flutterVariation};
    u.motionControl     = {motion.noiseTiling, motion.interaction, motion.interactionMask, motion.fadeDistance};
    u.motionPerspective = {motion.perspectivePush, motion.perspectiveNoise, motion.perspectiveAngle, 0.f};
    u.motionInfo        = {motion.layer, std::uint32_t(motion.mode), 0u, 0u};

    u.misc = {s.normalScale, s.occlusionStrength, s.unlit ? 1.f : 0.f, float(inputs.skinCount)};
    for (int index = 0; index < std::min(inputs.lighting.count, 8); ++index)
        u.lights[index] = inputs.lighting.lights[index];
    u.lights[0].color.w = inputs.environmentIntensity;
    for (std::size_t index = 0; index < s.textures.size(); ++index) {
        const auto& binding  = s.textures[index];
        u.uvTransform[index] = {binding.offset[0], binding.offset[1], binding.scale[0], binding.scale[1]};
        u.uvInfo[index] = {binding.rotation, UINT32_MAX, binding.texture ? 1.f : 0.f, binding.srgbDecode ? 1.f : 0.f};
    }
    return u;
}

}  // namespace eve::graphics::detail

// PbrSurface.cpp
namespace eve::graphics {
Result<void> validatePbrSurface(const PbrSurface& s) {
    auto fail = [] {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid PBR surface factors or sampler", {}, {}, "graphics.material"));
    };
    auto unit     = [](float x) { return std::isfinite(x) && x >= 0 && x <= 1; };
    auto positive = [](float x) { return std::isfinite(x) && x >= 0; };
    if (uint32_t(s.cullMode) > uint32_t(PbrCullMode::Front)) return fail();
    if (!unit(s.occlusionStrength) || !unit(s.specularFactor) || !unit(s.anisotropyStrength) ||
        !unit(s.clearcoatFactor) || !unit(s.clearcoatRoughness) || !positive(s.emissiveStrength) ||
        !std::isfinite(s.normalScale) || !std::isfinite(s.clearcoatNormalScale) ||
        !std::isfinite(s.anisotropyRotation) || !std::isfinite(s.ior) || (s.ior != 0 && s.ior < 1))
        return fail();
    for (float v : s.emissive)
        if (!positive(v)) return fail();
    for (float v : s.motionHighlightColor)
        if (!positive(v)) return fail();
    if (!unit(s.vegetationColor.overlay) || !unit(s.vegetationColor.wetness) ||
        !unit(s.vegetationColor.overlayVariation) || !unit(s.vegetationColor.overlayProjection) ||
        !unit(s.vegetationColor.vertexOcclusionAlpha) || !unit(s.vegetationColor.overlayNormalScale) ||
        !unit(s.vegetationColor.wetnessNormalScale) || !unit(s.vegetationColor.overlaySmoothness) ||
        !unit(s.vegetationColor.wetnessContrast) || !unit(s.vegetationColor.overlaySubsurface) ||
        !unit(s.vegetationColor.colorsCoverage) || !std::isfinite(s.vegetationColor.colorsIntensity) ||
        s.vegetationColor.colorsIntensity < 0 || s.vegetationColor.colorsIntensity > 2 ||
        !unit(s.vegetationColor.colorsMask) || !unit(s.vegetationColor.colorsVariation) ||
        !unit(s.vegetationColor.globalColorMaskMinimum) || !unit(s.vegetationColor.globalColorMaskMaximum) ||
        !unit(s.vegetationColor.globalOverlayMaskMinimum) || !unit(s.vegetationColor.globalOverlayMaskMaximum) ||
        !std::isfinite(s.vegetationColor.globalAlphaThresholdOffset) ||
        s.vegetationColor.globalAlphaThresholdOffset < -.5f || s.vegetationColor.globalAlphaThresholdOffset > .5f ||
        s.vegetationColor.globalColorMaskMaximum - s.vegetationColor.globalColorMaskMinimum + .0001f == 0.f ||
        s.vegetationColor.globalOverlayMaskMaximum - s.vegetationColor.globalOverlayMaskMinimum + .0001f == 0.f ||
        !unit(s.vegetationColor.vertexOcclusionMinimum) || !unit(s.vegetationColor.vertexOcclusionMaximum) ||
        s.vegetationColor.vertexOcclusionMaximum - s.vegetationColor.vertexOcclusionMinimum + .0001f == 0.f)
        return fail();
    if (uint32_t(s.vegetationColor.backfaceNormalMode) > uint32_t(PbrVegetationBackfaceNormalMode::Same)) return fail();
    for (std::size_t i = 0; i < s.vegetationColor.fieldColor.size(); ++i)
        if ((i == 3 ? !unit(s.vegetationColor.fieldColor[i]) : !positive(s.vegetationColor.fieldColor[i])))
            return fail();
    for (float v : s.vegetationColor.overlayColor)
        if (!positive(v)) return fail();
    for (float v : s.vegetationColor.vertexOcclusionColor)
        if (!positive(v)) return fail();
    const auto& detail = s.vegetationDetail;
    if (!unit(detail.value) || !unit(detail.normalBlendValue) || !std::isfinite(detail.normalValue) ||
        detail.normalValue < -8 || detail.normalValue > 8 || !unit(detail.albedoValue) || !unit(detail.metallicValue) ||
        !unit(detail.occlusionValue) || !unit(detail.smoothnessValue) || !unit(detail.blendMinimum) ||
        !unit(detail.blendMaximum) || !unit(detail.maskMinimum) || !unit(detail.maskMaximum) ||
        !unit(detail.meshMinimum) || !unit(detail.meshMaximum) ||
        detail.blendMaximum - detail.blendMinimum + .0001f == 0 ||
        detail.maskMaximum - detail.maskMinimum + .0001f == 0 ||
        detail.meshMaximum - detail.meshMinimum + .0001f == 0 || detail.uvMode > 2 || detail.colorMode > 1 ||
        detail.blendMode > 1 || detail.alphaMode > 1 || detail.maskMode > 1 || detail.meshMode > 1)
        return fail();
    for (size_t i = 0; i < detail.color.size(); ++i)
        if (!positive(detail.color[i]) || (i == 3 && detail.color[i] > 1)) return fail();
    for (size_t i = 0; i < detail.colorTwo.size(); ++i)
        if (!positive(detail.colorTwo[i]) || (i == 3 && detail.colorTwo[i] > 1)) return fail();
    for (float value : detail.uvScale)
        if (!std::isfinite(value) || value == 0) return fail();
    for (float value : detail.uvOffset)
        if (!std::isfinite(value)) return fail();
    for (float v : s.specularColor)
        if (!positive(v)) return fail();
    if (!unit(s.colorMaskMin) || !unit(s.colorMaskMax) || s.colorMaskMax - s.colorMaskMin + .0001f == 0.f ||
        !unit(s.albedoTextureStrength))
        return fail();
    for (float v : s.colorMaskSecondary)
        if (!positive(v)) return fail();
    const auto& mask = s.textures[std::size_t(PbrTextureSlot::MetallicRoughness)];
    if (s.colorMaskEnabled && (!mask.texture || mask.srgbDecode)) return fail();
    const auto& trans = s.translucency;
    if (!unit(trans.maskMinimum) || !unit(trans.maskMaximum) || trans.maskMaximum - trans.maskMinimum + .0001f == 0.f)
        return fail();
    if (!unit(trans.intensity) || !unit(trans.normalDistortion) || !unit(trans.direct) || !unit(trans.ambient) ||
        !unit(trans.shadow) || !unit(trans.maskAmount) || !positive(trans.strength) || trans.strength > 50 ||
        !std::isfinite(trans.scattering) || trans.scattering < 1 || trans.scattering > 50 ||
        !positive(trans.globalIntensity) || !positive(trans.overlay) ||
        !std::isfinite(trans.globalIntensity * trans.overlay))
        return fail();
    for (float v : trans.color)
        if (!positive(v)) return fail();
    if (trans.intensity > 0 && trans.maskAmount > 0 && (!mask.texture || mask.srgbDecode)) return fail();
    if (uint32_t(s.normalMode) > uint32_t(PbrNormalMode::VegetationAG)) return fail();
    if (s.normalMode != PbrNormalMode::TangentXYZ) {
        const auto& normal = s.textures[std::size_t(PbrTextureSlot::Normal)];
        if (!normal.texture || normal.srgbDecode || s.normalScale < -8.f || s.normalScale > 8.f) return fail();
    }
    auto validateBinding = [&](const auto& b) {
        for (float v : b.offset)
            if (!std::isfinite(v)) return false;
        for (float v : b.scale)
            if (!std::isfinite(v)) return false;
        auto wrap = [](uint32_t v) { return v == 10497 || v == 33071 || v == 33648; };
        if (!std::isfinite(b.rotation) || !wrap(b.wrapS) || !wrap(b.wrapT) ||
            (b.magFilter != 9728 && b.magFilter != 9729) ||
            (b.minFilter != 9728 && b.minFilter != 9729 && (b.minFilter < 9984 || b.minFilter > 9987)))
            return false;
        return true;
    };
    for (const auto& b : s.textures)
        if (!validateBinding(b)) return fail();
    for (const auto& b : detail.textures)
        if (!validateBinding(b)) return fail();
    if (detail.value > 0 && (!detail.textures[0].texture || !detail.textures[1].texture)) return fail();
    const auto& extras = s.vegetationExtras;
    if (extras.layer > 8) return fail();
    for (float value : extras.usage)
        if (!unit(value)) return fail();
    for (float value : extras.fallback)
        if (!std::isfinite(value)) return fail();
    for (float value : extras.coords)
        if (!std::isfinite(value)) return fail();
    const auto& alpha = s.vegetationAlpha;
    if (!unit(alpha.global) || !unit(alpha.variation) || !unit(alpha.glancing) || !unit(alpha.camera) ||
        !unit(alpha.constant) || !std::isfinite(alpha.cameraFadeMin) || alpha.cameraFadeMin < 0 ||
        alpha.cameraFadeMin > 1000000 || !std::isfinite(alpha.cameraFadeMax) || alpha.cameraFadeMax < 0 ||
        alpha.cameraFadeMax > 1000000 || !std::isfinite(alpha.noiseTiling) || alpha.noiseTiling < 0 ||
        alpha.noiseTiling > 1000000)
        return fail();
    const auto& emission = s.vegetationEmission;
    if (!unit(emission.minimum) || !unit(emission.maximum) || emission.maximum - emission.minimum + .0001f == 0 ||
        !unit(emission.phase) || !unit(emission.global))
        return fail();
    const auto& gradient = s.vegetationGradient;
    if (!unit(gradient.minimum) || !unit(gradient.maximum) || gradient.maximum - gradient.minimum + .0001f == 0)
        return fail();
    for (float value : gradient.colorOne)
        if (!positive(value)) return fail();
    for (float value : gradient.colorTwo)
        if (!positive(value)) return fail();
    const auto& colors = s.vegetationColors;
    if (colors.layer > 8) return fail();
    for (float value : colors.usage)
        if (!unit(value)) return fail();
    for (size_t i = 0; i < colors.fallback.size(); ++i)
        if (!std::isfinite(colors.fallback[i]) || (i == 3 ? !unit(colors.fallback[i]) : colors.fallback[i] < 0))
            return fail();
    for (float value : colors.coords)
        if (!std::isfinite(value)) return fail();
    const auto& vertex = s.vegetationVertex;
    if (vertex.layer > 8 || uint32_t(vertex.source) > uint32_t(PbrVegetationDeformationSource::GpuFields))
        return fail();
    for (float value : vertex.usage)
        if (!unit(value)) return fail();
    for (float value : vertex.fallback)
        if (!std::isfinite(value)) return fail();
    for (float value : vertex.coords)
        if (!std::isfinite(value)) return fail();
    if (!unit(vertex.globalSize) || !std::isfinite(vertex.sizeFadeStart) || vertex.sizeFadeStart < 0 ||
        !std::isfinite(vertex.sizeFadeEnd) || vertex.sizeFadeEnd < vertex.sizeFadeStart ||
        !std::isfinite(vertex.distanceFadeBias) || vertex.distanceFadeBias < .0001f)
        return fail();
    const auto& motion = s.vegetationMotion;
    if (motion.layer > 8 || uint32_t(motion.mode) > uint32_t(PbrVegetationMotionMode::Object)) return fail();
    for (float value : motion.usage)
        if (!unit(value)) return fail();
    for (float value : motion.fallback)
        if (!std::isfinite(value)) return fail();
    for (float value : motion.coords)
        if (!std::isfinite(value)) return fail();
    for (float value : motion.globalDirection)
        if (!std::isfinite(value)) return fail();
    for (float value : motion.worldOrigin)
        if (!std::isfinite(value)) return fail();
    if (!std::isfinite(motion.time) || std::abs(motion.time) > 1e12 || !unit(motion.dynamicMode) ||
        !unit(motion.rigidity) || !unit(motion.facing) || !unit(motion.interactionMask))
        return fail();
    for (float value : {motion.bending,          motion.bendingSpeed,    motion.bendingScale, motion.bendingVariation,
                        motion.branch,           motion.rolling,         motion.branchSpeed,  motion.branchScale,
                        motion.branchVariation,  motion.flutter,         motion.flutterSpeed, motion.flutterScale,
                        motion.flutterVariation, motion.globalBending,   motion.globalBranch, motion.globalFlutter,
                        motion.noiseTiling,      motion.interaction,     motion.fadeDistance, motion.perspectivePush,
                        motion.perspectiveNoise, motion.perspectiveAngle})
        if (!std::isfinite(value) || value < 0 || value > 1000000) return fail();
    if (motion.mode == PbrVegetationMotionMode::Object && (!motion.texture || !motion.noise)) return fail();
    if (motion.mode != PbrVegetationMotionMode::Disabled && vertex.source != PbrVegetationDeformationSource::GpuFields)
        return fail();
    if (vertex.source == PbrVegetationDeformationSource::GpuFields && !vertex.texture &&
        motion.mode == PbrVegetationMotionMode::Disabled)
        return fail();
    return Result<void>::success();
}
}  // namespace eve::graphics
