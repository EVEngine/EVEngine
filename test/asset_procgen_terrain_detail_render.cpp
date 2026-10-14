#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
#include "asset/AssetCooker.h"
#include "asset/EvaArchive.h"
#include "asset/EvpackImageDecoder.h"
#include "asset/EvpackResourceReader.h"
#include "asset/SourcePng.h"
#include "asset/import/UnityImporter.h"
#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/TerrainDetailCard.h"
#include "asset/procgen/TerrainMaterialAtlas.h"
#include "asset/procgen/TerrainVegetationGpu.h"
#include "asset/procgen/TerrainVegetationRuntime.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "procgen/PointGraph.h"
#include "procgen/SpatialData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
