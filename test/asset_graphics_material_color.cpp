#include "asset/RuntimeDefinition.h"
#include "asset/graphics/CookedMaterial.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {
Value::Object definition() {
    return {{"schema", "eve.material"},
            {"schemaVersion", 5},
            {"shadingModel", "pbr"},
            {"baseColor", Value::Array{2.0, .5, .25, .8}},
            {"albedoTextureStrength", .4},
            {"colorMask", Value::Object{{"secondary", Value::Array{.1, 3.0, .2}}, {"minimum", .75}, {"maximum", .25}}},
            {"metallicRoughnessTexture", "asset://018f6f22-2490-7ad2-bf58-4f1dbca31042"}};
}

Result<asset_graphics::detail::CookedMaterial> load(Value::Object root, unsigned version = 5) {
    auto encoded = encodeRuntimeDefinition(Value(std::move(root)));
    REQUIRE(encoded.ok());
    EvpackBuild build;
    build.packageId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks.push_back({build.packageId,
                            "eve.material",
                            SchemaVersion(version),
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(encoded).takeValue()});
    auto packed = buildEvpack(std::move(build));
    REQUIRE(packed.ok());
    auto parsed = parseEvpack(packed.value());
    REQUIRE(parsed.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(parsed).takeValue()));
    auto                 ref = AssetRef::fromId(*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"));
    REQUIRE(ref.ok());
    return asset_graphics::detail::readCookedMaterial(
        reader, ref.value(), {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
}
}  // namespace

TEST_CASE("asset.graphics.materialTranslucencyRequiresV5AndOwnsParameters") {
    auto root             = definition();
    root["schemaVersion"] = 5;
    root["translucency"]  = Value::Object{
         {"intensity", .4},  {"strength", 3.0},   {"scattering", 6.0}, {"color", Value::Array{2.0, .5, .25}},
         {"maskAmount", .5}, {"maskMinimum", .2}, {"maskMaximum", .8}};
    auto loaded = load(root, 5);
    REQUIRE(loaded.ok());
    const auto& trans = loaded.value().surface.translucency;
    REQUIRE_EQ(trans.intensity, .4f);
    REQUIRE_EQ(trans.strength, 3.f);
    REQUIRE_EQ(trans.scattering, 6.f);
    REQUIRE_EQ(trans.color[0], 2.f);
    REQUIRE_EQ(trans.maskAmount, .5f);
    REQUIRE_EQ(trans.maskMinimum, .2f);
    REQUIRE_EQ(trans.maskMaximum, .8f);
    REQUIRE_EQ(trans.globalIntensity, 1.f);
    REQUIRE(loaded.value().surface.textures[1].texture == nullptr);
    auto old             = root;
    old["schemaVersion"] = 3;
    REQUIRE(!load(old, 3).ok());
    auto missing = root;
    missing.erase("colorMask");
    missing.erase("metallicRoughnessTexture");
    REQUIRE(!load(missing, 5).ok());
    auto unknown                                                 = root;
    (*unknown.at("translucency").getIf<Value::Object>())["typo"] = 1;
    REQUIRE(!load(unknown, 5).ok());
    auto invalid                                                       = root;
    (*invalid.at("translucency").getIf<Value::Object>())["scattering"] = 0;
    REQUIRE(!load(invalid, 5).ok());
}

TEST_CASE("asset.graphics.materialColorDefinitionOwnsUnboundParameters") {
    auto material = load(definition());
    REQUIRE(material.ok());
    const auto& s = material.value().surface;
    REQUIRE(s.colorMaskEnabled);
    REQUIRE_EQ(s.colorMaskSecondary[1], 3.f);
    REQUIRE_EQ(s.colorMaskMin, .75f);
    REQUIRE_EQ(s.colorMaskMax, .25f);
    REQUIRE_EQ(s.albedoTextureStrength, .4f);
    REQUIRE(material.value().images[1].has_value());
    REQUIRE(s.textures[1].texture == nullptr);
    REQUIRE(!s.textures[1].srgbDecode);
    // The CPU definition cannot be submitted before its image has been resolved.
    auto unbound = graphics::validatePbrSurface(s);
    REQUIRE(!unbound.ok());
}
TEST_CASE("asset.graphics.materialVegetationNormalEncodingIsVersionedAndValidated") {
    for (const auto name : {"tve-rg", "tve-rag", "tve-ag"}) {
        auto root              = definition();
        root["normalEncoding"] = name;
        root["normalScale"]    = -2;
        root["normalTexture"]  = "asset://018f6f22-2490-7ad2-bf58-4f1dbca31042";
        auto material          = load(root);
        REQUIRE(material.ok());
        REQUIRE(material.value().surface.normalMode != graphics::PbrNormalMode::TangentXYZ);
        REQUIRE_EQ(material.value().surface.normalScale, -2.f);
        root["normalScale"] = 9;
        REQUIRE(!load(root).ok());
        root["normalScale"] = 1;
        root.erase("normalTexture");
        REQUIRE(!load(root).ok());
        root.erase("colorMask");
        root.erase("albedoTextureStrength");
        root.erase("baseColor");
        root["schemaVersion"] = 2;
        REQUIRE(!load(root, 2).ok());
    }
    auto root              = definition();
    root["normalEncoding"] = "unknown";
    REQUIRE(!load(root).ok());
}

TEST_CASE("asset.graphics.materialColorRejectsMalformedOrUnversionedParameters") {
    auto missing = definition();
    missing.erase("metallicRoughnessTexture");
    REQUIRE(!load(missing).ok());
    for (const auto key : {"secondary", "minimum", "maximum"}) {
        auto malformed = definition();
        malformed.at("colorMask").getIf<Value::Object>()->erase(key);
        REQUIRE(!load(malformed).ok());
    }
    auto extra                                              = definition();
    (*extra.at("colorMask").getIf<Value::Object>())["typo"] = true;
    REQUIRE(!load(extra).ok());
    auto range                     = definition();
    range["albedoTextureStrength"] = 1.1;
    REQUIRE(!load(range).ok());
    auto alpha                                        = definition();
    (*alpha.at("baseColor").getIf<Value::Array>())[3] = 2.0;
    REQUIRE(!load(alpha).ok());
    auto old = definition();
    REQUIRE(!load(old, 2).ok());
    old["schemaVersion"] = 2;
    REQUIRE(!load(old, 2).ok());
    old.erase("colorMask");
    old.erase("albedoTextureStrength");
    old.erase("baseColor");
    REQUIRE(!load(old, 2).ok());
    old["schemaVersion"] = 5;
    auto compatible      = load(old, 5);
    REQUIRE(compatible.ok());
    REQUIRE(!compatible.value().surface.colorMaskEnabled);
    REQUIRE_EQ(compatible.value().surface.albedoTextureStrength, 1.f);
    old["schemaVersion"] = 1;
    REQUIRE(!load(old, 1).ok());
    auto additive               = definition();
    additive["vendorExtension"] = 42;
    REQUIRE(load(additive).ok());
}

TEST_CASE("asset.graphics.materialMotionHighlightRequiresV5") {
    auto root = definition();
    root["motionHighlightColor"] = Value::Array{3.0, .5, 0.0};
    REQUIRE(!load(root, 4).ok());
    root["schemaVersion"] = 5;
    auto material = load(root, 5);
    REQUIRE(material.ok());
    REQUIRE_EQ(material.value().surface.motionHighlightColor[0], 3.f);
    root["motionHighlightColor"] = Value::Array{1, -1, 0};
    REQUIRE(!load(root, 5).ok());
    root["motionHighlightColor"] = Value::Array{1, 0};
    REQUIRE(!load(root, 5).ok());
}

TEST_CASE("asset.graphics.materialVegetationSurfaceRequiresV6") {
    auto root = definition();
    root["vegetationSurface"] = Value::Object{{"sourceFamily", "tve-12"},
                                                {"overlay", .3},
                                                {"wetness", .8},
                                                {"overlayVariation", .1},
                                                {"overlayProjection", .6},
                                                {"vertexOcclusionAlpha", .34117648},
                                                {"invertVertexOcclusion", false}};
    REQUIRE(!load(root, 5).ok());
    root["schemaVersion"] = 6;
    auto material = load(root, 6);
    REQUIRE(material.ok());
    REQUIRE(material.value().vegetationSurface.has_value());
    REQUIRE_EQ(material.value().vegetationSurface->overlayCoverage, .3f);
    REQUIRE_EQ(material.value().vegetationSurface->wetnessCoverage, .8f);
    REQUIRE_EQ(material.value().vegetationSurface->overlayVariation, .1f);
    REQUIRE_EQ(material.value().vegetationSurface->overlayProjection, .6f);
    REQUIRE_EQ(material.value().vegetationSurface->vertexOcclusionAlpha, .34117648f);
    REQUIRE_EQ(material.value().vegetationSurface->albedo[0], 2.f);
    REQUIRE_EQ(material.value().vegetationSurface->roughness, 1.f);
    REQUIRE_EQ(material.value().vegetationSurface->alphaCutoff, .5f);
    auto malformed = root;
    malformed.at("vegetationSurface").getIf<Value::Object>()->erase("overlayProjection");
    REQUIRE(!load(malformed, 6).ok());
    auto invalid = root;
    (*invalid.at("vegetationSurface").getIf<Value::Object>())["overlay"] = 1.1;
    REQUIRE(!load(invalid, 6).ok());
}
