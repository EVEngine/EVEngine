#include "zeroerr/assert.h"

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
    auto root                    = definition();
    root["motionHighlightColor"] = Value::Array{3.0, .5, 0.0};
    REQUIRE(!load(root, 4).ok());
    root["schemaVersion"] = 5;
    auto material         = load(root, 5);
    REQUIRE(material.ok());
    REQUIRE_EQ(material.value().surface.motionHighlightColor[0], 3.f);
    root["motionHighlightColor"] = Value::Array{1, -1, 0};
    REQUIRE(!load(root, 5).ok());
    root["motionHighlightColor"] = Value::Array{1, 0};
    REQUIRE(!load(root, 5).ok());
}

TEST_CASE("asset.graphics.materialVegetationSurfaceRequiresV6") {
    auto root = definition();
    root["vegetationSurface"] =
        Value::Object{{"sourceFamily", "tve-12"},      {"overlay", .3},           {"wetness", .8},
                      {"overlayVariation", .1},        {"overlayProjection", .6}, {"vertexOcclusionAlpha", .34117648},
                      {"invertVertexOcclusion", false}};
    REQUIRE(!load(root, 5).ok());
    root["schemaVersion"] = 6;
    auto material         = load(root, 6);
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
    auto invalid                                                         = root;
    (*invalid.at("vegetationSurface").getIf<Value::Object>())["overlay"] = 1.1;
    REQUIRE(!load(invalid, 6).ok());
}

#include <array>
#include <limits>
#include "graphics/Material.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"

namespace test_material_foliage_value_unit {
using namespace eve::graphics;

TEST_CASE("graphics.vegetation.materialFoliageTranslucencyPublishesAtomically") {
    Material material;
    auto     accepted = material.setFoliageTranslucency(.34f, .55f, .18f, .52f, 2.f, .35f, 3.f, .85f, .18f, .35f);
    REQUIRE(accepted.ok());
    const auto published = material.pbrSurface();
    CHECK_EQ(published.translucency.color, (std::array<float, 3>{.34f, .55f, .18f}));
    CHECK_EQ(published.translucency.intensity, .52f);
    CHECK_EQ(published.translucency.strength, 2.f);
    CHECK_EQ(published.translucency.scattering, 3.f);
    CHECK_EQ(published.vegetationColor.backfaceNormalMode, PbrVegetationBackfaceNormalMode::Flip);
    CHECK(material.getDoubleSided());

    auto rejected = material.setFoliageTranslucency(.9f, .1f, .1f, 2.f, 2.f, .35f, 3.f, .85f, .18f, .35f);
    CHECK(!rejected.ok());
    CHECK_EQ(material.pbrSurface().translucency.color, published.translucency.color);
    CHECK_EQ(material.pbrSurface().translucency.intensity, published.translucency.intensity);
}

TEST_CASE("graphics.vegetation.materialFoliageWindPreservesPbrAndUpdatesExplicitTime") {
    Material   material;
    Texture    motion;
    Texture    noise;
    PbrSurface initial;
    initial.translucency.intensity = .52f;
    initial.albedoTextureStrength  = .73f;
    REQUIRE(material.setPbrSurface(initial).ok());

    REQUIRE(
        material.setFoliageWind(&motion, &noise, .05f, 0.f, .5f, .18f, 2.f, .12f, 2.f, .072f, 1.f, 400.f, 2.f).ok());
    const auto configured = material.pbrSurface();
    CHECK_EQ(configured.translucency.intensity, .52f);
    CHECK_EQ(configured.albedoTextureStrength, .73f);
    CHECK_EQ(configured.vegetationMotion.texture, &motion);
    CHECK_EQ(configured.vegetationMotion.noise, &noise);
    CHECK_EQ(configured.vegetationMotion.fallback[0], 1.f);
    CHECK_EQ(configured.vegetationMotion.fallback[2], .5f);
    CHECK_EQ(configured.vegetationMotion.mode, PbrVegetationMotionMode::Object);
    CHECK_EQ(configured.vegetationVertex.source, PbrVegetationDeformationSource::GpuFields);

    REQUIRE(material.setFoliageWindTime(3.25).ok());
    CHECK_EQ(material.pbrSurface().vegetationMotion.time, 3.25);
    const auto beforeInvalid = material.pbrSurface();
    CHECK(!material.setFoliageWind(nullptr, &noise, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f).ok());
    CHECK_EQ(material.pbrSurface().vegetationMotion.time, beforeInvalid.vegetationMotion.time);
}

TEST_CASE("graphics.vegetation.translucency_validates_factors") {
    PbrSurface surface;
    REQUIRE(validatePbrSurface(surface).ok());
    surface.motionHighlightColor = {3, 2, 0};
    REQUIRE(validatePbrSurface(surface).ok());
    surface.motionHighlightColor[1] = -1;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.motionHighlightColor             = {0, 0, 0};
    surface.vegetationColor.overlayVariation = -0.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlayVariation  = .5f;
    surface.vegetationColor.overlayProjection = 1.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlayProjection    = .5f;
    surface.vegetationColor.vertexOcclusionAlpha = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.vertexOcclusionAlpha = .5f;
    surface.vegetationColor.overlaySubsurface    = 1.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlaySubsurface = .5f;
    surface.translucency.intensity            = .5f;
    surface.translucency.scattering           = 0;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.translucency.scattering = 2;
    surface.translucency.color[1]   = std::numeric_limits<float>::infinity();
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.translucency.color[1]   = 1;
    surface.translucency.maskAmount = 1;
    REQUIRE(!validatePbrSurface(surface).ok());
}
}  // namespace test_material_foliage_value_unit
