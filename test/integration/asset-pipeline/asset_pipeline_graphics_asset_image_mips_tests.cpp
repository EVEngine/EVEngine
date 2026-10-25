// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "asset-pipeline",
  "cases": {
    "asset-files": [
      "asset.import.unityTerrainPrefabUsesGuidFileIdAndCanonicalCoordinates",
      "asset.packageStore.reopensBeforeAtomicPublishAndFailureKeepsOldEvpack",
      "asset.packageStore.publishesAndReopensEva",
      "asset.import.realGlbDecodesCanonicalMeshAndTraversesRuntimePack",
      "asset.import.animationFixtureBecomesCanonicalEvaAndTwoRuntimeTargets",
      "asset.import.animationFixtureRejectsDeclaredKeyCountMismatch",
      "asset.import.structuredModelsScadCompilerAbsent",
      "asset.import.structuredModelsScadCompilerTimeout"
    ],
    "render-offscreen": [
      "asset.import.unityTexture3dSurvivesCookAndVulkanUpload"
    ]
  },
  "module": "asset",
  "modules": [
    "asset",
    "asset_graphics",
    "asset_import"
  ],
  "origins": [
    "asset_cooker.cpp",
    "asset_graphics_image_mips.cpp",
    "asset_graphics_loader.cpp",
    "asset_image_mips.cpp",
    "asset_import.cpp",
    "asset_import_animation.cpp",
    "asset_import_gltf_animation.cpp",
    "asset_import_gltf_material.cpp",
    "asset_import_structured_models.cpp",
    "asset_import_tga.cpp",
    "asset_import_tiff.cpp",
    "asset_import_unity_collection.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_cooker)
#include "PrivateTestScope.h"
#include "asset/AssetCooker.h"
#include "asset/RuntimeDefinition.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_cooker.cpp
namespace test_asset_cooker {
using namespace eve;
using namespace eve::asset;

namespace {

EvaArchive sourceArchive() {
    constexpr std::string_view json     = R"json({
      "schema":"eve.asset-archive","schemaVersion":1,
      "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
      "packageName":"cook.test","packageVersion":"1.0.0","unknownFields":"preserve",
      "assets":[{
        "asset":"asset://550e8400-e29b-41d4-a716-446655440000",
        "type":"eve.mesh","schemaVersion":1,
        "definition":"assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
        "contentHash":"sha256:19e0e9f91631c94f804a0523113a21b29900d08e6e67ff3f41dc47214b5b4e4c",
        "tags":[]
      }],
      "dependencies":[],"entrypoints":{"default":"asset://550e8400-e29b-41d4-a716-446655440000"},
      "provenance":{"license":{"redistribution":"unknown"}}
    })json";
    auto                       manifest = parseEvaManifest(json);
    REQUIRE(manifest.ok());
    std::vector<EvaArchiveEntry> entries = {
        {"assets/550e8400-e29b-41d4-a716-446655440000/asset.json",
         {'{', '"', 's', 'c', 'h', 'e', 'm', 'a', '"', ':', '"', 'e', 'v', 'e', '.', 'm', 'e', 's', 'h', '"',
          ',', '"', 's', 'c', 'h', 'e', 'm', 'a', 'V', 'e', 'r', 's', 'i', 'o', 'n', '"', ':', '1', '}'}},
        {"assets/550e8400-e29b-41d4-a716-446655440000/stream/lod0.bin", {1, 2, 3}}};
    auto bytes = buildEvaArchive(manifest.value(), std::move(entries));
    REQUIRE(bytes.ok());
    auto archive = parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    return std::move(archive).takeValue();
}

AssetCookProfile profile(std::string quality = "high") {
    return {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", std::move(quality), {}},
            CookPublication::LocalInspection,
            16};
}

}  // namespace

TEST_CASE("asset.cook.evaToEvpackIsDeterministicAndPreservesStreamChunks") {
    auto first  = cookEvaToEvpack(sourceArchive(), profile());
    auto second = cookEvaToEvpack(sourceArchive(), profile());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().buildId, second.value().buildId);
    CHECK_EQ(first.value().bytes, second.value().bytes);
    CHECK_EQ(first.value().chunkCount, std::uint32_t(2));

    auto parsed = parseEvpack(first.value().bytes);
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().chunks().size(), std::size_t(2));
    CHECK_EQ(static_cast<std::uint16_t>(parsed.value().chunks()[0].kind),
             static_cast<std::uint16_t>(EvpackChunkKind::Definition));
    CHECK_EQ(static_cast<std::uint16_t>(parsed.value().chunks()[1].kind),
             static_cast<std::uint16_t>(EvpackChunkKind::Stream));
    CHECK_EQ(parsed.value().chunks()[1].chunkId, std::uint32_t(1));
    auto definition = parsed.value().decodeChunk(0, 4096);
    REQUIRE(definition.ok());
    auto metadata = decodeRuntimeDefinition(definition.value());
    REQUIRE(metadata.ok());
    CHECK(metadata.value().isObject());
}

TEST_CASE("asset.cook.capabilityProfileParticipatesInBuildIdentity") {
    auto high   = cookEvaToEvpack(sourceArchive(), profile("high"));
    auto medium = cookEvaToEvpack(sourceArchive(), profile("medium"));
    REQUIRE(high.ok());
    REQUIRE(medium.ok());
    CHECK(high.value().buildId != medium.value().buildId);
}

TEST_CASE("asset.cook.stableTargetsCoverAllSixRuntimeOperatingSystems") {
    const std::vector<std::pair<std::string, std::string>> targets = {
        {"windows-x86_64-vulkan", "windows"}, {"linux-x86_64-vulkan", "linux"}, {"macos-arm64-vulkan", "macos"},
        {"android-arm64-vulkan", "android"},  {"ios-arm64-vulkan", "ios"},      {"web-wasm32-webgpu", "web"}};
    for (const auto& [name, os] : targets) {
        auto resolved = assetCookProfileForTarget(name);
        REQUIRE(resolved.ok());
        CHECK_EQ(resolved.value().variant.os, os);
        auto cooked = cookEvaToEvpack(sourceArchive(), resolved.value());
        REQUIRE(cooked.ok());
        auto pack = parseEvpack(cooked.value().bytes);
        REQUIRE(pack.ok());
        const auto& variant  = resolved.value().variant;
        auto        selected = selectEvpackVariant(pack.value(), {variant.os,
                                                                  variant.arch,
                                                                  variant.graphics,
                                                                  variant.textureFamilies,
                                                                  {variant.shaderFormat},
                                                                  {variant.quality},
                                                                  variant.features});
        REQUIRE(selected.ok());
        CHECK_EQ(selected.value().index, std::uint32_t(0));
    }
    auto unknown = assetCookProfileForTarget("amiga-m68k-vulkan");
    REQUIRE(!unknown.ok());
    CHECK_EQ(unknown.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.cook.publicationRejectsUnknownLicenseWithoutMutatingSource") {
    EvaArchive source         = sourceArchive();
    auto       publicProfile  = profile();
    publicProfile.publication = CookPublication::PublicDistribution;
    auto rejected             = cookEvaToEvpack(source, publicProfile);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::PreconditionViolation);
    CHECK_EQ(source.manifest.packageName, std::string("cook.test"));
}

TEST_CASE("asset.cook.optionalFallbackPolicyRemainsObservableAtRuntime") {
    EvaArchive source   = sourceArchive();
    auto       external = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440099");
    REQUIRE(external.ok());
    source.manifest.dependencies.push_back(
        {source.manifest.assets.front().asset,
         std::move(external).takeValue(),
         EvaDependencyKind::RuntimeOptional,
         "detail",
         {},
         "eve.image/2",
         {{"behavior", Value("use-default")}, {"observableCode", Value("mesh.detail-defaulted")}}});
    auto cooked = cookEvaToEvpack(source, profile());
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    auto definition = pack.value().decodeChunk(0, 4096);
    REQUIRE(definition.ok());
    auto metadata = decodeRuntimeDefinition(definition.value());
    REQUIRE(metadata.ok());
    const auto* root = metadata.value().getIf<Value::Object>();
    REQUIRE(root != nullptr);
    const auto found = root->find("dependencyFallbacks");
    REQUIRE(found != root->end());
    const auto* policies = found->second.getIf<Value::Array>();
    REQUIRE(policies != nullptr);
    REQUIRE_EQ(policies->size(), std::size_t(1));
    const auto* policy = policies->front().getIf<Value::Object>();
    REQUIRE(policy != nullptr);
    CHECK_EQ(policy->at("path").asString(), std::string("detail"));
    CHECK_EQ(policy->at("to").asString(), std::string("asset://550e8400-e29b-41d4-a716-446655440099"));
}
}  // namespace test_asset_cooker
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_graphics_image_mips)
#include "PrivateTestScope.h"
#include "asset/CanonicalImageCook.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_graphics_image_mips.cpp
namespace test_asset_graphics_image_mips {
using namespace eve;
namespace {
class MipFactory : public graphics::IImageResourceFactory {
public:
    Result<graphics::Texture*> uploadRgba8(uint32_t, uint32_t, const uint8_t*, bool) override {
        ++baseUploads;
        return Result<graphics::Texture*>::success(&texture);
    }
    Result<void>      releaseImage(graphics::Texture*) override { return Result<void>::success(); }
    int               baseUploads = 0;
    graphics::Texture texture;
};
class RecordingMips final : public MipFactory {
public:
    Result<graphics::Texture*> uploadRgba8MipChain(uint32_t w, uint32_t h, uint32_t levels,
                                                   std::span<const uint8_t> bytes) override {
        ++mipUploads;
        CHECK(w == 4);
        CHECK(h == 2);
        CHECK(levels == 3);
        pixels.assign(bytes.begin(), bytes.end());
        return Result<graphics::Texture*>::success(&texture);
    }
    int                  mipUploads = 0;
    std::vector<uint8_t> pixels;
};
struct Fixture {
    AssetRef                             ref;
    std::shared_ptr<const asset::Evpack> pack;
    std::vector<uint8_t>                 pixels;
};
Fixture fixture(bool mismatch = false) {
    auto ref = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(ref.ok());
    const std::string json =
        R"({"schema":"eve.image","schemaVersion":3,"width":4,"height":2,"encoding":"rgba8-mips","color":{"transfer":"linear"},"mipCount":3})";
    std::vector<uint8_t> pixels((8 + 2 + 1) * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i * 3);
    auto cooked =
        asset::cookCanonicalImageRgba8({reinterpret_cast<const uint8_t*>(json.data()), json.size()}, pixels, 4096);
    REQUIRE(cooked.ok());
    auto definition = Value::fromJson(std::string_view(reinterpret_cast<const char*>(cooked.value().definition.data()),
                                                       cooked.value().definition.size()));
    REQUIRE(definition.ok());
    if (mismatch) (*definition.value().getIf<Value::Object>())["mipCount"] = Value(int64_t(2));
    auto encoded = asset::encodeRuntimeDefinition(definition.value());
    REQUIRE(encoded.ok());
    asset::EvpackBuild build;
    build.packageId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks    = {{ref.value().id(),
                        "eve.image",
                        SchemaVersion(3),
                        0,
                        asset::EvpackChunkKind::Definition,
                        0,
                        asset::EvpackCodec::None,
                        8,
                        {},
                        std::move(encoded).takeValue()},
                       {ref.value().id(),
                        "eve.image",
                        SchemaVersion(3),
                        0,
                        asset::EvpackChunkKind::Bulk,
                        1,
                        asset::EvpackCodec::None,
                        8,
                        {},
                        std::move(cooked).takeValue().bulk}};
    auto bytes      = asset::buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto pack = asset::parseEvpack(bytes.value());
    REQUIRE(pack.ok());
    return {ref.value(), std::make_shared<const asset::Evpack>(std::move(pack).takeValue()), std::move(pixels)};
}
const asset::EvpackCapabilities capabilities{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
}  // namespace
TEST_CASE("asset.graphics.imageMipsCookPackLoadPreservesPayload") {
    auto                              data = fixture();
    asset::EvpackResourceReader       reader(data.pack);
    RecordingMips                     factory;
    asset_graphics::EvpackImageLoader loader(reader, factory);
    auto                              result = loader.load(data.ref, capabilities);
    REQUIRE(result.ok());
    CHECK(factory.mipUploads == 1);
    CHECK(factory.baseUploads == 0);
    CHECK(factory.pixels == data.pixels);
    REQUIRE(factory.releaseImage(result.value().texture).ok());
}
TEST_CASE("asset.graphics.imageMipsRejectMetadataMismatchBeforeUpload") {
    auto                              data = fixture(true);
    asset::EvpackResourceReader       reader(data.pack);
    RecordingMips                     factory;
    asset_graphics::EvpackImageLoader loader(reader, factory);
    REQUIRE(!loader.load(data.ref, capabilities).ok());
    CHECK(factory.mipUploads == 0);
    CHECK(factory.baseUploads == 0);
}
TEST_CASE("asset.graphics.imageMipsAbsentProviderDoesNotUseBaseUpload") {
    auto                              data = fixture();
    asset::EvpackResourceReader       reader(data.pack);
    MipFactory                        factory;
    asset_graphics::EvpackImageLoader loader(reader, factory);
    auto                              result = loader.load(data.ref, capabilities);
    REQUIRE(!result.ok());
    CHECK(result.error()->code() == DiagnosticCode::Unsupported);
    CHECK(factory.baseUploads == 0);
}
TEST_CASE("asset.image.decodeOwnsValidatedMipPayloadWithoutGpuProvider") {
    auto                        data = fixture();
    asset::EvpackResourceReader reader(data.pack);
    auto                        decoded = asset::decodeEvpackImage(reader, data.ref, capabilities);
    REQUIRE(decoded.ok());
    CHECK(decoded.value().width == 4);
    CHECK(decoded.value().height == 2);
    CHECK(decoded.value().levels == 3);
    CHECK(!decoded.value().srgb);
    CHECK(decoded.value().pixels == data.pixels);
}
TEST_CASE("asset.procgen.terrainAtlasesResolveFiveLayersAcrossTwoControlGroups") {
    auto                                 data = fixture();
    asset::EvpackResourceReader          reader(data.pack);
    asset_procgen::LoadedTerrainMaterial material{data.ref};
    material.layers.resize(5);
    for (auto& layer : material.layers) {
        layer.normalConvention = "opengl";
        layer.diffuseAsset     = data.ref;
        layer.normalAsset      = data.ref;
        layer.maskAsset        = data.ref;
    }
    material.layers[0].normalConvention = "directx";
    material.layers[0].maskRemapMinimum = {.2f, 0.f, 0.f, 0.f};
    material.layers[0].maskRemapMaximum = {.6f, 1.f, 1.f, 1.f};
    material.controlAssets[0]           = data.ref;
    material.controlAssets[1]           = data.ref;
    material.holesAsset                 = data.ref;
    auto atlases                        = asset_procgen::buildTerrainMaterialAtlases(reader, material, capabilities);
    REQUIRE(atlases.ok());
    REQUIRE_EQ(atlases.value().groups.size(), std::size_t(2));
    CHECK(atlases.value().groups[0].firstLayer == 0);
    CHECK(atlases.value().groups[0].layerCount == 4);
    CHECK(atlases.value().groups[1].firstLayer == 4);
    CHECK(atlases.value().groups[1].layerCount == 1);
    CHECK(atlases.value().groups[0].tileWidth == 4);
    CHECK(atlases.value().groups[0].tileHeight == 2);
    CHECK(atlases.value().groups[0].albedo.pixels[1] == data.pixels[1]);
    CHECK(atlases.value().groups[0].normal.pixels[1] == std::uint8_t(255 - data.pixels[1]));
    CHECK(std::abs(int(atlases.value().groups[0].mask.pixels[0]) - 51) <= 1);
    CHECK(atlases.value().holes.width == 4);
}

TEST_CASE("asset.procgen.terrainAtlasesRequireControlForEveryAdditionalGroup") {
    auto                                 data = fixture();
    asset::EvpackResourceReader          reader(data.pack);
    asset_procgen::LoadedTerrainMaterial material{data.ref};
    material.layers.resize(5);
    for (auto& layer : material.layers) layer.normalConvention = "opengl";
    auto rejected = asset_procgen::buildTerrainMaterialAtlases(reader, material, capabilities);
    REQUIRE(!rejected.ok());
    CHECK(rejected.error()->code() == DiagnosticCode::NotFound);
}
TEST_CASE("asset.procgen.terrainGpuGroupBindsSecondControlLayerSet") {
    auto ref = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(ref.ok());
    asset_procgen::LoadedTerrainMaterial material{ref.value()};
    material.layers.resize(5);
    material.layers[4].tileScaleMeters  = {8, 4};
    material.layers[4].tileOffsetMeters = {2, 1};
    material.layers[4].metallic         = .25f;
    material.layers[4].normalScale      = -2.f;
    material.layers[4].smoothness       = .75f;
    graphics::Texture                    textures[9];
    asset_procgen::TerrainMaterialGpuSet gpu;
    gpu.groups = {{&textures[0], &textures[1], &textures[2], &textures[3]},
                  {&textures[4], &textures[5], &textures[6], &textures[7]}};
    gpu.holes  = &textures[8];
    graphics::Shader shader;
    for (int slot = 0; slot < 4; ++slot) shader.declareVec4("terrainLayer" + std::to_string(slot) + "ST");
    shader.declareVec4("terrainMetallic");
    shader.declareVec4("terrainNormalScale");
    shader.declareVec4("terrainSmoothness");
    shader.declareVec4("terrainFeatures");
    auto bound = asset_procgen::bindTerrainMaterialGroup(shader, gpu, material, 1);
    REQUIRE(bound.ok());
    CHECK(shader.meshTexture(0) == &textures[4]);
    CHECK(shader.meshTexture(3) == &textures[8]);
    float st[4]{};
    CHECK_EQ(shader.getFromVar("terrainLayer0ST", st, sizeof(st)), static_cast<int>(sizeof(st)));
    CHECK(std::abs(st[0] - .125f) < .0001f);
    CHECK(std::abs(st[1] - .25f) < .0001f);
    CHECK(std::abs(st[2] - .25f) < .0001f);
    CHECK(std::abs(st[3] - .25f) < .0001f);
}
}  // namespace test_asset_graphics_image_mips
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_graphics_loader)
#include "PrivateTestScope.h"
#include <bit>
#include "asset/CanonicalMesh.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/IMeshResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_graphics_loader.cpp
namespace test_asset_graphics_loader {
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_graphics;

namespace {

PersistentId persistentId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

AssetRef assetRef(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& bytes, float value) { put32(bytes, std::bit_cast<std::uint32_t>(value)); }

std::vector<std::uint8_t> triangleBlob() {
    std::vector<std::uint8_t> bytes = {'E', 'V', 'M', 'E', 'S', 'H', 0, 1};
    put32(bytes, 3);
    put32(bytes, 3);
    put32(bytes, 3);
    put32(bytes, 0);
    const float vertices[][8] = {{0, 0, 0, 0, 0, 1, 0, 0}, {1, 0, 0, 0, 0, 1, 1, 0}, {0, 1, 0, 0, 0, 1, 0, 1}};
    for (const auto& vertex : vertices)
        for (float value : vertex) putFloat(bytes, value);
    put32(bytes, 0);
    put32(bytes, 1);
    put32(bytes, 2);
    return bytes;
}

std::shared_ptr<const Evpack> makePack(std::vector<std::uint8_t> mesh) {
    const auto  id = persistentId("550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId   = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId     = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants    = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}}};
    const auto schema = mesh.size() > 7 && mesh[7] == 3 ? SchemaVersion(3) : SchemaVersion(1);
    build.chunks = {{id, "eve.mesh", schema, 0, EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {}, {'{', '}'}},
                    {id, "eve.mesh", schema, 0, EvpackChunkKind::Bulk, 1, EvpackCodec::None, 16, {}, std::move(mesh)}};
    auto bytes   = buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value());
    REQUIRE(parsed.ok());
    return std::make_shared<const Evpack>(std::move(parsed).takeValue());
}

class RecordingMeshFactory final : public eve::graphics::IMeshResourceFactory {
public:
    Result<eve::graphics::Mesh*> uploadMesh(const float* positions, const float* normals, const float* texcoords,
                                            int vertexCount, const std::uint32_t* indices, int indexCount) override {
        ++uploads;
        observedPositions.assign(positions, positions + vertexCount * 3);
        observedNormals.assign(normals, normals + vertexCount * 3);
        observedTexcoords.assign(texcoords, texcoords + vertexCount * 2);
        observedIndices.assign(indices, indices + indexCount);
        if (reject)
            return Result<eve::graphics::Mesh*>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected upload rejection"));
        return Result<eve::graphics::Mesh*>::success(reinterpret_cast<eve::graphics::Mesh*>(&token));
    }

    Result<eve::graphics::Mesh*> uploadMeshColored(const float* positions, const float* normals, const float* texcoords,
                                                   const float* colors, int vertexCount, const std::uint32_t* indices,
                                                   int indexCount) override {
        auto result = uploadMesh(positions, normals, texcoords, vertexCount, indices, indexCount);
        if (result) observedColors.assign(colors, colors + vertexCount * 4);
        return result;
    }

    Result<void> releaseMesh(eve::graphics::Mesh*) override { return Result<void>::success(); }

    bool                       reject  = false;
    int                        uploads = 0;
    std::vector<float>         observedPositions;
    std::vector<float>         observedNormals;
    std::vector<float>         observedTexcoords;
    std::vector<float>         observedColors;
    std::vector<std::uint32_t> observedIndices;

private:
    std::uint64_t token = 0;
};

class RecordingImageFactory final : public eve::graphics::IImageResourceFactory {
public:
    Result<eve::graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
        ++uploads;
        if (reject)
            return Result<eve::graphics::Texture*>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected image upload rejection"));
        return Result<eve::graphics::Texture*>::success(reinterpret_cast<eve::graphics::Texture*>(&token));
    }
    Result<void> releaseImage(eve::graphics::Texture*) override { return Result<void>::success(); }
    bool         reject  = false;
    int          uploads = 0;

private:
    std::uint64_t token = 0;
};

std::shared_ptr<const Evpack> makeImagePack(std::vector<std::uint8_t> image) {
    const auto  id = persistentId("550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks    = {
        {id, "eve.image", SchemaVersion(2), 0, EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {}, {'{', '}'}},
        {id, "eve.image", SchemaVersion(2), 0, EvpackChunkKind::Bulk, 1, EvpackCodec::None, 8, {}, std::move(image)}};
    auto bytes = buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value());
    REQUIRE(parsed.ok());
    return std::make_shared<const Evpack>(std::move(parsed).takeValue());
}

std::vector<std::uint8_t> onePixelEvimg() {
    std::vector<std::uint8_t> bytes = {'E', 'V', 'I', 'M', 'G', 0, 1, 0};
    put32(bytes, 1);
    put32(bytes, 1);
    put32(bytes, 0);
    put32(bytes, 0);
    bytes.insert(bytes.end(), {10, 20, 30, 255});
    return bytes;
}

const EvpackCapabilities capabilities = {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}};

}  // namespace

TEST_CASE("asset.graphics.validatesCanonicalMeshBeforeBackendUpload") {
    EvpackResourceReader reader(makePack(triangleBlob()));
    RecordingMeshFactory factory;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().mesh != nullptr);
    CHECK_EQ(factory.uploads, 1);
    CHECK_EQ(factory.observedPositions.size(), std::size_t(9));
    CHECK_EQ(factory.observedNormals.size(), std::size_t(9));
    CHECK_EQ(factory.observedTexcoords.size(), std::size_t(6));
    CHECK_EQ(factory.observedIndices, std::vector<std::uint32_t>({0, 1, 2}));
}

TEST_CASE("asset.graphics.uploadsCanonicalVertexColorsWithoutDroppingThem") {
    asset::CanonicalMeshData mesh;
    mesh.positions    = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals      = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.texcoords[0] = {0, 0, 1, 0, 0, 1};
    mesh.colors       = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
    mesh.indices      = {0, 1, 2};
    auto encoded      = encodeCanonicalMesh(mesh);
    REQUIRE(encoded.ok());
    EvpackResourceReader reader(makePack(std::move(encoded).takeValue()));
    RecordingMeshFactory factory;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    CHECK_EQ(factory.observedColors, mesh.colors);
}

TEST_CASE("asset.graphics.invalidMeshNeverReachesBackend") {
    auto bytes   = triangleBlob();
    bytes.back() = 9;
    EvpackResourceReader reader(makePack(std::move(bytes)));
    RecordingMeshFactory factory;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::ParseError);
    CHECK_EQ(factory.uploads, 0);
}

TEST_CASE("asset.graphics.backendRejectionIsStructuredFailure") {
    EvpackResourceReader reader(makePack(triangleBlob()));
    RecordingMeshFactory factory;
    factory.reject = true;
    EvpackGraphicsLoader loader(reader, factory);
    auto loaded = loader.loadMesh(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::Failed);
    CHECK_EQ(factory.uploads, 1);
}

TEST_CASE("asset.graphics.imageValidationPrecedesTextureUpload") {
    auto invalid = onePixelEvimg();
    invalid.pop_back();
    EvpackResourceReader  reader(makeImagePack(std::move(invalid)));
    RecordingImageFactory factory;
    EvpackImageLoader     loader(reader, factory);
    auto                  loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                                               {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::InvalidArgument);
    CHECK_EQ(factory.uploads, 0);
}

TEST_CASE("asset.graphics.imageBackendRejectionIsStructuredFailure") {
    EvpackResourceReader  reader(makeImagePack(onePixelEvimg()));
    RecordingImageFactory factory;
    factory.reject = true;
    EvpackImageLoader loader(reader, factory);
    auto              loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440000"),
                                           {"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(!loaded.ok());
    CHECK_EQ(loaded.error()->code(), DiagnosticCode::Failed);
    CHECK_EQ(factory.uploads, 1);
}
}  // namespace test_asset_graphics_loader
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_image_mips)
#include "PrivateTestScope.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include "asset/AssetCooker.h"
#include "asset/AssetMigration.h"
#include "asset/AssetPackageStore.h"
#include "asset/CanonicalImageCook.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/VegetationAsset.h"
#include "asset/import/TerrainImporter.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "asset/import/VegetationPreset.h"
#include "asset/physics/EvpackClothModel.h"
#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/EvpackTerrainLoader.h"
#include "asset/procgen/EvpackTerrainMaterialLoader.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorDocumentService.h"
#include "physics/cloth/Cloth3D.h"
#include "procgen/editing/HeightmapTarget.h"
#include "procgen/heightmap_target/TerrainDocumentCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_image_mips.cpp
namespace test_asset_image_mips {
// asset_image_mips.cpp
TEST_CASE("asset.image.explicit_mip_cook_preserves_levels_and_rejects_malformed_chains") {
    const std::string prefix =
        R"({"schema":"eve.image","schemaVersion":3,"width":7,"height":3,"encoding":"rgba8-mips","color":{"transfer":"linear"},"mipCount":)";
    std::vector<uint8_t> pixels((7 * 3 + 3 + 1) * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i);
    auto cook = [&](const std::string& definition, std::span<const uint8_t> bytes, uint64_t budget) {
        return eve::asset::cookCanonicalImageRgba8(
            {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()}, bytes, budget);
    };
    auto result = cook(prefix + "3}", pixels, pixels.size() + 28);
    REQUIRE(result.ok());
    const auto& bulk = result.value().bulk;
    REQUIRE(bulk.size() == pixels.size() + 28);
    CHECK(bulk[6] == 2);
    CHECK(bulk[8] == 7);
    CHECK(bulk[12] == 3);
    CHECK(bulk[20] == 3);
    CHECK(std::all_of(bulk.begin() + 24, bulk.begin() + 28, [](uint8_t v) { return v == 0; }));
    CHECK(std::equal(pixels.begin(), pixels.end(), bulk.begin() + 28));
    REQUIRE(!cook(prefix + "2}", pixels, 4096).ok());
    REQUIRE(!cook(prefix + "0}", pixels, 4096).ok());
    REQUIRE(!cook(prefix + "3}", std::span<const uint8_t>(pixels).first(pixels.size() - 1), 4096).ok());
    REQUIRE(!cook(prefix + "3}", pixels, pixels.size() + 27).ok());
    auto legacy = prefix;
    legacy.replace(legacy.find("\"schemaVersion\":3"), 17, "\"schemaVersion\":2");
    REQUIRE(!cook(legacy + "3}", pixels, 4096).ok());
    auto srgb = prefix;
    srgb.replace(srgb.find("linear"), 6, "srgb");
    auto srgbResult = cook(srgb + "3}", pixels, 4096);
    REQUIRE(srgbResult.ok());
    CHECK(srgbResult.value().bulk[28 + 1] != pixels[1]);
    CHECK_EQ(srgbResult.value().bulk[28 + 3], pixels[3]);
}

// asset_import_unity.cpp
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

ImportPackageIdentity unityIdentity() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
            "unity.terrain-prefab",
            "1.0.0",
            {{"provider", Value("unity-project")},
             {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}

AssetRef reference(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

std::shared_ptr<const Evpack> runtimePack(std::string type, std::string definition, std::vector<std::uint8_t> bulk = {},
                                          SchemaVersion version = SchemaVersion(1)) {
    const auto  asset = reference("asset://550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId       = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId         = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants        = {{"android", "arm64", "vulkan", {"astc"}, "spirv-1.6", "high", {}}};
    auto parsedDefinition = Value::fromJson(definition);
    REQUIRE(parsedDefinition.ok());
    auto encodedDefinition = encodeRuntimeDefinition(parsedDefinition.value());
    REQUIRE(encodedDefinition.ok());
    build.chunks.push_back({asset.id(),
                            type,
                            version,
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(encodedDefinition).takeValue()});
    if (!bulk.empty())
        build.chunks.push_back(
            {asset.id(), type, version, 0, EvpackChunkKind::Bulk, 1, EvpackCodec::None, 8, {}, std::move(bulk)});
    auto encoded = buildEvpack(std::move(build));
    REQUIRE(encoded.ok());
    auto parsed = parseEvpack(encoded.value());
    REQUIRE(parsed.ok());
    return std::make_shared<const Evpack>(std::move(parsed).takeValue());
}

}  // namespace

TEST_CASE("asset.import.unityTerrainPrefabUsesGuidFileIdAndCanonicalCoordinates") {
    constexpr std::string_view terrain = R"yaml(%YAML 1.1
--- !u!156 &15600000
TerrainData:
  m_HeightmapResolution: 3
  m_HeightmapScale: {x: 2, y: 100, z: 2}
  m_Heights: 00000040008000c0ffff008000400000ffff
  m_HolesTexture: {fileID: 2800000, guid: 11112222333344445555666677778888, type: 3}
  m_AlphamapTextures:
  - {fileID: 2800000, guid: 99990000aaaabbbbccccddddeeeeffff, type: 3}
  m_TerrainLayers:
  - {fileID: 11400000, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 2}
  m_TreePrototypes:
  - prefab: {fileID: 100100000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 3}
  m_TreeInstances:
  - position: {x: 0.25, y: 0.5, z: 0.75}
    widthScale: 1.5
    heightScale: 2
    rotation: 1.5707963
    prototypeIndex: 0
  m_DetailPrototypes: []
)yaml";
    constexpr std::string_view layer   = R"yaml(%YAML 1.1
--- !u!114 &11400000
TerrainLayer:
  m_Name: Grass
  m_DiffuseTexture: {fileID: 2800000, guid: cccccccccccccccccccccccccccccccc, type: 3}
  m_NormalMapTexture: {fileID: 2800000, guid: dddddddddddddddddddddddddddddddd, type: 3}
  m_MaskMapTexture: {fileID: 2800000, guid: 1234567890abcdef1234567890abcdef, type: 3}
  m_MaskMapRemapMin: {x: 0.1, y: 0.2, z: 0.3, w: 0.4}
  m_MaskMapRemapMax: {x: 0.6, y: 0.7, z: 0.8, w: 0.9}
  m_Specular: {x: 0.11, y: 0.12, z: 0.13, w: 1}
  m_Metallic: 0.25
  m_NormalScale: -2
  m_Smoothness: 0.75
  m_TileSize: {x: 8, y: 12}
  m_TileOffset: {x: 2, y: 3}
)yaml";
    constexpr std::string_view prefab  = R"yaml(%YAML 1.1
--- !u!1 &1000
GameObject:
  m_Name: TerrainRoot
--- !u!4 &4000
Transform:
  m_GameObject: {fileID: 1000}
  m_LocalRotation: {x: 0, y: 0.7071068, z: 0, w: 0.7071068}
  m_LocalPosition: {x: 1, y: 2, z: 3}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Father: {fileID: 0}
--- !u!114 &11400000
MonoBehaviour:
  m_GameObject: {fileID: 1000}
  m_Script: {fileID: 11500000, guid: eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee, type: 3}
)yaml";
    UnityProjectImportRequest  request;
    request.package         = unityIdentity();
    request.terrainDataPath = "Assets/Terrain/TerrainData.asset";
    request.prefabPath      = "Assets/Terrain/Terrain.prefab";
    request.files           = {
        {request.terrainDataPath, bytes(terrain)},
        {request.terrainDataPath + ".meta", bytes("fileFormatVersion: 2\nguid: 11111111111111111111111111111111\n")},
        {"Assets/Terrain/Grass.terrainlayer", bytes(layer)},
        {"Assets/Terrain/Grass.terrainlayer.meta",
                   bytes("fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n")},
        {request.prefabPath, bytes(prefab)},
        {request.prefabPath + ".meta", bytes("fileFormatVersion: 2\nguid: ffffffffffffffffffffffffffffffff\n")}};
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    CHECK_EQ(prepared.value().manifest.assets.size(), std::size_t(4));
    CHECK_EQ(prepared.value().sourceMappings.size(), prepared.value().manifest.assets.size());
    CHECK(std::any_of(prepared.value().entries.begin(), prepared.value().entries.end(),
                      [](const auto& entry) { return entry.path == "reports/import.json"; }));
    CHECK(std::any_of(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.terrain"; }));
    CHECK(std::any_of(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.scene-template"; }));
    CHECK(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& finding) {
        return finding.feature.starts_with("MonoBehaviour") && finding.disposition == ImportDisposition::Unsupported;
    }));

    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    AssetCookProfile profile{
        {"android", "arm64", "vulkan", {"astc"}, "spirv-1.6", "high", {}}, CookPublication::LocalInspection, 16};
    auto cooked = cookEvaToEvpack(eva.value(), profile);
    REQUIRE(cooked.ok());
    AssetCookProfile webProfile{
        {"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}}, CookPublication::LocalInspection, 16};
    auto webCooked = cookEvaToEvpack(eva.value(), webProfile);
    REQUIRE(webCooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    REQUIRE(runtime.ok());
    auto webRuntime = parseEvpack(webCooked.value().bytes);
    REQUIRE(webRuntime.ok());
    CHECK(runtime.value().chunks().size() >= prepared.value().manifest.assets.size());
    CHECK_EQ(runtime.value().chunks().size(), webRuntime.value().chunks().size());
    auto                 androidAdmitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    auto                 webAdmitted     = std::make_shared<const Evpack>(std::move(webRuntime).takeValue());
    EvpackResourceReader androidReader(androidAdmitted);
    EvpackResourceReader webReader(webAdmitted);
    for (const auto& sourceAsset : prepared.value().manifest.assets) {
        auto currentVersion = currentAssetSchemaVersion(sourceAsset.type);
        REQUIRE(currentVersion.ok());
        const std::string expected = sourceAsset.type + "/" + std::to_string(currentVersion.value().value());
        auto              androidAsset =
            androidReader.read(sourceAsset.asset, expected,
                               {"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}}, 16 * 1024 * 1024);
        REQUIRE(androidAsset.ok());
        auto webAsset =
            webReader.read(sourceAsset.asset, expected,
                           {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}}, 16 * 1024 * 1024);
        REQUIRE(webAsset.ok());
        CHECK_EQ(androidAsset.value().chunks.front().bytes, webAsset.value().chunks.front().bytes);
    }

    const auto terrainAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.terrain"; });
    const auto sceneAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.scene-template"; });
    const auto instancesAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.instance-set"; });
    const auto materialAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.terrain-material"; });
    REQUIRE(terrainAsset != prepared.value().manifest.assets.end());
    REQUIRE(sceneAsset != prepared.value().manifest.assets.end());
    REQUIRE(instancesAsset != prepared.value().manifest.assets.end());
    REQUIRE(materialAsset != prepared.value().manifest.assets.end());
    const EvpackCapabilities androidCapabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    eve::asset_procgen::EvpackTerrainLoader terrainLoader(androidReader);
    auto loadedTerrain = terrainLoader.load(terrainAsset->asset, androidCapabilities);
    REQUIRE(loadedTerrain.ok());
    CHECK_EQ(loadedTerrain.value().heightmap.getWidth(), 3);
    CHECK_EQ(loadedTerrain.value().heightmap.getHeight(), 3);
    CHECK(std::abs(loadedTerrain.value().heightmap.height(1, 1) - 100.f) < 0.001f);
    auto sampled = loadedTerrain.value().spatial.sample(2.f, 42, 0.f);
    CHECK(sampled.getCount() > 0);

    // Imported runtime terrain terminates in the same versioned document used by
    // the level editor, then survives edit, atomic save, close and reopen.
    const auto documentRoot =
        std::filesystem::temp_directory_path() / ("eve-terrain-document-" + terrainAsset->asset.id().format());
    std::error_code cleanupError;
    std::filesystem::remove_all(documentRoot, cleanupError);
    std::filesystem::create_directories(documentRoot, cleanupError);
    REQUIRE(!cleanupError);
    editor::DiskAtomicDocumentStore documentStore(documentRoot);
    editor::DocumentService         firstDocuments(&documentStore);
    auto                            initialDocument = heightmap_target::encodeTerrainDocument(
        loadedTerrain.value().heightmap, loadedTerrain.value().spacingX, loadedTerrain.value().spacingZ);
    REQUIRE(initialDocument.ok());
    auto openedDocument =
        firstDocuments.open({editor::DocumentKind::Scene, editor::AssetGuid(terrainAsset->asset.id().format())},
                            "Imported Terrain", "content://World/Imported.terrain", initialDocument.value());
    REQUIRE(openedDocument.ok());
    procgen_editing::HeightmapTarget editTarget(terrainAsset->asset.id().format(), &loadedTerrain.value().heightmap);
    CHECK(editTarget.writeScalar(1, 1, 73.25F) == editing::FieldWriteStatus::Applied);
    auto editedDocument = heightmap_target::encodeTerrainDocument(
        loadedTerrain.value().heightmap, loadedTerrain.value().spacingX, loadedTerrain.value().spacingZ);
    REQUIRE(editedDocument.ok());
    REQUIRE(firstDocuments.edit(openedDocument.value().id, std::move(editedDocument).takeValue()).ok());
    auto save = firstDocuments.requestSave(openedDocument.value().id);
    REQUIRE(save.ok());
    REQUIRE(firstDocuments.executeSave(save.value()).ok());
    REQUIRE(firstDocuments.close(openedDocument.value().id).ok());

    editor::DocumentService reopenedDocuments(&documentStore);
    auto                    reopenedDocument =
        reopenedDocuments.open({editor::DocumentKind::Scene, editor::AssetGuid(terrainAsset->asset.id().format())},
                               "Imported Terrain", "content://World/Imported.terrain");
    REQUIRE(reopenedDocument.ok());
    auto persistedDocument = reopenedDocuments.content(reopenedDocument.value().id);
    REQUIRE(persistedDocument.ok());
    auto restoredTerrain = heightmap_target::decodeTerrainDocument(persistedDocument.value());
    REQUIRE(restoredTerrain.ok());
    CHECK_EQ(restoredTerrain.value().heightmap.getWidth(), 3);
    CHECK_EQ(restoredTerrain.value().heightmap.getHeight(), 3);
    CHECK(std::abs(restoredTerrain.value().heightmap.height(1, 1) - 73.25F) < 0.001F);
    std::filesystem::remove_all(documentRoot, cleanupError);

    eve::asset_scene::EvpackSceneTemplateLoader sceneLoader(androidReader);
    auto                                        loadedScene = sceneLoader.load(sceneAsset->asset, androidCapabilities);
    REQUIRE(loadedScene.ok());
    REQUIRE_EQ(loadedScene.value().root.children.size(), std::size_t(1));
    const auto& prefabRoot = loadedScene.value().root.children.front();
    CHECK_EQ(prefabRoot.name, std::string("TerrainRoot"));
    CHECK(std::abs(prefabRoot.x - 1.f) < 0.0001f);
    CHECK(std::abs(prefabRoot.y - 2.f) < 0.0001f);
    CHECK(std::abs(prefabRoot.z + 3.f) < 0.0001f);
    const bool yawConverted = std::abs(prefabRoot.yaw + 90.f) < 0.01f || std::abs(prefabRoot.yaw - 90.f) < 0.01f;
    CHECK(yawConverted);

    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(androidReader);
    auto loadedInstances = instanceLoader.load(instancesAsset->asset, androidCapabilities);
    REQUIRE(loadedInstances.ok());
    REQUIRE_EQ(loadedInstances.value().instances.size(), std::size_t(1));
    const auto& tree = loadedInstances.value().instances.front();
    CHECK_EQ(tree.prototype, std::string("unity-guid:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    CHECK(std::abs(tree.position[0] - 1.f) < 0.0001f);
    CHECK(std::abs(tree.position[1] - 50.f) < 0.0001f);
    CHECK(std::abs(tree.position[2] + 3.f) < 0.0001f);
    CHECK(std::abs(tree.scale[0] - 1.5f) < 0.0001f);
    CHECK(std::abs(tree.scale[1] - 2.f) < 0.0001f);
    CHECK(std::abs(tree.scale[2] - 1.5f) < 0.0001f);

    eve::asset_procgen::EvpackTerrainMaterialLoader materialLoader(androidReader);
    auto loadedMaterial = materialLoader.load(materialAsset->asset, androidCapabilities);
    REQUIRE(loadedMaterial.ok());
    REQUIRE_EQ(loadedMaterial.value().layers.size(), std::size_t(1));
    const auto& grass = loadedMaterial.value().layers.front();
    CHECK_EQ(grass.name, std::string("Assets/Terrain/Grass.terrainlayer"));
    CHECK_EQ(grass.normalConvention, std::string("opengl"));
    CHECK(std::abs(grass.tileSizeMeters - 8.f) < 0.0001f);
    CHECK_EQ(grass.maskSource, std::string("unity-guid:1234567890abcdef1234567890abcdef"));
    CHECK(std::abs(grass.tileScaleMeters[1] - 12.f) < 0.0001f);
    CHECK(std::abs(grass.tileOffsetMeters[0] - 2.f) < 0.0001f);
    CHECK(std::abs(grass.maskRemapMinimum[2] - .3f) < 0.0001f);
    CHECK(std::abs(grass.maskRemapMaximum[3] - .9f) < 0.0001f);
    CHECK(std::abs(grass.specular[1] - .12f) < 0.0001f);
    CHECK(std::abs(grass.metallic - .25f) < 0.0001f);
    CHECK(std::abs(grass.normalScale + 2.f) < 0.0001f);
    CHECK(std::abs(grass.smoothness - .75f) < 0.0001f);
    CHECK_EQ(loadedMaterial.value().holesSource, std::string("unity-guid:11112222333344445555666677778888"));
    CHECK_EQ(loadedMaterial.value().controlSources[0], std::string("unity-guid:99990000aaaabbbbccccddddeeeeffff"));
    REQUIRE(grass.diffuseAsset.has_value());
    REQUIRE(grass.normalAsset.has_value());
    REQUIRE(grass.maskAsset.has_value());
    REQUIRE(loadedMaterial.value().holesAsset.has_value());
    REQUIRE(loadedMaterial.value().controlAssets[0].has_value());
    CHECK_EQ(
        grass.diffuseAsset->format(),
        std::string("asset://") +
            unityIdentity().packageId.child("unity:cccccccccccccccccccccccccccccccc").child("image:default").format());
    CHECK_EQ(
        loadedMaterial.value().holesAsset->format(),
        std::string("asset://") +
            unityIdentity().packageId.child("unity:11112222333344445555666677778888").child("image:default").format());
}

TEST_CASE("asset.import.unityRejectsBinarySerializationAndBadHeightCounts") {
    UnityProjectImportRequest binary;
    binary.package                       = unityIdentity();
    binary.terrainDataPath               = "Assets/Terrain.asset";
    binary.files[binary.terrainDataPath] = {0, 1, 2};
    auto unsupported                     = prepareUnityProjectImport(binary);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.terrainRejectsInstancesThatRuntimeCouldNotAdmit") {
    CanonicalTerrainInput terrain;
    terrain.width = terrain.height = 2;
    terrain.heightsMeters          = {0.f, 0.f, 0.f, 0.f};
    CanonicalTerrainInstance instance;
    instance.prototype = std::string("bad\0prototype", 13);
    terrain.instances.push_back(instance);
    auto invalidText = prepareCanonicalTerrainImport(unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidText.ok());
    CHECK_EQ(invalidText.error()->code(), DiagnosticCode::InvalidArgument);

    terrain.instances.front().prototype   = "valid-prototype";
    terrain.instances.front().rotation[3] = 2.f;
    auto invalidRotation = prepareCanonicalTerrainImport(unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidRotation.ok());
    CHECK_EQ(invalidRotation.error()->code(), DiagnosticCode::InvalidArgument);

    terrain.instances.front().rotation[3] = 1.f;
    terrain.instances.front().scale[1]    = 0.f;
    auto invalidScale = prepareCanonicalTerrainImport(unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidScale.ok());
    CHECK_EQ(invalidScale.error()->code(), DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.import.unityTerrainDetailSidecarImportsPublicApiTransformsAtomically") {
    constexpr std::string_view terrain = R"yaml(%YAML 1.1
TerrainData:
  m_HeightmapResolution: 2
  m_HeightmapScale: {x: 1, y: 10, z: 1}
  m_Heights: 0000000000000000
  m_DetailPrototypes:
  - prototype: {fileID: 0}
)yaml";
    constexpr std::string_view details =
        R"json({"schema":"eve.unity-terrain-details","schemaVersion":3,"wavingGrass":{"amount":0.4,"speed":0.6,"strength":0.8,"tint":[0.3,0.7,0.4,1]},"prototypes":[{"prototype":"unity-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"VertexLit","usePrototypeMesh":true,"useInstancing":true,"minWidth":1,"maxWidth":2,"minHeight":1,"maxHeight":3,"noiseSeed":17,"noiseSpread":0.1,"density":1,"alignToGround":0.5,"positionJitter":0.25,"healthyColor":[0.2,0.8,0.3,1],"dryColor":[0.7,0.5,0.2,1],"bendFactor":0.35,"holeEdgePadding":0.4,"useDensityScaling":false}],"instances":[{"prototype":"unity-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","position":[1,2,-3],"rotation":[0,0.70710678,0,0.70710678],"scale":[1.5,2,1.5]}]})json";
    UnityProjectImportRequest request;
    request.package                   = unityIdentity();
    request.terrainDataPath           = "Assets/Terrain.asset";
    const std::string adjacentDetails = request.terrainDataPath + ".eve-details.json";
    request.files                     = {
        {request.terrainDataPath, bytes(terrain)},
        {request.terrainDataPath + ".meta", bytes("fileFormatVersion: 2\nguid: 11111111111111111111111111111111\n")},
        {adjacentDetails, bytes(details)}};
    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    CHECK(std::any_of(imported.value().findings.begin(), imported.value().findings.end(), [](const auto& finding) {
        return finding.feature == "TerrainData.detail-instances" &&
               finding.disposition == ImportDisposition::Translated;
    }));
    CHECK(std::any_of(
        imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.instance-set" && asset.schemaVersion == SchemaVersion(5); }));
    const auto instanceAsset =
        std::find_if(imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.instance-set"; });
    REQUIRE(instanceAsset != imported.value().manifest.assets.end());
    const auto instanceDefinition =
        std::find_if(imported.value().entries.begin(), imported.value().entries.end(),
                     [&](const auto& entry) { return entry.path == instanceAsset->definition; });
    REQUIRE(instanceDefinition != imported.value().entries.end());
    auto parsedDefinition =
        Value::fromJson(std::string(instanceDefinition->bytes.begin(), instanceDefinition->bytes.end()));
    REQUIRE(parsedDefinition.ok());
    const auto* definitionObject = parsedDefinition.value().getIf<Value::Object>();
    REQUIRE(definitionObject != nullptr);
    const auto* wavingGrass = definitionObject->at("wavingGrass").getIf<Value::Object>();
    REQUIRE(wavingGrass != nullptr);
    // The importer serializes the public float fields as promoted doubles.
    CHECK_EQ(wavingGrass->at("amount").asDouble(), double(.4f));
    CHECK_EQ(wavingGrass->at("strength").asDouble(), double(.8f));
    CHECK_EQ(wavingGrass->at("tint").getIf<Value::Array>()->at(1).asDouble(), double(.7f));
    const auto* prototypeArray = definitionObject->at("prototypes").getIf<Value::Array>();
    REQUIRE(prototypeArray != nullptr);
    REQUIRE_EQ(prototypeArray->size(), std::size_t(1));
    const auto* prototype = prototypeArray->front().getIf<Value::Object>();
    REQUIRE(prototype != nullptr);
    CHECK_EQ(prototype->at("prototype").asString(), std::string("unity-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    CHECK_EQ(prototype->at("renderMode").asString(), std::string("VertexLit"));
    CHECK(prototype->at("usePrototypeMesh").asBool());
    CHECK_EQ(prototype->at("noiseSeed").asInt(), 17);
    CHECK_EQ(prototype->at("alignToGround").asDouble(), .5);
    CHECK_EQ(prototype->at("bendFactor").asDouble(), double(.35f));
    CHECK_EQ(prototype->at("holeEdgePadding").asDouble(), double(.4f));
    CHECK(!prototype->at("useDensityScaling").asBool());
    CHECK_EQ(prototype->at("healthyColor").getIf<Value::Array>()->at(1).asDouble(), double(.8f));
    const auto expectedResource =
        AssetRef::fromId(unityIdentity().packageId.child("unity-prefab:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    REQUIRE(expectedResource.ok());
    CHECK_EQ(prototype->at("resourceAsset").asString(), expectedResource.value().format());

    request.files[adjacentDetails] = bytes(
        R"json({"schema":"eve.unity-terrain-details","schemaVersion":1,"prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":true,"minWidth":1,"maxWidth":2,"minHeight":1,"maxHeight":2,"noiseSeed":1,"noiseSpread":0.1,"density":1,"alignToGround":0,"positionJitter":0}],"instances":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","position":[0,0,0],"rotation":[0,0,0,2],"scale":[1,1,1]}]})json");
    auto invalid = prepareUnityProjectImport(request);
    REQUIRE(!invalid.ok());
    CHECK_EQ(invalid.error()->code(), DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.runtimeInstanceSetV2LoadsTerrainDetailPrototypeMetadata") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string        definition =
        R"json({"schema":"eve.instance-set","schemaVersion":2,"count":0,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":true,"minWidth":0.5,"maxWidth":1.5,"minHeight":1,"maxHeight":2,"noiseSeed":23,"noiseSpread":0.25,"density":0.75,"alignToGround":0.4,"positionJitter":0.2}]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto                      pack = runtimePack("eve.instance-set", definition, std::move(bulk), SchemaVersion(2));
    EvpackResourceReader      reader(pack);
    eve::asset_procgen::EvpackInstanceSetLoader loader(reader);
    auto loaded = loader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().prototypes.size(), std::size_t(1));
    const auto& runtimePrototype = loaded.value().prototypes.front();
    CHECK_EQ(runtimePrototype.renderMode, std::string("GrassBillboard"));
    CHECK(!runtimePrototype.usePrototypeMesh);
    CHECK(runtimePrototype.useInstancing);
    CHECK_EQ(runtimePrototype.noiseSeed, 23);
    CHECK_EQ(runtimePrototype.minWidth, .5f);
    CHECK_EQ(runtimePrototype.maxHeight, 2.f);
    CHECK_EQ(runtimePrototype.density, .75f);
    CHECK(runtimePrototype.resourceAsset.empty());
}

TEST_CASE("asset.import.unityVegetationScenePreservesManagerStateAtomically") {
    auto                       request      = UnityProjectImportRequest{unityIdentity()};
    constexpr std::string_view scene        = R"yaml(%YAML 1.1
--- !u!114 &1
MonoBehaviour:
  m_Script: {fileID: 11500000, guid: 504b8163e405ec24bab88beccbe43d42, type: 3}
  seasonControl: 3.25
  globalColor: {r: 0.2, g: 0.3, b: 0.4, a: 0.5}
  globalAlpha: 0.75
  overlayColor: {r: 1, g: 0.5, b: 0.25, a: 1}
  overlayAlbedo: {fileID: 2800000, guid: 11111111111111111111111111111111, type: 3}
--- !u!114 &2
MonoBehaviour:
  m_Script: {fileID: 11500000, guid: f3cf74665658f2249a2a56df92106e94, type: 3}
  layerColors: 2
  layerExtras: 3
  layerMotion: 4
  globalColor: 0.6
  globalAlpha: 0.7
  globalOverlay: 0.8
  globalWetness: 0.9
  colorMaskMin: 0.1
  colorMaskMax: 0.2
  overlayMaskMin: 0.3
  overlayMaskMax: 0.4
  alphaTreshold: 0.75
  motionHighlight: {r: 2, g: 3, b: 4, a: 1}
--- !u!114 &3
MonoBehaviour:
  m_GameObject: {fileID: 30}
  m_Script: {fileID: 11500000, guid: c879218671cebf84d9385947d79161cd, type: 3}
  mainDirection: {fileID: 31}
  windPower: 0.8
  noiseTilling: 2
  motionFadeDistance: 75
--- !u!114 &4
MonoBehaviour:
  m_Script: {fileID: 11500000, guid: eaf239e2056652741929c4183efd08b2, type: 3}
  renderScale: 1.5
  elementsVisibility: 20
  elementsSorting: 10
  elementsEdgeFade: 0.25
  renderColors:
    renderMode: 10
    textureWidth: 1024
    textureHeight: 512
  renderExtras:
    renderMode: -1
    textureWidth: 256
    textureHeight: 256
  renderMotion:
    renderMode: 20
    textureWidth: 2048
    textureHeight: 1024
  renderVertex:
    renderMode: 10
    textureWidth: 512
    textureHeight: 512
--- !u!4 &32
Transform:
  m_GameObject: {fileID: 31}
  m_LocalRotation: {x: 0, y: 0.7071067811865476, z: 0, w: 0.7071067811865476}
  m_LocalPosition: {x: 0, y: 0, z: 0}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Father: {fileID: 0}
--- !u!114 &40
MonoBehaviour:
  m_GameObject: {fileID: 41}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: 2d447c3f2fda29f41a5b7e81398c0ab0, type: 3}
  customVisibility: 10
  materialData:
    shader: {fileID: 4800000, guid: b3820571eb3f04f4f855cc7161c165d7, type: 3}
    props:
    - type: 2
      prop: _ElementLayer
      texture: {fileID: 0}
      vector: {r: 0, g: 0, b: 0, a: 0}
      value: 2
    - type: 2
      prop: _ElementIntensity
      texture: {fileID: 0}
      vector: {r: 0, g: 0, b: 0, a: 0}
      value: 0.75
    - type: 2
      prop: _MainValue
      texture: {fileID: 0}
      vector: {r: 0, g: 0, b: 0, a: 0}
      value: 0.625
--- !u!4 &42
Transform:
  m_GameObject: {fileID: 41}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 2, y: 3, z: 4}
  m_LocalScale: {x: 5, y: 1, z: 6}
  m_Father: {fileID: 0}
--- !u!114 &50
MonoBehaviour:
  m_GameObject: {fileID: 51}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: 2d447c3f2fda29f41a5b7e81398c0ab0, type: 3}
  customVisibility: -1
  materialData:
    shader: {fileID: 4800000, guid: 1179777fe2a84944798f2df69a9616a5, type: 3}
    props: []
--- !u!4 &52
Transform:
  m_GameObject: {fileID: 51}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 0, y: 0, z: 0}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Father: {fileID: 0}
)yaml";
    request.files["Assets/Demo.unity"]      = bytes(scene);
    request.files["Assets/Demo.unity.meta"] = bytes("fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    auto imported                           = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.size(), std::size_t(2));
    CHECK_EQ(imported.value().manifest.assets.front().type, std::string("eve.vegetation-scene"));
    CHECK(std::any_of(imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.scene-template"; }));
    CHECK(std::any_of(imported.value().manifest.dependencies.begin(), imported.value().manifest.dependencies.end(),
                      [](const auto& dependency) { return dependency.path == "vegetationScene"; }));
    const auto vegetationAsset = imported.value().manifest.assets.front().asset.id().format();
    auto       definition =
        std::find_if(imported.value().entries.begin(), imported.value().entries.end(),
                     [&](const auto& entry) { return entry.path == "assets/" + vegetationAsset + "/asset.json"; });
    REQUIRE(definition != imported.value().entries.end());
    auto parsed = Value::fromJson(
        std::string_view(reinterpret_cast<const char*>(definition->bytes.data()), definition->bytes.size()));
    REQUIRE(parsed.ok());
    const auto& root = *parsed.value().getIf<Value::Object>();
    CHECK_EQ(root.at("schemaVersion").asInt(), std::int64_t(1));
    const auto& control = *root.at("control").getIf<Value::Object>();
    CHECK_EQ(control.at("values").getIf<Value::Array>()->at(0).asDouble(), 3.25);
    CHECK_EQ(control.at("overlayAlbedoGuid").asString(), std::string("11111111111111111111111111111111"));
    const auto& details = *root.at("details").getIf<Value::Object>();
    CHECK_EQ(details.at("layers").getIf<Value::Array>()->at(2).asDouble(), 4.0);
    CHECK_EQ(details.at("motionHighlight").getIf<Value::Array>()->size(), std::size_t(3));
    const auto& motion = *root.at("motion").getIf<Value::Object>();
    CHECK_EQ(motion.at("direction").getIf<Value::Array>()->at(0).asDouble(), -1.0);
    const auto& volume = *root.at("volume").getIf<Value::Object>();
    CHECK_EQ(volume.at("motion").getIf<Value::Array>()->at(1).asDouble(), 2048.0);
    const auto& elements = *root.at("elements").getIf<Value::Array>();
    REQUIRE_EQ(elements.size(), std::size_t(2));
    const auto& element = *elements.front().getIf<Value::Object>();
    CHECK_EQ(element.at("kind").asString(), std::string("extras-overlay"));
    CHECK_EQ(element.at("layers").asInt(), std::int64_t(4));
    REQUIRE_EQ(element.at("properties").getIf<Value::Array>()->size(), std::size_t(3));
    CHECK_EQ(element.at("properties").getIf<Value::Array>()->at(2).getIf<Value::Object>()->at("name").asString(),
             std::string("_MainValue"));
    CHECK_EQ(element.at("position").getIf<Value::Array>()->at(0).asDouble(), 2.0);
    CHECK_EQ(element.at("position").getIf<Value::Array>()->at(2).asDouble(), -4.0);
    CHECK_EQ(element.at("value").getIf<Value::Array>()->at(2).asDouble(), 0.625);
    CHECK_EQ(elements.at(1).getIf<Value::Object>()->at("kind").asString(), std::string("color-effect"));

    auto       legacyScene = std::string(scene);
    const auto legacyBegin = legacyScene.find("  renderColors:");
    const auto legacyEnd   = legacyScene.find("--- !u!4 &32", legacyBegin);
    REQUIRE(legacyBegin != std::string::npos);
    REQUIRE(legacyEnd != std::string::npos);
    legacyScene.replace(legacyBegin, legacyEnd - legacyBegin,
                        "  renderColors: 1024\n  renderExtras: 512\n  renderMotion: 256\n  renderVertex: 128\n");
    request.files["Assets/Demo.unity"] = bytes(legacyScene);
    auto legacyImported                = prepareUnityProjectImport(request);
    REQUIRE(legacyImported.ok());
    const auto legacyDefinition =
        std::find_if(legacyImported.value().entries.begin(), legacyImported.value().entries.end(),
                     [](const auto& entry) { return entry.path.ends_with("asset.json"); });
    REQUIRE(legacyDefinition != legacyImported.value().entries.end());
    auto legacyParsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(legacyDefinition->bytes.data()),
                                                         legacyDefinition->bytes.size()));
    REQUIRE(legacyParsed.ok());
    const auto& legacyVolume = *legacyParsed.value().getIf<Value::Object>()->at("volume").getIf<Value::Object>();
    CHECK_EQ(legacyVolume.at("extras").getIf<Value::Array>()->at(1).asInt(), std::int64_t(512));
    CHECK_EQ(legacyVolume.at("extras").getIf<Value::Array>()->at(2).asInt(), std::int64_t(512));

    request.files["Assets/Demo.unity"] = bytes(scene.substr(0, scene.find("--- !u!114 &4")));
    auto incomplete                    = prepareUnityProjectImport(request);
    REQUIRE(!incomplete.ok());
    CHECK_EQ(incomplete.error()->code(), DiagnosticCode::ParseError);
}

TEST_CASE("asset.runtimeInstanceSetV3LoadsAndValidatesTerrainDetailResourceAsset") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string        resource = "asset://550e8400-e29b-41d4-a716-446655440099";
    const std::string        definition =
        std::string(
            R"json({"schema":"eve.instance-set","schemaVersion":3,"count":0,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":true,"minWidth":0.5,"maxWidth":1.5,"minHeight":1,"maxHeight":2,"noiseSeed":23,"noiseSpread":0.25,"density":0.75,"alignToGround":0.4,"positionJitter":0.2,"resourceAsset":")json") +
        resource + R"json("}]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto                      pack = runtimePack("eve.instance-set", definition, bulk, SchemaVersion(3));
    EvpackResourceReader      reader(pack);
    eve::asset_procgen::EvpackInstanceSetLoader loader(reader);
    auto loaded = loader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().prototypes.size(), std::size_t(1));
    CHECK_EQ(loaded.value().prototypes.front().resourceAsset, resource);

    const auto invalidDefinition = std::regex_replace(
        definition, std::regex("asset://550e8400-e29b-41d4-a716-446655440099"), "project://not-an-asset");
    auto invalidPack = runtimePack("eve.instance-set", invalidDefinition, std::move(bulk), SchemaVersion(3));
    EvpackResourceReader                        invalidReader(invalidPack);
    eve::asset_procgen::EvpackInstanceSetLoader invalidLoader(invalidReader);
    auto rejected = invalidLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}

TEST_CASE("asset.runtimeInstanceSetV4LoadsTerrainDetailAppearance") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string        definition =
        R"json({"schema":"eve.instance-set","schemaVersion":4,"count":0,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":false,"minWidth":0.5,"maxWidth":1.5,"minHeight":1,"maxHeight":2,"noiseSeed":23,"noiseSpread":0.25,"density":0.75,"alignToGround":0.4,"positionJitter":0.2,"resourceAsset":"asset://550e8400-e29b-41d4-a716-446655440099","healthyColor":[0.1,0.8,0.2,1],"dryColor":[0.7,0.4,0.1,1],"bendFactor":0.3,"holeEdgePadding":0.2,"useDensityScaling":false}]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto                      pack = runtimePack("eve.instance-set", definition, std::move(bulk), SchemaVersion(4));
    EvpackResourceReader      reader(pack);
    eve::asset_procgen::EvpackInstanceSetLoader loader(reader);
    auto loaded = loader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    const auto& prototype = loaded.value().prototypes.front();
    CHECK_EQ(prototype.healthyColor[1], .8f);
    CHECK_EQ(prototype.dryColor[0], .7f);
    CHECK_EQ(prototype.bendFactor, .3f);
    CHECK_EQ(prototype.holeEdgePadding, .2f);
    CHECK(!prototype.useDensityScaling);
}

TEST_CASE("asset.runtimeInstanceSetV5LoadsTerrainWavingGrass") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string        definition =
        R"json({"schema":"eve.instance-set","schemaVersion":5,"count":0,"partition":"single-cell","wavingGrass":{"amount":0.4,"speed":0.6,"strength":0.8,"tint":[0.3,0.7,0.4,1]},"prototypes":[]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto                      pack = runtimePack("eve.instance-set", definition, std::move(bulk), SchemaVersion(5));
    EvpackResourceReader      reader(pack);
    eve::asset_procgen::EvpackInstanceSetLoader loader(reader);
    auto loaded = loader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value().wavingGrassAmount, .4f);
    CHECK_EQ(loaded.value().wavingGrassSpeed, .6f);
    CHECK_EQ(loaded.value().wavingGrassStrength, .8f);
    CHECK_EQ(loaded.value().wavingGrassTint[1], .7f);
}

TEST_CASE("asset.runtimeWorldLoadersRejectCrossChunkMismatchAndSceneCycles") {
    const EvpackCapabilities  capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    std::vector<std::uint8_t> terrain     = {'E', 'V', 'T', 'R', 'N',  0,    1, 0, 1,    0,    0, 0, 1, 0,
                                             0,   0,   0,   0,   0x80, 0x3f, 0, 0, 0x80, 0x3f, 0, 0, 0, 0};
    auto                      terrainPack = runtimePack(
        "eve.terrain",
        R"json({"schema":"eve.terrain","schemaVersion":1,"width":2,"height":1,"spacingX":1,"spacingZ":1,"coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","heightUnit":"meter"})json",
        std::move(terrain));
    EvpackResourceReader                    terrainReader(terrainPack);
    eve::asset_procgen::EvpackTerrainLoader terrainLoader(terrainReader);
    auto mismatched = terrainLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!mismatched.ok());
    CHECK_EQ(mismatched.error()->code(), DiagnosticCode::ParseError);

    auto scenePack = runtimePack(
        "eve.scene-template",
        R"json({"schema":"eve.scene-template","schemaVersion":2,"coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","nodes":[{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31040","sourceFileId":1,"parentSourceFileId":2,"name":"A","position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31041","sourceFileId":2,"parentSourceFileId":1,"name":"B","position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}],"renderers":[]})json",
        {}, SchemaVersion(2));
    EvpackResourceReader                        sceneReader(scenePack);
    eve::asset_scene::EvpackSceneTemplateLoader sceneLoader(sceneReader);
    auto cyclic = sceneLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!cyclic.ok());
    CHECK_EQ(cyclic.error()->code(), DiagnosticCode::Conflict);

    std::vector<std::uint8_t> shortInstances = {'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0xff, 0xff, 0xff, 0x7f, 0, 0, 0, 0};
    auto                      instancePack   = runtimePack(
        "eve.instance-set",
        R"json({"schema":"eve.instance-set","schemaVersion":1,"count":2147483647,"partition":"single-cell"})json",
        std::move(shortInstances));
    EvpackResourceReader                        instanceReader(instancePack);
    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(instanceReader);
    auto oversized = instanceLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!oversized.ok());
    CHECK_EQ(oversized.error()->code(), DiagnosticCode::InvalidArgument);

    auto materialPack = runtimePack(
        "eve.terrain-material",
        R"json({"schema":"eve.terrain-material","schemaVersion":2,"layers":[{"name":"bad","diffuseSource":"","normalSource":"","weightSource":"","normalConvention":"opengl","tileSizeMeters":0}]})json",
        {}, SchemaVersion(2));
    EvpackResourceReader                            materialReader(materialPack);
    eve::asset_procgen::EvpackTerrainMaterialLoader materialLoader(materialReader);
    auto invalidMaterial = materialLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!invalidMaterial.ok());
    CHECK_EQ(invalidMaterial.error()->code(), DiagnosticCode::InvalidArgument);
}

// asset_import_unity_vegetation_preset.cpp
using namespace eve;
using namespace eve::asset_import;

namespace {
UnityProjectImportRequest requestFor(std::string text) {
    UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("22222222-3333-4444-8555-666666666666"), "preset.test", "12.6.0", {}};
    request.files["Assets/Tree It.tvepreset"] = {text.begin(), text.end()};
    return request;
}

Value definition(const PreparedAssetImport& prepared) {
    const auto& bytes  = prepared.entries.front().bytes;
    auto        parsed = Value::fromJson(std::string(bytes.begin(), bytes.end()));
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}
}  // namespace

TEST_CASE("asset.import.unityVegetationPresetRetainsCommandsConditionsAndIncludes") {
    auto             request = requestFor(R"(InfoTitle Tree It
Include Use Default Grass Masks
if OUTPUT_OPTION_CONTAINS Vegetation
{
    Mesh SetVariation GET_MASK_FROM_CHANNEL 1
    if MATERIAL_FLOAT_EQUALS _Mode 1
    {
        Material SET_FLOAT _MotionAmplitude_10 1
    }
}
)");
    UnitySourceAsset source{
        "Assets/Tree It.tvepreset", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Data, "DefaultImporter", {}};
    auto prepared = prepareUnityVegetationPreset(request, source);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.front().type, std::string("eve.vegetation-conversion-preset"));
    auto current = asset::currentAssetSchemaVersion("eve.vegetation-conversion-preset");
    REQUIRE(current.ok());
    REQUIRE_EQ(current.value(), SchemaVersion(1));
    const auto  decoded = definition(prepared.value());
    const auto& root    = *decoded.getIf<Value::Object>();
    REQUIRE_EQ(root.at("schemaVersion").asInt(), std::int64_t(1));
    const auto& statements = *root.at("statements").getIf<Value::Array>();
    REQUIRE_EQ(statements.size(), std::size_t(3));
    const auto& include = *statements[1].getIf<Value::Object>();
    REQUIRE_EQ(include.at("domain").asString(), std::string("Include"));
    REQUIRE_EQ(include.at("operation").asString(), std::string("Use"));
    const auto& condition = *statements[2].getIf<Value::Object>();
    REQUIRE_EQ(condition.at("predicate").asString(), std::string("OUTPUT_OPTION_CONTAINS"));
    REQUIRE_EQ(condition.at("negated").asBool(), false);
    REQUIRE_EQ(condition.at("statements").getIf<Value::Array>()->size(), std::size_t(2));
    auto archiveBytes = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = asset::parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().chunkCount, std::uint32_t(1));
}

TEST_CASE("asset.import.unityVegetationPresetRejectsMalformedBlocksAndRecordsKnownSourceTypo") {
    UnitySourceAsset source{
        "Assets/Tree It.tvepreset", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Data, "DefaultImporter", {}};
    auto malformed = requestFor("if SHADER_NAME_CONTAINS Plant\n{\nMaterial SET_FLOAT _Value 1\n");
    REQUIRE(!prepareUnityVegetationPreset(malformed, source).ok());
    auto recovered = requestFor("f SHADER_NAME_CONTAINS Prop\n{\nMaterial SET_FLOAT _Value 1\n}\n");
    auto prepared  = prepareUnityVegetationPreset(recovered, source);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().findings.size(), std::size_t(2));
    REQUIRE_EQ(prepared.value().findings.back().feature, std::string("TVE.preset.conditionTypo"));
}

TEST_CASE("asset.import.vegetationPresetExpandsIncludesAndEvaluatesSourceFacts") {
    auto parse = [&](std::string path, std::string guid, std::string text) {
        auto request = requestFor(std::move(text));
        auto bytes   = std::move(request.files.begin()->second);
        request.files.clear();
        request.files[path] = std::move(bytes);
        UnitySourceAsset source{path, guid, UnitySourceKind::Data, "DefaultImporter", {}};
        auto             prepared = prepareUnityVegetationPreset(request, source);
        REQUIRE(prepared.ok());
        return definition(prepared.value());
    };
    std::map<std::string, Value> library;
    library["Root"]   = parse("Assets/[PRESET] Root.tvepreset", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                              "Include Common\nif OUTPUT_OPTION_CONTAINS Vegetation\n{\nMesh SetHeight PROC 4\n}\n"
                                "if !MATERIAL_HAS_TEX _Mask\n{\nMaterial SET_FLOAT _Fallback 1\n}\n");
    library["Common"] = parse("Assets/[INCLUDE] Common.tvepreset", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                              "if SHADER_NAME_CONTAINS Plant Lit\n{\nMaterial COPY_TEX _MainTex _BaseMap\n}\n");
    VegetationPresetContext context;
    context.outputOptions.insert("Vegetation");
    context.shaderName = "Vendor Plant Lit";
    auto evaluated     = evaluateVegetationPreset(library, "Root", context);
    REQUIRE(evaluated.ok());
    REQUIRE_EQ(evaluated.value().size(), std::size_t(3));
    REQUIRE_EQ(evaluated.value()[0].operation, std::string("COPY_TEX"));
    REQUIRE_EQ(evaluated.value()[1].domain, std::string("Mesh"));
    REQUIRE_EQ(evaluated.value()[2].arguments, std::vector<std::string>({"_Fallback", "1"}));
}

TEST_CASE("asset.import.vegetationPresetRejectsMissingCyclicAndUnknownExecution") {
    auto value = [&](std::string text) {
        auto             request = requestFor(std::move(text));
        UnitySourceAsset source{"Assets/Tree It.tvepreset",
                                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                UnitySourceKind::Data,
                                "DefaultImporter",
                                {}};
        auto             prepared = prepareUnityVegetationPreset(request, source);
        REQUIRE(prepared.ok());
        return definition(prepared.value());
    };
    VegetationPresetContext      context;
    std::map<std::string, Value> library{{"Root", value("Include Use Default Flower Masks\n")},
                                         {"Use Default Flowers Masks", value("Mesh SetVariation PROC 3\n")}};
    auto                         aliased = evaluateVegetationPreset(library, "Root", context);
    REQUIRE(aliased.ok());
    REQUIRE_EQ(aliased.value().size(), std::size_t(1));
    library = {{"Root", value("Include Missing\n")}};
    REQUIRE(!evaluateVegetationPreset(library, "Root", context).ok());
    library = {{"Root", value("Include Loop\n")}, {"Loop", value("Include Root\n")}};
    REQUIRE(!evaluateVegetationPreset(library, "Root", context).ok());
    library = {{"Root", value("if UNKNOWN_PREDICATE value\n{\nMesh SetHeight PROC 4\n}\n")}};
    REQUIRE(!evaluateVegetationPreset(library, "Root", context).ok());
}

TEST_CASE("asset.import.vegetationPresetAppliesMaterialMeshTextureAndOutputAtomically") {
    VegetationConversionCandidate source;
    source.materialFloats["_Gloss"]              = 0.75;
    source.materialColors["_Color"]              = {0.1, 0.2, 0.3, 1};
    source.materialTextures["_MainTex"]          = "asset://image";
    source.materialTextures["_OtherTex"]         = "asset://other";
    source.materialTextureTransforms["_MainTex"] = {2, 3, 0.25, 0.5};
    std::vector<VegetationPresetCommand> commands{
        {"Shader", "SHADER_STANDARD_PLANT", {"TVE/Plant", "Standard", "Lit"}, "preset"},
        {"Material", "SET_SHADER", {"SHADER_STANDARD_PLANT"}, "preset"},
        {"Material", "COPY_FLOAT", {"_Gloss", "_Smoothness"}, "preset"},
        {"Material", "COPY_COLOR", {"_Color", "_MainColor"}, "preset"},
        {"Material", "COPY_TEX_FIRST_VALID", {"_MainAlbedoTex"}, "preset"},
        {"Material", "COPY_TEX", {"_MainTex", "_MainAlbedoTex"}, "preset"},
        {"Material", "COPY_TEX", {"_OtherTex", "_MainAlbedoTex"}, "preset"},
        {"Material", "COPY_ST_AS_VECTOR", {"_MainTex", "_MainUVs"}, "preset"},
        {"Material", "SET_FLOAT", {"_MotionAmplitude_10", "1.5"}, "preset"},
        {"Material", "ENABLE_INSTANCING", {}, "preset"},
        {"Material", "ENABLE_KEYWORD", {"_ALPHATEST_ON"}, "preset"},
        {"Mesh", "SetVariation", {"GET_MASK_FROM_CHANNEL", "1", "ACTION_REMAP_01"}, "preset"},
        {"Utility", "START_TEXTURE_PACKING", {}, "preset"},
        {"Texture", "PropName", {"_MainMaskTex"}, "preset"},
        {"Texture", "ImportType", {"DEFAULT"}, "preset"},
        {"Texture", "SetRed", {"GET_GREEN", "_MaskMap", "ACTION_ONE_MINUS"}, "preset"},
        {"Texture", "SetAlpha", {"NONE"}, "preset"},
        {"OutputMeshes", "CUSTOM", {}, "preset"}};
    auto applied = applyVegetationPreset(source, commands);
    REQUIRE(applied.ok());
    REQUIRE_EQ(applied.value().materialShader, std::string("TVE/Plant Standard Lit"));
    REQUIRE_EQ(applied.value().materialFloats.at("_Smoothness"), 0.75);
    REQUIRE_EQ(applied.value().materialColors.at("_MainColor"), source.materialColors.at("_Color"));
    REQUIRE_EQ(applied.value().materialTextures.at("_MainAlbedoTex"), std::string("asset://image"));
    REQUIRE_EQ(applied.value().materialVectors.at("_MainUVs"), source.materialTextureTransforms.at("_MainTex"));
    REQUIRE(applied.value().materialInstancing);
    REQUIRE(applied.value().materialKeywords.contains("_ALPHATEST_ON"));
    REQUIRE_EQ(applied.value().meshRules.at("SetVariation").size(), std::size_t(3));
    REQUIRE_EQ(applied.value().texturePacks.front().targetProperty, std::string("_MainMaskTex"));
    REQUIRE_EQ(applied.value().texturePacks.front().channels[0].action, std::string("ACTION_ONE_MINUS"));
    REQUIRE_EQ(applied.value().outputDirectives.at("OutputMeshes"), std::vector<std::string>({"CUSTOM"}));
}

TEST_CASE("asset.import.vegetationPresetApplicationRejectsUnknownCommandsWithoutMutatingBaseline") {
    VegetationConversionCandidate source;
    source.materialFloats["keep"] = 7;
    std::vector<VegetationPresetCommand> commands{{"Material", "SET_FLOAT", {"changed", "1"}, "preset"},
                                                  {"Material", "UNKNOWN", {}, "preset"}};
    auto                                 applied = applyVegetationPreset(source, commands);
    REQUIRE(!applied.ok());
    REQUIRE_EQ(source.materialFloats.size(), std::size_t(1));
    REQUIRE_EQ(source.materialFloats.at("keep"), 7.0);
}

TEST_CASE("asset.import.vegetationPresetExecutesTexturePackingPixels") {
    VegetationConversionCandidate candidate;
    VegetationTexturePackRecipe   recipe;
    recipe.targetProperty = "_MainMaskTex";
    recipe.channels[0]    = {"GET_GREEN", "_Mask", "ACTION_ONE_MINUS"};
    recipe.channels[1]    = {"GET_MAX", "_Mask", ""};
    recipe.channels[2]    = {"GET_GRAY", "_Mask", ""};
    recipe.channels[3]    = {"GET_ALPHA", "_Mask", ""};
    candidate.texturePacks.push_back(recipe);
    VegetationPresetImage image{1, 1, {51, 102, 204, 128}};
    auto                  packed = executeVegetationTexturePacks(candidate, {{"_Mask", image}});
    REQUIRE(packed.ok());
    const auto& pixels = packed.value().at("_MainMaskTex").pixels;
    REQUIRE_EQ(pixels[0], std::uint8_t(153));
    REQUIRE_EQ(pixels[1], std::uint8_t(204));
    REQUIRE_EQ(pixels[2], std::uint8_t(119));
    REQUIRE_EQ(pixels[3], std::uint8_t(128));
}

TEST_CASE("asset.import.vegetationPresetTexturePackingRejectsMeshDependentTransformAtomically") {
    VegetationConversionCandidate candidate;
    VegetationTexturePackRecipe   recipe;
    recipe.targetProperty = "_Normal";
    recipe.transformSpace = "OBJECT_TO_TANGENT";
    recipe.channels[0]    = {"GET_RED", "_Source", ""};
    candidate.texturePacks.push_back(recipe);
    VegetationPresetImage image{1, 1, {255, 0, 0, 255}};
    auto                  packed = executeVegetationTexturePacks(candidate, {{"_Source", image}});
    REQUIRE(!packed.ok());
}

TEST_CASE("asset.import.vegetationPresetExecutesMeshMasksIntoTveStreams") {
    asset::CanonicalMeshData mesh;
    mesh.positions                = {0, 0, 0, 1, 1, 0, 0, 2, 1};
    mesh.normals                  = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices                  = {0, 1, 2};
    mesh.attributes["_UNITY_UV0"] = {4, {0, 0, 0, 0, .5f, .5f, 0, 0, 1, 1, 0, 0}};
    mesh.attributes["_UNITY_UV1"] = {4, std::vector<float>(12, 0)};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetVariation"] = {"GET_MASK_PROCEDURAL", "4"};
    candidate.meshRules["SetOcclusion"] = {"GET_MASK_PROCEDURAL", "10"};
    candidate.meshRules["SetHeight"]    = {"GET_MASK_PROCEDURAL", "14"};
    candidate.meshRules["SetMotion2"]   = {"GET_MASK_PROCEDURAL", "4"};
    candidate.meshRules["SetMotion3"]   = {"GET_MASK_PROCEDURAL", "16"};
    auto converted                      = executeVegetationMeshRules(candidate, mesh);
    REQUIRE(converted.ok());
    const auto& colors = converted.value().attributes.at("COLOR_0").values;
    REQUIRE_EQ(colors[0], 0.f);
    REQUIRE_EQ(colors[4], .5f);
    REQUIRE_EQ(colors[8], 1.f);
    REQUIRE_EQ(colors[1], 1.f);
    REQUIRE(converted.value().attributes.at("_UNITY_UV0").values[6] > 0.f);
    REQUIRE_EQ(converted.value().attributes.at("_UNITY_UV3").values.size(), std::size_t(12));
}

TEST_CASE("asset.import.vegetationPresetPredictiveVariationIsStablePerConnectedElement") {
    asset::CanonicalMeshData mesh;
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0, 3, 0, 0, 4, 0, 0, 3, 1, 0};
    mesh.normals   = std::vector<float>(18, 0);
    for (std::size_t i = 0; i < 6; ++i) mesh.normals[i * 3 + 1] = 1;
    mesh.indices = {0, 1, 2, 3, 4, 5};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetVariation"] = {"GET_MASK_PROCEDURAL", "3"};
    auto converted                      = executeVegetationMeshRules(candidate, mesh, .75f);
    REQUIRE(converted.ok());
    const auto& color = converted.value().attributes.at("COLOR_0").values;
    REQUIRE_EQ(color[0], color[4]);
    REQUIRE_EQ(color[4], color[8]);
    REQUIRE_EQ(color[12], color[16]);
    REQUIRE_EQ(color[16], color[20]);
    REQUIRE_NE(color[0], color[12]);
}

TEST_CASE("asset.import.vegetationPresetExecutesFractionalActionAndProceduralPivots") {
    asset::CanonicalMeshData mesh;
    mesh.positions                = {1, 0, 2, 3, 1, 4, 2, 2, 6};
    mesh.normals                  = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices                  = {0, 1, 2};
    mesh.attributes["COLOR_0"]    = {4, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    mesh.attributes["_UNITY_UV0"] = {4, {0, 0, 1.25f, 0, 0, 0, 2.5f, 0, 0, 0, 3.75f, 0}};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetVariation"] = {"GET_MASK_FROM_CHANNEL", "6", "ACTION_FRACTIONAL_VALUES"};
    candidate.meshRules["SetPivots"]    = {"GET_PIVOTS_PROCEDURAL", "0"};
    auto converted                      = executeVegetationMeshRules(candidate, mesh);
    REQUIRE(converted.ok());
    const auto& color = converted.value().attributes.at("COLOR_0").values;
    REQUIRE_EQ(color[0], .25f);
    REQUIRE_EQ(color[4], .5f);
    REQUIRE_EQ(color[8], .75f);
    const auto& pivot = converted.value().attributes.at("_UNITY_UV3").values;
    REQUIRE_EQ(pivot[0], 2.f);
    REQUIRE_EQ(pivot[1], -4.f);
    REQUIRE_EQ(pivot[4], 2.f);
    REQUIRE_EQ(pivot[5], -4.f);
}

TEST_CASE("asset.import.vegetationPresetExecutesFlatAndSphericalNormals") {
    asset::CanonicalMeshData mesh;
    mesh.positions = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    mesh.normals   = {1, 0, 0, 1, 0, 0, 1, 0, 0};
    mesh.indices   = {0, 1, 2};
    VegetationConversionCandidate flat;
    flat.meshRules["SetNormals"] = {"GET_NORMALS_PROCEDURAL", "3"};
    auto flattened               = executeVegetationMeshRules(flat, mesh);
    REQUIRE(flattened.ok());
    REQUIRE_EQ(flattened.value().normals[0], 0.f);
    REQUIRE_EQ(flattened.value().normals[1], 1.f);
    VegetationConversionCandidate spherical;
    spherical.meshRules["SetNormals"] = {"GET_NORMALS_PROCEDURAL", "6"};
    auto rounded                      = executeVegetationMeshRules(spherical, mesh);
    REQUIRE(rounded.ok());
    REQUIRE_EQ(rounded.value().normals[0], 1.f);
    REQUIRE_EQ(rounded.value().normals[4], 1.f);
    REQUIRE_EQ(rounded.value().normals[8], 1.f);
}

TEST_CASE("asset.import.vegetationPresetConversionPublishesAllOutputsAtomically") {
    VegetationConversionCandidate source;
    source.materialTextures["_Mask"] = "asset://mask";
    asset::CanonicalMeshData mesh;
    mesh.positions = {0, 0, 0, 0, 1, 0, 1, 1, 0};
    mesh.normals   = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices   = {0, 1, 2};
    std::vector<VegetationPresetCommand> commands{{"Material", "SET_FLOAT", {"_Wind", "2"}, "preset"},
                                                  {"Mesh", "SetHeight", {"GET_MASK_PROCEDURAL", "4"}, "preset"},
                                                  {"Utility", "START_TEXTURE_PACKING", {}, "preset"},
                                                  {"Texture", "PropName", {"_Packed"}, "preset"},
                                                  {"Texture", "SetRed", {"GET_RED", "_Mask"}, "preset"}};
    VegetationPresetImage                image{1, 1, {64, 0, 0, 255}};
    auto converted = executeVegetationConversion(source, commands, mesh, {{"_Mask", image}});
    REQUIRE(converted.ok());
    REQUIRE_EQ(converted.value().candidate.materialFloats.at("_Wind"), 2.0);
    REQUIRE(converted.value().mesh.has_value());
    REQUIRE_EQ(converted.value().textures.at("_Packed").pixels[0], std::uint8_t(64));
    commands.push_back({"Material", "UNKNOWN", {}, "preset"});
    auto rejected = executeVegetationConversion(source, commands, mesh, {{"_Mask", image}});
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(source.materialFloats.size(), std::size_t(0));
}

TEST_CASE("asset.import.vegetationPresetSamplesTextureMaskThroughSelectedUv") {
    asset::CanonicalMeshData mesh;
    mesh.positions                = {0, 0, 0, 0, 1, 0, 1, 1, 0};
    mesh.normals                  = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices                  = {0, 1, 2};
    mesh.attributes["_UNITY_UV1"] = {4, {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetVariation"] = {"GET_MASK_FROM_TEXTURE", "0", "_Mask", "GET_COORD", "1"};
    VegetationPresetImage image{2, 2, {0, 0, 0, 255, 64, 0, 0, 255, 128, 0, 0, 255, 255, 0, 0, 255}};
    auto                  converted = executeVegetationMeshRules(candidate, mesh, {{"_Mask", image}});
    REQUIRE(converted.ok());
    const auto& colors = converted.value().attributes.at("COLOR_0").values;
    REQUIRE_EQ(colors[0], 0.f);
    REQUIRE_EQ(colors[4], 64.f / 255.f);
    REQUIRE_EQ(colors[8], 1.f);
}

TEST_CASE("asset.import.vegetationPresetMeshSurvivesCanonicalCodecAndRuntimeAdmission") {
    asset::CanonicalMeshData mesh;
    mesh.positions                = {0, 0, 0, 0, 1, 0, 1, 1, 0};
    mesh.normals                  = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices                  = {0, 1, 2};
    mesh.texcoords[0]             = {0, 0, 0, 1, 1, 1};
    mesh.attributes["_UNITY_UV0"] = {4, {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}};
    mesh.attributes["_UNITY_UV1"] = {4, std::vector<float>(12, 0)};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetVariation"] = {"GET_MASK_PROCEDURAL", "4"};
    candidate.meshRules["SetPivots"]    = {"GET_PIVOTS_PROCEDURAL", "0"};
    auto converted                      = executeVegetationMeshRules(candidate, mesh);
    REQUIRE(converted.ok());
    auto encoded = asset::encodeCanonicalMesh(converted.value());
    REQUIRE(encoded.ok());
    auto decoded = asset::decodeCanonicalMesh(encoded.value());
    REQUIRE(decoded.ok());
    auto runtime = asset_graphics::VegetationAsset::fromCanonical(decoded.value());
    REQUIRE(runtime.ok());
    REQUIRE_EQ(runtime.value()->vertexCount(), std::uint32_t(3));
}

TEST_CASE("asset.import.vegetationPresetRasterizesObjectNormalIntoTangentSpace") {
    asset::CanonicalMeshData mesh;
    mesh.positions             = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals               = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices               = {0, 1, 2};
    mesh.texcoords[0]          = {0, 0, 1, 0, 0, 1};
    mesh.attributes["TANGENT"] = {4, {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}};
    VegetationConversionCandidate candidate;
    VegetationTexturePackRecipe   recipe;
    recipe.targetProperty = "_MainNormalTex";
    recipe.transformSpace = "OBJECT_TO_TANGENT";
    recipe.channels[0]    = {"GET_RED", "_NormalMapOS", ""};
    recipe.channels[1]    = {"GET_GREEN", "_NormalMapOS", ""};
    recipe.channels[2]    = {"GET_BLUE", "_NormalMapOS", ""};
    candidate.texturePacks.push_back(recipe);
    VegetationPresetImage source{1, 1, {128, 255, 128, 255}};
    auto                  packed = executeVegetationTexturePacks(candidate, {{"_NormalMapOS", source}}, mesh);
    REQUIRE(packed.ok());
    const auto& pixel = packed.value().at("_MainNormalTex").pixels;
    REQUIRE_EQ(pixel[0], std::uint8_t(255));
    REQUIRE(std::abs(int(pixel[1]) - 128) <= 1);
    REQUIRE(std::abs(int(pixel[2]) - 128) <= 1);
}

TEST_CASE("asset.import.vegetationPresetNormalSpaceConversionRoundTrips") {
    asset::CanonicalMeshData mesh;
    mesh.positions             = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals               = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices               = {0, 1, 2};
    mesh.texcoords[0]          = {0, 0, 1, 0, 0, 1};
    mesh.attributes["TANGENT"] = {4, {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}};
    VegetationPresetImage source{1, 1, {128, 255, 128, 255}};
    auto                  convert = [&](std::string space, std::string sourceName, const VegetationPresetImage& image) {
        VegetationConversionCandidate candidate;
        VegetationTexturePackRecipe   recipe;
        recipe.targetProperty = "_Converted";
        recipe.transformSpace = std::move(space);
        recipe.channels[0]    = {"GET_RED", sourceName, ""};
        recipe.channels[1]    = {"GET_GREEN", sourceName, ""};
        recipe.channels[2]    = {"GET_BLUE", sourceName, ""};
        candidate.texturePacks.push_back(recipe);
        return executeVegetationTexturePacks(candidate, {{sourceName, image}}, mesh);
    };
    auto tangent = convert("OBJECT_TO_TANGENT", "_Object", source);
    REQUIRE(tangent.ok());
    auto object = convert("TANGENT_TO_OBJECT", "_Tangent", tangent.value().at("_Converted"));
    REQUIRE(object.ok());
    const auto& pixel = object.value().at("_Converted").pixels;
    REQUIRE(std::abs(int(pixel[0]) - 128) <= 1);
    REQUIRE(std::abs(int(pixel[1]) - 255) <= 1);
    REQUIRE(std::abs(int(pixel[2]) - 128) <= 1);
}

TEST_CASE("asset.import.vegetationPresetTileBinsPreserveLateTriangleOverlap") {
    asset::CanonicalMeshData mesh;
    mesh.positions             = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals               = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices               = {0, 1, 2, 3, 4, 5};
    mesh.texcoords[0]          = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.attributes["TANGENT"] = {4, {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1}};
    VegetationConversionCandidate candidate;
    VegetationTexturePackRecipe   recipe;
    recipe.targetProperty = "_Normal";
    recipe.transformSpace = "OBJECT_TO_TANGENT";
    recipe.channels[0]    = {"GET_RED", "_Source", ""};
    recipe.channels[1]    = {"GET_GREEN", "_Source", ""};
    recipe.channels[2]    = {"GET_BLUE", "_Source", ""};
    candidate.texturePacks.push_back(recipe);
    VegetationPresetImage source{33, 17, std::vector<std::uint8_t>(33 * 17 * 4)};
    for (std::size_t offset = 0; offset < source.pixels.size(); offset += 4) {
        source.pixels[offset]     = 128;
        source.pixels[offset + 1] = 255;
        source.pixels[offset + 2] = 128;
        source.pixels[offset + 3] = 255;
    }
    auto converted = executeVegetationTexturePacks(candidate, {{"_Source", source}}, mesh);
    REQUIRE(converted.ok());
    const auto  offset = (std::size_t(4) * 33 + 20) * 4;
    const auto& pixels = converted.value().at("_Normal").pixels;
    REQUIRE(std::abs(int(pixels[offset]) - 128) <= 1);
    REQUIRE_EQ(pixels[offset + 1], std::uint8_t(255));
    REQUIRE(std::abs(int(pixels[offset + 2]) - 128) <= 1);
}

TEST_CASE("asset.import.vegetationPresetTileBinsScaleAcrossDenseMesh") {
    constexpr std::uint32_t  grid = 64, dimension = 512;
    asset::CanonicalMeshData mesh;
    for (std::uint32_t y = 0; y <= grid; ++y)
        for (std::uint32_t x = 0; x <= grid; ++x) {
            mesh.positions.insert(mesh.positions.end(), {float(x) / grid, float(y) / grid, 0});
            mesh.normals.insert(mesh.normals.end(), {0, 0, 1});
            mesh.texcoords[0].insert(mesh.texcoords[0].end(), {float(x) / grid, float(y) / grid});
            mesh.attributes["TANGENT"].values.insert(mesh.attributes["TANGENT"].values.end(), {1, 0, 0, 1});
        }
    mesh.attributes["TANGENT"].components = 4;
    for (std::uint32_t y = 0; y < grid; ++y)
        for (std::uint32_t x = 0; x < grid; ++x) {
            const auto a = y * (grid + 1) + x, b = a + 1, c = a + grid + 1, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    VegetationConversionCandidate candidate;
    VegetationTexturePackRecipe   recipe;
    recipe.targetProperty = "_Normal";
    recipe.transformSpace = "OBJECT_TO_TANGENT";
    recipe.channels[0]    = {"GET_RED", "_Source", ""};
    recipe.channels[1]    = {"GET_GREEN", "_Source", ""};
    recipe.channels[2]    = {"GET_BLUE", "_Source", ""};
    candidate.texturePacks.push_back(recipe);
    VegetationPresetImage source{dimension, dimension,
                                 std::vector<std::uint8_t>(std::size_t(dimension) * dimension * 4)};
    for (std::size_t offset = 0; offset < source.pixels.size(); offset += 4) {
        source.pixels[offset] = source.pixels[offset + 1] = 128;
        source.pixels[offset + 2] = source.pixels[offset + 3] = 255;
    }
    const auto start     = std::chrono::steady_clock::now();
    auto       converted = executeVegetationTexturePacks(candidate, {{"_Source", source}}, mesh);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(converted.ok());
    REQUIRE_EQ(converted.value().at("_Normal").pixels.size(), std::size_t(dimension) * dimension * 4);
    std::printf("vegetation tile bins: %u triangles, %ux%u pixels, %lld ms\n", grid * grid * 2, dimension, dimension,
                static_cast<long long>(milliseconds));
}

TEST_CASE("asset.import.vegetationPresetPublishesBoundsAndReadabilityMetadata") {
    asset::CanonicalMeshData mesh;
    mesh.positions = {-2, -1, 0, 1, 4, 3, 0, 2, -1};
    mesh.normals   = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices   = {0, 1, 2};
    std::vector<VegetationPresetCommand> commands{{"Mesh", "SetBounds", {"GET_BOUNDS_PROCEDURAL", "2"}, "bounds"},
                                                  {"Mesh", "SetReadWrite", {"MARK_MESHES_AS_READABLE"}, "readable"}};
    auto                                 converted = executeVegetationConversion({}, commands, mesh, {});
    REQUIRE(converted.ok());
    REQUIRE(converted.value().meshBoundsMinimum.has_value());
    REQUIRE(converted.value().meshBoundsMaximum.has_value());
    REQUIRE_EQ((*converted.value().meshBoundsMinimum)[0], -4.8f);
    REQUIRE_EQ((*converted.value().meshBoundsMinimum)[1], -1.f);
    REQUIRE_EQ((*converted.value().meshBoundsMaximum)[0], 4.8f);
    REQUIRE_EQ((*converted.value().meshBoundsMaximum)[1], 5.f);
    REQUIRE(converted.value().meshCpuReadable);
}

TEST_CASE("asset.import.vegetationPresetAcceptsSourceNoOpsAndIgnoredCopyFloatToken") {
    VegetationConversionCandidate source;
    source.materialFloats["_RenderNormals"] = 2;
    asset::CanonicalMeshData mesh;
    mesh.positions                = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals                  = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices                  = {0, 1, 2};
    mesh.attributes["_UNITY_UV3"] = {4, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};
    std::vector<VegetationPresetCommand> commands{
        {"Material", "COPY_FLOAT", {"_RenderNormals", "_DoubleSidedNormalMode", "0"}, "TVE to DE Commons"},
        {"Mesh", "SetPivots", {"NONE"}, "default pivots"}};
    auto converted = executeVegetationConversion(source, commands, mesh, {});
    REQUIRE(converted.ok());
    REQUIRE_EQ(converted.value().candidate.materialFloats.at("_DoubleSidedNormalMode"), 2.0);
    REQUIRE_EQ(converted.value().mesh->attributes.at("_UNITY_UV3").values, mesh.attributes.at("_UNITY_UV3").values);
}

TEST_CASE("asset.import.vegetationPresetNormalOverrideRebuildsCanonicalTangents") {
    asset::CanonicalMeshData mesh;
    mesh.positions             = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals               = {1, 0, 0, 1, 0, 0, 1, 0, 0};
    mesh.indices               = {0, 1, 2};
    mesh.texcoords[0]          = {0, 0, 1, 0, 0, 1};
    mesh.attributes["TANGENT"] = {4, std::vector<float>(12, 0)};
    VegetationConversionCandidate candidate;
    candidate.meshRules["SetNormals"] = {"GET_NORMALS_PROCEDURAL", "0"};
    auto converted                    = executeVegetationMeshRules(candidate, mesh);
    REQUIRE(converted.ok());
    const auto& tangent = converted.value().attributes.at("TANGENT");
    REQUIRE_EQ(tangent.components, std::uint32_t(4));
    REQUIRE(std::abs(tangent.values[0] - 1.f) <= 1e-6f);
    REQUIRE(std::abs(tangent.values[1]) <= 1e-6f);
    REQUIRE(std::abs(tangent.values[2]) <= 1e-6f);
    REQUIRE_EQ(std::abs(tangent.values[3]), 1.f);
}

TEST_CASE("asset.import.vegetationPresetDecodesUnityMaterialBaseline") {
    const std::string yaml = R"(%YAML 1.1
--- !u!21 &2100000
Material:
  m_Shader: {fileID: 4800000, guid: a933075b367f9b24981408633f72ff34, type: 3}
  m_ValidKeywords:
  - TVE_FEATURE_CLIP
  m_EnableInstancingVariants: 1
  m_SavedProperties:
    m_TexEnvs:
    - _MainTex:
        m_Texture: {fileID: 2800000, guid: 0123456789abcdef0123456789abcdef, type: 3}
        m_Scale: {x: 2, y: 3}
        m_Offset: {x: 0.25, y: 0.5}
    m_Floats:
    - _Glossiness: 0.75
    m_Colors:
    - _Color: {r: 0.1, g: 0.2, b: 0.3, a: 1}
)";
    const auto bytes   = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(yaml.data()), yaml.size());
    auto       decoded = decodeUnityVegetationConversionCandidate(bytes, "Assets/Plant.mat");
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().materialShader, std::string("a933075b367f9b24981408633f72ff34"));
    REQUIRE_EQ(decoded.value().materialFloats.at("_Glossiness"), .75);
    REQUIRE_EQ(decoded.value().materialColors.at("_Color")[2], .3);
    REQUIRE_EQ(decoded.value().materialVectors.at("_Color")[0], .1);
    REQUIRE_EQ(decoded.value().materialTextures.at("_MainTex"), std::string("0123456789abcdef0123456789abcdef"));
    REQUIRE_EQ(decoded.value().materialTextureTransforms.at("_MainTex"), (std::array<double, 4>{2, 3, .25, .5}));
    REQUIRE(decoded.value().materialKeywords.contains("TVE_FEATURE_CLIP"));
    REQUIRE(decoded.value().materialInstancing);
    decoded.value().materialKeywords.insert("_ALPHATEST_ON");
    auto context = makeVegetationPresetContext(decoded.value(), "TVE/Plant Standard", "Oak", "URP", {"Mesh"});
    REQUIRE_EQ(context.shaderName, std::string("TVE/Plant Standard"));
    REQUIRE_EQ(context.materialName, std::string("Oak"));
    REQUIRE_EQ(context.shaderPipeline, std::string("URP"));
    REQUIRE(context.outputOptions.contains("Mesh"));
    REQUIRE(context.materialProperties.contains("_Color"));
    REQUIRE(context.materialProperties.contains("_MainTex"));
    REQUIRE(context.materialTextures.contains("_MainTex"));
    REQUIRE(context.materialKeywords.contains("_ALPHATEST_ON"));
}

TEST_CASE("asset.import.vegetationPresetMaterialBridgeRoundTripsAppliedCandidate") {
    VegetationConversionCandidate candidate;
    candidate.materialShader                         = "BOXOPHOBIC/The Vegetation Engine/Geometry/Plant Subsurface Lit";
    candidate.materialFloats["_MainSmoothnessValue"] = .625;
    candidate.materialColors["_MainColor"]           = {.1, .2, .3, 1};
    candidate.materialVectors["_MainUVs"]            = {2, 3, .25, .5};
    candidate.materialTextures["_MainAlbedoTex"]     = "0123456789abcdef0123456789abcdef";
    candidate.materialTextureTransforms["_MainAlbedoTex"] = {2, 3, .25, .5};
    candidate.materialKeywords.insert("TVE_FEATURE_CLIP");
    candidate.materialInstancing = true;
    auto encoded                 = encodeUnityVegetationConversionMaterial(candidate);
    REQUIRE(encoded.ok());
    auto decoded = decodeUnityVegetationConversionCandidate(encoded.value(), "Converted.mat");
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().materialShader, std::string("a933075b367f9b24981408633f72ff34"));
    REQUIRE_EQ(decoded.value().materialFloats.at("_MainSmoothnessValue"), .625);
    REQUIRE_EQ(decoded.value().materialColors.at("_MainColor"), candidate.materialColors.at("_MainColor"));
    REQUIRE_EQ(decoded.value().materialVectors.at("_MainUVs"), candidate.materialVectors.at("_MainUVs"));
    REQUIRE_EQ(decoded.value().materialTextures.at("_MainAlbedoTex"), candidate.materialTextures.at("_MainAlbedoTex"));
    REQUIRE(decoded.value().materialKeywords.contains("TVE_FEATURE_CLIP"));
    REQUIRE(decoded.value().materialInstancing);
    candidate.materialTextures["_Broken"] = "asset://not-a-unity-guid";
    REQUIRE(!encodeUnityVegetationConversionMaterial(candidate).ok());
}

TEST_CASE("asset.import.vegetationPresetExecutesUnitySourceToDetachedOutputsAtomically") {
    const std::string                yaml = R"(%YAML 1.1
--- !u!21 &2100000
Material:
  m_Name: Oak
  m_Shader: {fileID: 4800000, guid: a933075b367f9b24981408633f72ff34, type: 3}
  m_SavedProperties:
    m_Floats:
    - _Mode: 1
)";
    UnityVegetationConversionRequest request;
    request.materialYaml              = {yaml.begin(), yaml.end()};
    request.sourcePath                = "Assets/Oak.mat";
    request.shaderName                = "BOXOPHOBIC/The Vegetation Engine/Geometry/Plant Standard Lit";
    request.materialName              = "Oak";
    request.shaderPipeline            = "BuiltIn";
    request.rootPreset                = "Root";
    request.presetDefinitions["Root"] = Value::Object{
        {"schema", "eve.vegetation-conversion-preset"},
        {"schemaVersion", std::int64_t(1)},
        {"sourcePath", "Root.tvepreset"},
        {"statements", Value::Array{Value::Object{
                           {"kind", "condition"},
                           {"predicate", "MATERIAL_FLOAT_EQUALS"},
                           {"negated", false},
                           {"arguments", Value::Array{"_Mode", "1"}},
                           {"statements", Value::Array{Value::Object{{"kind", "command"},
                                                                     {"domain", "Material"},
                                                                     {"operation", "SET_FLOAT"},
                                                                     {"arguments", Value::Array{"_Converted", "7"}},
                                                                     {"sourcePath", "Root.tvepreset"}}}}}}}};
    auto converted = executeUnityVegetationConversion(request);
    REQUIRE(converted.ok());
    REQUIRE_EQ(converted.value().candidate.materialFloats.at("_Converted"), 7.0);
    REQUIRE(!converted.value().mesh.has_value());
    REQUIRE(converted.value().textures.empty());
    request.rootPreset = "Missing";
    auto rejected      = executeUnityVegetationConversion(request);
    REQUIRE(!rejected.ok());
    REQUIRE_EQ(request.rootPreset, std::string("Missing"));
}

TEST_CASE("asset.import.vegetationPresetPreparesMaterialPackedImageAndMeshAtomically") {
    const std::string                 yaml = R"(%YAML 1.1
--- !u!21 &2100000
Material:
  m_Name: Oak
  m_Shader: {fileID: 4800000, guid: 7befaa6f41d00a6478d5f4af21d66518, type: 3}
  m_SavedProperties:
    m_Floats:
    - _RenderMode: 0
)";
    UnityVegetationAssetImportRequest request;
    request.package = {*PersistentId::parse("22222222-3333-4444-8666-777777777777"), "converted.oak", "12.6.0", {}};
    request.materialGuid              = "abcdefabcdefabcdefabcdefabcdefab";
    request.conversion.materialYaml   = {yaml.begin(), yaml.end()};
    request.conversion.sourcePath     = "Assets/Oak.mat";
    request.conversion.shaderName     = "BOXOPHOBIC/The Vegetation Engine/Geometry/Plant Standard Lit";
    request.conversion.materialName   = "Oak";
    request.conversion.shaderPipeline = "Standard";
    request.conversion.rootPreset     = "Root";
    request.conversion.mesh.emplace();
    request.conversion.mesh->positions           = {0, 0, 0, 0, 1, 0, 1, 1, 0};
    request.conversion.mesh->normals             = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    request.conversion.mesh->indices             = {0, 1, 2};
    request.conversion.presetDefinitions["Root"] = Value::Object{
        {"schema", "eve.vegetation-conversion-preset"},
        {"schemaVersion", std::int64_t(1)},
        {"sourcePath", "Root.tvepreset"},
        {"statements",
         Value::Array{Value::Object{{"kind", "command"},
                                    {"domain", "Shader"},
                                    {"operation", "SHADER_STANDARD_PLANT"},
                                    {"arguments", Value::Array{"BOXOPHOBIC/The", "Vegetation", "Engine/Geometry/Plant",
                                                               "Standard", "Lit"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Material"},
                                    {"operation", "SET_SHADER"},
                                    {"arguments", Value::Array{"SHADER_STANDARD_PLANT"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Utility"},
                                    {"operation", "START_TEXTURE_PACKING"},
                                    {"arguments", Value::Array{}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Texture"},
                                    {"operation", "SetRed"},
                                    {"arguments", Value::Array{"NONE"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Texture"},
                                    {"operation", "SetGreen"},
                                    {"arguments", Value::Array{"NONE"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Texture"},
                                    {"operation", "SetBlue"},
                                    {"arguments", Value::Array{"NONE"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Texture"},
                                    {"operation", "SetAlpha"},
                                    {"arguments", Value::Array{"NONE"}},
                                    {"sourcePath", "Root.tvepreset"}},
                      Value::Object{{"kind", "command"},
                                    {"domain", "Texture"},
                                    {"operation", "PropName"},
                                    {"arguments", Value::Array{"_MainMaskTex"}},
                                    {"sourcePath", "Root.tvepreset"}}}}};
    auto prepared = prepareUnityVegetationConversionImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(std::count_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                          [](const auto& asset) { return asset.type == "eve.material"; }) == 1);
    REQUIRE(std::count_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                          [](const auto& asset) { return asset.type == "eve.mesh"; }) == 1);
    REQUIRE(std::count_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                          [](const auto& asset) { return asset.type == "eve.image"; }) >= 1);
    REQUIRE(prepared.value().manifest.entrypoints.contains("converted-mesh"));
    REQUIRE(std::count_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                          [](const auto& entry) { return entry.path == "reports/import.json"; }) == 1);
    auto archive = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archive.ok());
    auto parsed = asset::parseEvaArchive(archive.value());
    REQUIRE(parsed.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(parsed.value(), profile.value());
    REQUIRE(cooked.ok());
    REQUIRE(cooked.value().chunkCount >= 3);
    request.outputKey            = "oak";
    auto second                  = request;
    second.outputKey             = "pine";
    second.materialGuid          = "123456789abcdef0123456789abcdef0";
    second.conversion.sourcePath = "Assets/Pine.mat";
    UnityVegetationBatchImportRequest batchRequest{request.package, {request, second}, request.limits};
    auto                              batch = prepareUnityVegetationConversionBatch(batchRequest);
    REQUIRE(batch.ok());
    REQUIRE(batch.value().manifest.entrypoints.contains("oak"));
    REQUIRE(batch.value().manifest.entrypoints.contains("oak#converted-mesh"));
    REQUIRE(batch.value().manifest.entrypoints.contains("pine"));
    REQUIRE(batch.value().manifest.entrypoints.contains("pine#converted-mesh"));
    REQUIRE(std::count_if(batch.value().entries.begin(), batch.value().entries.end(),
                          [](const auto& entry) { return entry.path == "reports/import.json"; }) == 1);
    batchRequest.objects[1].outputKey = "oak";
    REQUIRE(!prepareUnityVegetationConversionBatch(batchRequest).ok());
    request.conversion.rootPreset = "Missing";
    REQUIRE(!prepareUnityVegetationConversionImport(request).ok());
}

TEST_CASE("asset.import.vegetationPresetOutputPlanControlsDetachedPublication") {
    asset::CanonicalMeshData mesh;
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    mesh.normals   = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.indices   = {0, 1, 2};
    std::vector<VegetationPresetCommand> commands{{"OutputMeshes", "NONE", {}, "output"},
                                                  {"OutputMaterials", "NONE", {}, "output"},
                                                  {"OutputTextures", "SAVE_TEXTURES_AS_TGA", {}, "output"},
                                                  {"OutputTransforms", "KEEP_ORIGINAL_TRANSFORMS", {}, "output"},
                                                  {"Mesh", "SetHeight", {"GET_MASK_PROCEDURAL", "4"}, "mesh"},
                                                  {"Utility", "START_TEXTURE_PACKING", {}, "texture"},
                                                  {"Texture", "PropName", {"_Packed"}, "texture"},
                                                  {"Texture", "SetRed", {"GET_RED", "_Source"}, "texture"}};
    VegetationPresetImage                sourceImage{1, 1, {255, 0, 0, 255}};
    auto converted = executeVegetationConversion({}, commands, mesh, {{"_Source", sourceImage}});
    REQUIRE(converted.ok());
    REQUIRE(!converted.value().mesh.has_value());
    REQUIRE_EQ(converted.value().meshOutput, VegetationMeshOutputMode::Off);
    REQUIRE_EQ(converted.value().materialOutput, VegetationMaterialOutputMode::Off);
    REQUIRE_EQ(converted.value().textureOutput, VegetationTextureOutputEncoding::Tga);
    REQUIRE_EQ(converted.value().transformOutput, VegetationTransformOutputMode::KeepOriginal);
    REQUIRE(converted.value().textures.empty());
}

TEST_CASE("asset.import.vegetationPresetTransformsPointsAndUnityDirectionsToWorld") {
    asset::CanonicalMeshData mesh;
    mesh.positions             = {1, 1, 0, 0, 0, 0, 0, 1, 0};
    mesh.normals               = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.indices               = {0, 1, 2};
    mesh.attributes["TANGENT"] = {4, {1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1}};
    const std::array<float, 16> point{2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 10, 5, 6, 1};
    const std::array<float, 9>  direction{1, 0, 0, 0, 1, 0, 0, 0, 1};
    auto                        converted = executeVegetationConversion({}, {}, mesh, {}, 1, point, direction);
    REQUIRE(converted.ok());
    REQUIRE_EQ(converted.value().mesh->positions[0], 12.f);
    REQUIRE_EQ(converted.value().mesh->positions[1], 8.f);
    REQUIRE_EQ(converted.value().mesh->positions[2], 6.f);
    REQUIRE_EQ(converted.value().mesh->normals[1], 1.f);
    REQUIRE_EQ(converted.value().mesh->attributes.at("TANGENT").values[0], 1.f);
    REQUIRE_EQ(converted.value().mesh->attributes.at("TANGENT").values[3], -1.f);
    auto singular = direction;
    singular[8]   = 0;
    REQUIRE(!executeVegetationConversion({}, {}, mesh, {}, 1, point, singular).ok());
}

// asset_package_store.cpp
using namespace eve;
using namespace eve::asset;

namespace {

PersistentId packageStoreId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

std::filesystem::path packageStoreRoot() {
    return std::filesystem::temp_directory_path() /
           ("eve_package_store_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

EvpackBuild packageBuild(std::string_view buildId, std::uint8_t payload) {
    EvpackBuild build;
    build.packageId = packageStoreId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = packageStoreId(buildId);
    build.variants.push_back({"linux", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}});
    build.chunks.push_back({packageStoreId("550e8400-e29b-41d4-a716-446655440000"),
                            "eve.image",
                            SchemaVersion(1),
                            0,
                            EvpackChunkKind::Bulk,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            {payload}});
    return build;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("asset.packageStore.reopensBeforeAtomicPublishAndFailureKeepsOldEvpack") {
    const auto root        = packageStoreRoot();
    const auto destination = root / "terrain.evpack";
    auto       oldBytes    = buildEvpack(packageBuild("018f6f22-2490-7ad2-bf58-4f1dbca31041", 1));
    REQUIRE(oldBytes.ok());
    AtomicAssetPackageStore initialStore;
    auto                    initial = initialStore.publishEvpack(destination, oldBytes.value());
    REQUIRE(initial.ok());
    REQUIRE(std::filesystem::is_regular_file(destination));
    const auto oldDiskBytes = readFile(destination);

    auto replacement = buildEvpack(packageBuild("018f6f22-2490-7ad2-bf58-4f1dbca31042", 2));
    REQUIRE(replacement.ok());
    AtomicAssetPackageStore rejectingStore([](const auto&, const auto&) { return PackagePublishGateDecision::Reject; });
    auto                    rejected = rejectingStore.publishEvpack(destination, replacement.value());
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::Cancelled);
    CHECK_EQ(readFile(destination), oldDiskBytes);
    auto stillValid = parseEvpack(readFile(destination));
    REQUIRE(stillValid.ok());
    CHECK_EQ(stillValid.value().buildId(), packageStoreId("018f6f22-2490-7ad2-bf58-4f1dbca31041"));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("asset.packageStore.publishesAndReopensEva") {
    constexpr std::string_view json     = R"json({
      "schema":"eve.asset-archive","schemaVersion":1,
      "packageId":"018f6f22-2490-7ad2-bf58-4f1dbca31040",
      "packageName":"store.test","packageVersion":"1.0.0","unknownFields":"preserve",
      "assets":[],"dependencies":[],"entrypoints":{},"provenance":{}
    })json";
    auto                       manifest = parseEvaManifest(json);
    REQUIRE(manifest.ok());
    const auto              root        = packageStoreRoot();
    const auto              destination = root / "source.eva";
    AtomicAssetPackageStore store;
    auto receipt = store.publishEva(destination, manifest.value(), {{"sources/readme.txt", {'o', 'k'}}});
    REQUIRE(receipt.ok());
    auto parsed = parseEvaArchive(readFile(destination));
    REQUIRE(parsed.ok());
    CHECK_EQ(parsed.value().manifest.packageName, std::string("store.test"));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

// asset_physics_cloth.cpp
using namespace eve;

namespace {

PersistentId persistentId(std::string_view text) {
    auto parsed = PersistentId::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

AssetRef assetRef(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.hasValue());
    return std::move(parsed).takeValue();
}

std::shared_ptr<const asset::Evpack> makeClothPack(const Value& definition, bool addUnexpectedChunk = false) {
    auto encoded = asset::encodeRuntimeDefinition(definition);
    REQUIRE(encoded.hasValue());
    const auto         id = persistentId("550e8400-e29b-41d4-a716-446655440020");
    asset::EvpackBuild build;
    build.packageId = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId   = persistentId("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}}};
    build.chunks    = {{id,
                        "eve.cloth-model",
                        SchemaVersion(1),
                        0,
                        asset::EvpackChunkKind::Definition,
                        0,
                        asset::EvpackCodec::None,
                        8,
                        {},
                        std::move(encoded).takeValue()}};
    if (addUnexpectedChunk)
        build.chunks.push_back({id,
                                "eve.cloth-model",
                                SchemaVersion(1),
                                0,
                                asset::EvpackChunkKind::Bulk,
                                1,
                                asset::EvpackCodec::None,
                                8,
                                {},
                                {1}});
    auto bytes = asset::buildEvpack(std::move(build));
    REQUIRE(bytes.hasValue());
    auto parsed = asset::parseEvpack(bytes.value());
    REQUIRE(parsed.hasValue());
    return std::make_shared<const asset::Evpack>(std::move(parsed).takeValue());
}

asset::EvpackCapabilities windowsCapabilities() {
    return {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}};
}

}  // namespace

TEST_CASE("asset.physics.clothModelLoadsFromCanonicalEvpack") {
    auto model = physics::ClothModel::grid(3, 4, 0.5f, -1.f, 2.f, 3.f);
    REQUIRE(model.hasValue());
    asset::EvpackResourceReader           reader(makeClothPack(model.value().toValue()));
    asset_physics::EvpackClothModelLoader loader(reader);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440020"), windowsCapabilities());
    REQUIRE(loaded.hasValue());
    CHECK(loaded.value().model.gridCols() == 3);
    CHECK(loaded.value().model.gridRows() == 4);
    CHECK(loaded.value().model.particles().size() == 12);

    physics::Cloth3D runtime(loaded.value().model);
    CHECK(runtime.getParticleCount() == 12);
    CHECK(runtime.isPinned(0));
}

TEST_CASE("asset.physics.clothModelRejectsNonDefinitionPayloadBeforeRuntimeCreation") {
    auto model = physics::ClothModel::grid(2, 2, 0.5f, 0.f, 0.f, 0.f);
    REQUIRE(model.hasValue());
    asset::EvpackResourceReader           reader(makeClothPack(model.value().toValue(), true));
    asset_physics::EvpackClothModelLoader loader(reader);
    auto loaded = loader.load(assetRef("asset://550e8400-e29b-41d4-a716-446655440020"), windowsCapabilities());
    CHECK(!loaded.hasValue());
    REQUIRE(loaded.error() != nullptr);
    CHECK(loaded.error()->code() == DiagnosticCode::ParseError);
}

// asset_resource_reader.cpp
using namespace eve;
using namespace eve::asset;

namespace {
PersistentId id(std::string_view text) {
    auto value = PersistentId::parse(text);
    REQUIRE(value);
    return *value;
}
AssetRef ref(std::string_view text) {
    auto value = AssetRef::parse(text);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}
}  // namespace

TEST_CASE("asset.resourceReader.selectsBackendVariantAndPreservesCanonicalDefinition") {
    const auto  assetId       = id("550e8400-e29b-41d4-a716-446655440000");
    const auto  secondAssetId = id("550e8400-e29b-41d4-a716-446655440001");
    EvpackBuild input;
    input.packageId                           = id("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    input.buildId                             = id("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    input.variants                            = {{"windows", "x86_64", "vulkan", {"bc"}, "spirv-1.6", "high", {}},
                                                 {"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}}};
    const std::vector<std::uint8_t> canonical = {'{', '"', 'v', '"', ':', '1', '}'};
    input.chunks                              = {
        {assetId, "eve.mesh", SchemaVersion(1), 0, EvpackChunkKind::Definition, 0, EvpackCodec::Zstd, 8, {}, canonical},
        {assetId, "eve.mesh", SchemaVersion(1), 0, EvpackChunkKind::Bulk, 1, EvpackCodec::Zstd, 8, {}, {1, 2, 3}},
        {secondAssetId,
                                      "eve.mesh",
                                      SchemaVersion(1),
                                      0,
                                      EvpackChunkKind::Definition,
                                      0,
                                      EvpackCodec::Zstd,
                                      8,
                                      {},
                                      canonical},
        {assetId,
                                      "eve.mesh",
                                      SchemaVersion(1),
                                      1,
                                      EvpackChunkKind::Definition,
                                      0,
                                      EvpackCodec::Zstd,
                                      8,
                                      {},
                                      canonical}};
    auto bytes = buildEvpack(std::move(input));
    REQUIRE(bytes.ok());
    auto parsed = parseEvpack(bytes.value());
    REQUIRE(parsed.ok());
    auto                 pack = std::make_shared<const Evpack>(std::move(parsed).takeValue());
    EvpackResourceReader reader(pack);
    auto                 windows = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.mesh/1",
                                               {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 1024);
    auto                 web     = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.mesh/1",
                                               {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}}, 1024);
    REQUIRE(windows.ok());
    REQUIRE(web.ok());
    CHECK_EQ(windows.value().chunks.front().bytes, canonical);
    CHECK_EQ(web.value().chunks.front().bytes, canonical);
    CHECK_EQ(windows.value().variant.index, std::uint32_t(0));
    CHECK_EQ(web.value().variant.index, std::uint32_t(1));

    auto mismatch = reader.read(ref("asset://550e8400-e29b-41d4-a716-446655440000"), "eve.image/2",
                                {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 1024);
    REQUIRE(!mismatch.ok());
    CHECK_EQ(mismatch.error()->code(), DiagnosticCode::TypeMismatch);

    auto windowsAssets =
        reader.listAssets("eve.mesh/1", {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 2);
    REQUIRE(windowsAssets.ok());
    REQUIRE_EQ(windowsAssets.value().size(), std::size_t(2));
    CHECK_EQ(windowsAssets.value().at(0).id(), assetId);
    CHECK_EQ(windowsAssets.value().at(1).id(), secondAssetId);
    auto overBudget =
        reader.listAssets("eve.mesh/1", {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 1);
    REQUIRE(!overBudget.ok());
    CHECK_EQ(overBudget.error()->code(), DiagnosticCode::InvalidArgument);
}
}  // namespace test_asset_image_mips
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import)
#include "PrivateTestScope.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/import/AssetImporter.h"
#include "graphics/IMeshResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import.cpp
namespace test_asset_import {
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

PersistentId importId() { return *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"); }

ImportPackageIdentity identity(std::string name) {
    return {
        importId(),
        std::move(name),
        "1.0.0",
        {{"provider", Value("local")}, {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}

std::vector<std::uint8_t> fileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

const EvaArchiveEntry* entry(const PreparedAssetImport& prepared, std::string_view path) {
    const auto found = std::find_if(prepared.entries.begin(), prepared.entries.end(),
                                    [&](const auto& value) { return value.path == path; });
    return found == prepared.entries.end() ? nullptr : &*found;
}

AssetCookProfile linuxProfile() {
    return {{"linux", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, CookPublication::LocalInspection, 16};
}

class VerticalMeshFactory final : public eve::graphics::IMeshResourceFactory {
public:
    Result<eve::graphics::Mesh*> uploadMesh(const float*, const float*, const float*, int vertices,
                                            const std::uint32_t*, int indices) override {
        vertexCount = vertices;
        indexCount  = indices;
        return Result<eve::graphics::Mesh*>::success(reinterpret_cast<eve::graphics::Mesh*>(&token));
    }
    Result<void> releaseMesh(eve::graphics::Mesh*) override { return Result<void>::success(); }
    Result<void> setMeshTangentFrame(eve::graphics::Mesh*, std::span<const float> tangents,
                                     std::span<const float> bitangents) override {
        REQUIRE_EQ(tangents.size(), size_t(vertexCount) * 3);
        REQUIRE_EQ(bitangents.size(), tangents.size());
        tangentValues.assign(tangents.begin(), tangents.end());
        bitangentValues.assign(bitangents.begin(), bitangents.end());
        return Result<void>::success();
    }
    std::vector<float> tangentValues, bitangentValues;
    int                vertexCount = 0;
    int                indexCount  = 0;

private:
    std::uint64_t token = 0;
};

class VerticalImageFactory final : public eve::graphics::IImageResourceFactory {
public:
    Result<eve::graphics::Texture*> uploadRgba8(std::uint32_t widthValue, std::uint32_t heightValue,
                                                const std::uint8_t* pixels, bool srgbValue) override {
        width  = widthValue;
        height = heightValue;
        srgb   = srgbValue;
        firstPixel.assign(pixels, pixels + 4);
        return Result<eve::graphics::Texture*>::success(reinterpret_cast<eve::graphics::Texture*>(&token));
    }
    Result<void>              releaseImage(eve::graphics::Texture*) override { return Result<void>::success(); }
    std::uint32_t             width  = 0;
    std::uint32_t             height = 0;
    bool                      srgb   = true;
    std::vector<std::uint8_t> firstPixel;

private:
    std::uint64_t token = 0;
};

std::vector<uint8_t> pixelPng() {
    return {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
            0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
            0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
            0x72, 0x9c, 0x52, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
}

}  // namespace

TEST_CASE("asset.import.pngTraversesEvaCookAndEvpack") {
    // Complete 1x1 RGBA PNG. The source is retained losslessly in canonical eve.image.
    const auto png        = pixelPng();
    auto       corruptPng = png;
    corruptPng[45] ^= 1;
    auto corrupt =
        prepareImageImport({identity("image.corrupt"), "corrupt.png", corruptPng, ImageColorSpace::Srgb, "color", {}});
    REQUIRE(!corrupt.ok());
    CHECK_EQ(corrupt.error()->code(), DiagnosticCode::HashMismatch);
    auto prepared = prepareImageImport({identity("image.test"), "pixel.png", png, ImageColorSpace::Srgb, "color", {}});
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(1));
    REQUIRE(entry(prepared.value(), "reports/import.json") != nullptr);
    CHECK_EQ(prepared.value().sourceMappings.size(), std::size_t(1));
    CHECK(prepared.value().manifest.provenance.contains("importKey"));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    auto cooked = cookEvaToEvpack(eva.value(), linuxProfile());
    REQUIRE(cooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    REQUIRE(runtime.ok());
    CHECK_EQ(runtime.value().chunks().size(), std::size_t(2));
    auto runtimeDefinition = runtime.value().decodeChunk(0, 4096);
    auto runtimePixels     = runtime.value().decodeChunk(1, 4096);
    REQUIRE(runtimeDefinition.ok());
    REQUIRE(runtimePixels.ok());
    auto definition = decodeRuntimeDefinition(runtimeDefinition.value());
    REQUIRE(definition.ok());
    const auto* definitionObject = definition.value().getIf<Value::Object>();
    REQUIRE(definitionObject != nullptr);
    CHECK_EQ(definitionObject->at("encoding"), Value("rgba8"));
    REQUIRE_EQ(runtimePixels.value().size(), std::size_t(32));
    CHECK_EQ(std::string(reinterpret_cast<const char*>(runtimePixels.value().data()), 5), std::string("EVIMG"));

    auto                                   admitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    EvpackResourceReader                   reader(admitted);
    VerticalImageFactory                   imageFactory;
    eve::asset_graphics::EvpackImageLoader imageLoader(reader, imageFactory);
    auto                                   uploaded = imageLoader.load(prepared.value().manifest.assets.front().asset,
                                                                       {"linux", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value().texture != nullptr);
    CHECK_EQ(imageFactory.width, std::uint32_t(1));
    CHECK_EQ(imageFactory.height, std::uint32_t(1));
    CHECK(!imageFactory.srgb);
    CHECK_EQ(imageFactory.firstPixel.size(), std::size_t(4));

    auto unsupportedProfile                    = linuxProfile();
    unsupportedProfile.variant.textureFamilies = {"bc"};
    auto unsupported                           = cookEvaToEvpack(eva.value(), unsupportedProfile);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.realGlbDecodesCanonicalMeshAndTraversesRuntimePack") {
    const std::string path  = "test/assets/housegen/kenney-modular-buildings/building-block.glb";
    auto              bytes = fileBytes(path);
    REQUIRE(!bytes.empty());
    GltfImportRequest request{identity("gltf.test"), "building-block.glb", std::move(bytes), {}, {}};
    // The geometry fixture omits its external colormap. Supply an explicit test image;
    // this verifies mesh/package traversal, not the original material appearance.
    request.externalResources["Textures/colormap.png"] = pixelPng();
    auto prepared                                      = prepareGltfImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().manifest.assets.empty());
    REQUIRE(entry(prepared.value(), "reports/import.json") != nullptr);
    const auto meshCount =
        std::count_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                      [](const auto& a) { return a.type == "eve.mesh"; });
    REQUIRE_EQ(prepared.value().sourceMappings.size(), size_t(meshCount));
    CHECK_EQ(prepared.value().manifest.assets.front().type, std::string("eve.mesh"));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    auto cooked = cookEvaToEvpack(eva.value(), linuxProfile());
    REQUIRE(cooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    REQUIRE(runtime.ok());
    bool foundMesh = false;
    for (size_t i = 0; i < runtime.value().chunks().size(); ++i) {
        const auto& chunk = runtime.value().chunks()[i];
        if (chunk.type != "eve.mesh" || chunk.kind != EvpackChunkKind::Bulk) continue;
        auto meshBlob = runtime.value().decodeChunk(i, chunk.decodedSize);
        REQUIRE(meshBlob.ok());
        REQUIRE(meshBlob.value().size() >= 8);
        REQUIRE_EQ(std::string(reinterpret_cast<const char*>(meshBlob.value().data()), 6), std::string("EVMESH"));
        foundMesh = true;
    }
    REQUIRE(foundMesh);

    const auto entrypoint = prepared.value().manifest.entrypoints.find("default");
    REQUIRE(entrypoint != prepared.value().manifest.entrypoints.end());
    auto                                      admitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    EvpackResourceReader                      reader(admitted);
    VerticalMeshFactory                       factory;
    eve::asset_graphics::EvpackGraphicsLoader loader(reader, factory);
    auto                                      uploaded =
        loader.loadMesh(entrypoint->second, {"linux", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value().mesh != nullptr);
    CHECK(factory.vertexCount > 0);
    CHECK(factory.indexCount > 0);
}

TEST_CASE("asset.import.gltfRejectsNetworkAndTraversalUris") {
    const std::string json = R"json({"asset":{"version":"2.0"},"buffers":[
      {"uri":"../outside.bin","byteLength":4}],"bufferViews":[],"accessors":[],"meshes":[]})json";
    GltfImportRequest request{identity("gltf.unsafe"), "unsafe.gltf", {json.begin(), json.end()}, {}, {}};
    auto              rejected = prepareGltfImport(request);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.tamperedStructuredReportIsRejectedBeforePublication") {
    const std::vector<std::uint8_t> png = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    auto prepared =
        prepareImageImport({identity("image.report-security"), "pixel.png", png, ImageColorSpace::Srgb, "color", {}});
    REQUIRE(prepared.ok());
    auto report = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                               [](const auto& value) { return value.path == "reports/import.json"; });
    REQUIRE(report != prepared.value().entries.end());
    std::string reportText(report->bytes.begin(), report->bytes.end());
    const auto  source = reportText.find("\"sourceEngine\":\"image\"");
    REQUIRE(source != std::string::npos);
    reportText.replace(source, std::string("\"sourceEngine\":\"image\"").size(), "\"sourceEngine\":\"evil!\"");
    report->bytes.assign(reportText.begin(), reportText.end());
    auto rejected = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::HashMismatch);
}
}  // namespace test_asset_import
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_animation)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <memory>
#include <sstream>
#include "asset/AssetCooker.h"
#include "asset/CanonicalImageCook.h"
#include "asset/CanonicalVolumeTextureCook.h"
#include "asset/EvpackResourceReader.h"
#include "asset/SourceBc3.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/import/LegacyAnimationImporter.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnrealImporter.h"
#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/EvpackPointGraphLoader.h"
#include "asset/procgen/EvpackTerrainMaterialLoader.h"
#include "graphics/Graphics.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "procgen/PointGraph.h"
#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_animation.cpp
namespace test_asset_import_animation {
// asset_import_animation.cpp
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {
ImportPackageIdentity identity() {
    auto id = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    REQUIRE(id.has_value());
    return {
        *id, "mixamo.idle", "1.0.0", {{"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}
std::string fixture() {
    std::ifstream input("test/assets/mixamo/Idle.anim.txt");
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
AssetCookProfile target(std::string os, std::string arch, std::string graphics, std::string shader) {
    AssetCookProfile result;
    result.variant    = {std::move(os), std::move(arch), std::move(graphics), {"rgba8"}, std::move(shader), "high", {}};
    result.chunkCodec = EvpackCodec::Zstd;
    return result;
}
}  // namespace

TEST_CASE("asset.import.animationFixtureBecomesCanonicalEvaAndTwoRuntimeTargets") {
    auto prepared = prepareLegacyAnimationImport({identity(), "Idle.anim.txt", fixture(), {}});
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    REQUIRE_EQ(prepared.value().manifest.dependencies.size(), std::size_t(1));
    CHECK_EQ(prepared.value().manifest.dependencies.front().expectedType, std::string("eve.skeleton/1"));
    auto archiveBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());

    const auto skeleton =
        std::find_if(archive.value().entries.begin(), archive.value().entries.end(),
                     [](const EvaArchiveEntry& entry) { return entry.path.ends_with("skeleton.bin"); });
    REQUIRE(skeleton != archive.value().entries.end());
    REQUIRE(skeleton->bytes.size() > 32);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(skeleton->bytes.data()), 6), std::string("EVSKEL"));
    // First root bind translation remains zero after centimeter -> meter conversion.
    const std::uint32_t firstPositionBits =
        std::uint32_t(skeleton->bytes[28]) | (std::uint32_t(skeleton->bytes[29]) << 8) |
        (std::uint32_t(skeleton->bytes[30]) << 16) | (std::uint32_t(skeleton->bytes[31]) << 24);
    CHECK_EQ(std::bit_cast<float>(firstPositionBits), 0.0f);

    auto windows = cookEvaToEvpack(archive.value(), target("windows", "x86_64", "vulkan", "spirv-1.6"));
    auto web     = cookEvaToEvpack(archive.value(), target("web", "wasm32", "webgpu", "wgsl-1"));
    REQUIRE(windows.ok());
    REQUIRE(web.ok());
    CHECK(windows.value().buildId != web.value().buildId);
    REQUIRE(parseEvpack(windows.value().bytes).ok());
    REQUIRE(parseEvpack(web.value().bytes).ok());
}

TEST_CASE("asset.import.animationFixtureRejectsDeclaredKeyCountMismatch") {
    std::string text  = fixture();
    const auto  track = text.find("track ");
    REQUIRE(track != std::string::npos);
    const auto firstCount = text.find(' ', track + 6);
    REQUIRE(firstCount != std::string::npos);
    text.insert(firstCount + 1, "9");
    auto rejected = prepareLegacyAnimationImport({identity(), "corrupt.anim.txt", std::move(text), {}});
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}

// asset_import_bc3.cpp
using namespace eve;
TEST_CASE("asset.import.unityNativeTextureValidatesMipChainAndPreservesSource") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "bc3.test", "1.0.0", {}};
    const std::string source =
        "--- !u!28 &2800000\nTexture2D:\n  m_Width: 4\n  m_Height: 4\n"
        "  m_MipCount: 1\n  m_ColorSpace: 1\n  m_TextureFormat: 12\n  m_TextureDimension: 2\n"
        "  m_TextureSettings:\n    m_WrapU: 1\n    m_WrapV: 2\n    m_FilterMode: 2\n"
        "  m_ImageCount: 1\n  m_CompleteImageSize: 16\n  image data: 16\n"
        "  _typelessdata: ff000000000000001f0000f8e4e4e4e4\n  m_StreamData:\n    size: 0\n    path:\n";
    const std::string meta                    = "fileFormatVersion: 2\nguid: 86017fa9aef64ed4d9209b3d2f1e126f\n";
    request.files["Assets/Leaves.asset"]      = {source.begin(), source.end()};
    request.files["Assets/Leaves.asset.meta"] = {meta.begin(), meta.end()};
    auto imported                             = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.size(), std::size_t(1));
    bool retained = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path == "sources/unity/Assets/Leaves.asset") {
            REQUIRE_EQ(entry.bytes, request.files.at("Assets/Leaves.asset"));
            retained = true;
        }
    REQUIRE(retained);
    const std::string material =
        "--- !u!21 &2100000\nMaterial:\n"
        "  m_Shader: {fileID: 4800000, guid: 7befaa6f41d00a6478d5f4af21d66518, type: 3}\n"
        "  m_SavedProperties:\n    m_TexEnvs:\n    - _MainAlbedoTex:\n"
        "        m_Texture: {fileID: 2800000, guid: 86017fa9aef64ed4d9209b3d2f1e126f, type: 2}\n"
        "    m_Floats: []\n    m_Colors: []\n";
    const std::string materialMeta          = "fileFormatVersion: 2\nguid: 1234567890abcdef1234567890abcdef\n";
    request.files["Assets/Leaves.mat"]      = {material.begin(), material.end()};
    request.files["Assets/Leaves.mat.meta"] = {materialMeta.begin(), materialMeta.end()};
    auto bound                              = asset_import::prepareUnityProjectImport(request);
    REQUIRE(bound.ok());
    REQUIRE_EQ(bound.value().manifest.assets.size(), std::size_t(3));
    bool linked = false;
    for (const auto& dependency : bound.value().manifest.dependencies)
        if (dependency.to == imported.value().manifest.assets.front().asset) linked = true;
    REQUIRE(linked);
    bool samplerChecked = false;
    for (const auto& entry : bound.value().entries) {
        if (!entry.path.ends_with("/asset.json")) continue;
        auto json = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
        REQUIRE(json.ok());
        const auto& object = *json.value().getIf<Value::Object>();
        if (!object.contains("baseColorTextureSampler")) continue;
        const auto& sampler = *object.at("baseColorTextureSampler").getIf<Value::Object>();
        REQUIRE_EQ(sampler.at("wrapS").asInt(), 33071);
        REQUIRE_EQ(sampler.at("wrapT").asInt(), 33648);
        REQUIRE_EQ(sampler.at("minFilter").asInt(), 9729);
        REQUIRE_EQ(sampler.at("magFilter").asInt(), 9729);
        samplerChecked = true;
    }
    REQUIRE(samplerChecked);
    for (const auto& suffix : {std::string("  m_MipCount: 2\n"), std::string("  m_Width: 4\n")}) {
        auto invalid                         = source + suffix;
        request.files["Assets/Leaves.asset"] = {invalid.begin(), invalid.end()};
        REQUIRE(!asset_import::prepareUnityProjectImport(request).ok());
    }
    auto truncated = source;
    truncated.erase(truncated.find("ff0000"), 2);
    request.files["Assets/Leaves.asset"] = {truncated.begin(), truncated.end()};
    REQUIRE(!asset_import::prepareUnityProjectImport(request).ok());
}
TEST_CASE("asset.import.unityNativeTexturePublishesCompleteMipChain") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "bc3.mips", "1.0.0", {}};
    const std::string source =
        "--- !u!28 &2800000\nTexture2D:\n  m_Width: 4\n  m_Height: 4\n"
        "  m_MipCount: 3\n  m_ColorSpace: 1\n  m_TextureFormat: 12\n  m_TextureDimension: 2\n"
        "  m_ImageCount: 1\n  m_CompleteImageSize: 48\n  image data: 48\n"
        "  _typelessdata: ff000000000000001f0000f8e4e4e4e4ff000000000000001f0000f8e4e4e4e4"
        "ff000000000000001f0000f8e4e4e4e4\n  m_StreamData:\n    size: 0\n    path:\n";
    const std::string meta                    = "fileFormatVersion: 2\nguid: 86017fa9aef64ed4d9209b3d2f1e126f\n";
    request.files["Assets/Leaves.asset"]      = {source.begin(), source.end()};
    request.files["Assets/Leaves.asset.meta"] = {meta.begin(), meta.end()};
    auto imported                             = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const asset::EvaArchiveEntry* definition = nullptr;
    const asset::EvaArchiveEntry* blob       = nullptr;
    for (const auto& entry : imported.value().entries) {
        if (entry.path.ends_with("/asset.json")) definition = &entry;
        if (entry.path.ends_with("source.rgba8-mips")) blob = &entry;
    }
    REQUIRE(definition != nullptr);
    REQUIRE(blob != nullptr);
    CHECK_EQ(blob->bytes.size(), std::size_t(84));
    auto json = Value::fromJson(std::string(definition->bytes.begin(), definition->bytes.end()));
    REQUIRE(json.ok());
    const auto& object = *json.value().getIf<Value::Object>();
    CHECK_EQ(object.at("encoding").asString(), std::string("rgba8-mips"));
    CHECK_EQ(object.at("mipCount").asInt(), std::int64_t(3));
    auto cooked = asset::cookCanonicalImageRgba8(definition->bytes, blob->bytes, 1024 * 1024);
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().bulk.size(), std::size_t(28 + 84));
    const auto& bytes  = cooked.value().bulk;
    const auto  levels = std::uint32_t(bytes[20]) | (std::uint32_t(bytes[21]) << 8) | (std::uint32_t(bytes[22]) << 16) |
                        (std::uint32_t(bytes[23]) << 24);
    CHECK_EQ(levels, std::uint32_t(3));
}
TEST_CASE("asset.import.bc3EndpointsSelectorsAndPartialBlocks") {
    for (bool descending : {false, true}) {
        std::vector<std::uint8_t> bytes(16);
        bytes[0] = descending ? 255 : 0;
        bytes[1] = descending ? 0 : 255;
        // Ascending color endpoints still require four opaque BC3 colors.
        bytes[8]                = 31;
        bytes[11]               = 248;
        std::uint64_t selectors = 0;
        for (unsigned i = 0; i < 16; ++i) selectors |= std::uint64_t(i % 8) << (3 * i);
        for (unsigned i = 0; i < 6; ++i) bytes[2 + i] = std::uint8_t(selectors >> (8 * i));
        for (unsigned i = 0; i < 4; ++i) bytes[12 + i] = 0xe4;
        auto decoded = asset::detail::decodeBc3Rgba8(bytes, 3, 4, 48);
        REQUIRE(decoded.ok());
        const unsigned alphaDescending[]{255, 0, 218, 182, 145, 109, 72, 36};
        const unsigned alphaAscending[]{0, 255, 51, 102, 153, 204, 0, 255};
        auto           full = asset::detail::decodeBc3Rgba8(bytes, 4, 4, 64);
        REQUIRE(full.ok());
        for (unsigned i = 0; i < 16; ++i)
            REQUIRE_EQ(unsigned(full.value()[i * 4 + 3]), descending ? alphaDescending[i % 8] : alphaAscending[i % 8]);
        REQUIRE_EQ(unsigned(full.value()[12]), 170u);
        REQUIRE_EQ(unsigned(full.value()[14]), 85u);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 3; ++x) {
                const auto at = (y * 3 + x) * 4;
                REQUIRE_EQ(unsigned(decoded.value()[at]), x == 0 ? 0u : x == 1 ? 255u : 85u);
                REQUIRE_EQ(unsigned(decoded.value()[at + 2]), x == 0 ? 255u : x == 1 ? 0u : 170u);
                REQUIRE_EQ(unsigned(decoded.value()[at + 3]),
                           descending ? alphaDescending[(y * 4 + x) % 8] : alphaAscending[(y * 4 + x) % 8]);
            }
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 3, 4, 47).ok());
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 0, 4, 48).ok());
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 0xffffffffu, 0xffffffffu, ~std::uint64_t(0)).ok());
        bytes.pop_back();
        REQUIRE(!asset::detail::decodeBc3Rgba8(bytes, 3, 4, 48).ok());
    }
}

// asset_import_unity_volume_texture.cpp
using namespace eve;

TEST_CASE("asset.volumeTextureCookRejectsMalformedShapeAndBudget") {
    const std::string definition =
        R"({"schema":"eve.volume-texture","schemaVersion":1,"width":2,"height":2,"depth":2,"encoding":"r8","usage":"noise","blob":"assets/id/source.r8"})";
    const std::array<uint8_t, 8> source{0, 16, 32, 64, 128, 192, 224, 255};
    auto                         cooked = asset::cookCanonicalVolumeTextureRgba8(
        {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()}, source, 56);
    REQUIRE(cooked.ok());
    REQUIRE_EQ(cooked.value().bulk.size(), size_t(56));
    REQUIRE_EQ(cooked.value().bulk[24], uint8_t(0));
    REQUIRE_EQ(cooked.value().bulk[28], uint8_t(16));
    REQUIRE_EQ(cooked.value().bulk[31], uint8_t(255));
    REQUIRE(!asset::cookCanonicalVolumeTextureRgba8(
                 {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()},
                 std::span<const uint8_t>(source).first(7), 56)
                 .ok());
    REQUIRE(!asset::cookCanonicalVolumeTextureRgba8(
                 {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()}, source, 55)
                 .ok());
    const std::string unknown = definition.substr(0, definition.size() - 1) + R"(,"extra":1})";
    REQUIRE(!asset::cookCanonicalVolumeTextureRgba8({reinterpret_cast<const uint8_t*>(unknown.data()), unknown.size()},
                                                    source, 56)
                 .ok());
    const std::string overflowing =
        R"({"schema":"eve.volume-texture","schemaVersion":1,"width":4294967295,"height":4294967295,"depth":4294967295,"encoding":"r8","usage":"noise","blob":"assets/id/source.r8"})";
    REQUIRE(!asset::cookCanonicalVolumeTextureRgba8(
                 {reinterpret_cast<const uint8_t*>(overflowing.data()), overflowing.size()}, {}, UINT64_MAX)
                 .ok());
}

TEST_CASE("asset.import.unityTexture3dSurvivesCookAndVulkanUpload") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("b785114f-cde4-4571-9d8d-56d3bbf7ab12"), "volume.fixture", "1.0.0", {}};
    auto put        = [&](const std::string& path, const std::string& text) {
        request.files[path] = {text.begin(), text.end()};
    };
    put("Assets/noise.asset.meta", "fileFormatVersion: 2\nguid: 39df7cffe2e2add468339598342dfac0\n");
    put("Assets/noise.asset", R"(--- !u!117 &11700000
Texture3D:
  m_Name: Internal NoiseTex3D
  m_ColorSpace: 0
  m_Format: 5
  m_Width: 2
  m_Height: 2
  m_Depth: 2
  m_MipCount: 1
  m_DataSize: 8
  image data: 8
  _typelessdata: 0010204080c0e0ff
  m_StreamData:
    offset: 0
    size: 0
    path:
)");
    auto imported = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto volume = imported.value().manifest.entrypoints.at("Assets/noise.asset");
    const auto found  = std::find_if(imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
                                     [&](const auto& asset) { return asset.asset == volume; });
    REQUIRE(found != imported.value().manifest.assets.end());
    REQUIRE_EQ(found->type, std::string("eve.volume-texture"));
    REQUIRE_EQ(found->schemaVersion, SchemaVersion(1));

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

    auto* gfx = graphics::Graphics::create();
    gfx->initHeadless(16, 16);
    if (gfx->getBackendName() != "vulkan") return;
    asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    asset_graphics::EvpackVolumeTextureLoader   loader(reader, factory);
    const auto&                                 variant = profile.value().variant;
    auto                                        loaded  = loader.load(volume,
                                                                      {variant.os,
                                                                       variant.arch,
                                                                       variant.graphics,
                                                                       variant.textureFamilies,
                                                                       {variant.shaderFormat},
                                                                       {variant.quality},
                                                                       variant.features},
                                                                      {16, 4096, 65536});
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().texture->width, 2);
    REQUIRE_EQ(loaded.value().texture->height, 2);
    REQUIRE_EQ(loaded.value().texture->depth, 2);
    REQUIRE(loaded.value().texture->sampler.repeatU);
    REQUIRE(loaded.value().texture->sampler.repeatV);
    REQUIRE(loaded.value().texture->sampler.repeatW);
    graphics::PbrSurface surface;
    surface.vegetationAlpha.noise   = loaded.value().texture;
    surface.vegetationAlpha.enabled = true;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(factory.releaseImage(loaded.value().texture).ok());
}

// asset_import_unreal.cpp
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

ImportPackageIdentity unrealIdentity() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
            "fab.m4-terrain",
            "1.0.0",
            {{"provider", Value("fab")},
             {"productId", Value("m4-representative")},
             {"license", Value(Value::Object{{"redistribution", Value("project-only")}, {"ueOnly", Value(false)}})}}};
}

std::vector<std::uint8_t> text(std::string_view value) { return {value.begin(), value.end()}; }

AssetCookProfile target(std::string os, std::string arch, std::string graphics, std::string texture,
                        std::string shader) {
    return {{std::move(os), std::move(arch), std::move(graphics), {std::move(texture)}, std::move(shader), "high", {}},
            CookPublication::LocalInspection,
            16};
}

}  // namespace

TEST_CASE("asset.import.unrealM4LandscapeTraversesWindowsAndWebRuntimePacks") {
    constexpr std::string_view descriptor = R"json({
      "schema":"eve.unreal-landscape-import","schemaVersion":1,
      "landscape":{"width":3,"height":2,"heightmap":"Content/M4/Landscape.r16",
                   "scaleCm":[100,200,100]},
      "layers":[{"name":"Grass","diffuse":"Content/M4/T_Grass_D.png",
                 "normal":"Content/M4/T_Grass_N.png","weight":"Content/M4/Grass.weight",
                 "tileSizeCm":800}],
      "grass":[{"id":"grass-main","prototype":"/Game/M4/SM_Grass",
                "layer":"Grass","densityPerSquareMeter":2.5,"seed":42}],
      "instances":[{"prototype":"/Game/M4/SM_Rock","positionCm":[100,200,300],
                    "rotationQuat":[0,0,0,1],"scale":[1,2,3]}],
      "unsupported":["Blueprint","RVT","VHFM","Nanite-private-payload"]
    })json";
    // 3x2 UE Landscape samples: 32768, 32896, 32640, 32768, 33024, 32512.
    const std::vector<std::uint8_t> heights = {0x00, 0x80, 0x80, 0x80, 0x80, 0x7f, 0x00, 0x80, 0x00, 0x81, 0x00, 0x7f};
    UnrealProjectImportRequest      request;
    request.package        = unrealIdentity();
    request.descriptorPath = "Config/M4.evimport.json";
    request.files          = {{request.descriptorPath, text(descriptor)},
                              {"Content/M4/Landscape.r16", heights},
                              {"Content/M4/T_Grass_D.png", {1, 2, 3}},
                              {"Content/M4/T_Grass_N.png", {4, 5, 6}},
                              {"Content/M4/Grass.weight", {7, 8, 9}}};
    auto prepared          = prepareUnrealM4Import(request);
    REQUIRE(prepared.ok());
    CHECK_EQ(prepared.value().manifest.assets.size(), std::size_t(4));
    CHECK_EQ(prepared.value().sourceMappings.size(), prepared.value().manifest.assets.size());
    CHECK(std::any_of(prepared.value().entries.begin(), prepared.value().entries.end(),
                      [](const auto& entry) { return entry.path == "reports/import.json"; }));
    CHECK(std::any_of(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.pcg-graph"; }));
    CHECK(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& finding) {
        return finding.feature == "RVT" && finding.disposition == ImportDisposition::Unsupported;
    }));
    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());

    auto windows = cookEvaToEvpack(eva.value(), target("windows", "x86_64", "vulkan", "bc", "spirv-1.6"));
    auto web     = cookEvaToEvpack(eva.value(), target("web", "wasm32", "webgpu", "rgba8", "wgsl-1"));
    REQUIRE(windows.ok());
    REQUIRE(web.ok());
    CHECK(windows.value().buildId != web.value().buildId);
    auto windowsPack = parseEvpack(windows.value().bytes);
    auto webPack     = parseEvpack(web.value().bytes);
    REQUIRE(windowsPack.ok());
    REQUIRE(webPack.ok());
    CHECK_EQ(windowsPack.value().packageId(), webPack.value().packageId());
    CHECK_EQ(windowsPack.value().chunks().size(), webPack.value().chunks().size());
    auto                 windowsAdmitted = std::make_shared<const Evpack>(std::move(windowsPack).takeValue());
    auto                 webAdmitted     = std::make_shared<const Evpack>(std::move(webPack).takeValue());
    EvpackResourceReader windowsReader(windowsAdmitted);
    EvpackResourceReader webReader(webAdmitted);
    for (const auto& sourceAsset : prepared.value().manifest.assets) {
        const std::string expected = sourceAsset.type + "/" + std::to_string(sourceAsset.schemaVersion.value());
        auto              windowsAsset =
            windowsReader.read(sourceAsset.asset, expected,
                               {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, 16 * 1024 * 1024);
        auto webAsset =
            webReader.read(sourceAsset.asset, expected,
                           {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}}, 16 * 1024 * 1024);
        REQUIRE(windowsAsset.ok());
        REQUIRE(webAsset.ok());
        CHECK_EQ(windowsAsset.value().chunks.front().bytes, webAsset.value().chunks.front().bytes);
    }

    const auto pcgAsset = std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                                       [](const auto& asset) { return asset.type == "eve.pcg-graph"; });
    REQUIRE(pcgAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackPointGraphLoader loader(windowsReader);
    auto                                       terrain = eve::procgen::SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 1.f);
    auto                                       loaded =
        loader.load(pcgAsset->asset, {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}}, terrain);
    REQUIRE(loaded.ok());
    std::unique_ptr<eve::procgen::PointSet> first(loaded.value().graph->execute(loaded.value().outputNode));
    REQUIRE(bool(first));
    REQUIRE(first->getCount() > 0);
    CHECK_EQ(first->getStringAttribute(0, "prototype", ""), std::string("/Game/M4/SM_Grass"));
    CHECK_EQ(first->getStringAttribute(0, "layer", ""), std::string("Grass"));
    std::unique_ptr<eve::procgen::PointSet> second(loaded.value().graph->execute(loaded.value().outputNode));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), second->getCount());
    CHECK_EQ(first->getPointSeed(0), second->getPointSeed(0));

    const auto instancesAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.instance-set"; });
    REQUIRE(instancesAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(windowsReader);
    auto                                        loadedInstances = instanceLoader.load(instancesAsset->asset,
                                                                                      {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loadedInstances.ok());
    REQUIRE_EQ(loadedInstances.value().instances.size(), std::size_t(1));
    const auto& rock = loadedInstances.value().instances.front();
    CHECK_EQ(rock.prototype, std::string("/Game/M4/SM_Rock"));
    CHECK(std::abs(rock.position[0] - 2.f) < 0.0001f);
    CHECK(std::abs(rock.position[1] - 3.f) < 0.0001f);
    CHECK(std::abs(rock.position[2] + 1.f) < 0.0001f);
    CHECK(std::abs(rock.scale[0] - 2.f) < 0.0001f);
    CHECK(std::abs(rock.scale[1] - 3.f) < 0.0001f);
    CHECK(std::abs(rock.scale[2] - 1.f) < 0.0001f);

    const auto materialAsset =
        std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                     [](const auto& asset) { return asset.type == "eve.terrain-material"; });
    REQUIRE(materialAsset != prepared.value().manifest.assets.end());
    eve::asset_procgen::EvpackTerrainMaterialLoader materialLoader(windowsReader);
    auto                                            loadedMaterial =
        materialLoader.load(materialAsset->asset, {"windows", "x86_64", "vulkan", {"bc"}, {"spirv-1.6"}, {"high"}, {}});
    REQUIRE(loadedMaterial.ok());
    REQUIRE_EQ(loadedMaterial.value().layers.size(), std::size_t(1));
    const auto& grassLayer = loadedMaterial.value().layers.front();
    CHECK_EQ(grassLayer.name, std::string("Grass"));
    CHECK_EQ(grassLayer.normalConvention, std::string("directx"));
    CHECK(std::abs(grassLayer.tileSizeMeters - 8.f) < 0.0001f);
}

TEST_CASE("asset.import.unrealM4RejectsTraversalAndWrongR16Length") {
    UnrealProjectImportRequest traversal;
    traversal.package        = unrealIdentity();
    traversal.descriptorPath = "../M4.json";
    auto unsafe              = prepareUnrealM4Import(traversal);
    REQUIRE(!unsafe.ok());
    CHECK_EQ(unsafe.error()->code(), DiagnosticCode::InvalidArgument);

    const std::string          descriptor = R"json({"schema":"eve.unreal-landscape-import","schemaVersion":1,
      "landscape":{"width":2,"height":2,"heightmap":"Content/L.r16","scaleCm":[100,100,100]}})json";
    UnrealProjectImportRequest truncated;
    truncated.package        = unrealIdentity();
    truncated.descriptorPath = "Config/M4.json";
    truncated.files          = {{truncated.descriptorPath, text(descriptor)}, {"Content/L.r16", {0, 0}}};
    auto rejected            = prepareUnrealM4Import(truncated);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}
}  // namespace test_asset_import_animation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_gltf_animation)
#include "PrivateTestScope.h"
#include <algorithm>
#include <bit>
#include "asset/AssetCooker.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_gltf_animation.cpp
namespace test_asset_import_gltf_animation {
using namespace eve;
using namespace eve::asset_import;

namespace {
GltfImportRequest animatedTriangle() {
    GltfImportRequest request;
    request.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "animated.test", "1.0.0", {}};
    request.sourceName = "triangle.gltf";
    auto& bytes        = request.externalResources["data.bin"];
    auto  scalar       = [&](float value) {
        auto bits = std::bit_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    };
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) scalar(value);
    for (int i = 0; i < 12; ++i) bytes.push_back(0);
    for (int v = 0; v < 3; ++v)
        for (int i = 0; i < 4; ++i) scalar(i == 0 ? 1.f : 0.f);
    scalar(0);
    scalar(1);
    for (float value : {0.f, 0.f, 0.f, 0.f, 2.f, 0.f}) scalar(value);
    const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data.bin","byteLength":128}],
      "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":8},{"buffer":0,"byteOffset":104,"byteLength":24}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"VEC3"}],
      "nodes":[{"name":"root","children":[1]},{"name":"joint"},{"mesh":0,"skin":0}],"skins":[{"joints":[1]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],
      "animations":[{"name":"move","samplers":[{"input":3,"output":4,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
    request.documentBytes.assign(json.begin(), json.end());
    return request;
}
}  // namespace

TEST_CASE("asset.import.gltfAnimationSurvivesArchiveAndCook") {
    auto imported = prepareGltfImport(animatedTriangle());
    REQUIRE(imported.ok());
    std::vector<std::string> types;
    for (const auto& asset : imported.value().manifest.assets) types.push_back(asset.type);
    REQUIRE(std::find(types.begin(), types.end(), "eve.skeleton") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "eve.animation-clip") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "eve.skin") != types.end());
    REQUIRE_EQ(imported.value().manifest.dependencies.size(), std::size_t(3));
    for (const auto& definition : imported.value().manifest.assets) {
        const auto base  = "assets/" + definition.asset.id().format() + "/data.bin";
        auto       found = std::find_if(imported.value().entries.begin(), imported.value().entries.end(),
                                        [&](const auto& e) { return e.path == base; });
        if (definition.type == "eve.skin") {
            REQUIRE(found != imported.value().entries.end());
            // Header + one joint/identity inverse bind + three vertices * four
            // (joint,weight) pairs. The joint maps to bone 2 after parent ordering.
            REQUIRE_EQ(found->bytes.size(), std::size_t(188));
            REQUIRE_EQ(found->bytes[24], std::uint8_t(2));
            REQUIRE_EQ(found->bytes[92 + 6], std::uint8_t(128));  // FLOAT 1.0
        }
        if (definition.type == "eve.animation-clip") {
            REQUIRE(found != imported.value().entries.end());
            REQUIRE_EQ(found->bytes.size(), std::size_t(76));
            REQUIRE_EQ(found->bytes[28], std::uint8_t(2));   // remapped target bone
            REQUIRE_EQ(found->bytes[71], std::uint8_t(64));  // final Y = 2.0
        }
    }
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto parsed = asset::parseEvaArchive(archive.value());
    REQUIRE(parsed.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(parsed.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    REQUIRE_EQ(pack.value().chunks().size(), std::size_t(8));
}

TEST_CASE("asset.import.gltfAnimationRejectsInvalidJointWithoutPartialSuccess") {
    auto request                              = animatedTriangle();
    request.externalResources["data.bin"][36] = 1;
    auto imported                             = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRejectsUnsupportedInterpolation") {
    auto        request = animatedTriangle();
    std::string json(request.documentBytes.begin(), request.documentBytes.end());
    json.replace(json.find("LINEAR"), 6, "UNKNOWN");
    request.documentBytes.assign(json.begin(), json.end());
    auto imported = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRejectsMissingTargetAndCycles") {
    for (const auto& change :
         std::vector<std::pair<std::string, std::string>>{{"\"target\":", "\"missing\":"},
                                                          {"\"name\":\"joint\"", "\"name\":\"joint\",\"children\":[0]"},
                                                          {"\"joints\":[1]", "\"joints\":[9]"}}) {
        auto        request = animatedTriangle();
        std::string json(request.documentBytes.begin(), request.documentBytes.end());
        json.replace(json.find(change.first), change.first.size(), change.second);
        request.documentBytes.assign(json.begin(), json.end());
        auto imported = prepareGltfImport(request);
        REQUIRE(!imported.ok());
    }
}

TEST_CASE("asset.import.gltfAnimationHonorsDecodedBudget") {
    auto request                       = animatedTriangle();
    request.limits.maximumDecodedBytes = 128;
    auto imported                      = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRetainsAndNormalizesMultipleQuantizedWeightSets") {
    auto  request = animatedTriangle();
    auto& bytes   = request.externalResources["data.bin"];
    for (std::size_t i = 48; i < 96; ++i) bytes[i] = 0;
    for (std::size_t vertex = 0; vertex < 3; ++vertex) bytes[48 + vertex * 4] = 127;
    for (int vertex = 0; vertex < 3; ++vertex) bytes.insert(bytes.end(), {1, 0, 0, 0});
    std::string json(request.documentBytes.begin(), request.documentBytes.end());
    auto        replace = [&](std::string from, std::string to) {
        const auto pos = json.find(from);
        REQUIRE(pos != std::string::npos);
        json.replace(pos, from.size(), to);
    };
    replace("\"byteLength\":128", "\"byteLength\":140");
    replace("\"byteOffset\":104,\"byteLength\":24}",
            "\"byteOffset\":104,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}");
    replace("\"bufferView\":2,\"componentType\":5126", "\"bufferView\":2,\"normalized\":true,\"componentType\":5121");
    replace("\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}",
            "\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},{\"bufferView\":5,"
            "\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"}");
    replace("\"joints\":[1]", "\"joints\":[1,2]");
    replace("\"WEIGHTS_0\":2", "\"WEIGHTS_0\":2,\"JOINTS_1\":5,\"WEIGHTS_1\":2");
    request.documentBytes.assign(json.begin(), json.end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& entry : imported.value().entries) {
        if (entry.bytes.size() != 352 || entry.bytes[2] != 'S') continue;
        found = true;
        REQUIRE_EQ(entry.bytes[16], std::uint8_t(8));
        REQUIRE_EQ(entry.bytes[167], std::uint8_t(63));  // first set weight = 0.5
        REQUIRE_EQ(entry.bytes[199], std::uint8_t(63));  // second set weight = 0.5
    }
    REQUIRE(found);
    REQUIRE(std::any_of(imported.value().findings.begin(), imported.value().findings.end(),
                        [](const auto& finding) { return finding.disposition == ImportDisposition::Baked; }));
}
}  // namespace test_asset_import_gltf_animation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_gltf_material)
#include "PrivateTestScope.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/CookedMaterial.h"
#endif
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/import/AssetImporter.h"
#include "asset/import/ImportCommon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_gltf_material.cpp
namespace test_asset_import_gltf_material {
#ifdef EVE_TEST_MESH_UPLOAD

#endif


using namespace eve;
using namespace eve::asset_import;
namespace {
GltfImportRequest materialTriangle() {
    GltfImportRequest r;
    r.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "material.test", "1.0.0", {}};
    r.sourceName = "material.gltf";
    r.externalResources["vertices.bin"].resize(36);
    r.externalResources["uv.bin"].resize(24);
    r.externalResources["pixel.png"] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0,    0,    0,    0x0d, 0x49, 0x48, 0x44, 0x52, 0, 0,
        0,    1,    0,    0,    0,    1,    8,    6,    0,    0,    0,    0x1f, 0x15, 0xc4, 0x89, 0,    0, 0,
        0x0d, 0x49, 0x44, 0x41, 0x54, 8,    0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0,    5,    0,    1, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0,    0,    0,    0,    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"vertices.bin","byteLength":36},{"uri":"uv.bin","byteLength":24}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":1,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.3,0.4,0.5],"metallicFactor":0.7,"roughnessFactor":0.8,"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}},"normalTexture":{"index":0,"scale":0.6},"occlusionTexture":{"index":0,"strength":0.4},"emissiveTexture":{"index":0},"emissiveFactor":[0.1,0.2,0.3],"alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true}],"textures":[{"source":0}],"images":[{"uri":"pixel.png"}]})";
    r.documentBytes.assign(json.begin(), json.end());
    return r;
}
}  // namespace
TEST_CASE("asset.import.gltfMaterialAndImagesSurviveCook") {
    auto result = prepareGltfImport(materialTriangle());
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().manifest.assets.size(), size_t(7));
    REQUIRE_EQ(result.value().manifest.dependencies.size(), size_t(6));
    bool found = false;
    for (const auto& a : result.value().manifest.assets)
        if (a.type == "eve.material") {
            found = true;
            for (const auto& e : result.value().entries)
                if (e.path == a.definition) {
                    std::string text(e.bytes.begin(), e.bytes.end());
                    REQUIRE(text.find("metallicRoughnessTexture") != std::string::npos);
                    REQUIRE(text.find("normalTexture") != std::string::npos);
                    REQUIRE(text.find("masked") != std::string::npos);
                    auto decoded = Value::fromJson(text);
                    REQUIRE(decoded.ok());
                    const auto* object = decoded.value().getIf<Value::Object>();
                    REQUIRE(object != nullptr);
                    REQUIRE_EQ(object->at("metallic").asDouble(), 0.7);
                    REQUIRE_EQ(object->at("roughness").asDouble(), 0.8);
                    REQUIRE_EQ(object->at("normalScale").asDouble(), 0.6);
                    REQUIRE_EQ(object->at("occlusionStrength").asDouble(), 0.4);
                    REQUIRE_EQ(object->at("alphaCutoff").asDouble(), 0.3);
                }
        }
    REQUIRE(found);
    auto bytes = asset::buildEvaArchive(result.value().manifest, result.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto pack = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(pack.ok());
}
TEST_CASE("asset.import.gltfMissingTextureFailsAtomically") {
    auto request = materialTriangle();
    request.externalResources.erase("pixel.png");
    REQUIRE(!prepareGltfImport(request).ok());
}

#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.import.materialV14ColorParametersSurviveV15CookWithImageDependency") {
    auto imported = prepareGltfImport(materialTriangle());
    REQUIRE(imported.ok());
    auto bytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    const auto ref = imported.value().manifest.entrypoints.at("material:0");
    // Stage an authored N-1 definition. Cooking migrates and rehashes the owning candidate.
    for (auto& material : archive.value().manifest.assets) {
        if (material.asset != ref) continue;
        material.schemaVersion = SchemaVersion(14);
        for (auto& entry : archive.value().entries) {
            if (entry.path != material.definition) continue;
            auto decoded = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
            REQUIRE(decoded.ok());
            auto& root            = *decoded.value().getIf<Value::Object>();
            root["schemaVersion"] = 14;
            root.erase("alphaToCoverage");
            root["albedoTextureStrength"] = .25;
            root["colorMask"] =
                Value::Object{{"secondary", Value::Array{2.0, .1, .3}}, {"minimum", .2}, {"maximum", .8}};
            auto json = decoded.value().toJson();
            REQUIRE(json.ok());
            entry.bytes.assign(json.value().begin(), json.value().end());
            material.contentHash = asset_import::detail::sha256(entry.bytes);
        }
    }
    for (auto& dependency : archive.value().manifest.dependencies)
        if (dependency.to == ref) dependency.expectedType = "eve.material/14";
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    if (!cooked) std::cerr << cooked.error()->message() << '\n';
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                        runtime = asset_graphics::detail::readCookedMaterial(reader, ref, caps);
    REQUIRE(runtime.ok());
    REQUIRE(runtime.value().surface.colorMaskEnabled);
    REQUIRE_EQ(runtime.value().surface.colorMaskSecondary[0], 2.f);
    REQUIRE_EQ(runtime.value().surface.albedoTextureStrength, .25f);
    REQUIRE(runtime.value().images[1].has_value());
    auto image = reader.read(*runtime.value().images[1], "eve.image/3", caps, 1024 * 1024);
    REQUIRE(image.ok());
}
#endif

TEST_CASE("asset.import.gltfMaterialSupportsIdentityTransformAndRejectsBudget") {
    auto        request = materialTriangle();
    std::string text(request.documentBytes.begin(), request.documentBytes.end());
    auto        at = text.find("\"baseColorTexture\":{\"index\":0}");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string("\"baseColorTexture\":{\"index\":0}").size(),
                 "\"baseColorTexture\":{\"index\":0,\"extensions\":{\"KHR_texture_transform\":{}}}");
    request.documentBytes.assign(text.begin(), text.end());
    REQUIRE(prepareGltfImport(request).ok());
    request                      = materialTriangle();
    request.limits.maximumAssets = 2;
    REQUIRE(!prepareGltfImport(request).ok());
}

TEST_CASE("asset.import.gltfMaterialExtensionsRetainParametersAndTextures") {
    auto request = materialTriangle();
    auto parsed  = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(parsed.ok());
    auto& root = *parsed.value().getIf<Value::Object>();
    auto& mat  = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    auto  ext  = Value::fromJson(
        R"({"KHR_materials_specular":{"specularFactor":0.4,"specularColorFactor":[2,1,0.5],"specularTexture":{"index":0},"specularColorTexture":{"index":0}},"KHR_materials_anisotropy":{"anisotropyStrength":0.7,"anisotropyRotation":-1.5,"anisotropyTexture":{"index":0}},"KHR_materials_clearcoat":{"clearcoatFactor":0.8,"clearcoatRoughnessFactor":0.2,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0},"clearcoatNormalTexture":{"index":0,"scale":0.3}},"KHR_materials_emissive_strength":{"emissiveStrength":3},"KHR_materials_ior":{"ior":1.7}})");
    REQUIRE(ext.ok());
    mat["extensions"]          = std::move(ext).takeValue();
    root["extensionsRequired"] = Value(Value::Array{Value("KHR_materials_specular"), Value("KHR_materials_ior")});
    auto text                  = parsed.value().toJson();
    REQUIRE(text.ok());
    request.documentBytes.assign(text.value().begin(), text.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool checked = false;
    for (const auto& entry : imported.value().entries) {
        if (!entry.path.ends_with("/asset.json")) continue;
        auto value = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
        REQUIRE(value.ok());
        auto* o = value.value().getIf<Value::Object>();
        if (o->at("schema").asString() != "eve.material") continue;
        REQUIRE_EQ(o->at("schemaVersion").asInt(), int64_t(15));
        REQUIRE(!o->at("alphaToCoverage").asBool());
        REQUIRE_EQ(o->at("specularFactor").asDouble(), 0.4);
        REQUIRE_EQ(o->at("anisotropyRotation").asDouble(), -1.5);
        REQUIRE_EQ(o->at("emissiveStrength").asDouble(), 3.0);
        REQUIRE_EQ(o->at("ior").asDouble(), 1.7);
        REQUIRE(o->contains("clearcoatNormalTexture"));
        REQUIRE_EQ(o->at("clearcoatNormalScale").asDouble(), 0.3);
        checked = true;
    }
    REQUIRE(checked);
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto decoded = asset::parseEvaArchive(archive.value());
    REQUIRE(decoded.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(decoded.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                        payload =
        reader.read(imported.value().manifest.entrypoints.at("material:0"), "eve.material/15", caps, 1024 * 1024);
    REQUIRE(payload.ok());
#ifdef EVE_TEST_MESH_UPLOAD
    auto runtime = eve::asset_graphics::detail::readCookedMaterial(
        reader, imported.value().manifest.entrypoints.at("material:0"), caps);
    REQUIRE(runtime.ok());
    REQUIRE_EQ(runtime.value().surface.specularFactor, .4f);
    REQUIRE_EQ(runtime.value().surface.specularColor[0], 2.f);
    REQUIRE_EQ(runtime.value().surface.anisotropyStrength, .7f);
    REQUIRE_EQ(runtime.value().surface.clearcoatNormalScale, .3f);
    REQUIRE(runtime.value().images[10].has_value());
    REQUIRE(!runtime.value().surface.textures[6].srgbDecode);
#endif
    bool retained = false;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Definition) continue;
        auto value = asset::decodeRuntimeDefinition(chunk.bytes);
        REQUIRE(value.ok());
        auto& material = *value.value().getIf<Value::Object>();
        REQUIRE_EQ(material.at("specularColorFactor").getIf<Value::Array>()->at(0).asDouble(), 2.0);
        REQUIRE_EQ(material.at("clearcoatNormalScale").asDouble(), 0.3);
        auto imageRef = AssetRef::parse(material.at("specularColorTexture").asString());
        REQUIRE(imageRef.ok());
        auto image = reader.read(imageRef.value(), "eve.image/3", caps, 1024 * 1024);
        REQUIRE(image.ok());
        retained = true;
    }
    REQUIRE(retained);
}

TEST_CASE("asset.import.gltfExtensionsRejectInvalidInputs") {
    for (const char* ext :
         {R"({"KHR_materials_ior":{"ior":0.5}})", R"({"KHR_materials_specular":{"specularFactor":2}})",
          R"({"KHR_materials_anisotropy":{"anisotropyStrength":-1}})",
          R"({"KHR_materials_clearcoat":{"clearcoatTexture":{"index":99}}})",
          R"({"KHR_materials_emissive_strength":{"emissiveStrength":-1}})",
          R"({"KHR_materials_unlit":[],"KHR_materials_specular":{}})",
          R"({"KHR_materials_unlit":{},"KHR_materials_specular":{}})", R"({"EXT_unknown":{}})"}) {
        auto request  = materialTriangle();
        request.mode  = GltfImportMode::Strict;
        auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
        REQUIRE(document.ok());
        auto extension = Value::fromJson(ext);
        REQUIRE(extension.ok());
        auto& mat = *document.value()
                         .getIf<Value::Object>()
                         ->at("materials")
                         .getIf<Value::Array>()
                         ->at(0)
                         .getIf<Value::Object>();
        mat["extensions"] = std::move(extension).takeValue();
        auto text         = document.value().toJson();
        REQUIRE(text.ok());
        request.documentBytes.assign(text.value().begin(), text.value().end());
        REQUIRE(!prepareGltfImport(request).ok());
    }
}
TEST_CASE("asset.import.gltfUnlitAndTextureTransformSurvive") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& mat =
        *document.value().getIf<Value::Object>()->at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    mat["extensions"]      = Value(Value::Object{{"KHR_materials_unlit", Value(Value::Object{})}});
    auto textureDefinition = Value::fromJson(
        R"({"index":0,"texCoord":1,"extensions":{"KHR_texture_transform":{"offset":[-2,3],"scale":[2,-4],"rotation":-1.2,"texCoord":0}}})");
    REQUIRE(textureDefinition.ok());
    mat.at("pbrMetallicRoughness").getIf<Value::Object>()->at("baseColorTexture") =
        std::move(textureDefinition).takeValue();
    auto text = document.value().toJson();
    REQUIRE(text.ok());
    request.documentBytes.assign(text.value().begin(), text.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material") {
            for (const auto& entry : imported.value().entries)
                if (entry.path == a.definition) {
                    auto value = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
                    REQUIRE(value.ok());
                    auto& o = *value.value().getIf<Value::Object>();
                    REQUIRE_EQ(o.at("shadingModel").asString(), std::string("unlit"));
                    auto& transform = *o.at("baseColorTextureTransform").getIf<Value::Object>();
                    REQUIRE_EQ(transform.at("rotation").asDouble(), -1.2);
                    REQUIRE_EQ(transform.at("offset").getIf<Value::Array>()->at(0).asDouble(), -2.0);
                    REQUIRE_EQ(transform.at("scale").getIf<Value::Array>()->at(1).asDouble(), -4.0);
                    found = true;
                }
        }
    REQUIRE(found);
}

TEST_CASE("asset.import.gltfTextureTransformRejectsUnavailableUvSet") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& root    = *document.value().getIf<Value::Object>();
    auto& mat     = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    auto  texture = Value::fromJson(R"({"index":0,"extensions":{"KHR_texture_transform":{"texCoord":1}}})");
    REQUIRE(texture.ok());
    mat.at("pbrMetallicRoughness").getIf<Value::Object>()->at("baseColorTexture") = std::move(texture).takeValue();
    auto json                                                                     = document.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto rejected = prepareGltfImport(request);
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error()->message().find("UV set") != std::string::npos);
}
TEST_CASE("asset.import.gltfMaterialSelectsNonzeroUvWithoutTransform") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& root  = *document.value().getIf<Value::Object>();
    auto& attrs = *root.at("meshes")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("primitives")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("attributes")
                       .getIf<Value::Object>();
    attrs["TEXCOORD_2"] = Value(int64_t(1));
    auto& material      = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    material.at("pbrMetallicRoughness")
        .getIf<Value::Object>()
        ->at("baseColorTexture")
        .getIf<Value::Object>()
        ->emplace("texCoord", Value(int64_t(2)));
    auto json = document.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material")
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto d = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(d.ok());
                    REQUIRE_EQ(d.value().getIf<Value::Object>()->at("baseColorTextureTexCoord").asInt(), int64_t(2));
                    found = true;
                }
    REQUIRE(found);
}

TEST_CASE("asset.import.gltfPermissiveClampsSpecularAndWarns") {
    auto request = materialTriangle();
    auto doc     = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(doc.ok());
    auto& mat =
        *doc.value().getIf<Value::Object>()->at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    mat["extensions"] =
        Value(Value::Object{{"KHR_materials_specular", Value(Value::Object{{"specularFactor", Value(38406.7422)}})}});
    auto json = doc.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool warned = false, clamped = false;
    for (const auto& finding : imported.value().findings)
        if (finding.severity == ImportSeverity::Warning) {
            REQUIRE(finding.feature.find("specularFactor") != std::string::npos);
            warned = true;
        }
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material")
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto d = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(d.ok());
                    REQUIRE_EQ(d.value().getIf<Value::Object>()->at("specularFactor").asDouble(), 1.0);
                    clamped = true;
                }
    REQUIRE(warned);
    REQUIRE(clamped);
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto decoded = asset::parseEvaArchive(archive.value());
    REQUIRE(decoded.ok());
    bool persisted = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path.find("report") != std::string::npos) {
            std::string report(entry.bytes.begin(), entry.bytes.end());
            if (report.find("warning") != std::string::npos && report.find("specularFactor") != std::string::npos)
                persisted = true;
        }
    REQUIRE(persisted);
    request.mode = GltfImportMode::Strict;
    REQUIRE(!prepareGltfImport(request).ok());
}
}  // namespace test_asset_import_gltf_material
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_structured_models)
#include "PrivateTestScope.h"
#include <algorithm>
#include <bit>
#include <filesystem>
#include "PathBesideSource.h"
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/StructuredModelImporter.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "graphics/IImageResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_structured_models.cpp
namespace test_asset_import_structured_models {
using namespace eve;
using namespace eve::asset_import;
namespace {
ImportPackageIdentity package() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
            "structured-model",
            "1.0.0",
            {{"provider", "local"}, {"license", Value::Object{{"redistribution", "project-only"}}}}};
}
LDrawImportRequest ldraw(std::string text) {
    return {package(), "model.ldr", {{"model.ldr", {text.begin(), text.end()}}}};
}
asset::CanonicalMeshData mesh(const PreparedAssetImport& prepared, std::string_view part) {
    const auto ref  = prepared.manifest.entrypoints.at("mesh:" + std::string(part));
    const auto path = "assets/" + ref.id().format() + "/mesh.bin";
    const auto entry =
        std::find_if(prepared.entries.begin(), prepared.entries.end(), [&](const auto& e) { return e.path == path; });
    REQUIRE(entry != prepared.entries.end());
    auto decoded = asset::decodeCanonicalMesh(entry->bytes);
    REQUIRE(decoded.ok());
    return std::move(decoded).takeValue();
}
ScadImportRequest stl() {
    ScadImportRequest request{
        package(), "triangle.scad", {'c', 'u', 'b', 'e', '(', '1', ')', ';'}, std::vector<std::uint8_t>(84, 0)};
    request.binaryStl[80] = 1;
    auto put              = [&](float value) {
        auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned i = 0; i < 4; ++i) request.binaryStl.push_back(std::uint8_t(bits >> (i * 8)));
    };
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1000.f, 0.f, 0.f, 0.f, 1000.f, 0.f}) put(value);
    request.binaryStl.push_back(0);
    request.binaryStl.push_back(0);
    return request;
}

void verifyComposition(const PreparedAssetImport& prepared, std::size_t expectedParts) {
    auto eva = asset::buildEvaArchive(prepared.manifest, prepared.entries);
    REQUIRE(eva.ok());
    auto archive = asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader     reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    const asset::EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto scene = asset_scene::EvpackSceneTemplateLoader(reader).load(prepared.manifest.entrypoints.at("default"), caps);
    REQUIRE(scene.ok());
    REQUIRE_EQ(scene.value().renderers.size(), expectedParts);
    class Meshes final : public graphics::IMeshResourceFactory {
    public:
        int                     token = 0, uploads = 0, releases = 0;
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float*, int vertices, const std::uint32_t*,
                                           int indices) override {
            REQUIRE(vertices > 0);
            REQUIRE(indices > 0);
            ++uploads;
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } meshes;
    class Images final : public graphics::IImageResourceFactory {
    public:
        Result<graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
            return Result<graphics::Texture*>::failure(Diagnostic::error(DiagnosticCode::Failed, "unexpected texture"));
        }
        Result<void> releaseImage(graphics::Texture*) override { return Result<void>::success(); }
    } images;
    {
        auto prefab = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images,
                                                               prepared.manifest.entrypoints.at("default"), caps);
        REQUIRE(prefab.ok());
        REQUIRE_EQ(prefab.value()->drawCount(), expectedParts);
        REQUIRE_EQ(std::size_t(meshes.uploads), expectedParts);
    }
    REQUIRE_EQ(meshes.releases, meshes.uploads);
}
}  // namespace

TEST_CASE("asset.import.structuredModelsLDrawMpdReferenceColourAndComposition") {
    auto request = ldraw(
        "0 FILE model.ldr\n1 4 20 -40 0 1 0 0 0 1 0 0 0 1 part.dat\n"
        "1 1 -20 0 0 1 0 0 0 1 0 0 0 1 part.dat\n0 FILE part.dat\n"
        "0 BFC CERTIFY CCW\n4 16 0 0 0 20 0 0 20 20 0 0 20 0\n0 NOFILE\n");
    auto prepared = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    auto red = mesh(prepared.value(), "colour-4");
    REQUIRE_EQ(red.indices.size(), std::size_t(6));
    REQUIRE(std::abs(red.positions[0] - .008f) < 1e-6f);
    REQUIRE(std::abs(red.positions[1] - .016f) < 1e-6f);
    verifyComposition(prepared.value(), 2);
    auto again = prepareLDrawImport(request);
    REQUIRE(again.ok());
    auto firstArchive  = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    auto secondArchive = asset::buildEvaArchive(again.value().manifest, again.value().entries);
    REQUIRE(firstArchive.ok());
    REQUIRE(secondArchive.ok());
    REQUIRE(firstArchive.value() == secondArchive.value());
}
TEST_CASE("asset.import.structuredModelsLDrawLibraryCaseAndDirectColour") {
    auto              request         = ldraw("1 16 0 0 0 1 0 0 0 1 0 0 0 1 S\\PART.DAT\n");
    const std::string triangle        = "3 0x2FF0000 0 0 0 20 0 0 0 20 0\n";
    request.files["parts/s/part.dat"] = {triangle.begin(), triangle.end()};
    auto prepared                     = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 1);
    auto              externalMpd     = ldraw("1 4 0 0 0 1 0 0 0 1 0 0 0 1 assembly.mpd\n");
    const std::string assembly        = "0 FILE root.ldr\n3 16 0 0 0 20 0 0 0 20 0\n0 NOFILE\n";
    externalMpd.files["assembly.mpd"] = {assembly.begin(), assembly.end()};
    auto nested                       = prepareLDrawImport(externalMpd);
    REQUIRE(nested.ok());
    verifyComposition(nested.value(), 1);
    auto sameName = prepareLDrawImport(ldraw("0 Preamble\n0 FILE model.ldr\n3 4 0 0 0 20 0 0 0 20 0\n"));
    REQUIRE(sameName.ok());
}
TEST_CASE("asset.import.structuredModelsLDrawBfcMirrorAndEdges") {
    auto request = ldraw(
        "0 BFC INVERTNEXT\n1 4 0 0 0 -1 0 0 0 1 0 0 0 1 part.dat\n"
        "2 24 0 0 0 20 0 0\n5 24 0 0 0 20 0 0 0 20 0 20 20 0\n");
    const std::string triangle = "0 BFC CERTIFY CCW\n3 16 0 0 0 20 0 0 0 20 0\n";
    request.files["part.dat"]  = {triangle.begin(), triangle.end()};
    auto prepared              = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    auto decoded = mesh(prepared.value(), "colour-4");
    REQUIRE(decoded.normals[2] < -.99f);
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(),
                        [](const auto& f) { return f.feature == "LDraw.edgeLines"; }));
}
TEST_CASE("asset.import.structuredModelsLDrawConfigColour") {
    auto              request     = ldraw("3 100 0 0 0 20 0 0 0 20 0\n");
    const std::string config      = "0 !COLOUR Custom CODE 100 VALUE #00FF00 EDGE #000000 ALPHA 128\n";
    request.files["LDConfig.ldr"] = {config.begin(), config.end()};
    auto prepared                 = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 1);
}
TEST_CASE("asset.import.structuredModelsLDrawColourScopeDoesNotLeak") {
    auto request = ldraw(
        "0 !COLOUR Local CODE 100 VALUE #FF0000 EDGE #000000\n"
        "1 100 0 0 0 1 0 0 0 1 0 0 0 1 part.dat\n"
        "3 100 0 0 0 20 0 0 0 20 0\n");
    const std::string part =
        "3 100 0 0 0 20 0 0 0 20 0\n"
        "0 !COLOUR Local CODE 100 VALUE #0000FF EDGE #000000\n"
        "3 100 0 0 0 20 0 0 0 20 0\n"
        "3 16 0 0 0 20 0 0 0 20 0\n";
    request.files["part.dat"]      = {part.begin(), part.end()};
    const std::string unrelated    = "0 !COLOUR Local CODE 100 VALUE #00FF00 EDGE #000000\n";
    request.files["unrelated.dat"] = {unrelated.begin(), unrelated.end()};
    // A complete official library also contains an alternative palette; it is not the selected default.
    request.files["LDCfgalt.ldr"] = {unrelated.begin(), unrelated.end()};
    auto prepared                 = prepareLDrawImport(request);
    REQUIRE(prepared.ok());
    verifyComposition(prepared.value(), 2);
    request.files["model.ldr"] = {'3', ' ', '1', '0', '0', ' ', '0', ' ', '0', ' ', '0', ' ', '2',
                                  '0', ' ', '0', ' ', '0', ' ', '0', ' ', '2', '0', ' ', '0', '\n'};
    auto undefined             = prepareLDrawImport(request);
    REQUIRE(!undefined.ok());
}
TEST_CASE("asset.import.structuredModelsLDrawMissingCycleAndInvalidGeometry") {
    for (const auto* source : {"1 16 0 0 0 1 0 0 0 1 0 0 0 1 missing.dat\n", "1 16 0 0 0 1 0 0 0 1 0 0 0 1 model.ldr\n",
                               "3 4 0 0 0 1 0 0\n", "3 4 0 0 0 0 0 0 0 0 0\n", "3 99999 0 0 0 20 0 0 0 20 0\n",
                               "0 !TEXMAP START PLANAR\n", "0 BFC INVERTNEXT\n", "6 4 0 0 0\n"}) {
        auto prepared = prepareLDrawImport(ldraw(source));
        REQUIRE(!prepared.ok());
    }
}
TEST_CASE("asset.import.structuredModelsLDrawBudgetsAndAmbiguousPaths") {
    auto request                               = ldraw("3 4 0 0 0 20 0 0 0 20 0\n");
    request.limits.maximumVerticesPerPrimitive = 2;
    auto vertices                              = prepareLDrawImport(request);
    REQUIRE(!vertices.ok());
    request.limits.maximumVerticesPerPrimitive = 100;
    request.limits.maximumDecodedBytes         = 50;
    auto bytes                                 = prepareLDrawImport(request);
    REQUIRE(!bytes.ok());
    request.limits.maximumDecodedBytes = 10000;
    request.files["MODEL.LDR"]         = request.files.at("model.ldr");
    auto duplicate                     = prepareLDrawImport(request);
    REQUIRE(!duplicate.ok());
    request.files.erase("MODEL.LDR");
    request.files["../escape.dat"] = {};
    auto escape                    = prepareLDrawImport(request);
    REQUIRE(!escape.ok());
}
TEST_CASE("asset.import.structuredModelsScadUnitsNormalsAndComposition") {
    auto prepared = prepareScadImport(stl());
    REQUIRE(prepared.ok());
    auto decoded = mesh(prepared.value(), "default");
    REQUIRE_EQ(decoded.positions[3], 1.f);
    REQUIRE_EQ(decoded.positions[8], -1.f);
    REQUIRE_EQ(decoded.normals[1], 1.f);
    verifyComposition(prepared.value(), 1);
}
TEST_CASE("asset.import.structuredModelsScadMalformedAndBudgets") {
    auto request = stl();
    request.binaryStl.pop_back();
    auto truncated = prepareScadImport(request);
    REQUIRE(!truncated.ok());
    request                  = stl();
    request.binaryStl.back() = 1;
    auto extended            = prepareScadImport(request);
    REQUIRE(!extended.ok());
    request                                    = stl();
    request.limits.maximumVerticesPerPrimitive = 2;
    auto vertices                              = prepareScadImport(request);
    REQUIRE(!vertices.ok());
    request               = stl();
    request.binaryStl[98] = 0x80;
    request.binaryStl[99] = 0x7f;
    auto infinity         = prepareScadImport(request);
    REQUIRE(!infinity.ok());
}
TEST_CASE("asset.import.structuredModelsScadCompilerAbsent") {
    ScadFileImportRequest request{package(),
                                  (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() /
                                   "fixtures/structured_models/cup.scad")
                                      .string(),
                                  "eve-definitely-missing-openscad-executable"};
    auto                  prepared = prepareScadFileImport(request);
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->message().find("OpenSCAD") != std::string::npos);
    request.timeoutSeconds = 0;
    auto timeout           = prepareScadFileImport(request);
    REQUIRE(!timeout.ok());
}

#if defined(EVE_SCAD_TEST_COMPILER)
TEST_CASE("asset.import.structuredModelsScadCompilerTimeout") {
    ScadFileImportRequest request{package(),
                                  (std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() /
                                   "fixtures/structured_models/cup.scad")
                                      .string(),
                                  EVE_SCAD_TEST_COMPILER, 1};
    auto                  prepared = prepareScadFileImport(request);
    REQUIRE(!prepared.ok());
    REQUIRE(prepared.error()->message().find("timed out") != std::string::npos);
}
#endif
}  // namespace test_asset_import_structured_models
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_tga)
#include "PrivateTestScope.h"
#include "asset/CanonicalImageCook.h"
#include "asset/SourceTga.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_tga.cpp
namespace test_asset_import_tga {
using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(bool rle, unsigned channels, unsigned orientation) {
    std::vector<std::uint8_t> bytes(18, 0);
    bytes[2]  = rle ? 10 : 2;
    bytes[12] = 2;
    bytes[14] = 2;
    bytes[16] = std::uint8_t(channels * 8);
    bytes[17] = std::uint8_t(orientation | (channels == 4 ? 8 : 0));
    if (rle) bytes.push_back(0x81);
    const unsigned copies = rle ? 1 : 2;
    for (unsigned i = 0; i < copies; ++i) {
        bytes.insert(bytes.end(), {30, 20, 10});
        if (channels == 4) bytes.push_back(40);
    }
    if (rle) bytes.push_back(1);
    bytes.insert(bytes.end(), {70, 60, 50});
    if (channels == 4) bytes.push_back(80);
    bytes.insert(bytes.end(), {110, 100, 90});
    if (channels == 4) bytes.push_back(120);
    return bytes;
}
}  // namespace
TEST_CASE("asset.import.tgaRawAndRlePreserveOrientationAndAlpha") {
    for (bool rle : {false, true})
        for (unsigned channels : {3u, 4u})
            for (unsigned orientation : {0u, 16u, 32u, 48u}) {
                auto decoded = asset::detail::decodeTgaRgba8(fixture(rle, channels, orientation), 1024);
                REQUIRE(decoded.ok());
                const std::vector<std::uint8_t> source{10, 20, 30, 40, 10, 20,  30,  40,
                                                       50, 60, 70, 80, 90, 100, 110, 120};
                for (unsigned i = 0; i < 4; ++i) {
                    const auto x = (orientation & 16) ? 1 - i % 2 : i % 2, y = (orientation & 32) ? i / 2 : 1 - i / 2;
                    for (unsigned c = 0; c < 4; ++c)
                        REQUIRE_EQ(decoded.value().rgba[(y * 2 + x) * 4 + c],
                                   channels == 3 && c == 3 ? std::uint8_t(255) : source[i * 4 + c]);
                }
            }
}
TEST_CASE("asset.import.tgaRejectsBadPacketsAndCooksLinearPixels") {
    auto source = fixture(true, 4, 32);
    for (std::size_t size = 0; size < source.size(); ++size)
        REQUIRE(!asset::detail::decodeTgaRgba8(std::span(source).first(size), 1024).ok());
    REQUIRE(!asset::detail::decodeTgaRgba8(source, 39).ok());
    auto overflow = source;
    overflow[18]  = 255;
    REQUIRE(!asset::detail::decodeTgaRgba8(overflow, 1024).ok());
    auto interleaved = source;
    interleaved[17] |= 64;
    REQUIRE(!asset::detail::decodeTgaRgba8(interleaved, 1024).ok());
    auto footer = source;
    footer.insert(footer.end(), {255, 255, 255, 255, 0, 0, 0, 0});
    const std::string signature("TRUEVISION-XFILE.\0", 18);
    footer.insert(footer.end(), signature.begin(), signature.end());
    REQUIRE(!asset::detail::decodeTgaRgba8(footer, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tga.test", "1.0.0", {}};
    request.sourceName   = "mask.tga";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    bool checked = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path == imported.value().manifest.assets.front().definition) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk[28], std::uint8_t(10));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[43], std::uint8_t(120));
            checked = true;
        }
    REQUIRE(checked);
}
}  // namespace test_asset_import_tga
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_tiff)
#include "PrivateTestScope.h"
#include <zlib.h>
#include <array>
#include "asset/CanonicalImageCook.h"
#include "asset/SourceTiff.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_tiff.cpp
namespace test_asset_import_tiff {
using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(unsigned compression, unsigned bits, bool little, bool reverse = false) {
    std::vector<std::uint8_t> raw;
    const auto                put = [&](std::vector<std::uint8_t>& v, unsigned n, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) v.push_back(std::uint8_t(n >> ((little ? i : bytes - 1 - i) * 8)));
    };
    for (auto value : {10u, 20u, 30u, 40u, 90u, 90u, 90u, 90u}) put(raw, bits == 8 ? value : value * 257, bits / 8);
    std::vector<std::uint8_t> encoded;
    if (compression == 1)
        encoded = raw;
    else if (compression == 8) {
        uLongf length = compressBound(uLong(raw.size()));
        encoded.resize(length);
        REQUIRE_EQ(compress2(encoded.data(), &length, raw.data(), uLong(raw.size()), 6), Z_OK);
        encoded.resize(length);
    } else {
        std::vector<unsigned> codes{256};
        for (auto v : raw) codes.push_back(v);
        codes.push_back(257);
        unsigned acc = 0, count = 0;
        for (auto code : codes) {
            acc = (acc << 9) | code;
            count += 9;
            while (count >= 8) {
                count -= 8;
                encoded.push_back(std::uint8_t(acc >> count));
            }
        }
        if (count) encoded.push_back(std::uint8_t(acc << (8 - count)));
    }
    constexpr unsigned        tags = 12, bitsOffset = 8 + 2 + tags * 12 + 4, stripOffset = bitsOffset + 8;
    std::vector<std::uint8_t> out{std::uint8_t(little ? 'I' : 'M'), std::uint8_t(little ? 'I' : 'M')};
    put(out, 42, 2);
    put(out, 8, 4);
    put(out, tags, 2);
    auto tag = [&](unsigned id, unsigned type, unsigned n, unsigned value) {
        put(out, id, 2);
        put(out, type, 2);
        put(out, n, 4);
        if (type == 3 && n == 1) {
            put(out, value, 2);
            put(out, 0, 2);
        } else
            put(out, value, 4);
    };
    tag(256, 4, 1, 2);
    tag(257, 4, 1, 1);
    tag(258, 3, 4, bitsOffset);
    tag(259, 3, 1, compression);
    tag(262, 3, 1, 2);
    tag(273, 4, 1, stripOffset);
    tag(274, 3, 1, reverse ? 2 : 1);
    tag(277, 3, 1, 4);
    tag(278, 4, 1, 1);
    tag(279, 4, 1, unsigned(encoded.size()));
    tag(317, 3, 1, 2);
    tag(338, 3, 1, 0);
    put(out, 0, 4);
    for (int i = 0; i < 4; ++i) put(out, bits, 2);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}
}  // namespace
TEST_CASE("asset.import.tiffPreservesFourthChannelPredictorAndEndian") {
    for (auto compression : {1u, 5u, 8u})
        for (auto bits : {8u, 16u})
            for (bool little : {false, true}) {
                auto source = fixture(compression, bits, little);
                auto result = asset::detail::decodeTiffRgba8(source, 1024);
                REQUIRE(result.ok());
                REQUIRE_EQ(result.value().rgba, (std::vector<std::uint8_t>{10, 20, 30, 40, 100, 110, 120, 130}));
                auto reversed = asset::detail::decodeTiffRgba8(fixture(compression, bits, little, true), 1024);
                REQUIRE(reversed.ok());
                REQUIRE_EQ(reversed.value().rgba, (std::vector<std::uint8_t>{100, 110, 120, 130, 10, 20, 30, 40}));
            }
}
TEST_CASE("asset.import.tiffRejectsTruncationAndBudgetAndCooksPixels") {
    const auto source = fixture(5, 8, true);
    for (std::size_t n = 0; n < source.size(); ++n) {
        auto truncated = asset::detail::decodeTiffRgba8(std::span(source).first(n), 1024);
        REQUIRE(!truncated.ok());
    }
    REQUIRE(!asset::detail::decodeTiffRgba8(source, 31).ok());
    auto malformed = source;
    malformed[4]   = 255;
    REQUIRE(!asset::detail::decodeTiffRgba8(malformed, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tiff.test", "1.0.0", {}};
    request.sourceName   = "mask.tif";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    const auto path = imported.value().manifest.assets.front().definition;
    for (const auto& entry : imported.value().entries)
        if (entry.path == path) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk.size(), std::size_t(36));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[35], std::uint8_t(130));
            const auto json =
                Value::fromJson(std::string(cooked.value().definition.begin(), cooked.value().definition.end()));
            REQUIRE(json.ok());
            REQUIRE_EQ(json.value().getIf<Value::Object>()->at("sourceEncoding").asString(), std::string("tiff"));
        }
}
}  // namespace test_asset_import_tiff
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_collection)
#include "PrivateTestScope.h"
#include <algorithm>
#include "asset/AssetCooker.h"
#include "asset/EvaArchive.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySource.h"
#include "asset_import/UnityPackageFixture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_import_unity_collection.cpp
namespace test_asset_import_unity_collection {
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {
std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

UnityProjectImportRequest collection() {
    UnityProjectImportRequest request;
    request.package = {
        *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
        "unity.collection",
        "1.0.0",
        {{"provider", Value("local")}, {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
    request.files["Assets/pixel.png"] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    request.files["Assets/pixel.png.meta"] = bytes(
        "fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "TextureImporter:\n  sRGBTexture: 0\n  spriteMode: 1\n");
    request.files["Assets/prop.prefab"]      = bytes(R"yaml(%YAML 1.1
--- !u!1 &1
GameObject:
  m_Name: Prop
--- !u!4 &4
Transform:
  m_GameObject: {fileID: 1}
  m_Father: {fileID: 0}
  m_LocalPosition: {x: 1, y: 2, z: 3}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalScale: {x: 1, y: 1, z: 1}
--- !u!23 &23
MeshRenderer:
  m_GameObject: {fileID: 1}
)yaml");
    request.files["Assets/prop.prefab.meta"] = bytes("guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nPrefabImporter:\n");
    request.files["Assets/walk.anim"]        = bytes("%YAML 1.1\n--- !u!74 &7400000\nAnimationClip:\n  m_Name: Walk\n");
    request.files["Assets/walk.anim.meta"]   = bytes("guid: cccccccccccccccccccccccccccccccc\nNativeFormatImporter:\n");
    return request;
}
}  // namespace

TEST_CASE("asset.import.unityCollectionAcceptsCrLfMetadata") {
    auto request = collection();
    for (auto& [path, data] : request.files) {
        if (!path.ends_with(".meta")) continue;
        std::string crlf;
        for (const auto byte : data) {
            if (byte == '\n') crlf += '\r';
            crlf += static_cast<char>(byte);
        }
        data = bytes(crlf);
    }
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
}

TEST_CASE("asset.import.unityCollectionReportsNegativeComponentIdsWithoutLosingHierarchy") {
    auto       request  = collection();
    const auto collider = bytes("\n--- !u!65 &-4356345918748047990\nBoxCollider:\n  m_GameObject: {fileID: 1}\n");
    auto&      prefab   = request.files.at("Assets/prop.prefab");
    prefab.insert(prefab.end(), collider.begin(), collider.end());
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.feature == "Prefab.component:65:-4356345918748047990" &&
               f.disposition == ImportDisposition::Unsupported;
    }));
}

TEST_CASE("asset.import.unityCollectionCombinesAssetsPreservesSourcesAndCooks") {
    const auto request  = collection();
    auto       prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/pixel.png"));
    for (const auto& [path, data] : request.files) {
        auto found = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                                  [&](const auto& entry) { return entry.path == "sources/unity/" + path; });
        REQUIRE(found != prepared.value().entries.end());
        REQUIRE(found->bytes == data);
    }
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.feature.starts_with("Prefab.component:23") && f.disposition == ImportDisposition::Unsupported;
    }));
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.sourcePath == "Assets/walk.anim" && f.disposition == ImportDisposition::Unsupported;
    }));
    auto encoded = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, CookPublication::LocalInspection, 16};
    auto cooked = cookEvaToEvpack(archive.value(), profile);
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    REQUIRE_EQ(pack.value().chunks().size(), std::size_t(3));
    auto repeated = prepareUnityProjectImport(request);
    REQUIRE(repeated.ok());
    auto repeatedBytes = buildEvaArchive(repeated.value().manifest, repeated.value().entries);
    REQUIRE(repeatedBytes.ok());
    REQUIRE(encoded.value() == repeatedBytes.value());
}

TEST_CASE("asset.import.unityCollectionRetainsIdentityAfterMoveAndAddsDistinctImages") {
    auto request = collection();
    auto before  = prepareUnityProjectImport(request);
    REQUIRE(before.ok());
    const auto originalImage               = before.value().manifest.entrypoints.at("Assets/pixel.png");
    const auto originalPrefab              = before.value().manifest.entrypoints.at("Assets/prop.prefab");
    request.files["Assets/moved.png"]      = std::move(request.files.at("Assets/pixel.png"));
    request.files["Assets/moved.png.meta"] = std::move(request.files.at("Assets/pixel.png.meta"));
    request.files.erase("Assets/pixel.png");
    request.files.erase("Assets/pixel.png.meta");
    request.files["Assets/renamed.prefab"]      = std::move(request.files.at("Assets/prop.prefab"));
    request.files["Assets/renamed.prefab.meta"] = std::move(request.files.at("Assets/prop.prefab.meta"));
    request.files.erase("Assets/prop.prefab");
    request.files.erase("Assets/prop.prefab.meta");
    request.files["Assets/other.png"]      = request.files.at("Assets/moved.png");
    request.files["Assets/other.png.meta"] = bytes("guid: dddddddddddddddddddddddddddddddd\nTextureImporter:\n");
    auto after                             = prepareUnityProjectImport(request);
    REQUIRE(after.ok());
    REQUIRE_EQ(after.value().manifest.assets.size(), std::size_t(3));
    REQUIRE(after.value().manifest.entrypoints.at("Assets/moved.png") == originalImage);
    REQUIRE(after.value().manifest.entrypoints.at("Assets/renamed.prefab") == originalPrefab);
    REQUIRE(after.value().manifest.entrypoints.at("Assets/other.png") != originalImage);
}

TEST_CASE("asset.import.unityCollectionRejectsCorruptConvertibleAndUnsupportedOnlyInputs") {
    auto request = collection();
    request.files.at("Assets/pixel.png")[45] ^= 1;
    auto corrupt = prepareUnityProjectImport(request);
    REQUIRE(!corrupt.ok());
    request = collection();
    request.files.erase("Assets/pixel.png");
    request.files.erase("Assets/pixel.png.meta");
    request.files.erase("Assets/prop.prefab");
    request.files.erase("Assets/prop.prefab.meta");
    auto unsupported = prepareUnityProjectImport(request);
    REQUIRE(!unsupported.ok());
    REQUIRE(unsupported.error()->code() == DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.unityCollectionTracksMetadataChangesAndGuidCase") {
    auto request = collection();
    auto before  = prepareUnityProjectImport(request);
    REQUIRE(before.ok());
    const auto oldKey                        = before.value().manifest.provenance.at("importKey").asString();
    const auto oldPrefab                     = before.value().manifest.entrypoints.at("Assets/prop.prefab");
    request.files["Assets/prop.prefab.meta"] = bytes("guid: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\nPrefabImporter:\n");
    auto&      meta                          = request.files.at("Assets/pixel.png.meta");
    const auto settings                      = bytes("  spritePixelsToUnits: 64\n");
    meta.insert(meta.end(), settings.begin(), settings.end());
    auto after = prepareUnityProjectImport(request);
    REQUIRE(after.ok());
    REQUIRE(after.value().manifest.entrypoints.at("Assets/prop.prefab") == oldPrefab);
    REQUIRE(after.value().manifest.provenance.at("importKey").asString() != oldKey);
}

TEST_CASE("asset.import.unityCollectionRejectsMalformedNestedPrefabBeforePublication") {
    auto request                        = collection();
    request.files["Assets/prop.prefab"] = bytes("%YAML 1.1\n--- !u!1001 &123\nPrefabInstance:\n");
    auto result                         = prepareUnityProjectImport(request);
    REQUIRE(!result.ok());
}

TEST_CASE("asset.import.unityPackageAndDirectoryProduceIdenticalCanonicalArchive") {
    auto request = collection();
    auto indexed = indexUnitySources(request.files);
    REQUIRE(indexed.ok());
    std::vector<std::uint8_t> tar;
    for (const auto& source : indexed.value().assets) {
        unity_test::appendTar(tar, source.guid + "/asset", request.files.at(source.path));
        unity_test::appendTar(tar, source.guid + "/asset.meta", request.files.at(source.path + ".meta"));
        unity_test::appendTar(tar, source.guid + "/pathname", bytes(source.path));
    }
    tar.resize(tar.size() + 1024);
    auto unpacked = readUnityPackage(unity_test::gzip(tar));
    REQUIRE(unpacked.ok());
    REQUIRE(unpacked.value() == request.files);
    auto direct = prepareUnityProjectImport(request);
    REQUIRE(direct.ok());
    request.files = std::move(unpacked).takeValue();
    auto packaged = prepareUnityProjectImport(request);
    REQUIRE(packaged.ok());
    auto directArchive = buildEvaArchive(direct.value().manifest, direct.value().entries);
    REQUIRE(directArchive.ok());
    auto packagedArchive = buildEvaArchive(packaged.value().manifest, packaged.value().entries);
    REQUIRE(packagedArchive.ok());
    REQUIRE(directArchive.value() == packagedArchive.value());
}
}  // namespace test_asset_import_unity_collection
#endif
