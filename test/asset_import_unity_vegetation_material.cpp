#include <cmath>
#include "asset/AssetCooker.h"
#include "asset/CanonicalImageCook.h"
#include "asset/SourcePng.h"
#include "asset/graphics/CookedMaterial.h"
#include "asset/import/UnityImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset_import;
namespace {
UnityProjectImportRequest fixture() {
    UnityProjectImportRequest r;
    r.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "vegetation.material", "1.0.0", {}};
    auto put  = [&](const std::string& path, const std::string& text) { r.files[path] = {text.begin(), text.end()}; };
    put("Assets/leaf.mat.meta", "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    put("Assets/mask.png.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
    const std::array<std::uint8_t, 8> pixels{200, 128, 64, 55, 20, 0, 255, 255};
    auto                              png = asset::detail::encodeSourcePng(2, 1, pixels, 4096);
    REQUIRE(png.ok());
    r.files["Assets/mask.png"] = std::move(png).takeValue();
    put("Assets/leaf.mat", R"(--- !u!21 &2100000
Material:
  m_Shader: {fileID: 4800000, guid: 7befaa6f41d00a6478d5f4af21d66518, type: 3}
  m_SavedProperties:
    m_TexEnvs:
    - _MainMaskTex:
        m_Texture: {fileID: 2800000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    - _EmissiveTex:
        m_Texture: {fileID: 2800000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    m_Floats:
    - _MainOcclusionValue: 0.5
    - _MainSmoothnessValue: 0.5
    - _MainColorMode: 1
    - _MainAlbedoValue: 0.4
    - _MainMaskMinValue: 0.2
    - _MainMaskMaxValue: 0.8
    - _RenderSpecular: 0
    - _RenderNormals: 2
    - _RenderCull: 2
    - _RenderCoverage: 1
    - _GlobalOverlay: 0.3
    - _GlobalWetness: 0.8
    - _OverlayVariationValue: 0.1
    - _OverlayProjectionValue: 0.6
    - _VertexOcclusionOverlayMode: 1
    - _GlobalColors: 0.7
    - _ColorsIntensityValue: 1.4
    - _ColorsMaskValue: 0.25
    - _ColorsVariationValue: 0.2
    - _VertexOcclusionColorsMode: 1
    - _VertexOcclusionMinValue: 0.15
    - _VertexOcclusionMaxValue: 0.85
    - _GlobalAlpha: 0.65
    - _AlphaVariationValue: 0.35
    - _DetailFadeMode: 1
    - _FadeGlancingValue: 0.2
    - _FadeCameraValue: 0.4
    - _FadeConstantValue: 0.6
    - _LayerExtrasValue: 3
    - _ExtrasPositionMode: 1
    - _LayerColorsValue: 4
    - _ColorsPositionMode: 1
    - _LayerMotionValue: 5
    - _LayerVertexValue: 6
    - _GlobalSize: 0.75
    - _SizeFadeStartValue: 10
    - _SizeFadeEndValue: 80
    - _VertexDynamicMode: 1
    - _MotionPosition_10: 0.45
    - _MotionFacingValue: 0.55
    - _MotionAmplitude_10: 0.31
    - _MotionSpeed_10: 2.1
    - _MotionScale_10: 1.1
    - _MotionVariation_10: 0.11
    - _MotionAmplitude_20: 0.32
    - _MotionAmplitude_22: 0.12
    - _MotionSpeed_20: 6.1
    - _MotionScale_20: 3.1
    - _MotionVariation_20: 0.21
    - _MotionAmplitude_32: 0.33
    - _MotionSpeed_32: 20.1
    - _MotionScale_32: 10.1
    - _MotionVariation_32: 0.31
    - _InteractionAmplitude: 0.81
    - _InteractionMaskValue: 0.71
    - _PerspectivePushValue: 0.41
    - _PerspectiveNoiseValue: 0.51
    - _PerspectiveAngleValue: 0.61
    - _EmissiveMode: 1
    - _EmissiveIntensityMode: 0
    - _EmissiveIntensityValue: 3
    - _emissive_intensity_value: 2.5
    - _EmissivePhaseValue: 0.8
    - _EmissiveTexMinValue: 0.1
    - _EmissiveTexMaxValue: 0.9
    - _GlobalEmissive: 0.7
    - _GradientMinValue: 0.2
    - _GradientMaxValue: 0.8
    m_Colors:
    - _MainColor: {r: 2, g: 0.5, b: 0.25, a: 1}
    - _MainColorTwo: {r: 0.1, g: 0.2, b: 0.3, a: 1}
    - _MainUVs: {r: 2, g: 3, b: 0.25, a: 0.1}
    - _VertexOcclusionColor: {r: 0.1, g: 0.2, b: 0.3, a: 0.34117648}
    - _EmissiveColor: {r: 4, g: 2, b: 1, a: 1}
    - _EmissiveUVs: {r: 3, g: 2, b: 0.2, a: 0.1}
    - _GradientColorOne: {r: 2, g: 3, b: 4, a: 1}
    - _GradientColorTwo: {r: 0.1, g: 0.2, b: 0.3, a: 1}
)");
    return r;
}
}  // namespace

TEST_CASE("asset.import.unityCollectionRetainsOtherAssetsWhenMaterialDependencyIsAbsent") {
    auto request = fixture();
    auto& materialBytes = request.files["Assets/leaf.mat"];
    std::string material(materialBytes.begin(), materialBytes.end());
    const std::string presentGuid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const std::string absentGuid  = "cccccccccccccccccccccccccccccccc";
    for (std::size_t offset = material.find(presentGuid); offset != std::string::npos;
         offset = material.find(presentGuid, offset + absentGuid.size()))
        material.replace(offset, presentGuid.size(), absentGuid);
    materialBytes.assign(material.begin(), material.end());

    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    CHECK(!imported.value().manifest.entrypoints.contains("Assets/leaf.mat"));
    bool dependencyFinding = false;
    for (const auto& finding : imported.value().findings)
        if (finding.sourcePath == "Assets/leaf.mat" && finding.feature == "resource.dependency" &&
            finding.disposition == ImportDisposition::Unsupported &&
            finding.message.find("TVE texture source is absent") != std::string::npos)
            dependencyFinding = true;
    CHECK(dependencyFinding);
}

TEST_CASE("asset.import.unityVegetationMaterialConvertsMaskAndPreservesSource") {
    auto       request  = fixture();
    const auto original = request.files;
    auto       imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto materialRef = imported.value().manifest.entrypoints.at("Assets/leaf.mat");
    Value      material;
    for (const auto& a : imported.value().manifest.assets)
        if (a.asset == materialRef) {
            REQUIRE_EQ(a.schemaVersion, SchemaVersion(15));
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto value = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(value.ok());
                    material = std::move(value).takeValue();
                }
        }
    REQUIRE(material.isObject());
    const auto& object = *material.getIf<Value::Object>();
    REQUIRE_EQ(object.at("albedoTextureStrength").asDouble(), .4);
    REQUIRE_EQ(object.at("baseColor").getIf<Value::Array>()->at(0).asDouble(), 2.0);
    REQUIRE_EQ(object.at("baseColor").getIf<Value::Array>()->at(1).asDouble(), .5);
    REQUIRE_EQ(object.at("specularFactor").asDouble(), 0.0);
    REQUIRE_EQ(object.at("cullMode").asString(), std::string("front"));
    REQUIRE(!object.at("doubleSided").asBool());
    REQUIRE(object.at("alphaToCoverage").asBool());
    const auto& vegetation = *object.at("vegetationSurface").getIf<Value::Object>();
    REQUIRE_EQ(vegetation.at("overlay").asDouble(), .3);
    REQUIRE_EQ(vegetation.at("wetness").asDouble(), .8);
    REQUIRE_EQ(vegetation.at("overlayVariation").asDouble(), .1);
    REQUIRE_EQ(vegetation.at("overlayProjection").asDouble(), .6);
    REQUIRE_EQ(vegetation.at("vertexOcclusionAlpha").asDouble(), .34117648);
    REQUIRE_EQ(vegetation.at("vertexOcclusionColor").getIf<Value::Array>()->at(1).asDouble(), .2);
    REQUIRE(vegetation.at("invertVertexOcclusion").asBool());
    REQUIRE_EQ(vegetation.at("colors").asDouble(), .7);
    REQUIRE_EQ(vegetation.at("colorsIntensity").asDouble(), 1.4);
    REQUIRE_EQ(vegetation.at("colorsMask").asDouble(), .25);
    REQUIRE_EQ(vegetation.at("colorsVariation").asDouble(), .2);
    REQUIRE_EQ(vegetation.at("vertexOcclusionMinimum").asDouble(), .15);
    REQUIRE_EQ(vegetation.at("vertexOcclusionMaximum").asDouble(), .85);
    REQUIRE(vegetation.at("invertVertexOcclusionColors").asBool());
    REQUIRE_EQ(vegetation.at("backfaceNormalMode").asInt(), int64_t(2));
    const auto& alpha = *object.at("vegetationAlpha").getIf<Value::Object>();
    REQUIRE_EQ(alpha.at("global").asDouble(), .65);
    REQUIRE_EQ(alpha.at("variation").asDouble(), .35);
    REQUIRE(alpha.at("detailFade").asBool());
    REQUIRE_EQ(alpha.at("glancing").asDouble(), .2);
    REQUIRE_EQ(alpha.at("camera").asDouble(), .4);
    REQUIRE_EQ(alpha.at("constant").asDouble(), .6);
    REQUIRE_EQ(object.at("emissive").getIf<Value::Array>()->at(0).asDouble(), 4.0);
    REQUIRE_EQ(object.at("emissiveStrength").asDouble(), 2.5);
    const auto& emission = *object.at("vegetationEmission").getIf<Value::Object>();
    REQUIRE_EQ(emission.at("minimum").asDouble(), .1);
    REQUIRE_EQ(emission.at("maximum").asDouble(), .9);
    REQUIRE_EQ(emission.at("phase").asDouble(), .8);
    REQUIRE_EQ(emission.at("global").asDouble(), .7);
    const auto& gradient = *object.at("vegetationGradient").getIf<Value::Object>();
    REQUIRE_EQ(gradient.size(), size_t(4));
    REQUIRE_EQ(gradient.at("colorOne").getIf<Value::Array>()->at(2).asDouble(), 4.0);
    REQUIRE_EQ(gradient.at("colorTwo").getIf<Value::Array>()->at(0).asDouble(), .1);
    REQUIRE_EQ(gradient.at("minimum").asDouble(), .2);
    REQUIRE_EQ(gradient.at("maximum").asDouble(), .8);
    const auto& emissionTransform = *object.at("emissiveTextureTransform").getIf<Value::Object>();
    REQUIRE_EQ(emissionTransform.at("scale").getIf<Value::Array>()->at(0).asDouble(), 3.0);
    REQUIRE_EQ(emissionTransform.at("offset").getIf<Value::Array>()->at(1).asDouble(), -1.1);
    const auto& fields = *object.at("vegetationFields").getIf<Value::Object>();
    REQUIRE_EQ(fields.at("extrasLayer").asInt(), int64_t(3));
    REQUIRE(fields.at("extrasUsePivotPosition").asBool());
    REQUIRE_EQ(fields.at("colorsLayer").asInt(), int64_t(4));
    REQUIRE(fields.at("colorsUsePivotPosition").asBool());
    REQUIRE_EQ(fields.at("motionLayer").asInt(), int64_t(5));
    REQUIRE_EQ(fields.at("vertexLayer").asInt(), int64_t(6));
    REQUIRE_EQ(fields.at("globalSize").asDouble(), .75);
    REQUIRE_EQ(fields.at("sizeFadeStart").asDouble(), 10.0);
    REQUIRE_EQ(fields.at("sizeFadeEnd").asDouble(), 80.0);
    const auto& motion = *object.at("vegetationMotion").getIf<Value::Object>();
    REQUIRE_EQ(motion.size(), size_t(21));
    REQUIRE_EQ(motion.at("dynamicMode").asDouble(), 1.0);
    REQUIRE_EQ(motion.at("rigidity").asDouble(), .45);
    REQUIRE_EQ(motion.at("facing").asDouble(), .55);
    REQUIRE_EQ(motion.at("bending").asDouble(), .31);
    REQUIRE_EQ(motion.at("branch").asDouble(), .32);
    REQUIRE_EQ(motion.at("flutter").asDouble(), .33);
    REQUIRE_EQ(motion.at("interactionMask").asDouble(), .71);
    REQUIRE_EQ(motion.at("perspectiveAngle").asDouble(), .61);
    const auto& transform = *object.at("metallicRoughnessTextureTransform").getIf<Value::Object>();
    REQUIRE_EQ(transform.at("offset").getIf<Value::Array>()->at(1).asDouble(), -2.1);
    auto maskRef = AssetRef::parse(object.at("metallicRoughnessTexture").asString());
    REQUIRE(maskRef.ok());
    bool checked = false;
    for (const auto& a : imported.value().manifest.assets)
        if (a.asset == maskRef.value()) {
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto value = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(value.ok());
                    const auto blob = value.value().getIf<Value::Object>()->at("blob").asString();
                    for (const auto& b : imported.value().entries)
                        if (b.path == blob) {
                            auto cooked = asset::cookCanonicalImageRgba8(e.bytes, b.bytes, 4096);
                            REQUIRE(cooked.ok());
                            REQUIRE_EQ((std::vector<std::uint8_t>(cooked.value().bulk.begin() + 28,
                                                                  cooked.value().bulk.end())),
                                       (std::vector<std::uint8_t>{192, 228, 0, 64, 128, 128, 0, 255}));
                            checked = true;
                        }
                }
        }
    REQUIRE(checked);
    REQUIRE_EQ(request.files, original);
    auto encoded = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(encoded.ok());
    auto archive = asset::parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto                        loaded = asset_graphics::detail::readCookedMaterial(
        reader, materialRef, {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loaded.ok());
    REQUIRE(loaded.value().surface.colorMaskEnabled);
    REQUIRE_EQ(loaded.value().surface.albedoTextureStrength, .4f);
    REQUIRE_EQ(loaded.value().surface.specularFactor, 0.f);
    REQUIRE_EQ(loaded.value().surface.cullMode, graphics::PbrCullMode::Front);
    REQUIRE(loaded.value().surface.alphaToCoverage);
    REQUIRE(loaded.value().vegetationSurface.has_value());
    REQUIRE_EQ(loaded.value().vegetationSurface->overlayCoverage, .3f);
    REQUIRE_EQ(loaded.value().vegetationSurface->wetnessCoverage, .8f);
    REQUIRE_EQ(loaded.value().vegetationSurface->overlayVariation, .1f);
    REQUIRE_EQ(loaded.value().vegetationSurface->overlayProjection, .6f);
    REQUIRE_EQ(loaded.value().vegetationSurface->vertexOcclusionAlpha, .34117648f);
    REQUIRE_EQ(loaded.value().vegetationSurface->vertexOcclusionColor[2], .3f);
    REQUIRE(loaded.value().vegetationSurface->invertVertexOcclusion);
    REQUIRE_EQ(loaded.value().vegetationSurface->colorsCoverage, .7f);
    REQUIRE_EQ(loaded.value().vegetationSurface->colorsIntensity, 1.4f);
    REQUIRE_EQ(loaded.value().vegetationSurface->colorsMask, .25f);
    REQUIRE_EQ(loaded.value().vegetationSurface->colorsVariation, .2f);
    REQUIRE_EQ(loaded.value().vegetationSurface->vertexOcclusionMinimum, .15f);
    REQUIRE_EQ(loaded.value().vegetationSurface->vertexOcclusionMaximum, .85f);
    REQUIRE(loaded.value().vegetationSurface->invertVertexOcclusionColors);
    REQUIRE_EQ(loaded.value().vegetationSurface->backfaceNormalMode, graphics::PbrVegetationBackfaceNormalMode::Same);
    REQUIRE_EQ(loaded.value().vegetationSurface->albedo[0], 2.f);
    REQUIRE_EQ(loaded.value().vegetationSurface->roughness, 1.f);
    REQUIRE_EQ(loaded.value().vegetationSurface->alphaCutoff, .5f);
    REQUIRE(loaded.value().surface.vegetationAlpha.enabled);
    REQUIRE_EQ(loaded.value().surface.vegetationAlpha.global, .65f);
    REQUIRE_EQ(loaded.value().surface.vegetationAlpha.variation, .35f);
    REQUIRE(loaded.value().surface.vegetationAlpha.detailFade);
    REQUIRE_EQ(loaded.value().surface.vegetationAlpha.glancing, .2f);
    REQUIRE_EQ(loaded.value().surface.vegetationAlpha.camera, .4f);
    REQUIRE_EQ(loaded.value().surface.vegetationAlpha.constant, .6f);
    REQUIRE(loaded.value().surface.vegetationEmission.enabled);
    REQUIRE_EQ(loaded.value().surface.vegetationEmission.minimum, .1f);
    REQUIRE_EQ(loaded.value().surface.vegetationEmission.maximum, .9f);
    REQUIRE_EQ(loaded.value().surface.vegetationEmission.phase, .8f);
    REQUIRE_EQ(loaded.value().surface.vegetationEmission.global, .7f);
    REQUIRE_EQ(loaded.value().surface.emissive[0], 4.f);
    REQUIRE_EQ(loaded.value().surface.emissiveStrength, 2.5f);
    REQUIRE(loaded.value().images[4].has_value());
    REQUIRE(loaded.value().surface.vegetationGradient.enabled);
    REQUIRE_EQ(loaded.value().surface.vegetationGradient.colorOne[2], 4.f);
    REQUIRE_EQ(loaded.value().surface.vegetationGradient.colorTwo[0], .1f);
    REQUIRE_EQ(loaded.value().surface.vegetationGradient.minimum, .2f);
    REQUIRE_EQ(loaded.value().surface.vegetationGradient.maximum, .8f);
    REQUIRE_EQ(loaded.value().surface.vegetationExtras.layer, 3u);
    REQUIRE(loaded.value().surface.vegetationExtras.usePivotPosition);
    REQUIRE_EQ(loaded.value().surface.vegetationColors.layer, 4u);
    REQUIRE(loaded.value().surface.vegetationColors.usePivotPosition);
    REQUIRE_EQ(loaded.value().surface.vegetationVertex.layer, 6u);
    REQUIRE_EQ(loaded.value().surface.vegetationVertex.globalSize, .75f);
    REQUIRE_EQ(loaded.value().surface.vegetationVertex.sizeFadeStart, 10.f);
    REQUIRE_EQ(loaded.value().surface.vegetationVertex.sizeFadeEnd, 80.f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.layer, 5u);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.mode, graphics::PbrVegetationMotionMode::Disabled);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.dynamicMode, 1.f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.rigidity, .45f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.facing, .55f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.bending, .31f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.bendingSpeed, 2.1f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.branch, .32f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.rolling, .12f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.flutter, .33f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.interaction, .81f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.interactionMask, .71f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.perspectivePush, .41f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.perspectiveNoise, .51f);
    REQUIRE_EQ(loaded.value().surface.vegetationMotion.perspectiveAngle, .61f);
    bool renderState = false;
    for (const auto& f : imported.value().findings)
        if (f.feature == "Material.TVE.renderState" && f.disposition == ImportDisposition::Translated)
            renderState = true;
    REQUIRE(renderState);
}
TEST_CASE("asset.import.unityVegetationDetailSurvivesCookWithThreeNamedImages") {
    auto                                                     request = fixture();
    const std::array<std::pair<const char*, const char*>, 3> sources{
        {{"detail-albedo.png", "cccccccccccccccccccccccccccccccc"},
         {"detail-normal.png", "dddddddddddddddddddddddddddddddd"},
         {"detail-mask.png", "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"}}};
    const std::array<uint8_t, 4> pixel{128, 64, 32, 255};
    auto                         png = asset::detail::encodeSourcePng(1, 1, pixel, 4096);
    REQUIRE(png.ok());
    for (const auto& [name, guid] : sources) {
        request.files["Assets/" + std::string(name)] = png.value();
        const std::string meta = "guid: " + std::string(guid) + "\ntextureType: 0\nenableMipMap: 0\n";
        request.files["Assets/" + std::string(name) + ".meta"] = {meta.begin(), meta.end()};
    }
    auto&       bytes = request.files["Assets/leaf.mat"];
    std::string material(bytes.begin(), bytes.end());
    const auto  floats = material.find("    m_Floats:");
    REQUIRE(floats != std::string::npos);
    material.insert(floats, R"(    - _SecondAlbedoTex:
        m_Texture: {fileID: 2800000, guid: cccccccccccccccccccccccccccccccc, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    - _SecondNormalTex:
        m_Texture: {fileID: 2800000, guid: dddddddddddddddddddddddddddddddd, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    - _SecondMaskTex:
        m_Texture: {fileID: 2800000, guid: eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
)");
    material.insert(material.find("    m_Colors:"), R"(    - _DetailMode: 1
    - _DetailValue: 0.75
    - _SecondUVsMode: 1
    - _SecondNormalValue: 2
    - _DetailBlendMode: 1
    - _DetailMaskMode: 1
    - _DetailBlendMinValue: 0.15
    - _DetailBlendMaxValue: 0.2
)");
    material.append(R"(    - _SecondUVs: {r: 2, g: 1.5, b: 0.1, a: 0.2}
    - _SecondColor: {r: 0.8, g: 0.7, b: 0.6, a: 1}
    - _SecondColorTwo: {r: 1, g: 1, b: 1, a: 1}
)");
    bytes.assign(material.begin(), material.end());
    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    auto eva = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(eva.ok());
    auto archive = asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto                        loaded = asset_graphics::detail::readCookedMaterial(
        reader, imported.value().manifest.entrypoints.at("Assets/leaf.mat"),
        {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().surface.vegetationDetail.value, .75f);
    REQUIRE_EQ(loaded.value().surface.vegetationDetail.uvMode, 1u);
    REQUIRE_EQ(loaded.value().surface.vegetationDetail.normalValue, 2.f);
    REQUIRE_EQ(loaded.value().surface.vegetationDetail.blendMinimum, .15f);
    REQUIRE_EQ(loaded.value().surface.vegetationDetail.blendMaximum, .2f);
    REQUIRE(loaded.value().detailImages[0].has_value());
    REQUIRE(loaded.value().detailImages[1].has_value());
    REQUIRE(loaded.value().detailImages[2].has_value());
}
TEST_CASE("asset.import.unityVegetationMaterialRejectsMissingMaskAndPngBudget") {
    auto request = fixture();
    request.files.erase("Assets/mask.png");
    request.files.erase("Assets/mask.png.meta");
    REQUIRE(!prepareUnityProjectImport(request).ok());
    request                     = fixture();
    const std::string duplicate = "    - _MainColorMode: 0\n";
    request.files["Assets/leaf.mat"].insert(request.files["Assets/leaf.mat"].end(), duplicate.begin(), duplicate.end());
    REQUIRE(!prepareUnityProjectImport(request).ok());
    const std::array<std::uint8_t, 4> pixel{1, 2, 3, 4};
    REQUIRE(!asset::detail::encodeSourcePng(UINT32_MAX, UINT32_MAX, pixel, 4096).ok());
    REQUIRE(!asset::detail::encodeSourcePng(1, 1, pixel, 8).ok());
    for (const auto& [property, invalid] : std::array<std::pair<const char*, const char*>, 6>{
             {{"_VertexDynamicMode: 1", "_VertexDynamicMode: 2"},
              {"_MotionAmplitude_10: 0.31", "_MotionAmplitude_10: -0.01"},
              {"_RenderSpecular: 0", "_RenderSpecular: 2"},
              {"_RenderNormals: 2", "_RenderNormals: 3"},
              {"_RenderCull: 2", "_RenderCull: 3"},
              {"_RenderCoverage: 1", "_RenderCoverage: 2"}}}) {
        request           = fixture();
        auto&       bytes = request.files["Assets/leaf.mat"];
        std::string text(bytes.begin(), bytes.end());
        const auto  at = text.find(property);
        REQUIRE(at != std::string::npos);
        text.replace(at, std::char_traits<char>::length(property), invalid);
        bytes.assign(text.begin(), text.end());
        REQUIRE(!prepareUnityProjectImport(request).ok());
    }
}

TEST_CASE("asset.import.unityVegetationEmissionRejectsMissingTextureAndInvalidControls") {
    auto replace = [&](UnityProjectImportRequest& request, std::string_view from, std::string_view to) {
        auto&       bytes = request.files["Assets/leaf.mat"];
        std::string text(bytes.begin(), bytes.end());
        const auto  at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        bytes.assign(text.begin(), text.end());
    };

    auto request = fixture();
    replace(request,
            "    - _EmissiveTex:\n        m_Texture: {fileID: 2800000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, "
            "type: 3}\n        m_Scale: {x: 1, y: 1}\n        m_Offset: {x: 0, y: 0}\n",
            "");
    REQUIRE(!prepareUnityProjectImport(request).ok());

    for (const auto& [valid, invalid] : std::array<std::pair<std::string_view, std::string_view>, 6>{
             {{"_EmissiveMode: 1", "_EmissiveMode: 2"},
              {"_EmissiveIntensityMode: 0", "_EmissiveIntensityMode: 2"},
              {"_emissive_intensity_value: 2.5", "_emissive_intensity_value: -0.01"},
              {"_EmissivePhaseValue: 0.8", "_EmissivePhaseValue: 1.01"},
              {"_EmissiveTexMaxValue: 0.9", "_EmissiveTexMaxValue: 1.01"},
              {"_EmissiveUVs: {r: 3, g: 2", "_EmissiveUVs: {r: 0, g: 2"}}}) {
        request = fixture();
        replace(request, valid, invalid);
        REQUIRE(!prepareUnityProjectImport(request).ok());
    }
}

TEST_CASE("asset.import.unityVegetationGradientRejectsInvalidHdrAndEndpoints") {
    auto replace = [&](UnityProjectImportRequest& request, std::string_view from, std::string_view to) {
        auto&       bytes = request.files["Assets/leaf.mat"];
        std::string text(bytes.begin(), bytes.end());
        const auto  at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        bytes.assign(text.begin(), text.end());
    };

    for (const auto& [valid, invalid] : std::array<std::pair<std::string_view, std::string_view>, 5>{
             {{"_GradientColorOne: {r: 2", "_GradientColorOne: {r: -0.01"},
              {"_GradientColorTwo: {r: 0.1, g: 0.2", "_GradientColorTwo: {r: 0.1, g: -0.01"},
              {"_GradientMinValue: 0.2", "_GradientMinValue: -0.01"},
              {"_GradientMaxValue: 0.8", "_GradientMaxValue: 1.01"},
              {"_VertexOcclusionColor: {r: 0.1", "_VertexOcclusionColor: {r: -0.01"}}}) {
        auto request = fixture();
        replace(request, valid, invalid);
        REQUIRE(!prepareUnityProjectImport(request).ok());
    }
}

TEST_CASE("asset.import.unityVegetationSubsurfaceReachesRuntimeMaterial") {
    auto        request = fixture();
    auto&       bytes   = request.files["Assets/leaf.mat"];
    std::string material(bytes.begin(), bytes.end());
    const auto  shader = material.find("7befaa6f41d00a6478d5f4af21d66518");
    REQUIRE(shader != std::string::npos);
    material.replace(shader, 32, "a933075b367f9b24981408633f72ff34");
    material.insert(material.find("    m_Colors:"),
                    "    - _SubsurfaceValue: 0.4\n    - _SubsurfaceScatteringValue: 3\n"
                    "    - _SubsurfaceAngleValue: 6\n    - _SubsurfaceMaskValue: 0.5\n");
    material += "    - _MotionHighlightColor: {r: 2.1185474, g: 0.5, b: 0, a: 1}\n";
    bytes.assign(material.begin(), material.end());
    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    auto ref = imported.value().manifest.entrypoints.at("Assets/leaf.mat");
    auto eva = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(eva.ok());
    auto archive = asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto                        loaded = asset_graphics::detail::readCookedMaterial(
        reader, ref, {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loaded.ok());
    const auto& trans = loaded.value().surface.translucency;
    REQUIRE_EQ(trans.intensity, .4f);
    REQUIRE_EQ(trans.strength, 3.f);
    REQUIRE_EQ(trans.scattering, 6.f);
    REQUIRE_EQ(trans.maskAmount, .5f);
    REQUIRE_EQ(trans.direct, 1.f);
    REQUIRE_EQ(trans.normalDistortion, 0.f);
    REQUIRE_EQ(trans.ambient, .2f);
    REQUIRE_EQ(trans.shadow, 1.f);
    REQUIRE_EQ(loaded.value().surface.motionHighlightColor[0], 2.1185474f);
    REQUIRE_EQ(loaded.value().surface.motionHighlightColor[1], .5f);
    REQUIRE_EQ(loaded.value().surface.motionHighlightColor[2], 0.f);
    REQUIRE(loaded.value().images[1].has_value());
}

TEST_CASE("asset.import.unityVegetationAuthoredNormalOddMipsMatchUnity") {
    auto        request       = fixture();
    auto&       materialBytes = request.files["Assets/leaf.mat"];
    std::string material(materialBytes.begin(), materialBytes.end());
    material.insert(material.find("    m_Floats:"),
                    "    - _MainNormalTex:\n        m_Texture: {fileID: 2800000, guid: "
                    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}\n");
    materialBytes.assign(material.begin(), material.end());
    const std::string meta =
        "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nTextureImporter:\n"
        "  textureType: 1\n  convertToNormalMap: 0\n  flipGreenChannel: 1\n"
        "  enableMipMap: 1\n  mipMapMode: 0\n  nPOTScale: 0\n";
    request.files["Assets/mask.png.meta"] = {meta.begin(), meta.end()};
    std::vector<uint8_t> pixels;
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 7; ++x)
            pixels.insert(pixels.end(), {uint8_t((37 * x + 71 * y) % 256), uint8_t((91 * x + 13 * y) % 256),
                                         uint8_t((17 * x + 43 * y) % 256), uint8_t((29 * x + 31 * y) % 256)});
    auto png = asset::detail::encodeSourcePng(7, 3, pixels, 4096);
    REQUIRE(png.ok());
    request.files["Assets/mask.png"] = std::move(png).takeValue();
    auto imported                    = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    // Unity RGBAFloat exports, canonical top-down RG, green flip enabled.
    const std::array<uint8_t, 50> reference{0,   255, 37,  164, 74,  73,  111, 238, 148, 147, 185, 56,  222,
                                            221, 71,  242, 108, 151, 145, 60,  182, 225, 219, 134, 0,   43,
                                            37,  208, 142, 229, 179, 138, 216, 47,  253, 212, 34,  121, 71,
                                            30,  108, 195, 96,  181, 182, 225, 12,  98,  182, 225};
    unsigned                      checked = 0;
    for (const auto& entry : imported.value().entries) {
        if (!entry.path.ends_with("normal.rgba8-mips")) continue;
        REQUIRE_EQ(entry.bytes.size(), size_t(100));
        for (size_t i = 0; i < 25; ++i) {
            for (size_t c = 0; c < 2; ++c)
                REQUIRE(std::abs(int(entry.bytes[i * 4 + c]) - int(reference[i * 2 + c])) <= 1);
            REQUIRE_EQ(entry.bytes[i * 4 + 2], uint8_t(255));
            REQUIRE_EQ(entry.bytes[i * 4 + 3], uint8_t(255));
        }
        ++checked;
    }
    REQUIRE_EQ(checked, 1u);
}

TEST_CASE("asset.import.unityVegetationNormalPreservesLinearSourceAndGeneratesHeight") {
    for (int mode : {0, 1, 2, 3, 4, 5, 6}) {
        const bool  height  = mode != 0 && mode != 6;
        auto        request = fixture();
        auto&       bytes   = request.files["Assets/leaf.mat"];
        std::string material(bytes.begin(), bytes.end());
        material.insert(material.find("    m_Floats:"),
                        "    - _MainNormalTex:\n        m_Texture: {fileID: 2800000, guid: "
                        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}\n");
        material.insert(material.find("    m_Floats:"),
                        "    - _MainAlbedoTex:\n        m_Texture: {fileID: 2800000, guid: "
                        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}\n");
        material.insert(material.find("    m_Colors:"), "    - _MainNormalValue: -2\n");
        bytes.assign(material.begin(), material.end());
        std::string meta =
            std::string(
                "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nTextureImporter:\n  textureType: 1\n  convertToNormalMap: ") +
            (height ? "1\n  wrapU: -1\n  wrapV: -1\n" : "0\n");
        if (mode == 2) meta += "  normalMapFilter: 1\n  filterMode: 2\n";
        if (mode == 4) meta += "  filterMode: 1\n";
        if (mode == 5 || mode == 6) meta += "  flipGreenChannel: 1\n";
        if (mode == 3) {
            meta += "  filterMode: 0\n  enableMipMap: 0\n";
            const auto u = meta.find("wrapU: -1");
            meta.replace(u, 9, "wrapU: 1");
            const auto v = meta.find("wrapV: -1");
            meta.replace(v, 9, "wrapV: 2");
        }
        request.files["Assets/mask.png.meta"] = {meta.begin(), meta.end()};
        if (height || mode == 6) {
            std::vector<std::uint8_t> ramp(8 * 8 * 4);
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x) {
                    const auto at = (y * 8 + x) * 4;
                    ramp[at] = ramp[at + 1] = ramp[at + 2] = std::uint8_t((mode == 5 ? y : x) * 32);
                    ramp[at + 3]                           = 255;
                    if (mode == 6) {
                        ramp[at + 1] = std::uint8_t(y * 32);
                        ramp[at + 2] = 17;
                        ramp[at + 3] = 43;
                    }
                }
            auto png = asset::detail::encodeSourcePng(8, 8, ramp, 4096);
            REQUIRE(png.ok());
            request.files["Assets/mask.png"] = std::move(png).takeValue();
        }
        auto imported = prepareUnityProjectImport(request);
        REQUIRE(imported.ok());
        const auto materialRef = imported.value().manifest.entrypoints.at("Assets/leaf.mat");
        auto       encoded     = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
        REQUIRE(encoded.ok());
        auto archive = asset::parseEvaArchive(encoded.value());
        REQUIRE(archive.ok());
        auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
        REQUIRE(profile.ok());
        auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
        REQUIRE(cooked.ok());
        auto pack = asset::parseEvpack(cooked.value().bytes);
        REQUIRE(pack.ok());
        asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
        auto                        loaded = asset_graphics::detail::readCookedMaterial(
            reader, materialRef, {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
        REQUIRE(loaded.ok());
        REQUIRE_EQ(loaded.value().surface.normalMode, graphics::PbrNormalMode::VegetationRG);
        REQUIRE_EQ(loaded.value().surface.normalScale, -2.f);
        REQUIRE(loaded.value().images[2].has_value());
        REQUIRE(!loaded.value().surface.textures[2].srgbDecode);
        REQUIRE_EQ(loaded.value().surface.textures[2].wrapS, mode == 3 ? 33071u : 10497u);
        REQUIRE_EQ(loaded.value().surface.textures[2].wrapT, mode == 3 ? 33648u : 10497u);
        for (const auto slot : {0u, 1u, 2u, 3u}) {
            REQUIRE_EQ(loaded.value().surface.textures[slot].wrapS, mode == 3 ? 33071u : 10497u);
            REQUIRE_EQ(loaded.value().surface.textures[slot].wrapT, mode == 3 ? 33648u : 10497u);
            REQUIRE_EQ(loaded.value().surface.textures[slot].minFilter, mode == 3                             ? 9728u
                                                                        : mode == 0 || mode == 4 || mode == 6 ? 9985u
                                                                                                              : 9987u);
            REQUIRE_EQ(loaded.value().surface.textures[slot].magFilter, mode == 3 ? 9728u : 9729u);
        }
        if (height || mode == 6) {
            bool checked = false;
            for (const auto& a : imported.value().manifest.assets) {
                if (a.asset != *loaded.value().images[2]) continue;
                for (const auto& e : imported.value().entries) {
                    if (e.path != a.definition) continue;
                    auto value = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(value.ok());
                    const auto blob = value.value().getIf<Value::Object>()->at("blob").asString();
                    for (const auto& b : imported.value().entries) {
                        if (b.path != blob) continue;
                        auto normal = asset::cookCanonicalImageRgba8(e.bytes, b.bytes, 4096);
                        REQUIRE(normal.ok());
                        // Unity 6000.0.79f1 RGBA32: RampX 8x8, Standard, heightScale 0.25.
                        std::vector<std::uint8_t> expected;
                        for (unsigned y = 0; y < 8; ++y)
                            for (unsigned x = 0; x < 8; ++x)
                                expected.insert(expected.end(),
                                                {std::uint8_t(x == 0 || x == 7 ? 172 : 112), 128, 255, 255});
                        REQUIRE_EQ(value.value().getIf<Value::Object>()->at("mipCount").asInt(),
                                   int64_t(mode == 3 ? 1 : 4));
                        if (mode != 3) {
                            // Unity's next height level is [16,80,144,208]/255, then
                            // 2x2 and 1x1 produce zero periodic central differences.
                            for (unsigned y = 0; y < 4; ++y)
                                for (unsigned x = 0; x < 4; ++x)
                                    expected.insert(expected.end(),
                                                    {uint8_t(x == 0 || x == 3 ? 143 : 112), 128, 255, 255});
                            for (unsigned i = 0; i < 5; ++i) expected.insert(expected.end(), {128, 128, 255, 255});
                        }
                        if (mode == 5) {
                            size_t offset = 0;
                            for (unsigned extent : {8u, 4u, 2u, 1u}) {
                                for (unsigned y = 0; y < extent; ++y)
                                    for (unsigned x = 0; x < extent; ++x) {
                                        const auto original = expected[offset + (x * extent + y) * 4];
                                        expected[offset + (y * extent + x) * 4 + 1] = original;
                                    }
                                for (unsigned i = 0; i < extent * extent; ++i) expected[offset + i * 4] = 128;
                                offset += extent * extent * 4;
                            }
                        }
                        if (mode == 6) {
                            expected.clear();
                            // Independently reduced linear ramps: centers are 0,16,48,112
                            // and steps 32,64,128,256. Source B/A do not enter normal RG.
                            for (unsigned extent : {8u, 4u, 2u, 1u}) {
                                const unsigned step  = 256 / extent;
                                const unsigned first = (step - 32) / 2;
                                for (unsigned y = 0; y < extent; ++y)
                                    for (unsigned x = 0; x < extent; ++x)
                                        expected.insert(expected.end(), {uint8_t(first + x * step),
                                                                         uint8_t(255 - first - y * step), 255, 255});
                            }
                            REQUIRE(loaded.value().images[0].has_value());
                            REQUIRE(*loaded.value().images[0] != *loaded.value().images[2]);
                        }
                        REQUIRE_EQ(
                            (std::vector<std::uint8_t>(normal.value().bulk.begin() + 28, normal.value().bulk.end())),
                            expected);
                        checked = true;
                    }
                }
            }
            REQUIRE(checked);
        }
        if (height) {
            for (const auto invalidSetting : {"  heightScale: 1.1\n", "  wrapU: 3\n", "  normalMapFilter: 7\n"}) {
                auto              invalid = request;
                const std::string invalidMeta =
                    "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nTextureImporter:\n"
                    "  textureType: 1\n  convertToNormalMap: 1\n" +
                    std::string(invalidSetting);
                invalid.files["Assets/mask.png.meta"] = {invalidMeta.begin(), invalidMeta.end()};
                auto rejected                         = prepareUnityProjectImport(invalid);
                if (std::string_view(invalidSetting).find("wrapU") != std::string_view::npos) {
                    REQUIRE(rejected.ok());
                    bool unsupported = false;
                    for (const auto& finding : rejected.value().findings)
                        if (finding.disposition == ImportDisposition::Unsupported &&
                            finding.feature == "resource.conversion")
                            unsupported = true;
                    REQUIRE(unsupported);
                    REQUIRE(!rejected.value().manifest.entrypoints.contains("Assets/leaf.mat"));
                } else {
                    REQUIRE(!rejected.ok());
                }
            }
        }
        const std::string duplicate = "  convertToNormalMap: 0\n";
        auto&             metadata  = request.files["Assets/mask.png.meta"];
        metadata.insert(metadata.end(), duplicate.begin(), duplicate.end());
        REQUIRE(!prepareUnityProjectImport(request).ok());
    }
}

TEST_CASE("asset.import.unityVegetationAutomaticNpotMatchesUnity") {
    // Unity 6000.0.79f1 RGBAFloat oracle, 5x7 independent RGB pattern,
    // default cubic resize, Standard height normal, heightScale=0.25.
    struct Reference {
        bool                 height;
        int                  mode, width, heightPixels, levels;
        std::vector<uint8_t> rg;
        unsigned             sourceWidth = 5, sourceHeight = 7;
        int                  algorithm = 0;
    };
    const std::vector<Reference> references = {
        {false, 1, 4, 8, 4, {17,  19,  60,  133, 107, 88,  151, 85,  65,  28,  108, 141, 155, 97,  181, 94,  127, 39,
                             165, 153, 202, 108, 130, 105, 185, 51,  172, 164, 135, 120, 95,  117, 139, 62,  126, 175,
                             90,  131, 131, 128, 72,  74,  109, 186, 146, 142, 190, 139, 119, 87,  165, 168, 167, 124,
                             181, 153, 167, 100, 216, 113, 126, 68,  74,  166, 63,  80,  149, 91,  162, 102, 141, 113,
                             112, 124, 139, 135, 167, 117, 137, 128, 128, 96,  139, 126, 134, 111}},
        {false, 2, 8, 8, 4, {13,  10,  27,  44,  49,  98,  72,  141, 95,  120, 118, 64,  140, 71,  154, 102, 61,
                             19,  75,  53,  97,  107, 120, 150, 143, 128, 163, 73,  176, 80,  182, 110, 123, 30,
                             137, 64,  156, 119, 173, 162, 192, 140, 194, 84,  155, 91,  114, 122, 178, 41,  190,
                             76,  187, 130, 152, 173, 134, 151, 135, 96,  111, 102, 84,  133, 131, 53,  143, 87,
                             141, 141, 106, 184, 88,  163, 103, 107, 122, 114, 133, 145, 67,  64,  81,  99,  101,
                             152, 118, 195, 136, 173, 158, 118, 180, 125, 194, 156, 116, 76,  130, 109, 152, 150,
                             171, 166, 172, 144, 164, 116, 173, 135, 186, 167, 164, 84,  178, 116, 200, 129, 211,
                             87,  166, 65,  92,  95,  73,  142, 84,  176, 44,  32,  85,  124, 130, 96,  163, 91,
                             157, 53,  167, 146, 164, 118, 116, 112, 106, 76,  117, 168, 121, 140, 157, 135, 147,
                             96,  184, 133, 149, 105, 129, 155, 113, 89,  143, 104, 138, 118, 139, 134, 133, 111}},
        {false, 3, 4, 4, 3, {39,  23,  84,  137, 134, 92,  166, 89,  159, 45,  169, 158, 166, 114,
                             113, 111, 107, 68,  117, 183, 118, 139, 163, 134, 140, 94,  189, 141,
                             149, 96,  129, 160, 113, 91,  145, 102, 138, 122, 140, 132, 134, 112}},
        {true, 1, 4, 8, 4, {133, 117, 115, 123, 122, 133, 140, 132, 132, 140, 115, 139, 123, 139, 140, 132, 125, 141,
                            116, 138, 130, 132, 139, 128, 122, 134, 124, 131, 133, 127, 131, 133, 127, 127, 124, 130,
                            128, 131, 131, 134, 126, 128, 119, 130, 129, 126, 136, 122, 119, 126, 126, 120, 135, 114,
                            129, 116, 122, 108, 131, 110, 133, 119, 124, 123, 128, 127, 128, 130, 128, 135, 128, 132,
                            128, 128, 128, 125, 128, 120, 128, 123, 128, 128, 128, 128, 128, 128}},
        {true, 2, 8, 8, 4, {147, 114, 116, 114, 114, 118, 120, 126, 128, 132, 126, 134, 120, 134, 148, 133, 146,
                            144, 116, 144, 114, 143, 120, 143, 129, 143, 127, 141, 122, 136, 147, 131, 135, 145,
                            116, 145, 115, 143, 121, 138, 130, 135, 133, 133, 130, 130, 140, 128, 128, 136, 118,
                            136, 121, 134, 128, 130, 134, 127, 132, 128, 127, 133, 132, 137, 136, 126, 118, 127,
                            121, 128, 128, 132, 133, 133, 127, 131, 121, 134, 136, 137, 139, 129, 116, 129, 115,
                            129, 124, 131, 134, 128, 131, 123, 122, 121, 139, 121, 128, 125, 117, 124, 118, 121,
                            129, 114, 140, 110, 135, 111, 123, 112, 129, 112, 127, 102, 120, 102, 126, 103, 135,
                            106, 137, 112, 129, 118, 121, 121, 127, 121, 131, 126, 120, 127, 124, 131, 135, 129,
                            126, 138, 123, 137, 129, 134, 132, 133, 128, 129, 124, 128, 127, 124, 131, 126, 124,
                            117, 128, 118, 131, 121, 127, 122, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128}},
        {true, 3, 4, 4, 3, {131, 126, 119, 127, 124, 131, 136, 129, 125, 138, 123, 137, 130, 134,
                            132, 133, 127, 129, 124, 128, 128, 124, 131, 126, 123, 117, 128, 118,
                            132, 121, 127, 122, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128}},
        {false,
         1,
         8,
         4,
         4,
         {0,   0,   32,  78, 63,  156, 95,  88,  127, 56,  159, 134, 190, 175, 222, 34,  47,  9,   79,  87, 111, 165,
          142, 96,  174, 65, 157, 143, 67,  184, 99,  43,  95,  17,  126, 95,  158, 173, 190, 105, 185, 73, 119, 151,
          29,  193, 61,  51, 142, 26,  174, 104, 205, 182, 237, 114, 159, 82,  45,  160, 76,  201, 108, 60, 40,  43,
          103, 126, 154, 99, 144, 109, 134, 61,  198, 144, 127, 117, 68,  126, 119, 93,  124, 113, 121, 103},
         7,
         3,
         1},
        {false,
         2,
         8,
         4,
         4,
         {0,   0,   32,  78, 63,  156, 95,  88,  127, 56,  159, 134, 190, 175, 222, 34,  47,  9,   79,  87, 111, 165,
          142, 96,  174, 65, 157, 143, 67,  184, 99,  43,  95,  17,  126, 95,  158, 173, 190, 105, 185, 73, 119, 151,
          29,  193, 61,  51, 142, 26,  174, 104, 205, 182, 237, 114, 159, 82,  45,  160, 76,  201, 108, 60, 40,  43,
          103, 126, 154, 99, 144, 109, 134, 61,  198, 144, 127, 117, 68,  126, 119, 93,  124, 113, 121, 103},
         7,
         3,
         1},
        {false,
         3,
         4,
         2,
         3,
         {32, 37, 96, 165, 161, 100, 162, 99, 138, 57, 203, 184, 100, 119, 76, 119, 117, 111, 125, 109, 121, 110},
         7,
         3,
         1},
        {true,
         1,
         8,
         4,
         4,
         {142, 117, 112, 117, 121, 117, 128, 117, 119, 124, 114, 130, 128, 122, 155, 122, 131, 138, 112, 138, 121, 138,
          128, 138, 122, 136, 125, 130, 136, 122, 145, 122, 126, 138, 112, 138, 121, 138, 130, 138, 127, 131, 128, 125,
          136, 133, 140, 133, 126, 117, 112, 117, 121, 117, 135, 117, 135, 119, 123, 125, 128, 133, 140, 133, 130, 128,
          120, 128, 125, 128, 135, 128, 125, 128, 124, 128, 130, 128, 131, 128, 128, 128, 128, 128, 128, 128},
         7,
         3,
         1},
        {true,
         2,
         8,
         4,
         4,
         {142, 117, 112, 117, 121, 117, 128, 117, 119, 124, 114, 130, 128, 122, 155, 122, 131, 138, 112, 138, 121, 138,
          128, 138, 122, 136, 125, 130, 136, 122, 145, 122, 126, 138, 112, 138, 121, 138, 130, 138, 127, 131, 128, 125,
          136, 133, 140, 133, 126, 117, 112, 117, 121, 117, 135, 117, 135, 119, 123, 125, 128, 133, 140, 133, 130, 128,
          120, 128, 125, 128, 135, 128, 125, 128, 124, 128, 130, 128, 131, 128, 128, 128, 128, 128, 128, 128},
         7,
         3,
         1},
        {true,
         3,
         4,
         2,
         3,
         {129, 128, 120, 128, 126, 128, 135, 128, 123, 128, 125, 128, 132, 128, 130, 128, 128, 128, 128, 128, 128, 128},
         7,
         3,
         1}};
    for (const auto& reference : references) {
        auto        request       = fixture();
        auto&       materialBytes = request.files["Assets/leaf.mat"];
        std::string material(materialBytes.begin(), materialBytes.end());
        material.insert(material.find("    m_Floats:"),
                        "    - _MainNormalTex:\n        m_Texture: {fileID: 2800000, guid: "
                        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}\n");
        materialBytes.assign(material.begin(), material.end());
        const std::string meta =
            "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nTextureImporter:\n  textureType: 1\n  convertToNormalMap: " +
            std::to_string(reference.height ? 1 : 0) + "\n  nPOTScale: " + std::to_string(reference.mode) +
            "\n  enableMipMap: 1\n  mipMapMode: 0\n  resizeAlgorithm: " + std::to_string(reference.algorithm) + "\n";
        request.files["Assets/mask.png.meta"] = {meta.begin(), meta.end()};
        std::vector<uint8_t> pixels;
        for (unsigned y = 0; y < reference.sourceHeight; ++y)
            for (unsigned x = 0; x < reference.sourceWidth; ++x)
                pixels.insert(pixels.end(), {uint8_t((37 * x + 71 * y) % 256), uint8_t((91 * x + 13 * y) % 256),
                                             uint8_t((17 * x + 43 * y) % 256), uint8_t((29 * x + 31 * y) % 256)});
        auto png = asset::detail::encodeSourcePng(reference.sourceWidth, reference.sourceHeight, pixels, 4096);
        REQUIRE(png.ok());
        request.files["Assets/mask.png"] = std::move(png).takeValue();
        auto imported                    = prepareUnityProjectImport(request);
        REQUIRE(imported.ok());
        unsigned checked = 0;
        for (const auto& entry : imported.value().entries) {
            if (!entry.path.ends_with("normal.rgba8-mips")) continue;
            REQUIRE_EQ(entry.bytes.size(), reference.rg.size() * 2);
            for (size_t i = 0; i < reference.rg.size() / 2; ++i) {
                REQUIRE(std::abs(int(entry.bytes[i * 4]) - int(reference.rg[i * 2])) <= 1);
                REQUIRE(std::abs(int(entry.bytes[i * 4 + 1]) - int(reference.rg[i * 2 + 1])) <= 1);
                REQUIRE_EQ(entry.bytes[i * 4 + 2], uint8_t(255));
                REQUIRE_EQ(entry.bytes[i * 4 + 3], uint8_t(255));
            }
            const auto definition = entry.path.substr(0, entry.path.rfind('/') + 1) + "asset.json";
            for (const auto& candidate : imported.value().entries)
                if (candidate.path == definition) {
                    auto value = Value::fromJson(std::string(candidate.bytes.begin(), candidate.bytes.end()));
                    REQUIRE(value.ok());
                    const auto& object = *value.value().getIf<Value::Object>();
                    REQUIRE_EQ(object.at("width").asInt(), int64_t(reference.width));
                    REQUIRE_EQ(object.at("height").asInt(), int64_t(reference.heightPixels));
                    REQUIRE_EQ(object.at("mipCount").asInt(), int64_t(reference.levels));
                    auto cooked = asset::cookCanonicalImageRgba8(candidate.bytes, entry.bytes, 4096);
                    REQUIRE(cooked.ok());
                    ++checked;
                }
        }
        REQUIRE_EQ(checked, 1u);
    }
}
