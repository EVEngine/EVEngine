#include "graphics/material/editing/MaterialTarget.h"

// Optional graphics adapter for the renderer-neutral material authoring contract.

#include "graphics/RenderSystem3D.h"

#include "ECS.hpp"

namespace eve::material_editing {
namespace {

template <class T>
EditorResult<T> runtimeError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue::Object* properties(const MaterialDocumentTarget& target,
                                      EditorValue& snapshot) {
    snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    if (!root) return nullptr;
    const auto found = root->find("properties");
    return found == root->end() ? nullptr : found->second.getIf<EditorValue::Object>();
}

template <class T>
const T* value(const EditorValue::Object& properties, const char* path) {
    const auto found = properties.find(path);
    return found == properties.end() ? nullptr : found->second.getIf<T>();
}

template <class T, class Resolver>
EditorResult<T*> resolveAsset(const std::string& asset, Resolver&& resolver) {
    if (asset.empty()) return eve::editing::applied<T*>(nullptr);
    return resolver(asset);
}

}  // namespace

struct Renderable3DMaterialRuntimeSink::Impl {
    ecs::EntityHandle handle;
    const IMaterialRuntimeAssetResolver* assets = nullptr;
};

Renderable3DMaterialRuntimeSink::Renderable3DMaterialRuntimeSink(
    graphics::Renderable3D* renderable, const IMaterialRuntimeAssetResolver* assets)
    : impl_(std::make_unique<Impl>()) {
    impl_->handle = ecs::handle_of(renderable);
    impl_->assets = assets;
}

double number(const EditorValue& value) {
    if (const auto* real = value.getIf<double>()) return *real;
    if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
    return 0.0;
}

Renderable3DMaterialRuntimeSink::~Renderable3DMaterialRuntimeSink() = default;

EditorResult<void> Renderable3DMaterialRuntimeSink::publish(
    const MaterialDocumentTarget& candidate) {
    if (!impl_->assets)
        return runtimeError<void>(EditorStatus::Rejected, "editor.material.runtime-input",
                                  "Material asset resolver is required");
    auto* renderable = dynamic_cast<graphics::Renderable3D*>(ecs::try_get(impl_->handle));
    if (!renderable)
        return runtimeError<void>(EditorStatus::Conflict, "editor.material.runtime-stale",
                                  "Renderable3D handle is missing or stale");
    const auto diagnostics = candidate.validate();
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    }
    EditorValue snapshot;
    const auto* values = properties(candidate, snapshot);
    if (!values)
        return runtimeError<void>(EditorStatus::Failed, "editor.material.runtime-properties",
                                  "Material properties are unavailable");
    const bool vegetationAlpha = *value<bool>(*values, "vegetation.alpha.enabled");
    const bool vegetationEmission = *value<bool>(*values, "vegetation.emission.enabled");
    const bool vegetationGradient = *value<bool>(*values, "vegetation.gradient.enabled");
    const bool vegetationTranslucency = *value<bool>(*values, "vegetation.translucency.enabled");
    const bool vegetationColor = *value<bool>(*values, "vegetation.color.enabled");
    const bool vegetationDetail = *value<bool>(*values, "vegetation.detail.enabled");
    const bool vegetationExtras = *value<bool>(*values, "vegetation.extras.enabled");
    const bool vegetationColors = *value<bool>(*values, "vegetation.colors.enabled");
    const bool vegetationVertex = *value<bool>(*values, "vegetation.vertex.enabled");
    const bool vegetationMotion = *value<std::string>(*values, "vegetation.motion.mode") == "object";
    const bool extendedTextures = !value<std::string>(*values, "textures.orm")->empty() ||
                                  !value<std::string>(*values, "textures.emissive")->empty();
    const bool vegetationExtended =
        vegetationAlpha || vegetationEmission || vegetationGradient || vegetationTranslucency || vegetationColor ||
        vegetationDetail || vegetationExtras || vegetationColors || vegetationVertex || vegetationMotion ||
        extendedTextures;
    const auto& surfaceMode    = *value<std::string>(*values, "surface.mode");
    if (*value<std::string>(*values, "shading.model") != "pbr" ||
        (surfaceMode != "opaque" && !(vegetationAlpha && surfaceMode == "masked")) ||
        *value<std::string>(*values, "surface.blend") != "alpha" ||
        *value<bool>(*values, "surface.double-sided"))
        return runtimeError<void>(EditorStatus::Unsupported,
                                  "editor.material.runtime-legacy-surface",
                                  "Legacy Renderable3D supports PBR opaque single-sided materials only");

    std::array<graphics::Texture*, 14> textures{};
    constexpr const char* texturePaths[] = {
        "textures.albedo",          "textures.normal",          "textures.height",
        "textures.orm",             "textures.emissive",        "vegetation.alpha.noise",
        "vegetation.detail.albedo", "vegetation.detail.normal", "vegetation.detail.mask",
        "vegetation.extras.texture", "vegetation.colors.texture", "vegetation.vertex.texture",
        "vegetation.motion.texture", "vegetation.motion.noise"};
    const std::array<bool, 14> textureEnabled = {true,
                                                  true,
                                                  true,
                                                  true,
                                                  true,
                                                  vegetationAlpha,
                                                  vegetationDetail,
                                                  vegetationDetail,
                                                  vegetationDetail,
                                                  vegetationExtras,
                                                  vegetationColors,
                                                  vegetationVertex,
                                                  vegetationMotion,
                                                  vegetationMotion};
    for (std::size_t i = 0; i < textures.size(); ++i) {
        if (!textureEnabled[i]) continue;
        auto resolved = resolveAsset<graphics::Texture>(
            *value<std::string>(*values, texturePaths[i]),
            [&](const std::string& asset) { return impl_->assets->resolveTexture(asset); });
        if (!resolved.ok()) return EditorResult<void>::failure(resolved.status());
        textures[i] = resolved.value();
    }
    auto shader = resolveAsset<graphics::Shader>(
        *value<std::string>(*values, "textures.shader"),
        [&](const std::string& asset) { return impl_->assets->resolveShader(asset); });
    if (!shader.ok()) return EditorResult<void>::failure(shader.status());

    graphics::Material* runtimeMaterial = renderable->getMaterial();
    if (vegetationExtended && !runtimeMaterial)
        return runtimeError<void>(EditorStatus::Unsupported, "editor.material.runtime-vegetation-material",
                                  "Extended PBR vegetation editing requires a Material bound to Renderable3D");
    if (runtimeMaterial && (vegetationExtended || runtimeMaterial->hasPbrSurface())) {
        graphics::PbrSurface surface             = runtimeMaterial->pbrSurface();
        surface.vegetationAlpha.enabled          = vegetationAlpha;
        surface.vegetationAlpha.noise            = textures[5];
        surface.vegetationAlpha.global           = float(*value<double>(*values, "vegetation.alpha.global"));
        surface.vegetationAlpha.variation        = float(*value<double>(*values, "vegetation.alpha.variation"));
        surface.vegetationAlpha.glancing         = float(*value<double>(*values, "vegetation.alpha.glancing"));
        surface.vegetationAlpha.camera           = float(*value<double>(*values, "vegetation.alpha.camera"));
        surface.vegetationAlpha.constant         = float(*value<double>(*values, "vegetation.alpha.constant"));
        surface.vegetationAlpha.cameraFadeMin    = float(*value<double>(*values, "vegetation.alpha.camera-min"));
        surface.vegetationAlpha.cameraFadeMax    = float(*value<double>(*values, "vegetation.alpha.camera-max"));
        surface.vegetationAlpha.noiseTiling      = float(*value<double>(*values, "vegetation.alpha.noise-tiling"));
        surface.vegetationAlpha.detailFade       = *value<bool>(*values, "vegetation.alpha.detail-fade");
        surface.vegetationEmission.enabled       = vegetationEmission;
        surface.vegetationEmission.minimum = float(*value<double>(*values, "vegetation.emission.minimum"));
        surface.vegetationEmission.maximum = float(*value<double>(*values, "vegetation.emission.maximum"));
        surface.vegetationEmission.phase   = float(*value<double>(*values, "vegetation.emission.phase"));
        surface.vegetationEmission.global  = float(*value<double>(*values, "vegetation.emission.global"));
        surface.vegetationGradient.enabled = vegetationGradient;
        const auto& gradientOne = *value<EditorValue::Array>(*values, "vegetation.gradient.color-one");
        const auto& gradientTwo = *value<EditorValue::Array>(*values, "vegetation.gradient.color-two");
        for (std::size_t i = 0; i < 3; ++i) {
            surface.vegetationGradient.colorOne[i] = float(number(gradientOne[i]));
            surface.vegetationGradient.colorTwo[i] = float(number(gradientTwo[i]));
        }
        surface.vegetationGradient.minimum = float(*value<double>(*values, "vegetation.gradient.minimum"));
        surface.vegetationGradient.maximum = float(*value<double>(*values, "vegetation.gradient.maximum"));
        auto& ormBinding = surface.textures[std::size_t(graphics::PbrTextureSlot::MetallicRoughness)];
        ormBinding.texture    = textures[3];
        ormBinding.srgbDecode = false;
        auto& emissiveBinding = surface.textures[std::size_t(graphics::PbrTextureSlot::Emissive)];
        emissiveBinding.texture    = textures[4];
        emissiveBinding.srgbDecode = true;
        surface.translucency.intensity =
            vegetationTranslucency ? float(*value<double>(*values, "vegetation.translucency.intensity")) : 0.f;
        const auto& transColor = *value<EditorValue::Array>(*values, "vegetation.translucency.color");
        for (std::size_t i = 0; i < 3; ++i) surface.translucency.color[i] = float(number(transColor[i]));
        surface.translucency.strength = float(*value<double>(*values, "vegetation.translucency.strength"));
        surface.translucency.normalDistortion =
            float(*value<double>(*values, "vegetation.translucency.normal-distortion"));
        surface.translucency.scattering = float(*value<double>(*values, "vegetation.translucency.scattering"));
        surface.translucency.direct     = float(*value<double>(*values, "vegetation.translucency.direct"));
        surface.translucency.ambient    = float(*value<double>(*values, "vegetation.translucency.ambient"));
        surface.translucency.shadow     = float(*value<double>(*values, "vegetation.translucency.shadow"));
        surface.translucency.maskAmount =
            vegetationTranslucency ? float(*value<double>(*values, "vegetation.translucency.mask-amount")) : 0.f;
        surface.translucency.globalIntensity = float(*value<double>(*values, "vegetation.translucency.global"));
        surface.translucency.overlay = float(*value<double>(*values, "vegetation.translucency.overlay"));
        surface.translucency.maskMinimum =
            float(*value<double>(*values, "vegetation.translucency.mask-minimum"));
        surface.translucency.maskMaximum =
            float(*value<double>(*values, "vegetation.translucency.mask-maximum"));
        surface.vegetationColor = {};
        if (vegetationColor) {
            const auto& field = *value<EditorValue::Array>(*values, "vegetation.color.field");
            const auto& overlay = *value<EditorValue::Array>(*values, "vegetation.color.overlay-color");
            const auto& occlusion =
                *value<EditorValue::Array>(*values, "vegetation.color.vertex-occlusion-color");
            for (std::size_t i = 0; i < 4; ++i) surface.vegetationColor.fieldColor[i] = float(number(field[i]));
            for (std::size_t i = 0; i < 3; ++i) {
                surface.vegetationColor.overlayColor[i] = float(number(overlay[i]));
                surface.vegetationColor.vertexOcclusionColor[i] = float(number(occlusion[i]));
            }
            const auto scalar = [&](const char* name) {
                return float(*value<double>(*values, (std::string("vegetation.color.") + name).c_str()));
            };
            surface.vegetationColor.overlay                  = scalar("overlay");
            surface.vegetationColor.wetness                  = scalar("wetness");
            surface.vegetationColor.overlayVariation         = scalar("overlay-variation");
            surface.vegetationColor.overlayProjection        = scalar("overlay-projection");
            surface.vegetationColor.vertexOcclusionAlpha     = scalar("vertex-occlusion-alpha");
            surface.vegetationColor.overlayNormalScale       = scalar("overlay-normal");
            surface.vegetationColor.wetnessNormalScale       = scalar("wetness-normal");
            surface.vegetationColor.overlaySmoothness        = scalar("overlay-smoothness");
            surface.vegetationColor.wetnessContrast          = scalar("wetness-contrast");
            surface.vegetationColor.overlaySubsurface        = scalar("overlay-subsurface");
            surface.vegetationColor.colorsCoverage           = scalar("colors-coverage");
            surface.vegetationColor.colorsIntensity          = scalar("colors-intensity");
            surface.vegetationColor.colorsMask               = scalar("colors-mask");
            surface.vegetationColor.colorsVariation          = scalar("colors-variation");
            surface.vegetationColor.globalColorMaskMinimum   = scalar("color-mask-minimum");
            surface.vegetationColor.globalColorMaskMaximum   = scalar("color-mask-maximum");
            surface.vegetationColor.globalOverlayMaskMinimum = scalar("overlay-mask-minimum");
            surface.vegetationColor.globalOverlayMaskMaximum = scalar("overlay-mask-maximum");
            surface.vegetationColor.globalAlphaThresholdOffset = scalar("alpha-threshold-offset");
            surface.vegetationColor.vertexOcclusionMinimum = scalar("vertex-occlusion-minimum");
            surface.vegetationColor.vertexOcclusionMaximum = scalar("vertex-occlusion-maximum");
            surface.vegetationColor.invertVertexOcclusion =
                *value<bool>(*values, "vegetation.color.invert-vertex-occlusion");
            surface.vegetationColor.invertVertexOcclusionColors =
                *value<bool>(*values, "vegetation.color.invert-vertex-occlusion-colors");
            const auto& backface = *value<std::string>(*values, "vegetation.color.backface-normal");
            surface.vegetationColor.backfaceNormalMode =
                backface == "same" ? graphics::PbrVegetationBackfaceNormalMode::Same
                : backface == "mirror" ? graphics::PbrVegetationBackfaceNormalMode::Mirror
                                         : graphics::PbrVegetationBackfaceNormalMode::Flip;
        }
        surface.vegetationDetail = {};
        if (vegetationDetail) {
            auto& detail = surface.vegetationDetail;
            detail.textures[0].texture = textures[6];
            detail.textures[0].srgbDecode = true;
            detail.textures[1].texture = textures[7];
            detail.textures[1].srgbDecode = false;
            detail.textures[2].texture = textures[8];
            detail.textures[2].srgbDecode = false;
            const auto& color = *value<EditorValue::Array>(*values, "vegetation.detail.color");
            const auto& colorTwo = *value<EditorValue::Array>(*values, "vegetation.detail.color-two");
            const auto& uvScale = *value<EditorValue::Array>(*values, "vegetation.detail.uv-scale");
            const auto& uvOffset = *value<EditorValue::Array>(*values, "vegetation.detail.uv-offset");
            for (std::size_t i = 0; i < 4; ++i) {
                detail.color[i] = float(number(color[i]));
                detail.colorTwo[i] = float(number(colorTwo[i]));
            }
            for (std::size_t i = 0; i < 2; ++i) {
                detail.uvScale[i] = float(number(uvScale[i]));
                detail.uvOffset[i] = float(number(uvOffset[i]));
            }
            const auto detailScalar = [&](const char* name) {
                return float(*value<double>(*values, (std::string("vegetation.detail.") + name).c_str()));
            };
            detail.value            = detailScalar("value");
            detail.normalValue      = detailScalar("normal-value");
            detail.normalBlendValue = detailScalar("normal-blend");
            detail.albedoValue      = detailScalar("albedo-value");
            detail.metallicValue    = detailScalar("metallic");
            detail.occlusionValue   = detailScalar("occlusion");
            detail.smoothnessValue  = detailScalar("smoothness");
            detail.blendMinimum     = detailScalar("blend-minimum");
            detail.blendMaximum     = detailScalar("blend-maximum");
            detail.maskMinimum      = detailScalar("mask-minimum");
            detail.maskMaximum      = detailScalar("mask-maximum");
            detail.meshMinimum      = detailScalar("mesh-minimum");
            detail.meshMaximum      = detailScalar("mesh-maximum");
            detail.inverseUvScale = *value<bool>(*values, "vegetation.detail.inverse-uv-scale");
            const auto& uvMode = *value<std::string>(*values, "vegetation.detail.uv-mode");
            detail.uvMode = uvMode == "world" ? 2u : uvMode == "detail" ? 1u : 0u;
            detail.colorMode = *value<std::string>(*values, "vegetation.detail.color-mode") == "variation";
            detail.blendMode = *value<std::string>(*values, "vegetation.detail.blend-mode") == "replace";
            detail.alphaMode = *value<std::string>(*values, "vegetation.detail.alpha-mode") == "detail";
            detail.maskMode = *value<std::string>(*values, "vegetation.detail.mask-mode") == "inverse";
            detail.meshMode = *value<std::string>(*values, "vegetation.detail.mesh-mode") == "inverse";
        }
        const auto publishField = [&](const char* group, auto& field, graphics::Texture* texture) {
            const std::string prefix = std::string("vegetation.") + group;
            field.texture            = texture;
            field.layer = uint32_t(*value<std::int64_t>(*values, (prefix + ".layer").c_str()));
            field.usePivotPosition = *value<bool>(*values, (prefix + ".use-pivot-position").c_str());
            const auto& fallback = *value<EditorValue::Array>(*values, (prefix + ".fallback").c_str());
            const auto& coords = *value<EditorValue::Array>(*values, (prefix + ".coords").c_str());
            for (std::size_t i = 0; i < 4; ++i) {
                field.fallback[i] = float(number(fallback[i]));
                field.coords[i]   = float(number(coords[i]));
            }
            for (std::size_t i = 0; i < field.usage.size(); ++i) {
                const std::string path = prefix + ".usage-" + std::to_string(i);
                field.usage[i]         = float(*value<double>(*values, path.c_str()));
            }
        };
        surface.vegetationExtras = {};
        if (vegetationExtras) publishField("extras", surface.vegetationExtras, textures[9]);
        surface.vegetationColors = {};
        if (vegetationColors) publishField("colors", surface.vegetationColors, textures[10]);
        surface.vegetationVertex = {};
        if (vegetationVertex) {
            auto& vertex   = surface.vegetationVertex;
            vertex.texture = textures[11];
            vertex.layer = uint32_t(*value<std::int64_t>(*values, "vegetation.vertex.layer"));
            const auto& fallback = *value<EditorValue::Array>(*values, "vegetation.vertex.fallback");
            const auto& coords = *value<EditorValue::Array>(*values, "vegetation.vertex.coords");
            for (std::size_t i = 0; i < 4; ++i) {
                vertex.fallback[i] = float(number(fallback[i]));
                vertex.coords[i]   = float(number(coords[i]));
            }
            for (std::size_t i = 0; i < vertex.usage.size(); ++i) {
                const std::string path = "vegetation.vertex.usage-" + std::to_string(i);
                vertex.usage[i]        = float(*value<double>(*values, path.c_str()));
            }
            vertex.globalSize = float(*value<double>(*values, "vegetation.vertex.global-size"));
            vertex.sizeFadeStart = float(*value<double>(*values, "vegetation.vertex.size-fade-start"));
            vertex.sizeFadeEnd = float(*value<double>(*values, "vegetation.vertex.size-fade-end"));
            vertex.distanceFadeBias = float(*value<double>(*values, "vegetation.vertex.distance-fade-bias"));
            const auto& source = *value<std::string>(*values, "vegetation.vertex.source");
            vertex.source = source == "gpu-fields"
                                ? graphics::PbrVegetationDeformationSource::GpuFields
                                : source == "cpu-deformed"
                                      ? graphics::PbrVegetationDeformationSource::CpuDeformed
                                      : graphics::PbrVegetationDeformationSource::RestMesh;
        }
        surface.vegetationMotion = {};
        if (vegetationMotion) {
            auto& motion   = surface.vegetationMotion;
            motion.texture = textures[12];
            motion.noise   = textures[13];
            motion.mode    = graphics::PbrVegetationMotionMode::Object;
            motion.layer = uint32_t(*value<std::int64_t>(*values, "vegetation.motion.layer"));
            const auto& fallback = *value<EditorValue::Array>(*values, "vegetation.motion.fallback");
            const auto& coords = *value<EditorValue::Array>(*values, "vegetation.motion.coords");
            const auto& direction = *value<EditorValue::Array>(*values, "vegetation.motion.global-direction");
            const auto& origin = *value<EditorValue::Array>(*values, "vegetation.motion.world-origin");
            for (std::size_t i = 0; i < 4; ++i) {
                motion.fallback[i] = float(number(fallback[i]));
                motion.coords[i]   = float(number(coords[i]));
            }
            for (std::size_t i = 0; i < 2; ++i) motion.globalDirection[i] = float(number(direction[i]));
            for (std::size_t i = 0; i < 3; ++i) motion.worldOrigin[i] = float(number(origin[i]));
            for (std::size_t i = 0; i < motion.usage.size(); ++i) {
                const std::string path = "vegetation.motion.usage-" + std::to_string(i);
                motion.usage[i]        = float(*value<double>(*values, path.c_str()));
            }
            const auto scalar = [&](const char* name) {
                const std::string path = std::string("vegetation.motion.") + name;
                return float(*value<double>(*values, path.c_str()));
            };
            motion.time             = *value<double>(*values, "vegetation.motion.time");
            motion.dynamicMode      = scalar("dynamic-mode");
            motion.rigidity         = scalar("rigidity");
            motion.facing           = scalar("facing");
            motion.bending          = scalar("bending");
            motion.bendingSpeed     = scalar("bending-speed");
            motion.bendingScale     = scalar("bending-scale");
            motion.bendingVariation = scalar("bending-variation");
            motion.branch           = scalar("branch");
            motion.rolling          = scalar("rolling");
            motion.branchSpeed      = scalar("branch-speed");
            motion.branchScale      = scalar("branch-scale");
            motion.branchVariation  = scalar("branch-variation");
            motion.flutter          = scalar("flutter");
            motion.flutterSpeed     = scalar("flutter-speed");
            motion.flutterScale     = scalar("flutter-scale");
            motion.flutterVariation = scalar("flutter-variation");
            motion.globalBending    = scalar("global-bending");
            motion.globalBranch     = scalar("global-branch");
            motion.globalFlutter    = scalar("global-flutter");
            motion.noiseTiling      = scalar("noise-tiling");
            motion.interaction      = scalar("interaction");
            motion.interactionMask  = scalar("interaction-mask");
            motion.fadeDistance     = scalar("fade-distance");
            motion.perspectivePush  = scalar("perspective-push");
            motion.perspectiveNoise = scalar("perspective-noise");
            motion.perspectiveAngle = scalar("perspective-angle");
        }
        auto published = runtimeMaterial->setPbrSurface(surface);
        if (!published)
            return runtimeError<void>(EditorStatus::Rejected, "editor.material.runtime-vegetation-invalid",
                                      published.error()->message());
    }

    const auto& tint = *value<EditorValue::Array>(*values, "shading.tint");
    if (runtimeMaterial) {
        runtimeMaterial->setAlbedoTexture(textures[0]);
        runtimeMaterial->setNormalTexture(textures[1]);
        runtimeMaterial->setHeightTexture(textures[2]);
        runtimeMaterial->setShader(shader.value());
        runtimeMaterial->setTint(static_cast<float>(number(tint[0])), static_cast<float>(number(tint[1])),
                                 static_cast<float>(number(tint[2])), static_cast<float>(number(tint[3])));
        runtimeMaterial->setMetallic(static_cast<float>(*value<double>(*values, "shading.metallic")));
        runtimeMaterial->setRoughness(static_cast<float>(*value<double>(*values, "shading.roughness")));
        runtimeMaterial->setParallax(static_cast<float>(*value<double>(*values, "parallax.scale")));
        runtimeMaterial->setReceiveLight(*value<bool>(*values, "lighting.receive"));
        runtimeMaterial->setCastShadow(*value<bool>(*values, "shadow.cast"));
        runtimeMaterial->setReceiveShadow(*value<bool>(*values, "shadow.receive"));
        runtimeMaterial->setSurfaceMode(surfaceMode);
        runtimeMaterial->setAlphaCutoff(static_cast<float>(*value<double>(*values, "surface.alpha-cutoff")));
        runtimeMaterial->setDepthWrite(*value<bool>(*values, "surface.depth-write"));
        runtimeMaterial->setDoubleSided(*value<bool>(*values, "surface.double-sided"));
        return eve::editing::applied<void>();
    }
    renderable->setTexture(textures[0]);
    renderable->setNormalTexture(textures[1]);
    renderable->setHeightTexture(textures[2]);
    renderable->setShader(shader.value());
    renderable->setTint(static_cast<float>(number(tint[0])),
                        static_cast<float>(number(tint[1])),
                        static_cast<float>(number(tint[2])),
                        static_cast<float>(number(tint[3])));
    renderable->setMetallic(static_cast<float>(*value<double>(*values, "shading.metallic")));
    renderable->setRoughness(static_cast<float>(*value<double>(*values, "shading.roughness")));
    renderable->setParallax(static_cast<float>(*value<double>(*values, "parallax.scale")));
    renderable->setReceiveLight(*value<bool>(*values, "lighting.receive"));
    renderable->setCastShadow(*value<bool>(*values, "shadow.cast"));
    renderable->setReceiveShadow(*value<bool>(*values, "shadow.receive"));
    return eve::editing::applied<void>();
}

}  // namespace eve::material_editing
