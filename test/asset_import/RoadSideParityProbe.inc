// Test-only fixture: imported unit cube, exact placement calibration, actual prefab sink.
#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <iomanip>
#include <sstream>
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/SourcePng.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/graphics/PrefabBoundsCatalog.h"
#include "asset/graphics/ProcgenPrefabSceneSink.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "common/Capability.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "procgen/Procgen.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "scene/Scene.h"
#include "window/Window.h"

namespace {
void sideRequire(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <class T>
void sideResult(const eve::Result<T>& result, const char* message) {
    if (!result.ok()) throw std::runtime_error(std::string(message) + ": " + result.error()->message());
}

eve::asset_import::UnityProjectImportRequest sideCubeRequest() {
    using namespace eve;
    using namespace eve::asset_import;
    aiScene source;
    source.mRootNode                           = new aiNode("Root");
    source.mRootNode->mNumChildren             = 1;
    source.mRootNode->mChildren                = new aiNode*[1]{new aiNode("RoadParityCube")};
    source.mRootNode->mChildren[0]->mParent    = source.mRootNode;
    source.mRootNode->mChildren[0]->mNumMeshes = 1;
    source.mRootNode->mChildren[0]->mMeshes    = new unsigned[1]{0};
    source.mNumMeshes                          = 1;
    source.mMeshes                             = new aiMesh*[1]{new aiMesh};
    auto& mesh                                 = *source.mMeshes[0];
    mesh.mName.Set("Cube");
    mesh.mNumVertices        = 36;
    mesh.mVertices           = new aiVector3D[36];
    mesh.mNormals            = new aiVector3D[36];
    mesh.mTextureCoords[0]   = new aiVector3D[36];
    mesh.mNumUVComponents[0] = 2;
    const glm::vec3 corners[]{{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                              {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
    const unsigned  indices[]{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                              1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2};
    mesh.mNumFaces = 12;
    mesh.mFaces    = new aiFace[12];
    for (unsigned face = 0; face < 12; ++face) {
        const auto normal = glm::normalize(glm::cross(corners[indices[face * 3 + 1]] - corners[indices[face * 3]],
                                                      corners[indices[face * 3 + 2]] - corners[indices[face * 3]]));
        for (unsigned vertex = 0; vertex < 3; ++vertex) {
            unsigned   index              = face * 3 + vertex;
            const auto position           = corners[indices[index]];
            mesh.mVertices[index]         = {position.x, position.y, position.z};
            mesh.mNormals[index]          = {normal.x, normal.y, normal.z};
            mesh.mTextureCoords[0][index] = {float(vertex == 1), float(vertex == 2), 0};
        }
        mesh.mFaces[face].mNumIndices = 3;
        mesh.mFaces[face].mIndices    = new unsigned[3]{face * 3, face * 3 + 1, face * 3 + 2};
    }
    source.mNumMaterials = 1;
    source.mMaterials    = new aiMaterial*[1]{new aiMaterial};
    Assimp::Exporter exporter;
    const auto*      blob = exporter.ExportToBlob(&source, "fbxa");
    sideRequire(blob != nullptr, "cube FBX export failed");
    UnityProjectImportRequest request;
    request.package   = {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041"), "road.side.parity", "1.0.0", {}};
    const auto* bytes = static_cast<const std::uint8_t*>(blob->data);
    request.files["Assets/cube.fbx"] = {bytes, bytes + blob->size};
    auto put = [&](const char* path, const std::string& text) { request.files[path] = {text.begin(), text.end()}; };
    put("Assets/cube.fbx.meta",
        "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nModelImporter:\n  internalIDToNameTable: []\n  meshes:\n    "
        "globalScale: 100\n    fileIdsGeneration: 2\n    useFileScale: 1\n    swapUVChannels: 0\n"
        "  tangentSpace:\n    normalImportMode: 0\n");
    UnitySourceAsset fbx{
        "Assets/cube.fbx", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Model, "ModelImporter", {}};
    auto preview = prepareUnityFbx(request, fbx);
    sideResult(preview, "cube FBX preparation failed");
    sideRequire(!preview.value().sourceMappings.empty(), "cube mesh identity missing");
    put("Assets/cube.mat.meta", "guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nNativeFormatImporter:\n");
    put("Assets/cube.mat",
        "--- !u!21 &2100000\nMaterial:\n  m_Shader: {fileID: 46, guid: 0000000000000000f000000000000000, type: 0}\n  "
        "m_SavedProperties:\n    m_Colors:\n    - _Color: {r: 1, g: 0, b: 1, a: 1}\n");
    const auto rotation = glm::angleAxis(glm::radians(20.f), glm::vec3(0, 1, 0)) *
                          glm::angleAxis(glm::radians(10.f), glm::vec3(1, 0, 0)) *
                          glm::angleAxis(glm::radians(30.f), glm::vec3(0, 0, 1));
    std::ostringstream prefab;
    prefab.precision(9);
    prefab << "--- !u!1 &1\nGameObject:\n  m_Name: contract-source\n  m_IsActive: 1\n"
              "--- !u!4 &4\nTransform:\n  m_GameObject: {fileID: 1}\n  m_Father: {fileID: 0}\n"
              "  m_LocalPosition: {x: 2, y: 3, z: 4}\n  m_LocalRotation: {x: "
           << rotation.x << ", y: " << rotation.y << ", z: " << rotation.z << ", w: " << rotation.w
           << "}\n"
              "  m_LocalScale: {x: 2, y: 3, z: 4}\n--- !u!33 &33\nMeshFilter:\n  m_GameObject: {fileID: 1}\n"
              "  m_Mesh: {fileID: "
           << preview.value().sourceMappings.front().sourceObject
           << ", guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, type: 3}\n"
              "--- !u!23 &23\nMeshRenderer:\n  m_GameObject: {fileID: 1}\n  m_Enabled: 1\n"
              "  m_Materials:\n  - {fileID: 2100000, guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb, type: 2}\n";
    put("Assets/cube.prefab", prefab.str());
    put("Assets/cube.prefab.meta", "guid: cccccccccccccccccccccccccccccccc\nPrefabImporter:\n");
    return request;
}

int runRoadSideParity(const std::filesystem::path& prefix, float nativeYawDegrees, int shape) {
    using namespace eve;
    auto imported = asset_import::prepareUnityProjectImport(sideCubeRequest());
    sideResult(imported, "cube Unity import failed");
    auto encoded = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    sideRequire(encoded.ok(), "cube EVA archive failed");
    auto archive = asset::parseEvaArchive(encoded.value());
    sideRequire(archive.ok(), "cube EVA decode failed");
    asset::AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, asset::CookPublication::LocalInspection, 16};
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile);
    sideRequire(cooked.ok(), "cube cook failed");
    auto parsed = asset::parseEvpack(cooked.value().bytes);
    sideRequire(parsed.ok(), "cube evpack decode failed");
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(parsed).takeValue()));
    auto*                       testWindow = window::Window::create();
    auto*                       gfx        = graphics::Graphics::create();
    window::WindowSettings      settings;
    settings.width  = 640;
    settings.height = 480;
    sideRequire(testWindow->setWindowSettings(settings), "cube window creation failed");
    gfx->setScreenReadbackEnabled(true);
    // Unity's reference project uses Gamma color space with no tone mapping.
    // Convert its display-referred background into native linear HDR input;
    // disable the native default ACES transform rather than fitting RGB values.
    sideResult(gfx->setSceneToneMapping(graphics::Graphics::SceneToneMapping::None),
               "unlit tone-map calibration failed");
    gfx->setSceneExposure(1.f);
    const float linearBackground = std::pow((.125f + .055f) / 1.055f, 2.4f);
    gfx->setBackgroundColor(graphics::Color(linearBackground, linearBackground, linearBackground, 1));
    for (const char* feature : {"shadow", "gbuffer", "ao", "gi", "aa", "atmosphere", "msaa", "visResolve", "gpuDriven"})
        gfx->getRenderControl()->disable(feature);
    auto* camera = graphics::Camera3D::createCamera();
    camera->setFov(45);
    camera->setClipPlanes(.1f, 250);
    camera->setTarget(0, 0, -15);
    camera->setAmbient(1, 1, 1);
    asset_graphics::GraphicsMeshFactoryAdapter  meshes(*gfx);
    asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    const asset::EvpackCapabilities caps{"win32", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                            loaded = asset_graphics::EvpackStaticPrefab::load(
        reader, meshes, images, imported.value().manifest.entrypoints.at("Assets/cube.prefab"), caps);
    sideRequire(loaded.ok(), "cube GPU loading failed");
    auto prefab = std::shared_ptr<asset_graphics::EvpackStaticPrefab>(std::move(loaded).takeValue());
    auto bounds = prefab->localBounds(PrefabRootPose::ReplaceTranslationRotation);
    sideRequire(bounds.ok(), "cube placed bounds missing");
    const std::array<float, 6> expected{-1, -1.5f, -2, 1, 1.5f, 2};
    for (unsigned i = 0; i < 6; ++i)
        sideRequire(std::fabs(bounds.value()[i] - expected[i]) < 1e-4f, "cube source geometry differs");
    auto* sceneOwner = scene::Scene::create();
    sideRequire(sceneOwner != nullptr, "Scene owner missing");
    auto* previous = cap::query<IProcgenSceneSink>();
    sideRequire(previous != nullptr, "Scene sink missing");
    asset_graphics::ProcgenPrefabSceneSink sink(*gfx, *previous);
    sideRequire(sink.admit(prefab).ok(), "cube admission failed");
    struct Restore {
        IProcgenSceneSink* value;
        ~Restore() { cap::provide<IProcgenSceneSink>(value); }
    } restore{previous};
    cap::provide<IProcgenSceneSink>(&sink);
    procgen::Procgen proc;
    auto             handle = proc.newPointSetHandle();
    sideRequire(handle.ok(), "cube PointSet allocation failed");
    auto points = proc.resolvePointSet(handle.value());
    sideRequire(points.isBound(), "cube PointSet missing");
    asset_graphics::PrefabBoundsCatalog boundsCatalog;
    sideResult(boundsCatalog.add(*prefab), "cube bounds admission failed");
    struct RestoreBounds {
        IProcgenAssetBounds* previous = cap::query<IProcgenAssetBounds>();
        ~RestoreBounds() { cap::provide<IProcgenAssetBounds>(previous); }
    } restoreBounds;
    cap::provide<IProcgenAssetBounds>(&boundsCatalog);
    procgen::road::RoadNetwork network;
    auto                       a = network.addNode(0, 0, 0), b = network.addNode(0, shape == 1 ? 6.f : 0.f, -30);
    sideResult(a, "road start failed");
    sideResult(b, "road end failed");
    procgen::road::RoadStyle style;
    style.laneWidth              = 3;
    style.automaticStructures    = false;
    style.sidePlacement          = {procgen::road::RoadSideReference::Center, 0, .02f, nativeYawDegrees};
    style.sideObjectDistribution = procgen::road::RoadSideDistribution::Average;
    style.sideTerrain.asset      = procgen::road::RoadSideAssetBinding{prefab->sourceAsset(), prefab->sourceBuildId()};
    if (shape != 0) {
        style.sideTerrain.alignment      = procgen::road::RoadSideAlignment::PathForward;
        style.sidePlacement.pathSampling = procgen::road::RoadSidePathSampling::SurfaceSegments;
    }
    sideResult(network.addEdge(
                   a.value(), b.value(),
                   {{0, 0, 0}, {shape == 2 ? 6.f : 0.f, shape == 1 ? 3.f : 0.f, -15}, {0, shape == 1 ? 6.f : 0.f, -30}},
                   1, 1, style),
               "road authoring failed");
    procgen::Params params;
    params.setBool("navigation", false);
    params.setBool("markings", false);
    params.setFloat("sideObjectSpacing", 5);
    params.setFloat("sideObjectClearance", 0);
    sideResult(procgen::road::setRoadRecipeNetwork(params, network), "road recipe capture failed");
    auto baked = procgen::road::bakeRoadNetworkRecipe(params);
    sideResult(baked, "road recipe producer failed");
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        if (baked.value().placements.getStringAttribute(i, "road_role", "") != "side.left") continue;
        sideResult(points->appendPointFrom(baked.value().placements, std::size_t(i)), "road point transfer failed");
    }
    sideRequire(points->getCount() == (shape == 2 ? 8 : 7), "road center producer count differs");
    std::ofstream placementLog(prefix.string() + "-placements.csv");
    placementLog << std::setprecision(9) << "x,y,z,pitch,yaw,roll\n";
    for (const auto& point : points->points())
        placementLog << point.x << ',' << point.y << ',' << point.z << ',' << point.pitch << ',' << point.yaw << ','
                     << point.roll << '\n';
    placementLog.close();
    if (shape != 0)
        std::cout << "ROAD_SIDE_ORIENTATION_COMPLETE shape=" << (shape == 1 ? "grade" : "curve")
                  << " alignment=PathForward pathSampling=SurfaceSegments\n";
    if (shape == 0)
        std::cout << "ROAD_SIDE_PRODUCER_COMPLETE requestedSpacing=5 length=30 mode=Average reference=Center count=7\n";
    if (nativeYawDegrees == -45.f) std::cout << "ROAD_SIDE_YAW_COMPLETE unity=45 native=-45\n";
    sideRequire(proc.publishInstances("road-side-parity", handle.value(), "asset", "").ok(),
                "cube standard publication failed");
    sideRequire(sink.rendererCount("road-side-parity") == (shape == 2 ? 8 : 7), "cube renderer count differs");
    unsigned renderers = 0;
    auto     view      = ecs::View<graphics::Renderable3D, graphics::Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [renderer] = *it;
        if (!renderer->visible) continue;
        sideRequire(renderer->material != nullptr, "cube material missing");
        renderer->material->setTint(1, 0, 1, 1);
        renderer->material->setShadingModel("unlit");
        renderer->material->setReceiveLight(false);
        ++renderers;
    }
    sideRequire(renderers == (shape == 2 ? 8u : 7u), "unexpected fixture renderers");
    for (bool top : {false, true}) {
        camera->setEye(top ? 0.f : 24.f, top ? 50.f : 18.f, top ? -15.f : -40.f);
        camera->setUp(0, top ? 0.f : 1.f, top ? -1.f : 0.f);
        for (int frame = 0; frame < 4; ++frame) {
            graphics::RenderSystem3D::render(*gfx);
            graphics::RenderSystem::render(*gfx);
        }
        std::vector<std::uint8_t> pixels;
        unsigned                  colored = 0;
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                const auto color = gfx->getPixel(x, y);
                colored += color.r > color.g + .2f && color.b > color.g + .2f;
                for (float channel : {color.r, color.g, color.b, color.a})
                    pixels.push_back(std::uint8_t(std::lround(std::clamp(channel, 0.f, 1.f) * 255.f)));
            }
        sideRequire(colored > 100, "cube frame is empty");
        auto png = asset::detail::encodeSourcePng(640, 480, pixels, 4 * 1024 * 1024);
        sideRequire(png.ok(), "cube PNG encoding failed");
        std::ofstream output(prefix.string() + (top ? "-top.png" : "-perspective.png"), std::ios::binary);
        output.write(reinterpret_cast<const char*>(png.value().data()), std::streamsize(png.value().size()));
        sideRequire(output.good(), "cube PNG write failed");
    }
    sideRequire(proc.removeInstances("road-side-parity").ok(), "cube removal failed");
    sideRequire(sink.collectRetired().ok(), "cube retirement failed");
    sideRequire(sink.release().ok(), "cube sink release failed");
    sideRequire(proc.releasePointSet(handle.value()).ok(), "cube PointSet release failed");
    sideRequire(prefab->release().ok(), "cube GPU release failed");
    testWindow->close();
    std::cout << "ROAD_SIDE_PARITY_COMPLETE renderers=" << renderers << " scope=calibrated-road-recipe-rigid-prefab\n";
    return 0;
}
}  // namespace
