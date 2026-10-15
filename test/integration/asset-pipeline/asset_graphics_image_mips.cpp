#include "asset/CanonicalImageCook.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    auto                        data = fixture();
    asset::EvpackResourceReader reader(data.pack);
    asset_procgen::LoadedTerrainMaterial material{data.ref};
    material.layers.resize(5);
    for (auto& layer : material.layers) {
        layer.normalConvention = "opengl";
        layer.diffuseAsset = data.ref;
        layer.normalAsset = data.ref;
        layer.maskAsset = data.ref;
    }
    material.layers[0].normalConvention = "directx";
    material.layers[0].maskRemapMinimum = {.2f, 0.f, 0.f, 0.f};
    material.layers[0].maskRemapMaximum = {.6f, 1.f, 1.f, 1.f};
    material.controlAssets[0] = data.ref;
    material.controlAssets[1] = data.ref;
    material.holesAsset = data.ref;
    auto atlases = asset_procgen::buildTerrainMaterialAtlases(reader, material, capabilities);
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
    auto                        data = fixture();
    asset::EvpackResourceReader reader(data.pack);
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
    material.layers[4].tileScaleMeters = {8, 4};
    material.layers[4].tileOffsetMeters = {2, 1};
    material.layers[4].metallic = .25f;
    material.layers[4].normalScale = -2.f;
    material.layers[4].smoothness = .75f;
    graphics::Texture textures[9];
    asset_procgen::TerrainMaterialGpuSet gpu;
    gpu.groups = {{&textures[0], &textures[1], &textures[2], &textures[3]},
                  {&textures[4], &textures[5], &textures[6], &textures[7]}};
    gpu.holes = &textures[8];
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
