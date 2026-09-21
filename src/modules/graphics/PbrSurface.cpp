#include "graphics/PbrSurface.h"
#include <cmath>
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
