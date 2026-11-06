// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "asset_graphics_material_color.cpp"
  ]
}
*/
// clang-format on

#include "asset/RuntimeDefinition.h"
#include "asset/graphics/CookedMaterial.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <utility>

using namespace eve;
using namespace eve::asset;

namespace {
constexpr auto imageUri = "asset://018f6f22-2490-7ad2-bf58-4f1dbca31042";

Value::Object baseDefinition(int version = 5) {
    return {{"schema", "eve.material"},
            {"schemaVersion", version},
            {"shadingModel", "pbr"},
            {"baseColor", Value::Array{2.0, .5, .25, .8}},
            {"albedoTextureStrength", .4},
            {"colorMask", Value::Object{{"secondary", Value::Array{.1, 3.0, .2}}, {"minimum", .75}, {"maximum", .25}}},
            {"metallicRoughnessTexture", imageUri}};
}

Result<asset_graphics::detail::CookedMaterial> decode(Value::Object definition, int packageVersion) {
    auto bytes = encodeRuntimeDefinition(Value(std::move(definition)));
    REQUIRE(bytes.ok());
    EvpackBuild package;
    package.packageId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    package.buildId   = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    package.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    package.chunks.push_back({package.packageId,
                              "eve.material",
                              SchemaVersion(packageVersion),
                              0,
                              EvpackChunkKind::Definition,
                              0,
                              EvpackCodec::None,
                              8,
                              {},
                              std::move(bytes).takeValue()});
    auto built = buildEvpack(std::move(package));
    REQUIRE(built.ok());
    auto parsed = parseEvpack(built.value());
    REQUIRE(parsed.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(parsed).takeValue()));
    auto                 ref = AssetRef::fromId(*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"));
    REQUIRE(ref.ok());
    return asset_graphics::detail::readCookedMaterial(
        reader, ref.value(), {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
}

Value::Object translucency() {
    return {{"intensity", .4},  {"strength", 3.0},   {"scattering", 6.0}, {"color", Value::Array{2.0, .5, .25}},
            {"maskAmount", .5}, {"maskMinimum", .2}, {"maskMaximum", .8}};
}
}  // namespace

TEST_CASE("asset.graphics.material.spec.decodedSurface") {
    SUB_CASE("v5 color mask owns CPU parameters and leaves GPU image unbound") {
        auto material = decode(baseDefinition(), 5);
        REQUIRE(material.ok());
        const auto& surface = material.value().surface;
        REQUIRE(surface.colorMaskEnabled);
        REQUIRE(surface.colorMaskSecondary[0] == .1f);
        REQUIRE(surface.colorMaskSecondary[1] == 3.f);
        REQUIRE(surface.colorMaskMin == .75f);
        REQUIRE(surface.colorMaskMax == .25f);
        REQUIRE(surface.albedoTextureStrength == .4f);
        REQUIRE(material.value().images[1].has_value());
        REQUIRE(surface.textures[1].texture == nullptr);
        REQUIRE(!surface.textures[1].srgbDecode);
        REQUIRE(!graphics::validatePbrSurface(surface).ok());
    };

    SUB_CASE("optional v5 fields use documented defaults") {
        auto root = baseDefinition();
        root.erase("colorMask");
        root.erase("albedoTextureStrength");
        root.erase("baseColor");
        root.erase("metallicRoughnessTexture");
        auto material = decode(root, 5);
        REQUIRE(material.ok());
        REQUIRE(!material.value().surface.colorMaskEnabled);
        REQUIRE(material.value().surface.albedoTextureStrength == 1.f);
    };

    SUB_CASE("document and package schema versions agree; unknown extensions are tolerated") {
        REQUIRE(!decode(baseDefinition(5), 6).ok());
        REQUIRE(!decode(baseDefinition(6), 5).ok());
        REQUIRE(!decode(baseDefinition(2), 2).ok());
        auto wrongSchema      = baseDefinition();
        wrongSchema["schema"] = "eve.other";
        REQUIRE(!decode(wrongSchema, 5).ok());
        auto extension               = baseDefinition();
        extension["vendorExtension"] = 42;
        REQUIRE(decode(extension, 5).ok());
    };

    SUB_CASE("color mask requires its full shape and image binding") {
        for (const char* field : {"secondary", "minimum", "maximum"}) {
            auto root = baseDefinition();
            root.at("colorMask").getIf<Value::Object>()->erase(field);
            REQUIRE(!decode(root, 5).ok());
        }
        auto root                                              = baseDefinition();
        (*root.at("colorMask").getIf<Value::Object>())["typo"] = true;
        REQUIRE(!decode(root, 5).ok());
        root = baseDefinition();
        root.erase("metallicRoughnessTexture");
        REQUIRE(!decode(root, 5).ok());
        root                          = baseDefinition();
        root["albedoTextureStrength"] = 1.1;
        REQUIRE(!decode(root, 5).ok());
        root                                             = baseDefinition();
        (*root.at("baseColor").getIf<Value::Array>())[3] = 2.0;
        REQUIRE(!decode(root, 5).ok());
    };

    SUB_CASE("translucency fields decode and invalid values are rejected") {
        auto root            = baseDefinition();
        root["translucency"] = translucency();
        auto material        = decode(root, 5);
        REQUIRE(material.ok());
        const auto& t = material.value().surface.translucency;
        REQUIRE(t.intensity == .4f);
        REQUIRE(t.strength == 3.f);
        REQUIRE(t.scattering == 6.f);
        REQUIRE(t.color[0] == 2.f);
        REQUIRE(t.maskAmount == .5f);
        REQUIRE(t.maskMinimum == .2f);
        REQUIRE(t.maskMaximum == .8f);
        REQUIRE(t.globalIntensity == 1.f);
        root.at("translucency").getIf<Value::Object>()->erase("intensity");
        REQUIRE(decode(root, 5).ok());
        root                                                            = baseDefinition();
        root["translucency"]                                            = translucency();
        (*root.at("translucency").getIf<Value::Object>())["scattering"] = 0;
        REQUIRE(!decode(root, 5).ok());
        root                                                      = baseDefinition();
        root["translucency"]                                      = translucency();
        (*root.at("translucency").getIf<Value::Object>())["typo"] = 1;
        REQUIRE(!decode(root, 5).ok());
    };
}

TEST_CASE("asset.graphics.material.spec.versionedVegetation") {
    SUB_CASE("vegetation normal encoding requires a texture and bounded scale") {
        for (const char* encoding : {"tve-rg", "tve-rag", "tve-ag"}) {
            auto root              = baseDefinition();
            root["normalEncoding"] = encoding;
            root["normalTexture"]  = imageUri;
            root["normalScale"]    = -2;
            auto material          = decode(root, 5);
            REQUIRE(material.ok());
            REQUIRE(material.value().surface.normalMode != graphics::PbrNormalMode::TangentXYZ);
            REQUIRE(material.value().surface.normalScale == -2.f);
            root["normalScale"] = 9;
            REQUIRE(!decode(root, 5).ok());
            root.erase("normalTexture");
            root["normalScale"] = 1;
            REQUIRE(!decode(root, 5).ok());
        }
        auto root              = baseDefinition();
        root["normalEncoding"] = "unknown";
        REQUIRE(!decode(root, 5).ok());
    };

    SUB_CASE("motion highlight validates its vector and v6 vegetation retains author parameters") {
        auto root                    = baseDefinition();
        root["motionHighlightColor"] = Value::Array{3.0, .5, 0.0};
        auto material                = decode(root, 5);
        REQUIRE(material.ok());
        REQUIRE(material.value().surface.motionHighlightColor[0] == 3.f);
        root["motionHighlightColor"] = Value::Array{1, -1, 0};
        REQUIRE(!decode(root, 5).ok());
        root["motionHighlightColor"] = Value::Array{1, 0};
        REQUIRE(!decode(root, 5).ok());

        root                      = baseDefinition();
        root["vegetationSurface"] = Value::Object{
            {"sourceFamily", "tve-12"},      {"overlay", .3},           {"wetness", .8},
            {"overlayVariation", .1},        {"overlayProjection", .6}, {"vertexOcclusionAlpha", .34117648},
            {"invertVertexOcclusion", false}};
        REQUIRE(!decode(root, 5).ok());
        root["schemaVersion"] = 6;
        auto vegetation       = decode(root, 6);
        REQUIRE(vegetation.ok());
        REQUIRE(vegetation.value().vegetationSurface.has_value());
        REQUIRE(vegetation.value().vegetationSurface->overlayCoverage == .3f);
        REQUIRE(vegetation.value().vegetationSurface->wetnessCoverage == .8f);
        REQUIRE(vegetation.value().vegetationSurface->albedo[0] == 2.f);
        root.at("vegetationSurface").getIf<Value::Object>()->erase("overlayProjection");
        REQUIRE(!decode(root, 6).ok());
    };
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
