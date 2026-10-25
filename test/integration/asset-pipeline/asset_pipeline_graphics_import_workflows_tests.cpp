// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "asset-pipeline",
  "module": "asset_graphics",
  "modules": [
    "asset",
    "asset_graphics",
    "asset_import",
    "asset_procgen",
    "asset_scene",
    "common",
    "graphics"
  ],
  "origins": [
    "asset_cooker.cpp",
    "asset_graphics_image_mips.cpp",
    "asset_graphics_loader.cpp",
    "asset_image_mips.cpp",
    "asset_import.cpp",
    "asset_import_animation.cpp",
    "asset_import_structured_models.cpp",
    "asset_import_unity_collection.cpp",
    "asset_import_unity_fbx.cpp",
    "asset_import_unity_native.cpp",
    "asset_import_unity_package.cpp",
    "asset_import_unity_sprite.cpp",
    "asset_import_unity_vegetation_material.cpp",
    "asset_procgen_terrain_detail_render.cpp",
    "asset_scene_bindings.cpp",
    "asset_shader.cpp",
    "graphics_vegetation_translucency.cpp"
  ],
  "cases": {
    "asset-files": [
      "asset.import.unityTerrainPrefabUsesGuidFileIdAndCanonicalCoordinates",
      "asset.packageStore.reopensBeforeAtomicPublishAndFailureKeepsOldEvpack",
      "asset.packageStore.publishesAndReopensEva",
      "asset.import.realGlbDecodesCanonicalMeshAndTraversesRuntimePack",
      "asset.import.animationFixtureBecomesCanonicalEvaAndTwoRuntimeTargets",
      "asset.import.animationFixtureRejectsDeclaredKeyCountMismatch",
      "asset.import.structuredModelsScadCompilerAbsent",
      "asset.import.structuredModelsScadCompilerTimeout",
      "asset.import.unityFbxCharacterProducesSkinAndAnimation"
    ],
    "render-offscreen": [
      "asset.import.unityTexture3dSurvivesCookAndVulkanUpload",
      "graphics.vegetation.translucency_gpu_backlight_and_ambient"
    ],
    "render-window": [
      "asset.import.unityStaticPrefabRendersOwnedEcsInstances",
      "asset.procgen.terrainDetailSidecarPublishesDecodableRuntimeResource"
    ]
  }
}
*/
// clang-format on

// Cohesive asset workflow contracts share one physical translation unit.

// Contract section: asset cooker
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "asset/AssetCooker.h"
#include "asset/RuntimeDefinition.h"
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


// Contract section: asset graphics image mips
#include "PrivateTestScope.h"

#include "asset/CanonicalImageCook.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
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
    CHECK(shader.getFromVar("terrainLayer0ST", st, sizeof(st)) == 0);
    CHECK(std::abs(st[0] - .125f) < .0001f);
    CHECK(std::abs(st[1] - .25f) < .0001f);
    CHECK(std::abs(st[2] - .25f) < .0001f);
    CHECK(std::abs(st[3] - .25f) < .0001f);
}
}  // namespace test_asset_graphics_image_mips


// Contract section: asset graphics loader
#include "PrivateTestScope.h"

#include <bit>
#include "asset/CanonicalMesh.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "graphics/IMeshResourceFactory.h"
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


// Contract section: asset image mips
#include "PrivateTestScope.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include "asset/AssetMigration.h"
#include "asset/AssetPackageStore.h"
#include "asset/EvpackResourceReader.h"
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
    CHECK_EQ(wavingGrass->at("amount").asDouble(), .4);
    CHECK_EQ(wavingGrass->at("strength").asDouble(), .8);
    CHECK_EQ(wavingGrass->at("tint").getIf<Value::Array>()->at(1).asDouble(), .7);
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
    CHECK_EQ(prototype->at("bendFactor").asDouble(), .35);
    CHECK_EQ(prototype->at("holeEdgePadding").asDouble(), .4);
    CHECK(!prototype->at("useDensityScaling").asBool());
    CHECK_EQ(prototype->at("healthyColor").getIf<Value::Array>()->at(1).asDouble(), .8);
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
        R"json({"schema":"eve.terrain-material","schemaVersion":1,"layers":[{"name":"bad","diffuseSource":"","normalSource":"","weightSource":"","normalConvention":"opengl","tileSizeMeters":0}]})json");
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


// Contract section: asset import
#include "PrivateTestScope.h"

#include <iterator>
#include "asset/import/AssetImporter.h"
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


// Contract section: asset import animation
#include "PrivateTestScope.h"

#include <array>
#include <memory>
#include <sstream>
#include "asset/CanonicalVolumeTextureCook.h"
#include "asset/SourceBc3.h"
#include "asset/import/LegacyAnimationImporter.h"
#include "asset/import/UnrealImporter.h"
#include "asset/procgen/EvpackPointGraphLoader.h"
#include "graphics/Graphics.h"
#include "graphics/PbrSurface.h"
#include "procgen/PointGraph.h"
#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"
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


// Contract section: asset import structured models
#include "PrivateTestScope.h"

#include "PathBesideSource.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/StructuredModelImporter.h"
#include "graphics/IImageResourceFactory.h"
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


// Contract section: asset import unity collection
#include "PrivateTestScope.h"

#include "asset/EvaArchive.h"
#include "asset/import/UnitySource.h"
#include "asset_import/UnityPackageFixture.h"
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

// Contract section: asset import unity fbx
#include "PrivateTestScope.h"

#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <limits>
#include "asset/SourcePng.h"
#include "asset/graphics/PrefabBoundsCatalog.h"
#include "asset/graphics/ProcgenPrefabSceneSink.h"
#include "asset/graphics/StaticPrefabRenderInstance.h"
#include "common/Capability.h"
#include "common/ProcgenWorldQuery.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "scene/Scene.h"
#include "window/Window.h"
// asset_import_unity_fbx.cpp
namespace test_asset_import_unity_fbx {
using namespace eve;
using namespace eve::asset_import;

namespace {
UnityProjectImportRequest triangleFbx(bool extendedStreams = true, bool twoSubmeshes = false) {
    aiScene scene;
    scene.mRootNode                           = new aiNode("Root");
    scene.mRootNode->mNumChildren             = 1;
    scene.mRootNode->mChildren                = new aiNode*[1]{new aiNode("GroundTileModular.002")};
    scene.mRootNode->mChildren[0]->mParent    = scene.mRootNode;
    const unsigned meshCount                  = twoSubmeshes ? 2 : 1;
    scene.mRootNode->mChildren[0]->mNumMeshes = meshCount;
    scene.mRootNode->mChildren[0]->mMeshes    = twoSubmeshes ? new unsigned[2]{0, 1} : new unsigned[1]{0};
    scene.mNumMeshes                          = meshCount;
    scene.mMeshes                             = new aiMesh*[meshCount];
    for (unsigned meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        scene.mMeshes[meshIndex] = new aiMesh;
        auto& mesh               = *scene.mMeshes[meshIndex];
        mesh.mName.Set("Geometry");
        mesh.mMaterialIndex      = meshIndex;
        mesh.mNumVertices        = 3;
        mesh.mVertices           = new aiVector3D[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        mesh.mNormals            = new aiVector3D[3]{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
        mesh.mTextureCoords[0]   = new aiVector3D[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mesh.mNumUVComponents[0] = 2;
        if (extendedStreams) {
            mesh.mTextureCoords[1]   = new aiVector3D[3]{{0.25f, 0.5f, 0}, {0.75f, 0.5f, 0}, {0.25f, 1.f, 0}};
            mesh.mNumUVComponents[1] = 2;
            mesh.mTangents           = new aiVector3D[3]{{1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
            mesh.mBitangents         = new aiVector3D[3]{{0, 0, -1}, {0, 0, -1}, {0, 0, -1}};
            mesh.mColors[0]          = new aiColor4D[3]{{1, 0, 0, 1}, {0, 1, 0, 0.5f}, {0, 0, 1, 0.25f}};
        }
        mesh.mNumFaces             = 1;
        mesh.mFaces                = new aiFace[1];
        mesh.mFaces[0].mNumIndices = 3;
        mesh.mFaces[0].mIndices    = new unsigned[3]{0, 1, 2};
    }
    scene.mNumMaterials = meshCount;
    scene.mMaterials    = new aiMaterial*[meshCount];
    for (unsigned index = 0; index < meshCount; ++index) scene.mMaterials[index] = new aiMaterial;
    Assimp::Exporter exporter;
    const auto*      blob = exporter.ExportToBlob(&scene, "fbxa");
    if (!blob) throw std::runtime_error(exporter.GetErrorString());
    UnityProjectImportRequest request;
    request.package   = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"), "fbx.test", "1.0.0", {}};
    const auto* begin = static_cast<const std::uint8_t*>(blob->data);
    request.files["Assets/tile.fbx"] = {begin, begin + blob->size};
    const std::string meta =
        "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nModelImporter:\n  internalIDToNameTable: []\n  meshes:\n"
        "    globalScale: 100\n    fileIdsGeneration: 2\n    useFileScale: 1\n    swapUVChannels: 0\n"
        "  tangentSpace:\n    normalImportMode: 0\n";
    request.files["Assets/tile.fbx.meta"] = {meta.begin(), meta.end()};
    return request;
}

namespace {
UnityProjectImportRequest trianglePrefab() {
    auto request = triangleFbx(false);
    auto put     = [&](const char* path, const std::string& data) { request.files[path] = {data.begin(), data.end()}; };
    put("Assets/tile.mat", R"(--- !u!21 &2100000
Material:
  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}
  m_SavedProperties:
    m_TexEnvs:
    - _MainTex:
        m_Texture: {fileID: 2800000, guid: dddddddddddddddddddddddddddddddd, type: 3}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    m_Floats:
    - _Metallic: 0.5
    - _Glossiness: 0.25
    - _Mode: 0
    m_Colors:
    - _Color: {r: 1, g: 0.5, b: 0.25, a: 1}
)");
    put("Assets/tile.mat.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nNativeFormatImporter:\n");
    request.files["Assets/pixel.png"] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0,    0,    0,    0x0d, 0x49, 0x48, 0x44, 0x52, 0, 0,
        0,    1,    0,    0,    0,    1,    8,    6,    0,    0,    0,    0x1f, 0x15, 0xc4, 0x89, 0,    0, 0,
        0x0d, 0x49, 0x44, 0x41, 0x54, 8,    0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0,    5,    0,    1, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0,    0,    0,    0,    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    put("Assets/pixel.png.meta", "guid: dddddddddddddddddddddddddddddddd\nTextureImporter:\n  sRGBTexture: 1\n");
    put("Assets/tile.prefab", R"(--- !u!1 &1
GameObject:
  m_Name: Tile
--- !u!4 &4
Transform:
  m_GameObject: {fileID: 1}
  m_Father: {fileID: 0}
  m_LocalPosition: {x: 1, y: 2, z: 3}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalScale: {x: 1, y: 1, z: 1}
--- !u!33 &33
MeshFilter:
  m_GameObject: {fileID: 1}
  m_Mesh: {fileID: -813904291765591093, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
--- !u!23 &23
MeshRenderer:
  m_GameObject: {fileID: 1}
  m_Enabled: 1
  m_Materials:
  - {fileID: 2100000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 2}
)");
    put("Assets/tile.prefab.meta", "guid: cccccccccccccccccccccccccccccccc\nPrefabImporter:\n");
    return request;
}
}  // namespace

TEST_CASE("asset.import.unityStaticPrefabResolvesAndReleasesGpuLeases") {
    auto request  = trianglePrefab();
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    auto encoded = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = asset::parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    asset::AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, asset::CookPublication::LocalInspection, 16};
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile);
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    class Meshes                final : public graphics::IMeshResourceFactory {
    public:
        int                     uploads = 0, releases = 0, token = 0;
        bool                    reject = false;
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float*, int, const std::uint32_t*,
                                                          int) override {
            ++uploads;
            if (reject)
                return Result<graphics::Mesh*>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "injected upload failure"));
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> setMeshTangentFrame(graphics::Mesh*, std::span<const float>, std::span<const float>) override {
            return Result<void>::success();
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } meshes;
    class Images final : public graphics::IImageResourceFactory {
    public:
        int                        token = 0, uploads = 0, releases = 0;
        bool                       reject = false;
        Result<graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
            ++uploads;
            if (reject)
                return Result<graphics::Texture*>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "injected texture failure"));
            return Result<graphics::Texture*>::success(reinterpret_cast<graphics::Texture*>(&token));
        }
        Result<void> releaseImage(graphics::Texture*) override {
            ++releases;
            return Result<void>::success();
        }
    } images;
    asset::EvpackCapabilities caps{"win32", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    const auto                ref          = prepared.value().manifest.entrypoints.at("Assets/tile.prefab");
    auto                      templateData = asset_scene::EvpackSceneTemplateLoader(reader).load(ref, caps);
    REQUIRE(templateData.ok());
    REQUIRE(templateData.value().root.children.size() == 1u);
    REQUIRE_EQ(templateData.value().root.children[0].x, 1.f);
    REQUIRE_EQ(templateData.value().root.children[0].y, 2.f);
    REQUIRE_EQ(templateData.value().root.children[0].z, -3.f);
    auto loaded = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(loaded.ok());
    auto owner = std::shared_ptr<asset_graphics::EvpackStaticPrefab>(std::move(loaded).takeValue());
    REQUIRE_EQ(owner->drawCount(), std::size_t(1));
    REQUIRE_EQ(meshes.uploads, 1);
    auto bounds = owner->localBounds();
    REQUIRE(bounds.ok());
    // This FBX export decodes to X/Z in [-1,0], Y in [0,1]; the prefab root retains (1,2,-3).
    const std::array<float, 6> expectedBounds{0.f, 2.f, -4.f, 1.f, 3.f, -3.f};
    for (std::size_t axis = 0; axis < expectedBounds.size(); ++axis)
        REQUIRE(std::fabs(bounds.value()[axis] - expectedBounds[axis]) < 1e-4f);
    auto packageBuild = reader.buildId();
    REQUIRE(packageBuild.ok());
    REQUIRE(owner->sourceAsset() == ref);
    REQUIRE(owner->sourceBuildId() == packageBuild.value());
    asset_graphics::PrefabBoundsCatalog catalog;
    REQUIRE(catalog.add(*owner).ok());
    REQUIRE(!catalog.add(*owner).ok());
    auto catalogBounds = catalog.localBounds(ref, packageBuild.value());
    REQUIRE(catalogBounds.ok());
    auto placementBounds = catalog.localBounds(ref, packageBuild.value(), PrefabRootPose::ReplaceTranslationRotation);
    REQUIRE(placementBounds.ok());
    const std::array<float, 6> expectedPlacementBounds{-1.f, 0.f, -1.f, 0.f, 1.f, 0.f};
    for (std::size_t axis = 0; axis < expectedPlacementBounds.size(); ++axis)
        REQUIRE(std::fabs(placementBounds.value()[axis] - expectedPlacementBounds[axis]) < 1e-4f);
    REQUIRE(!catalog.localBounds(ref, packageBuild.value(), static_cast<PrefabRootPose>(255)).ok());
    auto staleBuild = PersistentId::parse("00000000-0000-0000-0000-000000000001");
    REQUIRE(staleBuild.has_value());
    auto missingAsset = AssetRef::fromId(*staleBuild);
    REQUIRE(missingAsset.ok());
    REQUIRE(!catalog.localBounds(ref, *staleBuild).ok());
    REQUIRE(!catalog.localBounds(missingAsset.value(), packageBuild.value()).ok());
    class Ground final : public IProcgenWorldQuery {
        Result<ProcgenSurfaceHit> projectDown(float x, float z, float, float, std::uint64_t) override {
            ProcgenSurfaceHit hit;
            hit.hit = true;
            hit.x   = x;
            hit.z   = z;
            hit.y   = x + 2.f * z;
            return Result<ProcgenSurfaceHit>::success(hit);
        }
    } ground;
    struct RestoreProviders {
        IProcgenWorldQuery*  world  = eve::cap::query<IProcgenWorldQuery>();
        IProcgenAssetBounds* assets = eve::cap::query<IProcgenAssetBounds>();
        ~RestoreProviders() {
            eve::cap::provide<IProcgenWorldQuery>(world);
            eve::cap::provide<IProcgenAssetBounds>(assets);
        }
    } restoreProviders;
    eve::cap::provide<IProcgenWorldQuery>(&ground);
    eve::cap::provide<IProcgenAssetBounds>(&catalog);
    auto road = procgen::road::RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(road.ok());
    auto style              = road.value().edges()[0].style;
    style.sideObjectsRight  = false;
    style.sideTerrain.mode  = procgen::road::RoadSideTerrainMode::Average;
    style.sideTerrain.asset = procgen::road::RoadSideAssetBinding{ref, packageBuild.value()};
    REQUIRE(road.value().setEdgeStyle(1, style).ok());
    procgen::Params roadParams;
    roadParams.setString("placementWorldRevision", "fixture-plane/v1");
    REQUIRE(procgen::road::setRoadRecipeNetwork(roadParams, road.value()).ok());
    auto roadBake = procgen::road::bakeRoadNetworkRecipe(roadParams);
    REQUIRE(roadBake.ok());
    int sidePoints = 0;
    for (int i = 0; i < roadBake.value().placements.getCount(); ++i) {
        const auto& points = roadBake.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++sidePoints;
        const auto& p = points.points()[i];
        REQUIRE(std::fabs(p.y - (p.x + 2.f * p.z - .5f)) < 1e-4f);
        REQUIRE(points.getStringAttribute(i, "asset", "") == ref.format());
        REQUIRE(points.getStringAttribute(i, "asset_build_id", "") == packageBuild.value().format());
        REQUIRE(points.getStringAttribute(i, "asset_root_pose", "") == "placement");
    }
    REQUIRE(sidePoints > 0);
    auto* sceneGraphics = graphics::Graphics::create();
    REQUIRE(sceneGraphics != nullptr);
    auto placedScene = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics,
                                                                        PrefabRootPose::ReplaceTranslationRotation);
    REQUIRE(placedScene.ok());
    for (unsigned axis = 0; axis < 3; ++axis)
        REQUIRE(std::fabs(placedScene.value()->parts()[0].localTransform[12 + axis]) < 1e-5f);
    REQUIRE(placedScene.value()->release().ok());
    REQUIRE(!asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics, static_cast<PrefabRootPose>(255))
                 .ok());
    auto preparedScene = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
    REQUIRE(preparedScene.ok());
    REQUIRE(preparedScene.value()->parts().size() == 1u);
    const auto& scenePart = preparedScene.value()->parts()[0];
    REQUIRE(scenePart.mesh != nullptr);
    REQUIRE(scenePart.material != nullptr);
    REQUIRE(scenePart.material->hasPbrSurface());
    REQUIRE(std::fabs(scenePart.localTransform[12] - 1.f) < 1e-5f);
    auto secondScene = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
    REQUIRE(secondScene.ok());
    REQUIRE(secondScene.value()->parts()[0].mesh == scenePart.mesh);
    REQUIRE(secondScene.value()->parts()[0].material != scenePart.material);
    REQUIRE(!owner->release().ok());
    REQUIRE(meshes.releases == 0);
    REQUIRE(images.releases == 0);
    REQUIRE(owner->drawCount() == 1u);
    REQUIRE(preparedScene.value()->release().ok());
    REQUIRE(preparedScene.value()->release().ok());
    REQUIRE(preparedScene.value()->parts().empty());
    REQUIRE(!owner->release().ok());
    REQUIRE(secondScene.value()->parts().size() == 1u);
    REQUIRE(secondScene.value()->parts()[0].material->hasPbrSurface());
    REQUIRE(secondScene.value()->release().ok());
    REQUIRE(!asset_graphics::EvpackStaticPrefab::prepareScene({}, *sceneGraphics).ok());
    auto renderPreparation = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
    REQUIRE(renderPreparation.ok());
    const auto            sourceMatrix = renderPreparation.value()->parts()[0].localTransform;
    const auto*           sourceMesh   = renderPreparation.value()->parts()[0].mesh;
    std::array<float, 16> world{-2.f, 0.f, 0.f, 0.f, .5f, 3.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 10.f, 20.f, 30.f, 1.f};
    auto instance = asset_graphics::StaticPrefabRenderInstance::create(std::move(renderPreparation).takeValue(), world);
    REQUIRE(instance.ok());
    REQUIRE(instance.value()->rendererCount() == 1u);
    auto renderer = instance.value()->renderer(0);
    REQUIRE(renderer.ok());
    REQUIRE(renderer.value()->getMesh() == sourceMesh);
    REQUIRE(renderer.value()->getMaterial()->hasPbrSurface());
    REQUIRE(renderer.value()->getVisible());
    REQUIRE(renderer.value()->transform()->hasLinkedWorld);
    for (unsigned column = 0; column < 4; ++column)
        for (unsigned row = 0; row < 4; ++row) {
            float expected = 0.f;
            for (unsigned k = 0; k < 4; ++k) expected += world[k * 4 + row] * sourceMatrix[column * 4 + k];
            REQUIRE(std::fabs(renderer.value()->transform()->linkedWorld[column][row] - expected) < 1e-5f);
        }
    REQUIRE(!owner->release().ok());
    world[12] += 5.f;
    REQUIRE(instance.value()->setWorldTransform(world).ok());
    const float movedX       = renderer.value()->transform()->x;
    auto        invalidWorld = world;
    invalidWorld[12]         = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!instance.value()->setWorldTransform(invalidWorld).ok());
    REQUIRE(renderer.value()->transform()->x == movedX);
    auto rejectedPreparation = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
    REQUIRE(rejectedPreparation.ok());
    REQUIRE(
        !asset_graphics::StaticPrefabRenderInstance::create(std::move(rejectedPreparation).takeValue(), invalidWorld)
             .ok());
    REQUIRE(renderer.value()->transform()->x == movedX);
    invalidWorld    = world;
    invalidWorld[0] = std::numeric_limits<float>::max();
    REQUIRE(!instance.value()->setWorldTransform(invalidWorld).ok());
    REQUIRE(renderer.value()->transform()->x == movedX);
    REQUIRE(!instance.value()->renderer(1).ok());
    {
        auto deferredPreparation = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
        REQUIRE(deferredPreparation.ok());
        ecs::ScopedDefer deferred;
        REQUIRE(!asset_graphics::StaticPrefabRenderInstance::create(std::move(deferredPreparation).takeValue(), world)
                     .ok());
    }
    REQUIRE(renderer.value()->transform()->x == movedX);
    ecs::DestroyEntity(renderer.value());
    auto*      replacement       = graphics::Renderable3D::create();
    const auto replacementHandle = ecs::handle_of(replacement);
    REQUIRE(!instance.value()->renderer(0).ok());
    REQUIRE(!instance.value()->setWorldTransform(world).ok());
    REQUIRE(instance.value()->release().ok());
    REQUIRE(ecs::try_get(replacementHandle) == replacement);
    REQUIRE(instance.value()->release().ok());
    REQUIRE(instance.value()->rendererCount() == 0u);
    REQUIRE(!instance.value()->setWorldTransform(world).ok());
    ecs::DestroyEntity(replacement);
    auto ownedPreparation = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics);
    REQUIRE(ownedPreparation.ok());
    auto ownedInstance =
        asset_graphics::StaticPrefabRenderInstance::create(std::move(ownedPreparation).takeValue(), world);
    REQUIRE(ownedInstance.ok());
    auto ownedRenderer = ownedInstance.value()->renderer(0);
    REQUIRE(ownedRenderer.ok());
    const auto ownedHandle = ecs::handle_of(ownedRenderer.value());
    REQUIRE(ownedInstance.value()->release().ok());
    REQUIRE(ecs::try_get(ownedHandle) == nullptr);
    {
        auto* sceneHostSystem = eve::scene::Scene::create();
        auto* baseSink        = cap::query<IProcgenSceneSink>();
        REQUIRE(baseSink != nullptr);
        asset_graphics::ProcgenPrefabSceneSink sink(*sceneGraphics, *baseSink);
        REQUIRE(sink.admit(owner).ok());
        ProcgenInstanceDesc pose;
        pose.id            = "posed-facility";
        pose.sourcePointId = 77;
        pose.x             = 5.f;
        pose.pitch         = 30.f;
        pose.yaw           = 45.f;
        pose.roll          = -20.f;
        pose.scaleX        = -2.f;
        pose.scaleY        = 3.f;
        pose.scaleZ        = .5f;
        pose.asset         = owner->sourceAsset().format();
        pose.assetBinding  = ProcgenInstanceAssetBinding{owner->sourceAsset(), owner->sourceBuildId()};
        REQUIRE(sink.replaceBatch("posed-facilities", 1, {pose}).ok());
        auto host = sceneHostSystem->findHost("__pcg/posed-facilities");
        REQUIRE(host.ok());
        auto node = host.value()->findById(pose.id);
        REQUIRE(node.ok());
        const auto sceneWorld = node.value()->world;
        int        matched    = 0;
        {
            auto view = ecs::View<graphics::Renderable3D, graphics::Renderable3D::Transform3D,
                                  graphics::Renderable3D::MeshRenderer>();
            for (auto it = view.begin(); it != view.end(); ++it) {
                auto [transform, mesh] = *it;
                if (mesh->mesh != sourceMesh || !mesh->visible) continue;
                ++matched;
                for (unsigned column = 0; column < 4; ++column)
                    for (unsigned row = 0; row < 4; ++row) {
                        float expected = 0.f;
                        for (unsigned k = 0; k < 4; ++k) expected += sceneWorld[k][row] * sourceMatrix[column * 4 + k];
                        REQUIRE(std::fabs(transform->linkedWorld[column][row] - expected) < 1e-4f);
                    }
            }
        }
        REQUIRE(matched == 1);
        REQUIRE(!owner->release().ok());
        struct RestoreRecipeSink {
            IProcgenSceneSink* previous;
            ~RestoreRecipeSink() { cap::provide<IProcgenSceneSink>(previous); }
        } restoreSink{baseSink};
        cap::provide<IProcgenSceneSink>(&sink);
        procgen::Procgen proc;
        auto             generatedHandle = proc.newPointSetHandle();
        REQUIRE(generatedHandle.ok());
        auto generatedPoints = proc.resolvePointSet(generatedHandle.value());
        REQUIRE(generatedPoints.isBound());
        *generatedPoints.operator->() = roadBake.value().placements;
        REQUIRE(proc.publishInstances("road-baked-facilities", generatedHandle.value(), "asset", "").ok());
        REQUIRE(sink.rendererCount("road-baked-facilities") == static_cast<std::size_t>(sidePoints));
        REQUIRE(sink.instanceCount("road-baked-facilities") == roadBake.value().placements.getCount());
        REQUIRE(proc.removeInstances("road-baked-facilities").ok());
        REQUIRE(sink.collectRetired().ok());
        REQUIRE(proc.releasePointSet(generatedHandle.value()).ok());
        REQUIRE(sink.release().ok());
    }
    meshes.reject = true;
    auto failed   = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(!failed.ok());
    REQUIRE_EQ(owner->drawCount(), std::size_t(1));
    meshes.reject      = false;
    images.reject      = true;
    auto textureFailed = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(!textureFailed.ok());
    REQUIRE_EQ(meshes.releases, 1);
    REQUIRE_EQ(owner->drawCount(), std::size_t(1));
    auto released = owner->release();
    REQUIRE(released.ok());
    REQUIRE_EQ(meshes.releases, 2);
    REQUIRE_EQ(images.releases, 1);
    REQUIRE_EQ(owner->drawCount(), std::size_t(0));
    auto retainedBounds = owner->localBounds();
    REQUIRE(retainedBounds.ok());
    for (std::size_t axis = 0; axis < expectedBounds.size(); ++axis)
        REQUIRE(retainedBounds.value()[axis] == bounds.value()[axis]);
    REQUIRE(catalog.localBounds(ref, packageBuild.value()).ok());
    REQUIRE(!asset_graphics::EvpackStaticPrefab::prepareScene(owner, *sceneGraphics).ok());
    REQUIRE(!asset_graphics::EvpackStaticPrefab::pinResources(owner).ok());
    REQUIRE(!asset_graphics::EvpackStaticPrefab::pinResources({}).ok());
    // A source root is below the decoder's synthetic container. Replacing its
    // rotation/translation must retain both signed scale and the child's pose.
    images.reject                      = false;
    auto              hierarchyRequest = trianglePrefab();
    auto&             prefabBytes      = hierarchyRequest.files.at("Assets/tile.prefab");
    std::string       hierarchy(prefabBytes.begin(), prefabBytes.end());
    const std::string father = "m_Father: {fileID: 0}";
    hierarchy.replace(hierarchy.find(father), father.size(), "m_Father: {fileID: 400}");
    hierarchy += R"(--- !u!1 &100
GameObject:
  m_Name: PlacementRoot
  m_IsActive: 1
--- !u!4 &400
Transform:
  m_GameObject: {fileID: 100}
  m_Father: {fileID: 0}
  m_LocalPosition: {x: 7, y: 8, z: 9}
  m_LocalRotation: {x: 0, y: 0.70710678, z: 0, w: 0.70710678}
  m_LocalScale: {x: -2, y: 3, z: 4}
)";
    prefabBytes.assign(hierarchy.begin(), hierarchy.end());
    auto hierarchyImport = prepareUnityProjectImport(hierarchyRequest);
    REQUIRE(hierarchyImport.ok());
    auto hierarchyEva = asset::buildEvaArchive(hierarchyImport.value().manifest, hierarchyImport.value().entries);
    REQUIRE(hierarchyEva.ok());
    auto hierarchyArchive = asset::parseEvaArchive(hierarchyEva.value());
    REQUIRE(hierarchyArchive.ok());
    auto hierarchyCook = asset::cookEvaToEvpack(hierarchyArchive.value(), profile);
    REQUIRE(hierarchyCook.ok());
    auto hierarchyPack = asset::parseEvpack(hierarchyCook.value().bytes);
    REQUIRE(hierarchyPack.ok());
    asset::EvpackResourceReader hierarchyReader(
        std::make_shared<const asset::Evpack>(std::move(hierarchyPack).takeValue()));
    auto hierarchyLoad = asset_graphics::EvpackStaticPrefab::load(hierarchyReader, meshes, images, ref, caps);
    REQUIRE(hierarchyLoad.ok());
    auto hierarchyOwner  = std::shared_ptr<asset_graphics::EvpackStaticPrefab>(std::move(hierarchyLoad).takeValue());
    auto hierarchyBounds = hierarchyOwner->localBounds(PrefabRootPose::ReplaceTranslationRotation);
    REQUIRE(hierarchyBounds.ok());
    const std::array<float, 6> expectedHierarchyBounds{-2, 6, -16, 0, 9, -12};
    for (unsigned axis = 0; axis < 6; ++axis)
        REQUIRE(std::fabs(hierarchyBounds.value()[axis] - expectedHierarchyBounds[axis]) < 1e-4f);
    auto hierarchyScene = asset_graphics::EvpackStaticPrefab::prepareScene(hierarchyOwner, *sceneGraphics,
                                                                           PrefabRootPose::ReplaceTranslationRotation);
    REQUIRE(hierarchyScene.ok());
    const auto& hierarchyMatrix = hierarchyScene.value()->parts()[0].localTransform;
    REQUIRE(hierarchyMatrix[0] == -2.f);
    REQUIRE(hierarchyMatrix[5] == 3.f);
    REQUIRE(hierarchyMatrix[10] == 4.f);
    REQUIRE(hierarchyMatrix[12] == -2.f);
    REQUIRE(hierarchyMatrix[13] == 6.f);
    REQUIRE(hierarchyMatrix[14] == -12.f);
    REQUIRE(hierarchyScene.value()->release().ok());
    REQUIRE(hierarchyOwner->release().ok());
}
}  // namespace

TEST_CASE("asset.import.unityStaticPrefabRendersOwnedEcsInstances") {
    auto prepared = prepareUnityProjectImport(trianglePrefab());
    REQUIRE(prepared.ok());
    auto encoded = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = asset::parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    asset::AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, asset::CookPublication::LocalInspection, 16};
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile);
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    auto*                       testWindow = window::Window::create();
    auto*                       gfx        = graphics::Graphics::create();
    window::WindowSettings      settings;
    settings.width  = 320;
    settings.height = 240;
    REQUIRE(testWindow->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(graphics::Color(.04f, .06f, .09f, 1.f));
    auto* camera = graphics::Camera3D::createCamera();
    camera->setEye(-2.f, 2.f, -3.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(1.f, 1.f, 1.f);
    asset_graphics::GraphicsMeshFactoryAdapter  meshes(*gfx);
    asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    const asset::EvpackCapabilities capabilities{"win32", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                            loaded = asset_graphics::EvpackStaticPrefab::load(
        reader, meshes, images, prepared.value().manifest.entrypoints.at("Assets/tile.prefab"), capabilities);
    REQUIRE(loaded.ok());
    auto owner       = std::shared_ptr<asset_graphics::EvpackStaticPrefab>(std::move(loaded).takeValue());
    auto preparation = asset_graphics::EvpackStaticPrefab::prepareScene(owner, *gfx);
    REQUIRE(preparation.ok());
    std::array<float, 16> world{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, -.5f, -2.5f, 3.5f, 1.f};
    auto instance = asset_graphics::StaticPrefabRenderInstance::create(std::move(preparation).takeValue(), world);
    REQUIRE(instance.ok());
    for (const char* feature : {"shadow", "gbuffer", "ao", "gi", "aa", "atmosphere", "msaa", "visResolve", "gpuDriven"})
        gfx->getRenderControl()->disable(feature);
    auto render = [&] {
        for (int frame = 0; frame < 4; ++frame) {
            graphics::RenderSystem3D::render(*gfx);
            graphics::RenderSystem::render(*gfx);
        }
    };
    render();
    std::vector<std::uint8_t> pixels;
    pixels.reserve(settings.width * settings.height * 4);
    int redPixels = 0;
    for (int y = 0; y < settings.height; ++y)
        for (int x = 0; x < settings.width; ++x) {
            const auto color = gfx->getPixel(x, y);
            if (color.r > color.g + .15f && color.r > color.b + .15f) ++redPixels;
            for (float channel : {color.r, color.g, color.b, color.a})
                pixels.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
        }
    REQUIRE(redPixels > 100);
    auto png = asset::detail::encodeSourcePng(settings.width, settings.height, pixels, 4 * 1024 * 1024);
    REQUIRE(png.ok());
    const auto outputPath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" /
                            ("static-prefab-owned-" + gfx->getBackendName() + ".png");
    std::filesystem::create_directories(outputPath.parent_path());
    std::ofstream output(outputPath, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.value().data()), std::streamsize(png.value().size()));
    REQUIRE(output.good());
    REQUIRE(instance.value()->release().ok());
    render();
    int remainingRed = 0;
    for (int y = 0; y < settings.height; y += 2)
        for (int x = 0; x < settings.width; x += 2) {
            const auto color = gfx->getPixel(x, y);
            if (color.r > color.g + .15f && color.r > color.b + .15f) ++remainingRed;
        }
    REQUIRE(remainingRed == 0);
    {
        auto* sceneHostSystem = eve::scene::Scene::create();
        auto* baseSink        = cap::query<IProcgenSceneSink>();
        REQUIRE(baseSink != nullptr);
        asset_graphics::ProcgenPrefabSceneSink sink(*gfx, *baseSink);
        REQUIRE(sink.admit(owner).ok());
        REQUIRE(!sink.admit(owner).ok());
        REQUIRE(!owner->release().ok());
        struct RestoreSceneSink {
            IProcgenSceneSink* previous;
            ~RestoreSceneSink() { cap::provide<IProcgenSceneSink>(previous); }
        } restore{baseSink};
        cap::provide<IProcgenSceneSink>(&sink);
        procgen::Procgen proc;
        auto             pointHandle = proc.newPointSetHandle();
        REQUIRE(pointHandle.ok());
        auto points = proc.resolvePointSet(pointHandle.value());
        REQUIRE(points.isBound());
        const int point = points->add(-.5f, -2.5f, 3.5f);
        REQUIRE(points->trySetPointId(point, 1001).ok());
        points->setStringAttribute(point, "asset", owner->sourceAsset().format());
        points->setStringAttribute(point, "asset_build_id", owner->sourceBuildId().format());
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.rendererCount("prefab-facilities") == 1u);
        const auto           revision = sink.batchRevision("prefab-facilities");
        ProcgenInstanceDelta oversizedDelta;
        oversizedDelta.baseRevision   = revision;
        oversizedDelta.targetRevision = revision + 1;
        oversizedDelta.targetPointOrder.resize(65537, 1001);
        auto oversized = sink.applyDelta("prefab-facilities", oversizedDelta);
        REQUIRE(!oversized.ok());
        REQUIRE(oversized.error() != nullptr);
        REQUIRE(oversized.error()->code() == DiagnosticCode::PreconditionViolation);
        REQUIRE(sink.batchRevision("prefab-facilities") == revision);
        procgen::RuntimeGeneration savedRuntime(404);
        REQUIRE(savedRuntime.addLevel(10.f, 6.f, 1.5f) == 0);
        savedRuntime.updateSource(-5.f, 5.f, 1.f, 0.f);
        std::unique_ptr<procgen::ProcgenCellRequest> generated(savedRuntime.nextGenerate());
        REQUIRE(generated != nullptr);
        REQUIRE(savedRuntime.completeGeneration(generated.get(), points.operator->()));
        const auto persistedCell =
            savedRuntime.serializeCell(generated->getLevel(), generated->getX(), generated->getZ());
        REQUIRE(!persistedCell.empty());
        auto redMask = [&] {
            std::vector<bool> mask;
            for (int y = 0; y < settings.height; y += 2)
                for (int x = 0; x < settings.width; x += 2) {
                    const auto color = gfx->getPixel(x, y);
                    mask.push_back(color.r > color.g + .15f && color.r > color.b + .15f);
                }
            return mask;
        };
        render();
        const auto visible = redMask();
        REQUIRE(std::count(visible.begin(), visible.end(), true) > 25);
        pixels.clear();
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x) {
                const auto color = gfx->getPixel(x, y);
                for (float channel : {color.r, color.g, color.b, color.a})
                    pixels.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
            }
        auto batchPng = asset::detail::encodeSourcePng(settings.width, settings.height, pixels, 4 * 1024 * 1024);
        REQUIRE(batchPng.ok());
        const auto batchPath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" /
                               ("static-prefab-batch-" + gfx->getBackendName() + ".png");
        std::ofstream batchOutput(batchPath, std::ios::binary);
        batchOutput.write(reinterpret_cast<const char*>(batchPng.value().data()),
                          std::streamsize(batchPng.value().size()));
        REQUIRE(batchOutput.good());
        auto staleBuild = owner->sourceBuildId().format();
        staleBuild[0]   = staleBuild[0] == '0' ? '1' : '0';
        points->setStringAttribute(point, "asset_build_id", staleBuild);
        REQUIRE(!proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.batchRevision("prefab-facilities") == revision);
        render();
        REQUIRE(redMask() == visible);
        ProcgenInstanceDesc moved;
        moved.id              = "pcg-id-1001";
        moved.sourcePointId   = 1001;
        moved.x               = 10.f;
        moved.y               = -2.5f;
        moved.z               = 3.5f;
        moved.asset           = owner->sourceAsset().format();
        moved.assetBinding    = ProcgenInstanceAssetBinding{owner->sourceAsset(), owner->sourceBuildId()};
        auto missing          = moved;
        missing.id            = "missing-prefab";
        missing.sourcePointId = 1002;
        auto missingAsset     = AssetRef::parse("asset://018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
        REQUIRE(missingAsset.ok());
        missing.asset               = missingAsset.value().format();
        missing.assetBinding->asset = missingAsset.value();
        REQUIRE(
            !sink.replaceBatches({{"prefab-facilities", revision + 1, {moved}}, {"missing-facilities", 1, {missing}}})
                 .ok());
        REQUIRE(!sceneHostSystem->findHost("__pcg/missing-facilities").ok());
        REQUIRE(sink.batchRevision("prefab-facilities") == revision);
        REQUIRE(sink.pendingRetirements() == 0u);
        render();
        REQUIRE(redMask() == visible);
        REQUIRE(!sink.replaceBatchesCoordinated({{"prefab-facilities", revision + 1, {moved}}},
                                                [] {
                                                    return Result<void>::failure(Diagnostic::error(
                                                        DiagnosticCode::Failed, "injected prepared owner failure"));
                                                })
                     .ok());
        REQUIRE(sink.batchRevision("prefab-facilities") == revision);
        REQUIRE(sink.pendingRetirements() == 0u);
        render();
        REQUIRE(redMask() == visible);
        ProcgenInstanceDelta delta;
        delta.baseRevision     = revision;
        delta.targetRevision   = revision + 1;
        delta.updated          = {moved};
        delta.targetPointOrder = {1001};
        REQUIRE(sink.applyDelta("prefab-facilities", delta).ok());
        REQUIRE(sink.pendingRetirements() == 1u);
        REQUIRE(sink.collectRetired().ok());
        REQUIRE(!owner->release().ok());
        render();
        const auto hidden = redMask();
        REQUIRE(std::count(hidden.begin(), hidden.end(), true) == 0);
        procgen::RuntimeGeneration restoredRuntime(404);
        REQUIRE(restoredRuntime.addLevel(10.f, 6.f, 1.5f) == 0);
        REQUIRE(restoredRuntime.deserializeCell(persistedCell));
        std::unique_ptr<procgen::PointSet> restoredPoints(
            restoredRuntime.getCellOutput(generated->getLevel(), generated->getX(), generated->getZ()));
        REQUIRE(restoredPoints != nullptr);
        *points.operator->() = *restoredPoints;
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.collectRetired().ok());
        render();
        REQUIRE(redMask() == visible);
        auto                              reimportRequest = trianglePrefab();
        const std::array<std::uint8_t, 4> greenPixel{0, 255, 0, 255};
        auto                              greenImage = asset::detail::encodeSourcePng(1, 1, greenPixel, 4096);
        REQUIRE(greenImage.ok());
        reimportRequest.files["Assets/pixel.png"] = std::move(greenImage).takeValue();
        auto&             prefabBytes             = reimportRequest.files.at("Assets/tile.prefab");
        std::string       changedPrefab(prefabBytes.begin(), prefabBytes.end());
        const std::string oldScale      = "m_LocalScale: {x: 1, y: 1, z: 1}";
        const auto        scalePosition = changedPrefab.find(oldScale);
        REQUIRE(scalePosition != std::string::npos);
        changedPrefab.replace(scalePosition, oldScale.size(), "m_LocalScale: {x: 2, y: 1, z: 1}");
        prefabBytes.assign(changedPrefab.begin(), changedPrefab.end());
        auto reimported = prepareUnityProjectImport(reimportRequest);
        REQUIRE(reimported.ok());
        auto replacementEva = asset::buildEvaArchive(reimported.value().manifest, reimported.value().entries);
        REQUIRE(replacementEva.ok());
        auto replacementArchive = asset::parseEvaArchive(replacementEva.value());
        REQUIRE(replacementArchive.ok());
        auto replacementCook = asset::cookEvaToEvpack(replacementArchive.value(), profile);
        REQUIRE(replacementCook.ok());
        auto replacementPack = asset::parseEvpack(replacementCook.value().bytes);
        REQUIRE(replacementPack.ok());
        asset::EvpackResourceReader replacementReader(
            std::make_shared<const asset::Evpack>(std::move(replacementPack).takeValue()));
        REQUIRE(reimported.value().manifest.entrypoints.at("Assets/tile.prefab") == owner->sourceAsset());
        auto replacementLoad = asset_graphics::EvpackStaticPrefab::load(replacementReader, meshes, images,
                                                                        owner->sourceAsset(), capabilities);
        REQUIRE(replacementLoad.ok());
        auto replacementOwner =
            std::shared_ptr<asset_graphics::EvpackStaticPrefab>(std::move(replacementLoad).takeValue());
        REQUIRE(replacementOwner->sourceBuildId() != owner->sourceBuildId());
        asset_graphics::PrefabBoundsCatalog versionBounds;
        REQUIRE(versionBounds.add(*owner).ok());
        REQUIRE(versionBounds.add(*replacementOwner).ok());
        auto oldBounds = versionBounds.localBounds(owner->sourceAsset(), owner->sourceBuildId());
        auto newBounds = versionBounds.localBounds(owner->sourceAsset(), replacementOwner->sourceBuildId());
        REQUIRE(oldBounds.ok());
        REQUIRE(newBounds.ok());
        REQUIRE(newBounds.value()[0] < oldBounds.value()[0]);
        REQUIRE(sink.admit(replacementOwner).ok());
        REQUIRE(!sink.admit(replacementOwner).ok());
        REQUIRE(!replacementOwner->release().ok());
        render();
        REQUIRE(redMask() == visible);
        points->setStringAttribute(point, "asset_build_id", replacementOwner->sourceBuildId().format());
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.collectRetired().ok());
        REQUIRE(!owner->release().ok());
        render();
        int greenPixels = 0;
        pixels.clear();
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x) {
                const auto color = gfx->getPixel(x, y);
                if (color.g > color.r + .15f && color.g > color.b + .15f) ++greenPixels;
                for (float channel : {color.r, color.g, color.b, color.a})
                    pixels.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
            }
        REQUIRE(greenPixels > 100);
        const auto reimportMask = redMask();
        REQUIRE(std::count(reimportMask.begin(), reimportMask.end(), true) == 0);
        auto reimportPng = asset::detail::encodeSourcePng(settings.width, settings.height, pixels, 4 * 1024 * 1024);
        REQUIRE(reimportPng.ok());
        const auto reimportPath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" /
                                  ("static-prefab-reimport-" + gfx->getBackendName() + ".png");
        std::ofstream reimportOutput(reimportPath, std::ios::binary);
        reimportOutput.write(reinterpret_cast<const char*>(reimportPng.value().data()),
                             std::streamsize(reimportPng.value().size()));
        REQUIRE(reimportOutput.good());
        *points.operator->() = *restoredPoints;
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.collectRetired().ok());
        render();
        REQUIRE(redMask() == visible);
        points->setPosition(point, .5f, -.5f, .5f);
        points->setStringAttribute(point, "asset_root_pose", "placement");
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.collectRetired().ok());
        render();
        REQUIRE(redMask() == visible);
        procgen::RuntimeGeneration placedRuntime(404);
        REQUIRE(placedRuntime.addLevel(10.f, 6.f, 1.5f) == 0);
        placedRuntime.updateSource(-5.f, 5.f, 1.f, 0.f);
        std::unique_ptr<procgen::ProcgenCellRequest> placedRequest(placedRuntime.nextGenerate());
        REQUIRE(placedRequest != nullptr);
        REQUIRE(placedRuntime.completeGeneration(placedRequest.get(), points.operator->()));
        const auto placedCell =
            placedRuntime.serializeCell(placedRequest->getLevel(), placedRequest->getX(), placedRequest->getZ());
        REQUIRE(!placedCell.empty());
        REQUIRE(proc.removeInstances("prefab-facilities").ok());
        REQUIRE(sink.collectRetired().ok());
        render();
        const auto removedPlacedMask = redMask();
        REQUIRE(std::count(removedPlacedMask.begin(), removedPlacedMask.end(), true) == 0);
        procgen::RuntimeGeneration placedRestore(404);
        REQUIRE(placedRestore.addLevel(10.f, 6.f, 1.5f) == 0);
        REQUIRE(placedRestore.deserializeCell(placedCell));
        std::unique_ptr<procgen::PointSet> placedPoints(
            placedRestore.getCellOutput(placedRequest->getLevel(), placedRequest->getX(), placedRequest->getZ()));
        REQUIRE(placedPoints != nullptr);
        REQUIRE(placedPoints->getStringAttribute(point, "asset_root_pose", "") == "placement");
        REQUIRE(placedPoints->getStringAttribute(point, "asset_build_id", "") == owner->sourceBuildId().format());
        REQUIRE(placedPoints->points()[point].id == 1001u);
        *points.operator->() = *placedPoints;
        REQUIRE(proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.collectRetired().ok());
        render();
        REQUIRE(redMask() == visible);
        pixels.clear();
        for (int y = 0; y < settings.height; ++y)
            for (int x = 0; x < settings.width; ++x) {
                const auto color = gfx->getPixel(x, y);
                for (float channel : {color.r, color.g, color.b, color.a})
                    pixels.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
            }
        auto placedPng = asset::detail::encodeSourcePng(settings.width, settings.height, pixels, 4 * 1024 * 1024);
        REQUIRE(placedPng.ok());
        const auto placedPath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" /
                                ("static-prefab-placement-restore-" + gfx->getBackendName() + ".png");
        std::ofstream placedOutput(placedPath, std::ios::binary);
        placedOutput.write(reinterpret_cast<const char*>(placedPng.value().data()),
                           std::streamsize(placedPng.value().size()));
        REQUIRE(placedOutput.good());
        const auto placedRevision = sink.batchRevision("prefab-facilities");
        points->setStringAttribute(point, "asset_root_pose", "unknown");
        REQUIRE(!proc.publishInstances("prefab-facilities", pointHandle.value(), "asset", "").ok());
        REQUIRE(sink.batchRevision("prefab-facilities") == placedRevision);
        render();
        REQUIRE(redMask() == visible);
        REQUIRE(proc.removeInstances("prefab-facilities").ok());
        REQUIRE(sink.rendererCount("prefab-facilities") == 0u);
        REQUIRE(sink.pendingRetirements() == 1u);
        REQUIRE(sink.collectRetired().ok());
        REQUIRE(!owner->release().ok());
        REQUIRE(!replacementOwner->release().ok());
        REQUIRE(sink.release().ok());
        REQUIRE(replacementOwner->release().ok());
        REQUIRE(versionBounds.localBounds(owner->sourceAsset(), replacementOwner->sourceBuildId()).ok());
        REQUIRE(proc.releasePointSet(pointHandle.value()).ok());
    }
    REQUIRE(owner->release().ok());
    testWindow->close();
}

TEST_CASE("asset.import.unityFbxHashesNodeNameAndProducesCanonicalMesh") {
    auto             request = triangleFbx();
    UnitySourceAsset source{
        "Assets/tile.fbx", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Model, "ModelImporter", {}};
    auto prepared = prepareUnityFbx(request, source);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(1));
    REQUIRE_EQ(prepared.value().sourceMappings.front().sourceObject, std::string("-813904291765591093"));
    const auto meshEntry = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                                        [](const auto& entry) { return entry.path.ends_with("mesh.bin"); });
    REQUIRE(meshEntry != prepared.value().entries.end());
    auto decoded = asset::decodeCanonicalMesh(meshEntry->bytes);
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().texcoords.size(), std::size_t(2));
    REQUIRE_EQ(decoded.value().texcoords.at(1), std::vector<float>({0.25f, 0.5f, 0.75f, 0.5f, 0.25f, 0.f}));
    REQUIRE_EQ(decoded.value().attributes.at("TANGENT").components, std::uint32_t(4));
    REQUIRE_EQ(decoded.value().attributes.at("TANGENT").values,
               std::vector<float>({1, 0, 0, -1, 1, 0, 0, -1, 1, 0, 0, -1}));
    REQUIRE_EQ(decoded.value().attributes.at("COLOR_0").components, std::uint32_t(4));
    REQUIRE_EQ(decoded.value().attributes.at("COLOR_0").values,
               std::vector<float>({1, 0, 0, 1, 0, 1, 0, 0.5f, 0, 0, 1, 0.25f}));
    auto repeated = prepareUnityFbx(request, source);
    REQUIRE(repeated.ok());
    REQUIRE(prepared.value().sourceMappings.front().asset == repeated.value().sourceMappings.front().asset);
}

TEST_CASE("asset.import.unityFbxCharacterProducesSkinAndAnimation") {
    const auto    path = eve_test_path::pathBesideTestDir(__FILE__, "assets/mixamo/Idle.fbx");
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"), "fbx.character", "1.0.0", {}};
    request.files["Assets/Idle.fbx"] = {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const std::string meta =
        "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nModelImporter:\n  meshes:\n"
        "    globalScale: 100\n    fileIdsGeneration: 2\n    useFileScale: 1\n"
        "    swapUVChannels: 0\n  tangentSpace:\n    normalImportMode: 0\n";
    request.files["Assets/Idle.fbx.meta"] = {meta.begin(), meta.end()};
    UnitySourceAsset source{
        "Assets/Idle.fbx", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Model, "ModelImporter", {}};
    auto prepared = prepareUnityFbx(request, source);
    if (!prepared) throw std::runtime_error(prepared.status().diagnostics().front().message());
    REQUIRE(prepared.ok());
    const auto hasType = [&](const char* type) {
        return std::any_of(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                           [&](const auto& asset) { return asset.type == type; });
    };
    CHECK(hasType("eve.mesh"));
    CHECK(hasType("eve.skeleton"));
    CHECK(hasType("eve.skin"));
    CHECK(hasType("eve.animation-clip"));
    auto encoded = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = asset::parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    asset::AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, asset::CookPublication::LocalInspection, 16};
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile);
    REQUIRE(cooked.ok());
}

TEST_CASE("asset.import.unityFbxRejectsCorruptionAndRecoversReferencedLegacyIdentity") {
    auto             request = triangleFbx();
    UnitySourceAsset source{
        "Assets/tile.fbx", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Model, "ModelImporter", {}};
    request.files.at(source.path) = {1, 2, 3};
    auto corrupt                  = prepareUnityFbx(request, source);
    REQUIRE(!corrupt.ok());
    request           = triangleFbx();
    auto&       bytes = request.files.at(source.path + ".meta");
    std::string meta(bytes.begin(), bytes.end());
    meta.replace(meta.find("fileIdsGeneration: 2"), 20, "fileIdsGeneration: 1");
    bytes.assign(meta.begin(), meta.end());
    const std::string prefab =
        "--- !u!33 &3300000\nMeshFilter:\n  m_Mesh: {fileID: 1243444849563829797, guid: "
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}\n";
    request.files["Assets/legacy.prefab"] = {prefab.begin(), prefab.end()};
    auto legacy                           = prepareUnityFbx(request, source);
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value().sourceMappings.front().sourceObject, std::string("1243444849563829797"));

    request.files.erase("Assets/legacy.prefab");
    auto indexed = prepareUnityFbx(request, source);
    REQUIRE(indexed.ok());
    REQUIRE_EQ(indexed.value().sourceMappings.front().sourceObject, std::string("4300000"));
}
}  // namespace test_asset_import_unity_fbx


// Contract section: asset import unity native
#include "PrivateTestScope.h"

// asset_import_unity_native.cpp
namespace test_asset_import_unity_native {
using namespace eve;
using namespace eve::asset_import;
namespace {
void put(UnityProjectImportRequest& r, const std::string& path, const std::string& text) {
    r.files[path] = {text.begin(), text.end()};
}
UnityProjectImportRequest fixture() {
    UnityProjectImportRequest r;
    r.package = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
                 "native.test",
                 "1.0.0",
                 {{"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
    std::string data;
    const char* digits = "0123456789abcdef";
    const float vertices[]{1, 2, 3, 0, 1, 0, 0.25f, 0.75f, 4, 5, 6, 0, 1, 0, 0.5f, 0.5f, 7, 8, 9, 0, 1, 0, 1, 0};
    for (float f : vertices) {
        auto v = std::bit_cast<std::uint32_t>(f);
        for (unsigned i = 0; i < 4; ++i) {
            auto b = (v >> (8 * i)) & 255u;
            data += digits[b >> 4];
            data += digits[b & 15];
        }
    }
    std::string mesh =
        "%YAML 1.1\n--- !u!43 &4300000\nMesh:\n  serializedVersion: 9\n  m_MeshCompression: 0\n  m_BindPose: []\n  "
        "m_SubMeshes:\n";
    mesh += "  - serializedVersion: 2\n    firstByte: 0\n    indexCount: 3\n    topology: 0\n    baseVertex: 0\n";
    mesh += "  - serializedVersion: 2\n    firstByte: 6\n    indexCount: 0\n    topology: 0\n    baseVertex: 0\n";
    mesh += "  - serializedVersion: 2\n    firstByte: 6\n    indexCount: 3\n    topology: 0\n    baseVertex: 0\n";
    mesh +=
        "  m_IndexFormat: 0\n  m_IndexBuffer: 000001000200020001000000\n  m_VertexData:\n    m_VertexCount: 3\n    "
        "m_Channels:\n";
    for (int i = 0; i < 14; ++i)
        mesh += "    - stream: 0\n      offset: " +
                std::to_string(i == 1   ? 12
                               : i == 4 ? 24
                                        : 0) +
                "\n      format: 0\n      dimension: " +
                std::to_string(i == 0 || i == 1 ? 3
                               : i == 4         ? 2
                                                : 0) +
                "\n";
    mesh += "    m_DataSize: 96\n    _typelessdata: " + data + "\n  m_StreamData:\n    size: 0\n";
    put(r, "Assets/mesh.asset", mesh);
    put(r, "Assets/mesh.asset.meta", "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    put(r, "Assets/material.mat",
        "--- !u!21 &2100000\nMaterial:\n  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n  "
        "m_Floats:\n    - _Mode: 2\n    - _Metallic: 0\n    - _Glossiness: 0.5\n  m_Colors:\n    - _Color: {r: 1, g: "
        "0.5, b: 0.25, a: 0.3}\n");
    put(r, "Assets/material.mat.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
    std::string prefab =
        "--- !u!1 &1\nGameObject:\n  m_Name: Child\n  m_IsActive: 1\n--- !u!4 &4\nTransform:\n  m_GameObject: {fileID: "
        "1}\n  m_Father: {fileID: 0}\n  m_LocalPosition: {x: 0, y: 0, z: 0}\n  m_LocalRotation: {x: 0, y: 0, z: 0, w: "
        "1}\n  m_LocalScale: {x: 1, y: 1, z: 1}\n--- !u!33 &33\nMeshFilter:\n  m_GameObject: {fileID: 1}\n  m_Mesh: "
        "{fileID: 4300000, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}\n--- !u!23 &23\nMeshRenderer:\n  "
        "m_GameObject: {fileID: 1}\n  m_Enabled: 1\n  m_CastShadows: 0\n  m_ReceiveShadows: 0\n  m_Materials:\n";
    for (int i = 0; i < 3; ++i)
        prefab +=
            i == 1 ? "  - {fileID: 0}\n" : "  - {fileID: 2100000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 2}\n";
    put(r, "Assets/child.prefab", prefab);
    put(r, "Assets/child.prefab.meta", "guid: cccccccccccccccccccccccccccccccc\n");
    return r;
}
std::string instance(int id, int alias, const std::string& guid, float x) {
    return "--- !u!1001 &" + std::to_string(id) +
           "\nPrefabInstance:\n  m_Modification:\n    m_TransformParent: {fileID: 0}\n    m_Modifications:\n    - "
           "target: {fileID: 4, guid: " +
           guid + ", type: 3}\n      propertyPath: m_LocalPosition.x\n      value: " + std::to_string(x) +
           "\n      objectReference: {fileID: 0}\n    m_RemovedComponents: []\n  m_SourcePrefab: {fileID: 100100000, "
           "guid: " +
           guid + ", type: 3}\n--- !u!4 &" + std::to_string(alias) +
           " stripped\nTransform:\n  m_CorrespondingSourceObject: {fileID: 4, guid: " + guid +
           ", type: 3}\n  m_PrefabInstance: {fileID: " + std::to_string(id) + "}\n";
}
}  // namespace
TEST_CASE("asset.import.unityNativeMeshPreservesEmptySlotsAndCanonicalCoordinates") {
    auto r        = fixture();
    auto prepared = prepareUnityProjectImport(r);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(4));
    const auto materialRef = prepared.value().manifest.entrypoints.at("Assets/material.mat");
    const auto material = std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                                       [&](const auto& asset) { return asset.asset == materialRef; });
    REQUIRE(material != prepared.value().manifest.assets.end());
    REQUIRE_EQ(material->schemaVersion, SchemaVersion(15));
    std::size_t materialLinks = 0;
    for (const auto& dependency : prepared.value().manifest.dependencies) {
        if (dependency.to != materialRef) continue;
        REQUIRE_EQ(dependency.expectedType, std::string("eve.material/15"));
        ++materialLinks;
    }
    REQUIRE_EQ(materialLinks, std::size_t(2));
    auto eva = asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
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
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                        scene = asset_scene::EvpackSceneTemplateLoader(reader).load(
        prepared.value().manifest.entrypoints.at("Assets/child.prefab"), caps);
    REQUIRE(scene.ok());
    REQUIRE_EQ(scene.value().renderers.size(), std::size_t(2));
    REQUIRE(!scene.value().renderers[0].castShadows);
    REQUIRE(!scene.value().renderers[0].receiveShadows);
    REQUIRE(!scene.value().renderers[1].castShadows);
    REQUIRE(!scene.value().renderers[1].receiveShadows);
    class Factory final : public graphics::IMeshResourceFactory {
    public:
        int                        token = 0;
        std::vector<float>         positions, uv;
        std::vector<std::uint32_t> indices;
        Result<graphics::Mesh*> uploadMesh(const float* p, const float*, const float* t, int n, const std::uint32_t* i,
                                           int count) override {
            positions.assign(p, p + n * 3);
            uv.assign(t, t + n * 2);
            indices.assign(i, i + count);
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override { return Result<void>::success(); }
    } factory;
    auto mesh = asset_graphics::EvpackGraphicsLoader(reader, factory).loadMesh(scene.value().renderers[0].mesh, caps);
    REQUIRE(mesh.ok());
    REQUIRE_EQ(factory.positions[0], 1.f);
    REQUIRE_EQ(factory.positions[2], -3.f);
    REQUIRE_EQ(factory.uv[1], 0.25f);
    REQUIRE_EQ(factory.indices[1], std::uint32_t(2));
    REQUIRE_EQ(factory.indices[2], std::uint32_t(1));
    class Images final : public graphics::IImageResourceFactory {
    public:
        Result<graphics::Texture*> uploadRgba8(std::uint32_t, std::uint32_t, const std::uint8_t*, bool) override {
            return Result<graphics::Texture*>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "unexpected texture upload"));
        }
        Result<void> releaseImage(graphics::Texture*) override { return Result<void>::success(); }
    } images;
    auto drawable = asset_graphics::EvpackStaticPrefab::load(
        reader, factory, images, prepared.value().manifest.entrypoints.at("Assets/child.prefab"), caps);
    REQUIRE(drawable.ok());
    REQUIRE_EQ(drawable.value()->drawCount(), std::size_t(2));
    auto released = factory.releaseMesh(mesh.value().mesh);
    REQUIRE(released.ok());
}
TEST_CASE("asset.import.unityVegetationPrefabDependenciesUseCurrentMaterialSchema") {
    auto request = fixture();
    put(request, "Assets/material.mat",
        "--- !u!21 &2100000\nMaterial:\n  m_Shader: {fileID: 4800000, guid: 7befaa6f41d00a6478d5f4af21d66518, type: "
        "3}\n");
    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto  material = imported.value().manifest.entrypoints.at("Assets/material.mat");
    std::size_t count    = 0;
    for (const auto& dependency : imported.value().manifest.dependencies) {
        if (dependency.to != material) continue;
        REQUIRE_EQ(dependency.expectedType, std::string("eve.material/15"));
        ++count;
    }
    REQUIRE_EQ(count, std::size_t(2));
    auto bytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
}
TEST_CASE("asset.import.unityNativeMeshRejectsTruncatedAndOutOfRangeBuffers") {
    auto        r     = fixture();
    auto&       bytes = r.files.at("Assets/mesh.asset");
    std::string text(bytes.begin(), bytes.end());
    const auto  at = text.find("000001000200");
    REQUIRE(at != std::string::npos);
    text.replace(at, 4, "ffff");
    put(r, "Assets/mesh.asset", text);
    auto invalid = prepareUnityProjectImport(r);
    REQUIRE(!invalid.ok());
    r = fixture();
    text.assign(r.files.at("Assets/mesh.asset").begin(), r.files.at("Assets/mesh.asset").end());
    const auto data = text.find("_typelessdata: ");
    text.erase(data + 14, 2);
    put(r, "Assets/mesh.asset", text);
    auto truncated = prepareUnityProjectImport(r);
    REQUIRE(!truncated.ok());
}
TEST_CASE("asset.import.unityNestedPrefabInstancesHaveDistinctIdentityAndOverrides") {
    auto r = fixture();
    put(r, "Assets/parent.prefab",
        instance(100, 104, "cccccccccccccccccccccccccccccccc", 2) +
            instance(200, 204, "cccccccccccccccccccccccccccccccc", 7));
    put(r, "Assets/parent.prefab.meta", "guid: dddddddddddddddddddddddddddddddd\n");
    auto prepared = prepareUnityProjectImport(r);
    REQUIRE(prepared.ok());
    const auto ref   = prepared.value().manifest.entrypoints.at("Assets/parent.prefab");
    const auto asset = std::find_if(prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
                                    [&](const auto& a) { return a.asset == ref; });
    REQUIRE(asset != prepared.value().manifest.assets.end());
    const auto entry = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                                    [&](const auto& e) { return e.path == asset->definition; });
    REQUIRE(entry != prepared.value().entries.end());
    auto value = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
    REQUIRE(value.ok());
    const auto& object = *value.value().getIf<Value::Object>();
    REQUIRE_EQ(object.at("renderers").getIf<Value::Array>()->size(), std::size_t(4));
    std::vector<double> positions;
    for (const auto& n : *object.at("nodes").getIf<Value::Array>()) {
        const auto& node = *n.getIf<Value::Object>();
        if (node.at("parentSourceFileId").asInt() == 0)
            positions.push_back(node.at("position").getIf<Value::Array>()->at(0).asDouble());
    }
    std::sort(positions.begin(), positions.end());
    REQUIRE_EQ(positions.size(), std::size_t(2));
    REQUIRE_EQ(positions[0], 2.0);
    REQUIRE_EQ(positions[1], 7.0);
    auto repeated = prepareUnityProjectImport(r);
    REQUIRE(repeated.ok());
    REQUIRE_EQ(prepared.value().entries.size(), repeated.value().entries.size());
    for (std::size_t i = 0; i < prepared.value().entries.size(); ++i)
        REQUIRE_EQ(prepared.value().entries[i].bytes, repeated.value().entries[i].bytes);
    put(r, "Assets/child.prefab", instance(300, 304, "dddddddddddddddddddddddddddddddd", 0));
    auto cycle = prepareUnityProjectImport(r);
    REQUIRE(!cycle.ok());
}

TEST_CASE("asset.import.unityNestedPrefabRemovalAndBudgetAreValidated") {
    auto       r    = fixture();
    auto       text = instance(100, 104, "cccccccccccccccccccccccccccccccc", 2);
    const auto at   = text.find("m_RemovedComponents: []");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string("m_RemovedComponents: []").size(),
                 "m_RemovedComponents:\n    - {fileID: 23, guid: cccccccccccccccccccccccccccccccc, type: 3}");
    put(r, "Assets/parent.prefab", text);
    put(r, "Assets/parent.prefab.meta", "guid: dddddddddddddddddddddddddddddddd\n");
    auto index = indexUnitySources(r.files);
    REQUIRE(index.ok());
    const auto source = std::find_if(index.value().assets.begin(), index.value().assets.end(),
                                     [](const auto& a) { return a.path == "Assets/parent.prefab"; });
    REQUIRE(source != index.value().assets.end());
    auto expanded = expandUnityPrefab(r, index.value(), *source);
    REQUIRE(expanded.ok());
    const std::string flattened(expanded.value().bytes.begin(), expanded.value().bytes.end());
    REQUIRE(flattened.find("MeshRenderer:") == std::string::npos);
    REQUIRE(flattened.find("m_LocalPosition: {x: 2.000000,") != std::string::npos);
    r.limits.maximumAssets = 2;
    auto limited           = expandUnityPrefab(r, index.value(), *source);
    REQUIRE(!limited.ok());
}

TEST_CASE("asset.import.unityNativeSubmeshIdentitySurvivesEmptySlotBecomingPopulated") {
    auto r      = fixture();
    auto before = prepareUnityProjectImport(r);
    REQUIRE(before.ok());
    std::string text(r.files.at("Assets/mesh.asset").begin(), r.files.at("Assets/mesh.asset").end());
    auto        at = text.find("indexCount: 0");
    REQUIRE(at != std::string::npos);
    text.replace(at, 13, "indexCount: 3");
    at = text.rfind("firstByte: 6");
    REQUIRE(at != std::string::npos);
    text.replace(at, 12, "firstByte: 12");
    at = text.find("000001000200020001000000");
    REQUIRE(at != std::string::npos);
    text.insert(at + 24, "000001000200");
    put(r, "Assets/mesh.asset", text);
    auto after = prepareUnityProjectImport(r);
    REQUIRE(after.ok());
    const auto key    = "unity:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/4300000/submesh/2";
    auto       lookup = [&](const auto& candidate) {
        return std::find_if(candidate.sourceMappings.begin(), candidate.sourceMappings.end(),
                                  [&](const auto& m) { return m.sourceObject == key; });
    };
    const auto old = lookup(before.value()), updated = lookup(after.value());
    REQUIRE(old != before.value().sourceMappings.end());
    REQUIRE(updated != after.value().sourceMappings.end());
    REQUIRE(old->asset == updated->asset);
}

TEST_CASE("asset.import.unityNativeMeshManySlotsKeepDefinitionsAndMappingsAligned") {
    auto        r = fixture();
    std::string mesh(r.files.at("Assets/mesh.asset").begin(), r.files.at("Assets/mesh.asset").end());
    const auto  begin = mesh.find("  m_SubMeshes:");
    const auto  end   = mesh.find("  m_VertexData:");
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    std::string slots = "  m_SubMeshes:\n", indices;
    for (int i = 0; i < 12; ++i) {
        slots += "  - serializedVersion: 2\n    firstByte: " + std::to_string(i * 6) +
                 "\n    indexCount: 3\n    topology: 0\n    baseVertex: 0\n";
        indices += "000001000200";
    }
    slots += "  m_IndexFormat: 0\n  m_IndexBuffer: " + indices + "\n";
    mesh.replace(begin, end - begin, slots);
    put(r, "Assets/mesh.asset", mesh);
    auto index = indexUnitySources(r.files);
    REQUIRE(index.ok());
    const auto source = std::find_if(index.value().assets.begin(), index.value().assets.end(),
                                     [](const auto& a) { return a.path == "Assets/mesh.asset"; });
    REQUIRE(source != index.value().assets.end());
    auto prepared = prepareUnityNativeMesh(r, *source);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(12));
    auto archive = eve::asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archive.ok());
    for (int i = 0; i < 12; ++i) {
        const auto key = "4300000/submesh/" + std::to_string(i);
        const auto expected =
            r.package.packageId.child("unity:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:4300000:submesh:" + std::to_string(i));
        const auto mapping =
            std::find_if(prepared.value().sourceMappings.begin(), prepared.value().sourceMappings.end(),
                         [&](const auto& m) { return m.sourceObject == key; });
        REQUIRE(mapping != prepared.value().sourceMappings.end());
        REQUIRE(mapping->asset.id() == expected);
    }
}
}  // namespace test_asset_import_unity_native


// Contract section: asset import unity package
#include "PrivateTestScope.h"

#include <zlib.h>
#include <cstdio>
// asset_import_unity_package.cpp
namespace test_asset_import_unity_package {
using namespace eve;
using namespace eve::asset_import;

using namespace unity_test;

TEST_CASE("asset.import.unityPackageAcceptsLegacyPathTrailerAndDependencyManifest") {
    std::vector<std::uint8_t> tar;
    appendAsset(tar, guidA, "Assets/model.fbx\n00");
    appendTar(tar, ".icon.png", bytes("thumbnail"));
    appendTar(tar, "packagemanagermanifest", {}, '5');
    appendTar(tar, "packagemanagermanifest/pathname", bytes("Packages/manifest.json\n00"));
    appendTar(tar, "packagemanagermanifest/asset", bytes("{\"dependencies\":{}}"));
    tar.resize(tar.size() + 1024);
    auto result = readUnityPackage(gzip(tar));
    REQUIRE(result.ok());
    REQUIRE(result.value().contains("Assets/model.fbx"));
    REQUIRE(result.value().at("Packages/manifest.json") == bytes("{\"dependencies\":{}}"));
    for (const auto path :
         {"Assets/model.fbx\n01", "Assets/model.fbx\n00\nevil", "Assets/model.fbx\n\n00", "Packages/manifest.json"}) {
        std::vector<std::uint8_t> invalid;
        appendAsset(invalid, guidA, path);
        invalid.resize(invalid.size() + 1024);
        REQUIRE(!readUnityPackage(gzip(invalid)).ok());
    }
    for (const auto path : {"Assets/manifest.json", "Packages/../manifest.json", "Packages/other.json"}) {
        std::vector<std::uint8_t> invalid;
        appendTar(invalid, "packagemanagermanifest/pathname", bytes(path));
        appendTar(invalid, "packagemanagermanifest/asset", bytes("{}"));
        invalid.resize(invalid.size() + 1024);
        REQUIRE(!readUnityPackage(gzip(invalid)).ok());
    }
}


TEST_CASE("asset.import.unityPackageRestoresPathsMetadataAndFolders") {
    std::vector<std::uint8_t> tar;
    appendAsset(tar, guidA, "Assets/Models/模型.fbx\r\n", "binary-model-data");
    appendTar(tar, std::string(guidB) + "/pathname", bytes("Assets/Models"));
    appendTar(tar, std::string(guidB) + "/asset.meta", metadata(guidB, "folderAsset: yes\n"));
    tar.resize(tar.size() + 1024);
    auto unpacked = readUnityPackage(gzip(tar));
    REQUIRE(unpacked.ok());
    REQUIRE_EQ(unpacked.value().size(), std::size_t(3));
    REQUIRE(unpacked.value().contains("Assets/Models/模型.fbx"));
    REQUIRE(unpacked.value().at("Assets/Models/模型.fbx.meta") == metadata(guidA));
    auto index = indexUnitySources(unpacked.value());
    REQUIRE(index.ok());
    REQUIRE_EQ(index.value().assets.size(), std::size_t(2));
    REQUIRE(index.value().assets[0].kind == UnitySourceKind::Folder);
    REQUIRE(index.value().assets[1].kind == UnitySourceKind::Model);
}

TEST_CASE("asset.import.unityPackageRejectsUnsafeDestinationsAndCollisions") {
    for (const auto path : {"../evil", "Assets/../evil", "C:/evil", "Assets\\evil", "/Assets/evil", "Assets//evil",
                            "Assets/evil.", "Assets/evil ", "Library/cache"}) {
        std::vector<std::uint8_t> tar;
        appendAsset(tar, guidA, path);
        tar.resize(tar.size() + 1024);
        auto result = readUnityPackage(gzip(tar));
        REQUIRE(!result.ok());
    }
    std::vector<std::uint8_t> tar;
    appendAsset(tar, guidA, "Assets/Model.fbx");
    appendAsset(tar, guidB, "Assets/model.fbx");
    tar.resize(tar.size() + 1024);
    auto collision = readUnityPackage(gzip(tar));
    REQUIRE(!collision.ok());
    REQUIRE(collision.error()->code() == DiagnosticCode::Conflict);
}

TEST_CASE("asset.import.unityPackageRejectsCorruptionLinksDuplicatesAndMissingPayloads") {
    std::vector<std::uint8_t> validTar;
    appendAsset(validTar, guidA, "Assets/Model.fbx");
    validTar.resize(validTar.size() + 1024);
    auto compressed = gzip(validTar);
    compressed.pop_back();
    auto truncated = readUnityPackage(compressed);
    REQUIRE(!truncated.ok());
    compressed = gzip(validTar);
    compressed[compressed.size() - 8] ^= 1;
    auto crc = readUnityPackage(compressed);
    REQUIRE(!crc.ok());
    compressed = gzip(validTar);
    compressed.push_back(0);
    auto trailing = readUnityPackage(compressed);
    REQUIRE(!trailing.ok());
    auto badTar = validTar;
    badTar[0] ^= 1;
    auto checksum = readUnityPackage(gzip(badTar));
    REQUIRE(!checksum.ok());
    for (char type : {'1', '2', 'x'}) {
        std::vector<std::uint8_t> tar;
        appendTar(tar, std::string(guidA) + "/asset", {}, type);
        tar.resize(tar.size() + 1024);
        auto linked = readUnityPackage(gzip(tar));
        REQUIRE(!linked.ok());
    }
    std::vector<std::uint8_t> duplicate;
    appendAsset(duplicate, guidA, "Assets/model.fbx");
    appendTar(duplicate, std::string(guidA) + "/asset", bytes("duplicate"));
    duplicate.resize(duplicate.size() + 1024);
    auto repeated = readUnityPackage(gzip(duplicate));
    REQUIRE(!repeated.ok());
    std::vector<std::uint8_t> missing;
    appendTar(missing, std::string(guidA) + "/pathname", bytes("Assets/model.fbx"));
    appendTar(missing, std::string(guidA) + "/asset.meta", metadata(guidA));
    missing.resize(missing.size() + 1024);
    auto absent = readUnityPackage(gzip(missing));
    REQUIRE(!absent.ok());
}

TEST_CASE("asset.import.unityPackageHonorsCompressedDecodedAndAssetBudgets") {
    std::vector<std::uint8_t> tar;
    appendAsset(tar, guidA, "Assets/Model.fbx");
    tar.resize(tar.size() + 1024);
    auto              compressed = gzip(tar);
    AssetImportLimits limits;
    limits.maximumSourceBytes = compressed.size() - 1;
    auto source               = readUnityPackage(compressed, limits);
    REQUIRE(!source.ok());
    limits                     = {};
    limits.maximumDecodedBytes = tar.size() - 1;
    auto decoded               = readUnityPackage(compressed, limits);
    REQUIRE(!decoded.ok());
    limits               = {};
    limits.maximumAssets = 0;
    auto count           = readUnityPackage(compressed, limits);
    REQUIRE(!count.ok());
}

TEST_CASE("asset.import.unityIndexCoversAllContentFamiliesAndSignedReferences") {
    UnitySourceFiles                                                  files;
    const std::array<std::pair<std::string_view, UnitySourceKind>, 9> cases{{{"house.prefab", UnitySourceKind::Prefab},
                                                                             {"hero.fbx", UnitySourceKind::Model},
                                                                             {"walk.anim", UnitySourceKind::Animation},
                                                                             {"icons.png", UnitySourceKind::Image},
                                                                             {"click.wav", UnitySourceKind::Audio},
                                                                             {"font.ttf", UnitySourceKind::Font},
                                                                             {"surface.mat", UnitySourceKind::Material},
                                                                             {"world.unity", UnitySourceKind::Scene},
                                                                             {"logic.cs", UnitySourceKind::Script}}};
    for (std::size_t i = 0; i < cases.size(); ++i) {
        auto guid             = std::string(guidA);
        guid.back()           = static_cast<char>('0' + i);
        const auto path       = "Assets/" + std::string(cases[i].first);
        files[path]           = bytes("%YAML 1.1\n");
        files[path + ".meta"] = metadata(guid, "NativeFormatImporter:\n");
    }
    files["Assets/house.prefab"] =
        bytes("%YAML 1.1\n  m_Mesh: {fileID: -9223372036854775808, guid: " + std::string(guidB) +
              ", type: 3}\n  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n");
    auto index = indexUnitySources(files);
    REQUIRE(index.ok());
    REQUIRE_EQ(index.value().assets.size(), cases.size());
    REQUIRE_EQ(index.value().findings.size(), std::size_t(1));
    for (const auto& [name, kind] : cases) {
        auto it = std::find_if(index.value().assets.begin(), index.value().assets.end(),
                               [&](const auto& a) { return a.path == "Assets/" + std::string(name); });
        REQUIRE(it != index.value().assets.end());
        REQUIRE(it->kind == kind);
    }
    auto renamed                       = files;
    renamed["Assets/renamed.fbx"]      = renamed.at("Assets/hero.fbx");
    renamed["Assets/renamed.fbx.meta"] = renamed.at("Assets/hero.fbx.meta");
    renamed.erase("Assets/hero.fbx");
    renamed.erase("Assets/hero.fbx.meta");
    auto reindexed = indexUnitySources(renamed);
    REQUIRE(reindexed.ok());
    auto renamedEntry = std::find_if(reindexed.value().assets.begin(), reindexed.value().assets.end(),
                                     [](const auto& a) { return a.path == "Assets/renamed.fbx"; });
    REQUIRE(renamedEntry != reindexed.value().assets.end());
    REQUIRE_EQ(renamedEntry->guid, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1");
}

TEST_CASE("asset.import.unityIndexRejectsAmbiguousMetadataAndInvalidReferences") {
    UnitySourceFiles files{{"Assets/a.prefab", bytes("%YAML 1.1\n")}, {"Assets/a.prefab.meta", metadata(guidA)}};
    files["Assets/b.prefab"]      = files.at("Assets/a.prefab");
    files["Assets/b.prefab.meta"] = metadata(guidA);
    auto duplicate                = indexUnitySources(files);
    REQUIRE(!duplicate.ok());
    files.erase("Assets/b.prefab");
    files.erase("Assets/b.prefab.meta");
    files["Assets/a.prefab.meta"] = metadata(guidA, "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
    auto repeatedKey              = indexUnitySources(files);
    REQUIRE(!repeatedKey.ok());
    files["Assets/a.prefab.meta"] = metadata(guidA);
    files["Assets/a.prefab"] =
        bytes("%YAML 1.1\nm_Mesh: {fileID: 9223372036854775808, guid: " + std::string(guidB) + "}\n");
    auto overflow = indexUnitySources(files);
    REQUIRE(!overflow.ok());
}

TEST_CASE("asset.import.unityIndexRejectsFileDirectoryConflictsAndInvalidUtf8") {
    UnitySourceFiles files{{"Assets/props", bytes("file")}, {"Assets/props/house.fbx", bytes("model")}};
    auto             conflict = indexUnitySources(files);
    REQUIRE(!conflict.ok());
    files.clear();
    files["Assets/model.fbx"] = bytes("model");
    auto meta                 = metadata(guidA);
    meta.push_back(0xff);
    files["Assets/model.fbx.meta"] = meta;
    auto encoding                  = indexUnitySources(files);
    REQUIRE(!encoding.ok());
}
}  // namespace test_asset_import_unity_package


// Contract section: asset import unity sprite
#include "PrivateTestScope.h"

#include "asset/SpriteAnimation.h"
// asset_import_unity_sprite.cpp
namespace test_asset_import_unity_sprite {
using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;
namespace {
UnityProjectImportRequest spriteRequest() {
    UnityProjectImportRequest r;
    r.package               = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
                               "sprite-test",
                               "1.0.0",
                               {{"provider", "local"}, {"license", Value::Object{{"redistribution", "project-only"}}}}};
    auto put                = [&](const char* path, const std::string& s) { r.files[path] = {s.begin(), s.end()}; };
    r.files["Assets/s.png"] = {137, 80,  78, 71,  13, 10, 26,  10, 0,   0,   0,   13,  73,  72,  68,  82, 0, 0,
                               0,   1,   0,  0,   0,  1,  8,   6,  0,   0,   0,   31,  21,  196, 137, 0,  0, 0,
                               13,  73,  68, 65,  84, 8,  215, 99, 248, 207, 192, 240, 31,  0,   5,   0,  1, 255,
                               114, 156, 82, 103, 0,  0,  0,   0,  73,  69,  78,  68,  174, 66,  96,  130};
    std::string meta =
        "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nTextureImporter:\n  spriteMode: 2\n  spritePixelsToUnits: 100\n    "
        "filterMode: 0\n  spriteSheet:\n    sprites:";
    for (const auto id : {"-7", "-8"})
        meta += "\n    - serializedVersion: 2\n      name: frame" + std::string(id) +
                "\n      rect:\n        x: 0\n        y: 0\n        width: 1\n        height: 1\n      alignment: 0\n  "
                "    pivot: {x: 0, y: 0}\n      border: {x: 0, y: 0, z: 0, w: 0}\n      internalID: " +
                id + "\n";
    put("Assets/s.png.meta", meta);
    put("Assets/a.anim.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nNativeFormatImporter:\n");
    put("Assets/a.anim", R"(--- !u!74 &7400000
AnimationClip:
  m_RotationCurves: []
  m_CompressedRotationCurves: []
  m_EulerCurves: []
  m_PositionCurves: []
  m_ScaleCurves: []
  m_FloatCurves: []
  m_PPtrCurves:
  - curve:
    - time: 0
      value: {fileID: -7, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
    - time: 0.5
      value: {fileID: -8, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
    attribute: m_Sprite
    path:
    classID: 212
    script: {fileID: 0}
  m_SampleRate: 2
  m_AnimationClipSettings:
    m_StartTime: 0
    m_StopTime: 1
    m_LoopTime: 1
  m_Events: []
)");
    return r;
}
}  // namespace
TEST_CASE("asset.import.unitySpriteRoundTripsSignedKeysPivotAndLoopBoundary") {
    auto request  = spriteRequest();
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    auto encoded = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    auto profile = assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(pack).takeValue()));
    EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto clip = SpriteAnimationClip::load(reader, prepared.value().manifest.entrypoints.at("Assets/a.anim"), caps);
    REQUIRE(clip.ok());
    REQUIRE_EQ(clip.value().frameCount(), std::size_t(2));
    auto first = clip.value().sample(0);
    REQUIRE(first.ok());
    REQUIRE_EQ(first.value().pivot[0], .5);
    REQUIRE_EQ(first.value().pivot[1], .5);
    auto second = clip.value().sample(.5);
    REQUIRE(second.ok());
    REQUIRE_EQ(second.value().name, std::string("frame-8"));
    auto loop = clip.value().sample(1);
    REQUIRE(loop.ok());
    REQUIRE_EQ(loop.value().name, first.value().name);
    auto invalid = clip.value().sample(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!invalid.ok());
    auto negative = clip.value().sample(-1);
    REQUIRE(!negative.ok());
}
TEST_CASE("asset.import.unitySpriteRejectsMissingFileIdAndOutOfBoundsRect") {
    auto        request = spriteRequest();
    auto&       b       = request.files.at("Assets/a.anim");
    std::string s(b.begin(), b.end());
    s.replace(s.find("fileID: -8"), 10, "fileID: -9");
    b.assign(s.begin(), s.end());
    auto missing = prepareUnityProjectImport(request);
    REQUIRE(!missing.ok());
    request = spriteRequest();
    auto& m = request.files.at("Assets/s.png.meta");
    s.assign(m.begin(), m.end());
    s.replace(s.find("width: 1"), 8, "width: 2");
    m.assign(s.begin(), s.end());
    auto bounds = prepareUnityProjectImport(request);
    REQUIRE(!bounds.ok());
}

TEST_CASE("asset.import.unitySpriteSchemaRejectsVersionAndDuplicateTimeAndHoldsNonLoopEnd") {
    auto prepared = prepareUnityProjectImport(spriteRequest());
    REQUIRE(prepared.ok());
    const auto& assets = prepared.value().manifest.assets;
    auto        asset =
        std::find_if(assets.begin(), assets.end(), [](const auto& a) { return a.type == "eve.sprite-animation"; });
    REQUIRE(asset != assets.end());
    const auto& entries = prepared.value().entries;
    auto        entry =
        std::find_if(entries.begin(), entries.end(), [&](const auto& e) { return e.path == asset->definition; });
    REQUIRE(entry != entries.end());
    auto value = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
    REQUIRE(value.ok());
    auto& object          = *value.value().getIf<Value::Object>();
    object["loop"]        = false;
    object["futureField"] = "retained";
    auto clip             = SpriteAnimationClip::decode(value.value());
    REQUIRE(clip.ok());
    auto last = clip.value().sample(100);
    REQUIRE(last.ok());
    REQUIRE_EQ(last.value().name, std::string("frame-8"));
    object["schemaVersion"] = 2;
    auto version            = SpriteAnimationClip::decode(value.value());
    REQUIRE(!version.ok());
    object["schemaVersion"]                     = 1;
    auto& frames                                = *object.at("frames").getIf<Value::Array>();
    (*frames[1].getIf<Value::Object>())["time"] = 0;
    auto duplicate                              = SpriteAnimationClip::decode(value.value());
    REQUIRE(!duplicate.ok());
}
}  // namespace test_asset_import_unity_sprite


// Contract section: asset import unity vegetation material
#include "PrivateTestScope.h"

#include "asset/graphics/CookedMaterial.h"
// asset_import_unity_vegetation_material.cpp
namespace test_asset_import_unity_vegetation_material {
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
    auto              request       = fixture();
    auto&             materialBytes = request.files["Assets/leaf.mat"];
    std::string       material(materialBytes.begin(), materialBytes.end());
    const std::string presentGuid = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const std::string absentGuid  = "cccccccccccccccccccccccccccccccc";
    for (std::size_t offset = material.find(presentGuid); offset != std::string::npos;
         offset             = material.find(presentGuid, offset + absentGuid.size()))
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
}  // namespace test_asset_import_unity_vegetation_material


// Contract section: asset procgen terrain detail render
#include "PrivateTestScope.h"

#include <string_view>
#include "asset/EvpackImageDecoder.h"
#include "asset/procgen/TerrainDetailCard.h"
#include "asset/procgen/TerrainVegetationGpu.h"
#include "asset/procgen/TerrainVegetationRuntime.h"
#include "graphics/Light.h"
#include "image/ImageData.h"
// asset_procgen_terrain_detail_render.cpp
namespace test_asset_procgen_terrain_detail_render {
// asset_procgen_terrain_detail_render.cpp
using namespace eve;

namespace {

std::vector<std::uint8_t> terrainDetailBytes(std::string_view text) { return {text.begin(), text.end()}; }

bool saveReadback(const image::ImageData& screenshot, const std::filesystem::path& path) {
    std::vector<std::uint8_t> rgba;
    rgba.reserve(std::size_t(screenshot.getWidth()) * std::size_t(screenshot.getHeight()) * 4);
    for (int y = 0; y < screenshot.getHeight(); ++y)
        for (int x = 0; x < screenshot.getWidth(); ++x) {
            const auto pixel = screenshot.getPixel(x, y);
            for (const float value : {pixel.r, pixel.g, pixel.b, pixel.a})
                rgba.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f)));
        }
    auto png =
        asset::detail::encodeSourcePng(static_cast<std::uint32_t>(screenshot.getWidth()),
                                       static_cast<std::uint32_t>(screenshot.getHeight()), rgba, 4 * 1024 * 1024);
    if (!png) return false;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.value().data()), std::streamsize(png.value().size()));
    return output.good();
}

}  // namespace

TEST_CASE("asset.procgen.terrainDetailSidecarPublishesDecodableRuntimeResource") {
    asset_import::UnityProjectImportRequest request;
    request.package = {
        *PersistentId::parse("6cb7a302-f360-4ae3-b22f-dd722e63e18c"), "terrain.detail.render.fixture", "1.0.0", {}};
    request.terrainDataPath                = "Assets/Terrain.asset";
    request.files[request.terrainDataPath] = terrainDetailBytes(R"yaml(%YAML 1.1
TerrainData:
  m_HeightmapResolution: 2
  m_HeightmapScale: {x: 1, y: 1, z: 1}
  m_Heights: 0000000000000000
  m_DetailPrototypes:
  - prototype: {fileID: 2800000, guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}
)yaml");
    request.files[request.terrainDataPath + ".meta"] =
        terrainDetailBytes("fileFormatVersion: 2\nguid: 11111111111111111111111111111111\n");
    request.files["Assets/grass.png.meta"] = terrainDetailBytes(
        "fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "TextureImporter:\n  sRGBTexture: 1\n  alphaIsTransparency: 1\n");
    const std::array<std::uint8_t, 16> pixels{20, 170, 35, 255, 30, 220, 45, 255, 15, 120, 25, 255, 25, 190, 40, 255};
    auto                               png = asset::detail::encodeSourcePng(2, 2, pixels, 4096);
    REQUIRE(png.ok());
    request.files["Assets/grass.png"]                            = std::move(png).takeValue();
    request.files[request.terrainDataPath + ".eve-details.json"] = terrainDetailBytes(R"json(
{"schema":"eve.unity-terrain-details","schemaVersion":3,
 "wavingGrass":{"amount":0.12,"speed":0.7,"strength":0.08,"tint":[0.8,1,0.8,1]},
 "prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "renderMode":"Grass","usePrototypeMesh":false,"useInstancing":true,
  "minWidth":0.45,"maxWidth":0.65,"minHeight":0.8,"maxHeight":1.2,
  "noiseSeed":19,"noiseSpread":0.1,"density":1,"alignToGround":0,"positionJitter":0.1,
  "healthyColor":[0.8,1,0.8,1],"dryColor":[0.7,0.8,0.4,1],"bendFactor":0.5,
  "holeEdgePadding":0,"useDensityScaling":true}],
 "instances":[
  {"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","position":[-0.7,0,0],"rotation":[0,0,0,1],"scale":[0.7,1,0.7]},
  {"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","position":[0,0,0],"rotation":[0,0.3826834,0,0.9238795],"scale":[0.8,1.15,0.8]},
  {"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","position":[0.7,0,0],"rotation":[0,0.7071068,0,0.7071068],"scale":[0.65,0.9,0.65]}]}
)json");

    auto imported = asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto instanceAsset =
        std::find_if(imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
                     [](const auto& candidate) { return candidate.type == "eve.instance-set"; });
    REQUIRE(instanceAsset != imported.value().manifest.assets.end());
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
    asset::EvpackResourceReader     reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    const auto&                     variant = profile.value().variant;
    const asset::EvpackCapabilities capabilities{
        variant.os,        variant.arch,    variant.graphics, variant.textureFamilies, {variant.shaderFormat},
        {variant.quality}, variant.features};
    asset_procgen::EvpackInstanceSetLoader instanceLoader(reader);
    auto                                   loaded = instanceLoader.load(instanceAsset->asset, capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().prototypes.size(), std::size_t(1));
    REQUIRE_EQ(loaded.value().instances.size(), std::size_t(3));
    auto resource = AssetRef::parse(loaded.value().prototypes.front().resourceAsset);
    REQUIRE(resource.ok());
    auto image = asset::decodeEvpackImage(reader, resource.value(), capabilities);
    REQUIRE(image.ok());
    CHECK_EQ(image.value().width, 2u);
    CHECK_EQ(image.value().height, 2u);
    CHECK_EQ(image.value().pixels.size(), std::size_t(16));

    auto*                  testWindow = window::Window::create();
    auto*                  gfx        = graphics::Graphics::create();
    window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 240;
    REQUIRE(testWindow->setWindowSettings(settings));
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(graphics::Color(.04f, .06f, .09f, 1.f));
    auto* camera = graphics::Camera3D::createCamera();
    camera->setEye(0.f, 1.1f, 3.f);
    camera->setTarget(0.f, .5f, 0.f);
    camera->setAmbient(.55f, .55f, .55f);
    auto* sun = graphics::Light3D::createLight("dir");
    sun->setDirection(.4f, 1.f, .5f);
    sun->setColor(1.f, .96f, .9f, 1.4f);

    auto card = asset_procgen::buildTerrainDetailCard(loaded.value().prototypes.front());
    REQUIRE(card.ok());
    auto* mesh = gfx->newMeshFromArrays(card.value().positions.data(), card.value().normals.data(),
                                        card.value().texcoords.data(), int(card.value().positions.size() / 3),
                                        card.value().indices.data(), int(card.value().indices.size()));
    REQUIRE(mesh != nullptr);
    auto* texture = gfx->newTexture(int(image.value().width), int(image.value().height), image.value().pixels.data());
    REQUIRE(texture != nullptr);
    auto* material = gfx->newMaterial();
    material->setAlbedoTexture(texture);
    material->setSurfaceMode("masked");
    material->setDoubleSided(true);
    material->setRoughness(1.f);
    for (const auto& instance : loaded.value().instances) {
        auto* detail = graphics::Renderable3D::create();
        detail->setMesh(mesh);
        detail->setMaterial(material);
        detail->setPosition(instance.position[0], instance.position[1], instance.position[2]);
        detail->setScale(instance.scale[0], instance.scale[1], instance.scale[2]);
    }
    auto* renderControl = gfx->getRenderControl();
    for (const char* feature : {"shadow", "gbuffer", "ao", "gi", "aa", "atmosphere", "msaa", "visResolve", "gpuDriven"})
        renderControl->disable(feature);
    {
        for (int frame = 0; frame < 4; ++frame) {
            graphics::RenderSystem3D::render(*gfx);
            REQUIRE(!gfx->gpuDrivenEnabled());
            graphics::RenderSystem::render(*gfx);
        }
        auto screenshot  = std::make_unique<image::ImageData>(settings.width / 2, settings.height / 2, "RGBA8");
        int  greenPixels = 0;
        for (int y = 0; y < settings.height; y += 2)
            for (int x = 0; x < settings.width; x += 2) {
                const auto pixel = gfx->getPixel(x, y);
                screenshot->setPixel(x / 2, y / 2, {pixel.r, pixel.g, pixel.b, pixel.a});
                if (pixel.g > pixel.r + .04f && pixel.g > pixel.b + .02f) ++greenPixels;
            }
        REQUIRE_GT(greenPixels, 40);
        const auto outputPath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out" /
                                ("terrain-detail-imported-" + gfx->getBackendName() + ".png");
        REQUIRE(saveReadback(*screenshot, outputPath));
    }
    testWindow->close();
}

// asset_procgen_terrain_vegetation.cpp
using namespace eve::asset_procgen;

namespace {
class Resolver final : public ITerrainVegetationGpuResolver {
public:
    eve::Result<TerrainVegetationGpuPrototype> resolve(const TerrainVegetationGpuPrototypeRequest& request) override {
        ++calls;
        seenMetadata = seenMetadata || request.detail != nullptr;
        if (request.prototype == "missing")
            return eve::Result<TerrainVegetationGpuPrototype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing prototype"));
        if (request.prototype == "tree") {
            TerrainVegetationGpuPrototype result;
            std::array<float, 16>         first{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            auto                          second = first;
            second[12]                           = 2.f;
            result.parts.push_back({first, 20, 30, 7u, 3u});
            result.parts.push_back({second, 21, 31, 7u, 3u});
            return eve::Result<TerrainVegetationGpuPrototype>::success(std::move(result));
        }
        return eve::Result<TerrainVegetationGpuPrototype>::success({calls, calls + 10, 7u, 3u});
    }
    eve::Result<void> drawShadow(const TerrainVegetationGpuPrototypeRequest&, const std::array<float, 16>&,
                                 const std::array<float, 16>&) override {
        ++shadowCalls;
        return eve::Result<void>::success();
    }
    std::uint32_t calls        = 0;
    std::uint32_t shadowCalls  = 0;
    bool          seenMetadata = false;
};
}  // namespace

TEST_CASE("asset.procgen.terrainDetailCardsPreserveBillboardAndCrossedGrassSemantics") {
    RuntimeInstancePrototype billboard{"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "GrassBillboard", false,
                                       true};
    auto                     billboardGeometry = buildTerrainDetailCard(billboard);
    REQUIRE(billboardGeometry.ok());
    CHECK(billboardGeometry.value().cameraFacing);
    CHECK_EQ(billboardGeometry.value().positions.size(), std::size_t(12));
    CHECK_EQ(billboardGeometry.value().indices.size(), std::size_t(6));
    CHECK_EQ(billboardGeometry.value().positions[1], 0.f);
    CHECK_EQ(billboardGeometry.value().positions[7], 1.f);

    RuntimeInstancePrototype grass = billboard;
    grass.renderMode               = "Grass";
    auto crossed                   = buildTerrainDetailCard(grass);
    REQUIRE(crossed.ok());
    CHECK(!crossed.value().cameraFacing);
    CHECK_EQ(crossed.value().positions.size(), std::size_t(24));
    CHECK_EQ(crossed.value().indices.size(), std::size_t(12));

    RuntimeInstancePrototype mesh{"unity-guid:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "VertexLit", true, true};
    auto                     unsupported = buildTerrainDetailCard(mesh);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("asset.procgen.terrainVegetationRealizesGraphAndBakedInstancesIntoBuckets") {
    eve::procgen::Heightmap heightmap(3, 3);
    auto                    spatial    = eve::procgen::SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
    auto                    graphAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(graphAsset.ok());
    LoadedPointGraph loaded{std::move(graphAsset).takeValue(), std::make_unique<eve::procgen::PointGraph>(), "tag", {}};
    REQUIRE(loaded.graph->addNode("sample", "spatial.sample"));
    REQUIRE(loaded.graph->setNodeSpatial("sample", &spatial));
    REQUIRE(loaded.graph->setNodeFloat("sample", "spacing", 1.f));
    REQUIRE(loaded.graph->addNode("tag", "attribute.set.string"));
    REQUIRE(loaded.graph->connect("sample", "tag", 0));
    REQUIRE(loaded.graph->setNodeString("tag", "attribute", "prototype"));
    REQUIRE(loaded.graph->setNodeString("tag", "value", "grass-card"));

    auto instanceAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(instanceAsset.ok());
    LoadedInstanceSet baked{std::move(instanceAsset).takeValue(), {}, {}, {}};
    baked.wavingGrassAmount   = .4f;
    baked.wavingGrassSpeed    = .6f;
    baked.wavingGrassStrength = .8f;
    baked.wavingGrassTint     = {.3f, .7f, .4f, 1.f};
    baked.prototypes.push_back({"oak", "VertexLit", true, true});
    baked.instances.push_back({"oak", {3.f, 2.f, 1.f}, {0.f, 0.f, 0.f, 1.f}, {2.f, 3.f, 2.f}});
    auto realized = realizeTerrainVegetation(&loaded, &baked);
    REQUIRE(realized.ok());
    CHECK(realized.value().generatedCount > 0);
    CHECK_EQ(realized.value().explicitCount, 1u);
    CHECK_EQ(realized.value().wavingGrassAmount, .4f);
    CHECK_EQ(realized.value().wavingGrassSpeed, .6f);
    CHECK_EQ(realized.value().wavingGrassStrength, .8f);
    CHECK_EQ(realized.value().wavingGrassTint[1], .7f);
    REQUIRE_EQ(realized.value().prototypes.size(), std::size_t(1));
    REQUIRE_EQ(realized.value().buckets.size(), std::size_t(2));
    CHECK_EQ(realized.value().buckets[0].prototype, std::string("grass-card"));
    CHECK_EQ(realized.value().buckets[1].prototype, std::string("oak"));
    CHECK(realized.value().graphNodesEvaluated > 0);
    CHECK(realized.value().graphMilliseconds >= 0.0);
    for (const auto& instance : realized.value().instances) CHECK(instance.id != 0);
}

TEST_CASE("asset.procgen.terrainVegetationSupportsOptionalProvidersAndRejectsBudgets") {
    auto instanceAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(instanceAsset.ok());
    LoadedInstanceSet baked{std::move(instanceAsset).takeValue(), {}, {}, {}};
    baked.instances.push_back({"tree", {}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}});
    auto explicitOnly = realizeTerrainVegetation(nullptr, &baked);
    REQUIRE(explicitOnly.ok());
    CHECK_EQ(explicitOnly.value().explicitCount, 1u);

    TerrainVegetationLimits limits;
    limits.maximumTotalInstances = 1;
    baked.instances.push_back({"tree", {}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}});
    auto overBudget = realizeTerrainVegetation(nullptr, &baked, limits);
    REQUIRE(!overBudget.ok());
    CHECK_EQ(overBudget.error()->code(), eve::DiagnosticCode::InvalidArgument);

    auto absent = realizeTerrainVegetation(nullptr, nullptr);
    REQUIRE(!absent.ok());
    CHECK_EQ(absent.error()->code(), eve::DiagnosticCode::NotFound);
}

TEST_CASE("asset.procgen.terrainVegetationGpuPlanResolvesBucketsAtomically") {
    TerrainVegetationRealization realization;
    realization.instances = {{1, "grass", {}, {1.f, 2.f, 3.f}, {0.f, 0.f, 0.f, 1.f}, {2.f, 2.f, 2.f}},
                             {2, "tree", {}, {4.f, 5.f, 6.f}, {0.f, 0.f, 0.f, 1.f}, {1.f, 2.f, 1.f}}};
    realization.buckets   = {{"grass", 0, 1}, {"tree", 1, 1}};
    realization.prototypes.push_back({"grass", "GrassBillboard", false, true});
    realization.prototypes.back().useInstancing = false;
    realization.prototypes.back().minHeight     = 1.f;
    realization.prototypes.back().maxHeight     = 3.f;
    realization.prototypes.back().dryColor      = {0.6f, 0.4f, 0.2f, 1.f};
    realization.prototypes.back().healthyColor  = {0.2f, 0.8f, 0.4f, 1.f};
    realization.wavingGrassAmount               = .4f;
    realization.wavingGrassSpeed                = .6f;
    realization.wavingGrassStrength             = .8f;
    realization.wavingGrassTint                 = {.3f, .7f, .4f, 1.f};
    Resolver resolver;
    auto     plan = buildTerrainVegetationGpuPlan(realization, resolver, {2.5});
    REQUIRE(plan.ok());
    CHECK(resolver.seenMetadata);
    CHECK_EQ(resolver.calls, 2u);
    REQUIRE_EQ(plan.value().instances.size(), std::size_t(3));
    CHECK_EQ(plan.value().instances[0].meshId, 1u);
    CHECK_EQ(plan.value().instances[1].materialId, 30u);
    CHECK_EQ(plan.value().instances[2].materialId, 31u);
    CHECK(std::abs(plan.value().instances[2].model[3][0] - 6.f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].model[3][0] - 1.f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].color.r - .4f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].color.g - .6f) < 0.0001f);
    CHECK(std::abs(plan.value().instances[0].terrainWave.x - 1.5f) < 0.0001f);
    CHECK_EQ(plan.value().instances[0].terrainWave.y, .4f);
    CHECK_EQ(plan.value().instances[0].terrainWave.z, .8f);
    CHECK_EQ(plan.value().instances[0].terrainWave.w, 1.f);
    CHECK_EQ(plan.value().instances[0].terrainWaveTint.g, .7f);
    CHECK_EQ(plan.value().instances[1].terrainWave.w, 0.f);
    CHECK_EQ(plan.value().instances[2].terrainWave.w, 0.f);

    auto invalidFrame =
        buildTerrainVegetationGpuPlan(realization, resolver, {std::numeric_limits<double>::quiet_NaN()});
    REQUIRE(!invalidFrame.ok());
    CHECK_EQ(invalidFrame.error()->code(), eve::DiagnosticCode::InvalidArgument);

    realization.instances[1].prototype = "missing";
    auto rejected                      = buildTerrainVegetationGpuPlan(realization, resolver);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::Conflict);
}

TEST_CASE("asset.procgen.terrainVegetationRendererOwnsFrameStateAndRejectsInvalidTime") {
    TerrainVegetationRealization realization;
    Resolver                     resolver;
    TerrainVegetationRenderer    renderer(std::move(realization), resolver);

    CHECK(renderer.lastSubmissionError() == nullptr);
    CHECK(renderer.setFrame({12.5}).ok());
    auto invalid = renderer.setFrame({std::numeric_limits<double>::infinity()});
    REQUIRE(!invalid.ok());
    CHECK_EQ(invalid.error()->code(), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.procgen.terrainVegetationControlWeightsCullDeterministicallyAndFlipV") {
    TerrainVegetationRealization source;
    source.generatedCount = 3;
    source.explicitCount  = 1;
    source.instances      = {{1, "grass", "Grass", {0.f, 0.f, 0.f}},
                             {2, "grass", "Grass", {1.f, 0.f, 1.f}},
                             {3, "flower", "Flower", {0.f, 0.f, 1.f}},
                             {4, "tree", {}, {1.f, 0.f, 0.f}}};
    source.buckets        = {{"flower", 0, 1}, {"grass", 1, 2}, {"tree", 3, 1}};

    auto materialAsset = eve::AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440002");
    REQUIRE(materialAsset.ok());
    LoadedTerrainMaterial material{std::move(materialAsset).takeValue()};
    material.layers.resize(2);
    material.layers[0].name = "Grass";
    material.layers[1].name = "Flower";
    TerrainMaterialAtlases    atlases;
    TerrainMaterialAtlasGroup group;
    group.firstLayer = 0;
    group.layerCount = 2;
    group.control    = {2, 2, {255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0}};
    atlases.groups.push_back(std::move(group));

    auto normal = filterTerrainVegetationByLayerWeights(source, material, atlases, {0, 0, 1, 1, false});
    REQUIRE(normal.ok());
    CHECK_EQ(normal.value().instances.size(), std::size_t(3));
    CHECK_EQ(normal.value().weightCulledCount, 1u);
    CHECK_EQ(normal.value().explicitCount, 1u);
    auto flipped = filterTerrainVegetationByLayerWeights(source, material, atlases, {0, 0, 1, 1, true});
    REQUIRE(flipped.ok());
    CHECK_EQ(flipped.value().instances.size(), std::size_t(2));
    CHECK_EQ(flipped.value().weightCulledCount, 2u);
}
}  // namespace test_asset_procgen_terrain_detail_render


// Contract section: asset scene bindings
#include "PrivateTestScope.h"

// asset_scene_bindings.cpp
namespace test_asset_scene_bindings {
using namespace eve;
using namespace eve::asset;

namespace {
constexpr const char*    objectId = "018f6f22-2490-7ad2-bf58-4f1dbca31041";
constexpr const char*    assetId  = "018f6f22-2490-7ad2-bf58-4f1dbca31040";
const EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};

Value::Object definition() {
    return {{"schema", "eve.scene-template"},
            {"schemaVersion", 2},
            {"coordinateSystem", "right-handed-x-right-y-up-minus-z-forward"},
            {"nodes", Value::Array{Value::Object{{"objectId", objectId},
                                                 {"sourceFileId", 1},
                                                 {"parentSourceFileId", 0},
                                                 {"name", "Node"},
                                                 {"position", Value::Array{0, 0, 0}},
                                                 {"rotation", Value::Array{0, 0, 0, 1}},
                                                 {"scale", Value::Array{1, 1, 1}}}}},
            {"renderers", Value::Array{Value::Object{{"objectId", objectId},
                                                     {"mesh", std::string("asset://") + assetId},
                                                     {"material", std::string("asset://") + assetId},
                                                     {"enabled", true}}}}};
}

Result<asset_scene::LoadedSceneTemplate> load(Value::Object root, unsigned version = 2) {
    auto encoded = encodeRuntimeDefinition(Value(std::move(root)));
    REQUIRE(encoded.ok());
    EvpackBuild build;
    build.packageId = *PersistentId::parse(assetId);
    build.buildId   = *PersistentId::parse(objectId);
    build.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks.push_back({build.packageId,
                            "eve.scene-template",
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
    auto                 ref = AssetRef::fromId(*PersistentId::parse(assetId));
    REQUIRE(ref.ok());
    return asset_scene::EvpackSceneTemplateLoader(reader).load(ref.value(), caps);
}

std::array<double, 4> yxz(double yaw, double pitch, double roll) {
    const double y = yaw / 2, x = pitch / 2, z = roll / 2;
    return {std::cos(y) * std::sin(x) * std::cos(z) + std::sin(y) * std::cos(x) * std::sin(z),
            std::sin(y) * std::cos(x) * std::cos(z) - std::cos(y) * std::sin(x) * std::sin(z),
            std::cos(y) * std::cos(x) * std::sin(z) - std::sin(y) * std::sin(x) * std::cos(z),
            std::cos(y) * std::cos(x) * std::cos(z) + std::sin(y) * std::sin(x) * std::sin(z)};
}
}  // namespace

TEST_CASE("asset.sceneBindingsRejectDanglingDuplicateAndUnversionedBindings") {
    auto valid = load(definition());
    REQUIRE(valid.ok());
    REQUIRE_EQ(valid.value().renderers.size(), std::size_t(1));
    auto root                                                                                 = definition();
    (*root.at("renderers").getIf<Value::Array>()->front().getIf<Value::Object>())["objectId"] = assetId;
    auto invalid                                                                              = load(root);
    REQUIRE(!invalid.ok());
    root                                                = definition();
    auto duplicate                                      = root.at("nodes").getIf<Value::Array>()->front();
    (*duplicate.getIf<Value::Object>())["sourceFileId"] = 2;
    root.at("nodes").getIf<Value::Array>()->push_back(duplicate);
    auto repeated = load(root);
    REQUIRE(!repeated.ok());
    root                  = definition();
    root["schemaVersion"] = 1;
    auto unversioned      = load(root, 1);
    REQUIRE(!unversioned.ok());
    root.erase("renderers");
    auto legacy = load(root, 1);
    REQUIRE(!legacy.ok());
}

TEST_CASE("asset.sceneBindingsPreserveCombinedAndGimbalRotations") {
    constexpr double radians = 0.017453292519943295;
    for (double pitch : {37.0, 90.0, -90.0}) {
        const auto expected = yxz(23 * radians, pitch * radians, -19 * radians);
        auto       root     = definition();
        (*root.at("nodes").getIf<Value::Array>()->front().getIf<Value::Object>())["rotation"] =
            Value::Array{expected[0], expected[1], expected[2], expected[3]};
        auto loaded = load(root);
        REQUIRE(loaded.ok());
        const auto& node   = loaded.value().root.children.front();
        const auto  actual = yxz(node.yaw * radians, node.pitch * radians, node.roll * radians);
        double      dot    = 0;
        for (std::size_t i = 0; i < 4; ++i) dot += expected[i] * actual[i];
        REQUIRE(std::abs(std::abs(dot) - 1.0) < 1e-6);
    }
}
}  // namespace test_asset_scene_bindings


// Contract section: asset shader
#include "PrivateTestScope.h"

#include "asset/ShaderAsset.h"
#include "asset/import/ShaderImporter.h"
// asset_shader.cpp
namespace test_asset_shader {
using namespace eve;
using namespace eve::asset;

namespace {
Value shaderDefinition() {
    auto value = Value::fromJson(R"({"schema":"eve.shader","schemaVersion":1,
      "interface":"mesh3d","format":"spirv-1.6",
      "vertex":[119734787,67072,0,2,0,327695,0,1,1852399981,0],
      "fragment":[119734787,67072,0,2,0,327695,4,1,1852399981,0],
      "parameters":[{"name":"strength","default":[1.0]}]})");
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}
}  // namespace


TEST_CASE("asset.shader cooks reopens and loads beside Unity schema versions") {
    auto id = PersistentId::parse("550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(id);
    auto reference = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(reference.ok());
    auto json = shaderDefinition().toJson();
    REQUIRE(json.ok());
    EvaArchive source;
    source.manifest.packageId      = *id;
    source.manifest.packageName    = "shader.test";
    source.manifest.packageVersion = "1.0.0";
    const std::string path         = "assets/" + id->format() + "/asset.json";
    source.manifest.assets.push_back({reference.value(), "eve.shader", SchemaVersion(1), path, {}, {}});
    source.manifest.entrypoints.emplace("default", reference.value());
    source.entries.push_back({path, {json.value().begin(), json.value().end()}});
    auto profile = assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto first = cookEvaToEvpack(source, profile.value());
    REQUIRE(first.ok());
    auto second = cookEvaToEvpack(source, profile.value());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().bytes, second.value().bytes);
    auto package = parseEvpack(first.value().bytes);
    REQUIRE(package.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(package).takeValue()));
    EvpackCapabilities   capabilities{"windows", "x86_64", "vulkan", {"bc", "rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                 loaded = loadShaderAsset(reader, reference.value(), capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().parameters[0].name, std::string("strength"));
    capabilities.graphics = "webgpu";
    REQUIRE(!loadShaderAsset(reader, reference.value(), capabilities).ok());
    auto web = assetCookProfileForTarget("web-wasm32-webgpu");
    REQUIRE(web.ok());
    REQUIRE(!cookEvaToEvpack(source, web.value()).ok());
}

TEST_CASE("asset.shader import preserves identity and rejects invalid candidates") {
    auto id = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    REQUIRE(id);
    asset_import::ImportPackageIdentity identity{*id, "shader.test", "1.0.0", {}};
    auto                                definition = shaderDefinition();
    auto                                json       = definition.toJson();
    REQUIRE(json.ok());
    auto first = asset_import::prepareShaderImport(identity, json.value());
    REQUIRE(first.ok());
    auto second = asset_import::prepareShaderImport(identity, json.value());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().manifest.assets[0].asset, second.value().manifest.assets[0].asset);
    REQUIRE_EQ(first.value().manifest.assets[0].asset.id(), id->child("shader:default"));
    auto archive = buildEvaArchive(first.value().manifest, first.value().entries);
    REQUIRE(archive.ok());
    auto reopened = parseEvaArchive(archive.value());
    REQUIRE(reopened.ok());
    REQUIRE(!asset_import::prepareShaderImport(identity, "{}").ok());
    REQUIRE(!asset_import::prepareShaderImport({}, json.value()).ok());
}
}  // namespace test_asset_shader


// Contract section: graphics vegetation translucency
#include "PrivateTestScope.h"

#include "graphics/Canvas.h"
#include "graphics/Mesh.h"
// graphics_vegetation_translucency.cpp
namespace test_graphics_vegetation_translucency {
using namespace eve::graphics;


TEST_CASE("graphics.vegetation.translucency_gpu_backlight_and_ambient") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    positions[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]{0, 2, 1, 0, 3, 2};
    auto*          mesh = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.2f, .2f, .2f, 0};
    lighting.lights[0].posRadius = {0, 0, -1, 0};
    lighting.lights[0].color     = {.5f, .6f, .7f, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    Color tint(.5f, .5f, .5f, 1);
    float materialRoughness = 1;
    auto  render            = [&](const PbrSurface& surface) {
        auto drawSurface                               = surface;
        drawSurface.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, materialRoughness);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, tint);
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    PbrSurface surface;
    const auto baseline                  = render(surface);
    surface.vegetationColor.overlayColor = {.2f, .4f, .8f};
    surface.vegetationColor.overlay      = .25f;
    surface.vegetationColor.wetness      = .5f;
    const auto           staged          = render(surface);
    const auto           originalTint    = tint;
    std::array<float, 3> stagedRgb{};
    for (size_t c = 0; c < 3; ++c) {
        const float overlay = std::lerp(.5f, surface.vegetationColor.overlayColor[size_t(c)], .25f);
        stagedRgb[c]        = std::lerp(overlay, overlay * overlay, .25f);
    }
    tint                       = Color(stagedRgb[0], stagedRgb[1], stagedRgb[2], 1);
    surface.vegetationColor    = {};
    const auto stagedReference = render(surface);
    CHECK(std::abs(staged.r - stagedReference.r) < .005f);
    CHECK(std::abs(staged.g - stagedReference.g) < .005f);
    CHECK(std::abs(staged.b - stagedReference.b) < .005f);
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.2f, .4f, .8f};
    surface.vegetationColor.overlay              = .3f;
    surface.vegetationColor.wetness              = 0;
    surface.vegetationColor.overlayVariation     = .5f;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = .5f;
    std::vector<float> factors{.5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0};
    REQUIRE(mesh->adoptVegetationFactors(std::move(factors)).ok());
    const auto  factored    = render(surface);
    const float overlayMask = std::clamp((.3f * .75f * .5f - .1f) / .1001f, 0.f, 1.f);
    tint =
        Color(std::lerp(.5f, .2f, overlayMask), std::lerp(.5f, .4f, overlayMask), std::lerp(.5f, .8f, overlayMask), 1);
    surface.vegetationColor      = {};
    const auto factoredReference = render(surface);
    CHECK(std::abs(factored.r - factoredReference.r) < .005f);
    CHECK(std::abs(factored.g - factoredReference.g) < .005f);
    CHECK(std::abs(factored.b - factoredReference.b) < .005f);
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.2f, .4f, .8f};
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 1;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    const auto sideProjected                     = render(surface);
    CHECK(std::abs(sideProjected.r - baseline.r) < .005f);
    CHECK(std::abs(sideProjected.g - baseline.g) < .005f);
    CHECK(std::abs(sideProjected.b - baseline.b) < .005f);
    std::vector<float> clearFactors;
    REQUIRE(mesh->adoptVegetationFactors(std::move(clearFactors)).ok());
    surface.vegetationColor                      = {};
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlaySmoothness    = .8f;
    const auto overlaySmooth                     = render(surface);
    surface.vegetationColor                      = {};
    materialRoughness                            = .2f;
    const auto overlaySmoothReference            = render(surface);
    CHECK(std::abs(overlaySmooth.r - overlaySmoothReference.r) < .005f);
    CHECK(std::abs(overlaySmooth.g - overlaySmoothReference.g) < .005f);
    CHECK(std::abs(overlaySmooth.b - overlaySmoothReference.b) < .005f);
    materialRoughness                       = 1;
    surface.vegetationColor.wetness         = .5f;
    surface.vegetationColor.wetnessContrast = 0;
    const auto wetSmooth                    = render(surface);
    surface.vegetationColor                 = {};
    materialRoughness                       = .25f;
    const auto wetSmoothReference           = render(surface);
    CHECK(std::abs(wetSmooth.r - wetSmoothReference.r) < .005f);
    CHECK(std::abs(wetSmooth.g - wetSmoothReference.g) < .005f);
    CHECK(std::abs(wetSmooth.b - wetSmoothReference.b) < .005f);
    materialRoughness = 1;
    const uint8_t tiltedNormal[]{204, 128, 255, 255};
    auto*         tiltedNormalTexture = gfx->newTexture(1, 1, tiltedNormal);
    REQUIRE(tiltedNormalTexture != nullptr);
    auto& normalBinding                          = surface.textures[size_t(PbrTextureSlot::Normal)];
    normalBinding.texture                        = tiltedNormalTexture;
    normalBinding.srgbDecode                     = false;
    surface.normalScale                          = 1;
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlayNormalScale   = .25f;
    const auto overlayNormal                     = render(surface);
    surface.vegetationColor                      = {};
    surface.normalScale                          = .25f;
    const auto overlayNormalReference            = render(surface);
    CHECK(std::abs(overlayNormal.r - overlayNormalReference.r) < .005f);
    CHECK(std::abs(overlayNormal.g - overlayNormalReference.g) < .005f);
    CHECK(std::abs(overlayNormal.b - overlayNormalReference.b) < .005f);
    surface.textures[size_t(PbrTextureSlot::Normal)] = {};
    surface.normalScale                              = 1;
    tint                                             = originalTint;
    surface.translucency.intensity                   = .1f;
    surface.translucency.strength                    = 2;
    surface.translucency.direct                      = .8f;
    surface.translucency.ambient                     = .3f;
    const auto lit                                   = render(surface);
    // Center view lobe is approximately 0.5^2, not 1 from renormalizing
    // the distorted light. Two .5 albedo stages and source intensity*10 remain.
    CHECK(std::abs(lit.r - baseline.r - .065f) < .008f);
    CHECK(std::abs(lit.g - baseline.g - .078f) < .008f);
    CHECK(std::abs(lit.b - baseline.b - .091f) < .008f);
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlaySmoothness    = 0;
    surface.vegetationColor.overlaySubsurface    = .25f;
    const auto overlaySubsurface                 = render(surface);
    surface.vegetationColor                      = {};
    surface.translucency.intensity               = .025f;
    const auto overlaySubsurfaceReference        = render(surface);
    CHECK(std::abs(overlaySubsurface.r - overlaySubsurfaceReference.r) < .005f);
    CHECK(std::abs(overlaySubsurface.g - overlaySubsurfaceReference.g) < .005f);
    CHECK(std::abs(overlaySubsurface.b - overlaySubsurfaceReference.b) < .005f);
    surface.translucency.intensity = .1f;
    std::vector<float> highlightValues(4, .5f);
    REQUIRE(mesh->adoptMotionHighlights(std::move(highlightValues)).ok());
    surface.motionHighlightColor   = {1, 1, 1};
    const auto highlighted         = render(surface);
    surface.translucency.intensity = 0;
    const auto highlightedBaseline = render(surface);
    // Final albedo receives highlight once; the earlier translucency albedo
    // remains unchanged. Applying it twice would give 2.25 instead of 1.5.
    CHECK(std::abs(highlighted.r - highlightedBaseline.r - (lit.r - baseline.r) * 1.5f) < .008f);
    CHECK(std::abs(highlighted.g - highlightedBaseline.g - (lit.g - baseline.g) * 1.5f) < .008f);
    CHECK(std::abs(highlighted.b - highlightedBaseline.b - (lit.b - baseline.b) * 1.5f) < .008f);
    surface.motionHighlightColor   = {0, 0, 0};
    surface.translucency.intensity = .1f;
    std::vector<float> clearHighlights;
    REQUIRE(mesh->adoptMotionHighlights(std::move(clearHighlights)).ok());
    surface.motionHighlightColor = {1, 1, 1};
    const auto absentHighlight   = render(surface);
    CHECK(std::abs(absentHighlight.r - lit.r) < .005f);
    // The left and right vertices differ. Center sampling must interpolate,
    // rather than use a flat value from one endpoint.
    std::vector<float> gradientHighlights{0, 1, 1, 0};
    REQUIRE(mesh->adoptMotionHighlights(std::move(gradientHighlights)).ok());
    const auto gradientLit         = render(surface);
    surface.translucency.intensity = 0;
    const auto gradientBaseline    = render(surface);
    CHECK(std::abs(gradientLit.r - gradientBaseline.r - (lit.r - baseline.r) * 1.5f) < .008f);
    surface.motionHighlightColor   = {0, 0, 0};
    surface.translucency.intensity = .1f;
    clearHighlights.clear();
    REQUIRE(mesh->adoptMotionHighlights(std::move(clearHighlights)).ok());
    // Translucency mask remains active without enabling the separate RGB tint mask.
    const uint8_t orm[]{255, 255, 0, 128};
    auto*         mask = gfx->newTexture(1, 1, orm);
    REQUIRE(mask != nullptr);
    auto& binding                    = surface.textures[size_t(PbrTextureSlot::MetallicRoughness)];
    binding.texture                  = mask;
    binding.srgbDecode               = false;
    surface.translucency.maskMinimum = 0;
    surface.translucency.maskMaximum = 1;
    surface.translucency.maskAmount  = 1;
    REQUIRE(!surface.colorMaskEnabled);
    const auto masked = render(surface);
    CHECK(std::abs(masked.r - baseline.r - .0326f) < .008f);
    CHECK(std::abs(masked.g - baseline.g - .0391f) < .008f);
    CHECK(std::abs(masked.b - baseline.b - .0457f) < .008f);
    surface.translucency.maskAmount = 0;
    surface.translucency.direct     = 0;
    const auto ambientOnly          = render(surface);
    CHECK(std::abs(ambientOnly.r - baseline.r - .015f) < .006f);
    surface.translucency.direct  = .8f;
    surface.translucency.ambient = 0;
    const auto directOnly        = render(surface);
    CHECK(std::abs(directOnly.r - baseline.r - .05f) < .006f);
    surface.translucency.globalIntensity = .5f;
    surface.translucency.overlay         = .5f;
    const auto globalReduced             = render(surface);
    CHECK(std::abs(globalReduced.r - baseline.r - .0125f) < .006f);
    lighting.lights[0].color       = {0, 0, 0, 0};
    const auto noLight             = render(surface);
    surface.translucency.intensity = 0;
    const auto noLightBaseline     = render(surface);
    CHECK(std::abs(noLight.r - noLightBaseline.r) < .005f);
    CHECK(std::abs(noLight.g - noLightBaseline.g) < .005f);
    CHECK(std::abs(noLight.b - noLightBaseline.b) < .005f);
    // Exercise the same lighting through Unity import, canonical cooking and
    // runtime image resolution, including the source HDR tint without conversion.
    eve::asset_import::UnityProjectImportRequest request;
    request.package = {
        *eve::PersistentId::parse("11111111-2222-4333-8444-555555555555"), "translucency.gpu", "1.0.0", {}};
    auto put = [&](const std::string& path, const std::string& text) {
        request.files[path] = {text.begin(), text.end()};
    };
    put("Assets/leaf.mat.meta", "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    put("Assets/leaf.mat", R"(--- !u!21 &2100000
Material:
  m_Shader: {fileID: 4800000, guid: a933075b367f9b24981408633f72ff34, type: 3}
  m_SavedProperties:
    m_Floats:
    - _SubsurfaceValue: 0.1
    - _SubsurfaceScatteringValue: 2
    - _SubsurfaceAngleValue: 2
    - _SubsurfaceNormalValue: 0.5
    - _SubsurfaceDirectValue: 0.8
    - _SubsurfaceAmbientValue: 0.3
    - _SubsurfaceMaskValue: 0
    m_Colors:
    - _MainColor: {r: 0.5, g: 0.5, b: 0.5, a: 1}
    - _MotionHighlightColor: {r: 1, g: 1, b: 1, a: 1}
)");
    auto imported = eve::asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto ref = imported.value().manifest.entrypoints.at("Assets/leaf.mat");
    auto       eva = eve::asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(eva.ok());
    auto archive = eve::asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = eve::asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = eve::asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    eve::asset::EvpackResourceReader reader(std::make_shared<const eve::asset::Evpack>(std::move(pack).takeValue()));
    const eve::asset::EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                                 material = eve::asset_graphics::detail::readCookedMaterial(reader, ref, caps);
    REQUIRE(material.ok());
    eve::asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    eve::asset_graphics::EvpackImageLoader           loader(reader, factory);
    std::vector<Texture*>                            images;
    for (size_t slot = 0; slot < material.value().images.size(); ++slot) {
        if (!material.value().images[slot]) continue;
        auto image = loader.load(*material.value().images[slot], caps);
        REQUIRE(image.ok());
        material.value().surface.textures[slot].texture = image.value().texture;
        images.push_back(image.value().texture);
    }
    tint                     = material.value().color;
    lighting.lights[0].color = {.5f, .6f, .7f, 0};
    const auto sourceLit     = render(material.value().surface);
    CHECK(std::abs(sourceLit.r - lit.r) < .005f);
    CHECK(std::abs(sourceLit.g - lit.g) < .005f);
    CHECK(std::abs(sourceLit.b - lit.b) < .005f);
    std::vector<float> importedHighlights(4, .5f);
    REQUIRE(mesh->adoptMotionHighlights(std::move(importedHighlights)).ok());
    const auto sourceHighlighted = render(material.value().surface);
    CHECK(std::abs(sourceHighlighted.r - highlighted.r) < .005f);
    CHECK(std::abs(sourceHighlighted.g - highlighted.g) < .005f);
    CHECK(std::abs(sourceHighlighted.b - highlighted.b) < .005f);
    for (auto* image : images) REQUIRE(factory.releaseImage(image).ok());
}
}  // namespace test_graphics_vegetation_translucency
