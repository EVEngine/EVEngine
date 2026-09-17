#include "asset/AssetMigration.h"

#include "data/HashFunction.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {

EvaArchive legacyImage(SchemaVersion version = SchemaVersion(2)) {
    const auto package  = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    const auto identity = PersistentId::parse("550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(package.has_value());
    REQUIRE(identity.has_value());
    auto reference = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(reference.ok());
    const std::string definition =
        version.value() == 2
            ? R"({"blob":"assets/550e8400-e29b-41d4-a716-446655440000/source.png","color":{"transfer":"srgb","primaries":"srgb"},"extensionNote":"preserved","encoding":"png","height":1,"rowOrientation":"top-down","schema":"eve.image","schemaVersion":2,"usage":"color","width":1})"
            : std::string("{\"schema\":\"eve.image\",\"schemaVersion\":") + std::to_string(version.value()) + "}";
    EvaManifest manifest;
    manifest.packageId      = *package;
    manifest.packageName    = "migration.image";
    manifest.packageVersion = "1.0.0";
    manifest.assets.push_back({reference.value(),
                               "eve.image",
                               version,
                               "assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
                               "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                               {}});
    manifest.entrypoints.emplace("default", reference.value());
    std::vector<EvaArchiveEntry> entries = {
        {manifest.assets.front().definition, {definition.begin(), definition.end()}},
        {"assets/550e8400-e29b-41d4-a716-446655440000/source.png", {1}},
    };
    // Let the archive builder/parser calculate and enforce the real definition hash.
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")->hash("sha256", definition.data(), definition.size(), digest);
    static constexpr char hex[] = "0123456789abcdef";
    std::string           hash  = "sha256:";
    for (std::size_t index = 0; index < 32; ++index) {
        const auto byte = static_cast<std::uint8_t>(digest.data[index]);
        hash.push_back(hex[byte >> 4]);
        hash.push_back(hex[byte & 15]);
    }
    manifest.assets.front().contentHash = std::move(hash);
    auto bytes                          = buildEvaArchive(manifest, std::move(entries));
    REQUIRE(bytes.ok());
    auto parsed = parseEvaArchive(bytes.value());
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

}  // namespace

TEST_CASE("asset.migration.imageNMinusOneProducesCanonicalV3Candidate") {
    auto migrated = migrateEvaArchive(legacyImage());
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.assets.size(), std::size_t(1));
    CHECK_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(3));
    const auto& bytes = migrated.value().entries.front().bytes;
    auto definition   = Value::fromJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    REQUIRE(definition.ok());
    const auto* object = definition.value().getIf<Value::Object>();
    REQUIRE(object != nullptr);
    CHECK(object->contains("color"));
    CHECK_EQ(object->at("mipCount"), Value(int64_t(1)));
    CHECK_EQ(object->at("extensionNote"), Value("preserved"));
    CHECK_EQ(object->at("schemaVersion"), Value(std::int64_t(3)));
}

TEST_CASE("asset.migration.rejectsUnknownNewVersionWithoutChangingInput") {
    EvaArchive original = legacyImage(SchemaVersion(4));
    const auto before   = original.entries.front().bytes;
    auto       rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::UnknownVersion);
    CHECK_EQ(original.entries.front().bytes, before);
}

TEST_CASE("asset.migration.imageRejectsOutsideWindowAndUnversionedMips") {
    auto old      = legacyImage(SchemaVersion(1));
    auto rejected = migrateEvaArchive(old);
    REQUIRE(!rejected.ok());
    CHECK(rejected.error()->code() == DiagnosticCode::Unsupported);
    auto  original = legacyImage();
    auto& bytes    = original.entries.front().bytes;
    auto  value    = Value::fromJson(std::string(bytes.begin(), bytes.end()));
    REQUIRE(value.ok());
    (*value.value().getIf<Value::Object>())["mipCount"] = Value(int64_t(3));
    auto encoded                                        = value.value().toJson();
    REQUIRE(encoded.ok());
    bytes.assign(encoded.value().begin(), encoded.value().end());
    const auto before  = bytes;
    auto       invalid = migrateEvaArchive(original);
    REQUIRE(!invalid.ok());
    CHECK(invalid.error()->code() == DiagnosticCode::ParseError);
    CHECK(original.entries.front().bytes == before);
}

TEST_CASE("asset.migration.sceneV2PreservesUnknownFieldsAndAddsShadowDefaults") {
    auto original                         = legacyImage(SchemaVersion(2));
    original.manifest.assets.front().type = "eve.scene-template";
    const std::string text =
        R"({"schema":"eve.scene-template","schemaVersion":2,"nodes":[],"renderers":[{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31040","mesh":"asset://018f6f22-2490-7ad2-bf58-4f1dbca31041","material":"asset://018f6f22-2490-7ad2-bf58-4f1dbca31042","enabled":true,"vendorRendererField":17}],"vendorExtension":42})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto migrated = migrateEvaArchive(original);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().manifest.assets.front().schemaVersion == SchemaVersion(3));
    const auto& bytes   = migrated.value().entries.front().bytes;
    auto        decoded = Value::fromJson(std::string(bytes.begin(), bytes.end()));
    REQUIRE(decoded.ok());
    const auto& object = *decoded.value().getIf<Value::Object>();
    REQUIRE_EQ(object.at("vendorExtension").asInt(), std::int64_t(42));
    const auto& renderer = *object.at("renderers").getIf<Value::Array>()->front().getIf<Value::Object>();
    REQUIRE(renderer.at("castShadows").asBool());
    REQUIRE(renderer.at("receiveShadows").asBool());
    REQUIRE_EQ(renderer.at("vendorRendererField").asInt(), std::int64_t(17));
    REQUIRE(original.entries.front().bytes == std::vector<std::uint8_t>(text.begin(), text.end()));
    const std::string invalid =
        R"({"schema":"eve.scene-template","schemaVersion":2,"nodes":[],"renderers":[{"castShadows":false}]})";
    original.entries.front().bytes.assign(invalid.begin(), invalid.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
}

TEST_CASE("asset.migration.materialV14AddsDisabledAlphaToCoverage") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(14);
    const std::string text =
        R"({"schema":"eve.material","schemaVersion":14,"metallic":0.4,"vendorExtension":42,"translucency":{"intensity":0.6},"vegetationAlpha":{"global":0.6,"variation":0.3,"detailFade":true,"glancing":0.2,"camera":0.4,"constant":0.1},"vegetationEmission":{"minimum":0,"maximum":1,"phase":0.8,"global":0.7}})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    const auto ref = original.manifest.assets.front().asset;
    original.manifest.dependencies.push_back(
        {ref, ref, EvaDependencyKind::Editor, "test-schema", {}, "eve.material/14"});
    auto migrated = migrateEvaArchive(original);
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.dependencies.front().expectedType, std::string("eve.material/15"));
    REQUIRE_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(15));
    auto decoded = Value::fromJson(
        std::string(migrated.value().entries.front().bytes.begin(), migrated.value().entries.front().bytes.end()));
    REQUIRE(decoded.ok());
    const auto& object = *decoded.value().getIf<Value::Object>();
    REQUIRE_EQ(object.at("metallic").asDouble(), 0.4);
    REQUIRE_EQ(object.at("vendorExtension").asInt(), int64_t(42));
    REQUIRE_EQ(object.at("schemaVersion").asInt(), int64_t(15));
    REQUIRE(!object.at("alphaToCoverage").asBool());
    REQUIRE_EQ(object.at("translucency").getIf<Value::Object>()->at("intensity").asDouble(), .6);
    REQUIRE_EQ(object.at("vegetationEmission").getIf<Value::Object>()->at("phase").asDouble(), .8);
    REQUIRE(!object.contains("vegetationGradient"));
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}

TEST_CASE("asset.migration.terrainMaterialV2AddsStableImageAssetRefs") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.terrain-material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(2);
    const std::string text =
        R"({"schema":"eve.terrain-material","schemaVersion":2,"layers":[{"name":"grass","diffuseSource":"a","normalSource":"n","weightSource":"w","maskSource":"m","normalConvention":"opengl","tileSizeMeters":8,"tileScaleMeters":[8,8],"tileOffsetMeters":[0,0],"maskRemapMinimum":[0,0,0,0],"maskRemapMaximum":[1,1,1,1],"specular":[0,0,0,0],"metallic":0,"normalScale":1,"smoothness":0}],"holesSource":"h","controlSources":["c0","","",""],"boundsMultiplier":1,"vendorExtension":42})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto migrated = migrateEvaArchive(original);
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(3));
    auto decoded = Value::fromJson(
        std::string(migrated.value().entries.front().bytes.begin(), migrated.value().entries.front().bytes.end()));
    REQUIRE(decoded.ok());
    const auto& object = *decoded.value().getIf<Value::Object>();
    REQUIRE_EQ(object.at("schemaVersion").asInt(), int64_t(3));
    REQUIRE_EQ(object.at("vendorExtension").asInt(), int64_t(42));
    REQUIRE_EQ(object.at("controlAssets").getIf<Value::Array>()->size(), size_t(4));
    REQUIRE(object.at("holesAsset").asString().empty());
    const auto& layer = *object.at("layers").getIf<Value::Array>()->front().getIf<Value::Object>();
    REQUIRE(layer.at("diffuseAsset").asString().empty());
    REQUIRE(layer.at("normalAsset").asString().empty());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.instanceSetV3AddsTerrainDetailAppearanceDefaults") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.instance-set";
    original.manifest.assets.front().schemaVersion = SchemaVersion(3);
    const std::string text =
        R"({"schema":"eve.instance-set","schemaVersion":3,"count":2,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","resourceAsset":""}],"vendorExtension":42})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto migrated = migrateEvaArchive(original);
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(5));
    auto decoded = Value::fromJson(
        std::string(migrated.value().entries.front().bytes.begin(), migrated.value().entries.front().bytes.end()));
    REQUIRE(decoded.ok());
    const auto& object = *decoded.value().getIf<Value::Object>();
    REQUIRE_EQ(object.at("schemaVersion").asInt(), int64_t(5));
    const auto& prototypes = *object.at("prototypes").getIf<Value::Array>();
    REQUIRE_EQ(prototypes.size(), std::size_t(1));
    CHECK_EQ(prototypes.front().getIf<Value::Object>()->at("resourceAsset").asString(), std::string());
    REQUIRE_EQ(prototypes.front().getIf<Value::Object>()->at("bendFactor").asDouble(), 0.0);
    REQUIRE(prototypes.front().getIf<Value::Object>()->at("useDensityScaling").asBool());
    REQUIRE_EQ(object.at("wavingGrass").getIf<Value::Object>()->at("strength").asDouble(), 0.0);
    REQUIRE_EQ(object.at("vendorExtension").asInt(), int64_t(42));
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialV12RejectsUnversionedEmissionAtomically") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(12);
    const std::string text =
        R"({"schema":"eve.material","schemaVersion":12,"vegetationEmission":{"minimum":0,"maximum":1,"phase":1,"global":1}})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialV13RejectsUnversionedGradientAtomically") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(13);
    const std::string text =
        R"({"schema":"eve.material","schemaVersion":13,"vegetationGradient":{"colorOne":[1,1,1],"colorTwo":[1,1,1],"minimum":0,"maximum":1}})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialV13RejectsUnversionedOcclusionColorAtomically") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(13);
    const std::string text =
        R"({"schema":"eve.material","schemaVersion":13,"vegetationSurface":{"vertexOcclusionColor":[1,1,1]}})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialV13RejectsUnversionedBackfaceNormalModeAtomically") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(13);
    const std::string text =
        R"({"schema":"eve.material","schemaVersion":13,"vegetationSurface":{"backfaceNormalMode":2}})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialV13RejectsUnversionedCullModeAtomically") {
    auto original                                  = legacyImage();
    original.manifest.assets.front().type          = "eve.material";
    original.manifest.assets.front().schemaVersion = SchemaVersion(13);
    const std::string text = R"({"schema":"eve.material","schemaVersion":13,"cullMode":"front"})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    auto rejected = migrateEvaArchive(original);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
TEST_CASE("asset.migration.materialRejectsUnversionedVegetationSurfaceAndOutsideWindow") {
    auto original                         = legacyImage();
    original.manifest.assets.front().type = "eve.material";
    for (const auto version : {5, 6, 7, 10}) {
        original.manifest.assets.front().schemaVersion = SchemaVersion(version);
        REQUIRE(!migrateEvaArchive(original).ok());
    }
    original.manifest.assets.front().schemaVersion = SchemaVersion(8);
    {
        for (const auto field : {"vegetationAlpha", "vegetationFields"}) {
            const std::string text =
                std::string("{\"schema\":\"eve.material\",\"schemaVersion\":8,\"") + field + "\":{}}";
            original.entries.front().bytes.assign(text.begin(), text.end());
            auto rejected = migrateEvaArchive(original);
            REQUIRE(!rejected.ok());
            REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
        }
    }
    original.manifest.assets.front().schemaVersion = SchemaVersion(7);
    for (const auto field : {"vegetationDetail"}) {
        const std::string text = std::string("{\"schema\":\"eve.material\",\"schemaVersion\":7,\"") + field + "\":{}}";
        original.entries.front().bytes.assign(text.begin(), text.end());
        auto rejected = migrateEvaArchive(original);
        REQUIRE(!rejected.ok());
        REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
    }
}
TEST_CASE("asset.migration.meshV1PreservesIdentityAndUvMetadata") {
    auto original                         = legacyImage(SchemaVersion(1));
    original.manifest.assets.front().type = "eve.mesh";
    const std::string text = R"({"schema":"eve.mesh","schemaVersion":1,"texcoord0":true,"vendorExtension":42})";
    original.entries.front().bytes.assign(text.begin(), text.end());
    const auto ref = original.manifest.assets.front().asset;
    original.manifest.dependencies.push_back({ref, ref, EvaDependencyKind::Editor, "mesh", {}, "eve.mesh/1"});
    auto migrated = migrateEvaArchive(original);
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().manifest.assets.front().asset, ref);
    REQUIRE_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(2));
    REQUIRE_EQ(migrated.value().manifest.dependencies.front().expectedType, std::string("eve.mesh/2"));
    auto value = Value::fromJson(
        std::string(migrated.value().entries.front().bytes.begin(), migrated.value().entries.front().bytes.end()));
    REQUIRE(value.ok());
    const auto& object = *value.value().getIf<Value::Object>();
    REQUIRE(!object.contains("texcoord0"));
    REQUIRE_EQ(object.at("texcoordSets").getIf<Value::Array>()->at(0).asInt(), int64_t(0));
    REQUIRE_EQ(object.at("vendorExtension").asInt(), int64_t(42));
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
