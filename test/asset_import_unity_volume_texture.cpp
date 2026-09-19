#include "asset/AssetCooker.h"
#include "asset/CanonicalVolumeTextureCook.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/import/UnityImporter.h"
#include "graphics/Graphics.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <memory>

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
