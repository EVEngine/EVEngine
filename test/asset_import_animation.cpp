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
