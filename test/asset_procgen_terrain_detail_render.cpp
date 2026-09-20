#include "asset/AssetCooker.h"
#include "asset/EvaArchive.h"
#include "asset/EvpackImageDecoder.h"
#include "asset/EvpackResourceReader.h"
#include "asset/SourcePng.h"
#include "asset/import/UnityImporter.h"
#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/TerrainDetailCard.h"
#include "asset/procgen/TerrainVegetationRuntime.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/RenderControl.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

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
    auto png = asset::detail::encodeSourcePng(static_cast<std::uint32_t>(screenshot.getWidth()),
                                               static_cast<std::uint32_t>(screenshot.getHeight()), rgba,
                                               4 * 1024 * 1024);
    if (!png) return false;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(png.value().data()), std::streamsize(png.value().size()));
    return output.good();
}

}  // namespace

TEST_CASE("asset.procgen.terrainDetailSidecarPublishesDecodableRuntimeResource") {
    asset_import::UnityProjectImportRequest request;
    request.package = {*PersistentId::parse("6cb7a302-f360-4ae3-b22f-dd722e63e18c"),
                       "terrain.detail.render.fixture", "1.0.0", {}};
    request.terrainDataPath = "Assets/Terrain.asset";
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
    const std::array<std::uint8_t, 16> pixels{
        20, 170, 35, 255, 30, 220, 45, 255, 15, 120, 25, 255, 25, 190, 40, 255};
    auto png = asset::detail::encodeSourcePng(2, 2, pixels, 4096);
    REQUIRE(png.ok());
    request.files["Assets/grass.png"] = std::move(png).takeValue();
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
    const auto instanceAsset = std::find_if(imported.value().manifest.assets.begin(),
                                            imported.value().manifest.assets.end(), [](const auto& candidate) {
                                                return candidate.type == "eve.instance-set";
                                            });
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
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    const auto& variant = profile.value().variant;
    const asset::EvpackCapabilities capabilities{variant.os, variant.arch, variant.graphics,
                                                  variant.textureFamilies, {variant.shaderFormat},
                                                  {variant.quality}, variant.features};
    asset_procgen::EvpackInstanceSetLoader instanceLoader(reader);
    auto loaded = instanceLoader.load(instanceAsset->asset, capabilities);
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

    auto* testWindow = window::Window::create();
    auto* gfx        = graphics::Graphics::create();
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
    for (const char* feature : {"shadow", "gbuffer", "ao", "gi", "aa", "atmosphere", "msaa", "visResolve",
                                "gpuDriven"})
        renderControl->disable(feature);
    {
        for (int frame = 0; frame < 4; ++frame) {
            graphics::RenderSystem3D::render(*gfx);
            REQUIRE(!gfx->gpuDrivenEnabled());
            graphics::RenderSystem::render(*gfx);
        }
        auto screenshot = std::make_unique<image::ImageData>(settings.width / 2, settings.height / 2, "RGBA8");
        int greenPixels = 0;
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
