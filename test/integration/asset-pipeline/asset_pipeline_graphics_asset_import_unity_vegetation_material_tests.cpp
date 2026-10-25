// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "asset-pipeline",
  "cases": {
    "asset-files": [
      "asset.import.unityFbxCharacterProducesSkinAndAnimation"
    ],
    "render-offscreen": [
      "asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad",
      "graphics.vegetation.translucency_gpu_backlight_and_ambient"
    ],
    "render-window": [
      "asset.procgen.terrainDetailSidecarPublishesDecodableRuntimeResource",
      "asset.import.unityStaticPrefabRendersOwnedEcsInstances"
    ]
  },
  "module": "asset_import",
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
    "asset_import_unity_fbx.cpp",
    "asset_import_unity_native.cpp",
    "asset_import_unity_package.cpp",
    "asset_import_unity_sprite.cpp",
    "asset_import_unity_vegetation_material.cpp",
    "asset_mesh_attributes.cpp",
    "asset_mesh_uv.cpp",
    "asset_migration.cpp",
    "asset_procgen_terrain_detail_render.cpp",
    "asset_scene_bindings.cpp",
    "asset_shader.cpp",
    "graphics_vegetation_translucency.cpp",
    "medialoader_model_link.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_fbx)
#include <assimp/scene.h>
#include <algorithm>
#include <assimp/Exporter.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include "PathBesideSource.h"
#include "PrivateTestScope.h"
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/SourcePng.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/graphics/PrefabBoundsCatalog.h"
#include "asset/graphics/ProcgenPrefabSceneSink.h"
#include "asset/graphics/StaticPrefabRenderInstance.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "common/Capability.h"
#include "common/ProcgenWorldQuery.h"
#include "graphics/Graphics.h"
#include "graphics/IImageResourceFactory.h"
#include "graphics/IMeshResourceFactory.h"
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
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_native)
#include "PrivateTestScope.h"
#include <bit>
#include <cmath>
#include "asset/AssetCooker.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "graphics/IImageResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_package)
#include "PrivateTestScope.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include "asset/import/UnitySource.h"
#include "asset_import/UnityPackageFixture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_sprite)
#include "PrivateTestScope.h"
#include <algorithm>
#include <limits>
#include "asset/AssetCooker.h"
#include "asset/SpriteAnimation.h"
#include "asset/import/UnityImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_import_unity_vegetation_material)
#include "PrivateTestScope.h"
#include <cmath>
#include "asset/AssetCooker.h"
#include "asset/CanonicalImageCook.h"
#include "asset/SourcePng.h"
#include "asset/graphics/CookedMaterial.h"
#include "asset/import/UnityImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_mesh_attributes)
#include "PrivateTestScope.h"
#include <bit>
#include <cmath>
#include <limits>
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/EvpackGraphicsLoader.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/VegetationAsset.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/import/UnityImporter.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/import/UnitySourceInternal.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "graphics/Graphics.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "graphics/Mesh.h"
#endif
#ifdef EVE_TEST_MESH_UPLOAD
#include "graphics/VegetationMotion.h"
#endif
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_mesh_attributes.cpp
namespace test_asset_mesh_attributes {
#ifdef EVE_TEST_MESH_UPLOAD


#endif


using namespace eve;
namespace {
asset::CanonicalMeshData triangle() {
    asset::CanonicalMeshData m;
    m.positions                = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    m.indices                  = {0, 1, 2};
    m.texcoords[0]             = {0, 0, 1, 0, 0, 1};
    m.attributes["_UNITY_UV0"] = {4, {0, 0, 4194303, 1048063, 1, 0, 0, 1, 0, 1, 2, 3}};
    m.attributes["COLOR_0"]    = {4, {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1}};
    return m;
}
#ifdef EVE_TEST_MESH_UPLOAD
asset_import::UnityProjectImportRequest unityMesh() {
    asset_import::UnityProjectImportRequest r;
    r.package = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "vegetation.test", "1.0.0", {}};
    std::string text =
        "%YAML 1.1\n--- !u!43 &4300000\nMesh:\n  serializedVersion: 11\n"
        "  m_MeshCompression: 0\n  m_BindPose: []\n  m_SubMeshes:\n"
        "  - serializedVersion: 2\n    firstByte: 0\n    indexCount: 3\n    topology: 0\n    baseVertex: 0\n"
        "  m_IndexFormat: 0\n  m_IndexBuffer: 000001000200\n  m_VertexData:\n    m_VertexCount: 3\n    m_Channels:\n";
    const unsigned dimensions[] = {3, 3, 4, 4, 4, 4, 0, 4, 0, 0, 0, 0, 0, 0};
    unsigned       offset       = 0;
    for (auto d : dimensions) {
        text += "    - stream: 0\n      offset: " + std::to_string(d ? offset : 0) +
                "\n      format: 0\n      dimension: " + std::to_string(d) + "\n";
        offset += d * 4;
    }
    std::string hex;
    const char* digits = "0123456789abcdef";
    for (unsigned v = 0; v < 3; ++v) {
        const float values[] = {float(v), 1,   2,   0,       1,       0,   1,   0,   0,   1, .25f, .5f, .75f,
                                1,        .2f, .3f, 4194303, 1048063, .4f, .6f, .8f, .9f, 2, 3,    4,   0};
        for (auto f : values) {
            auto bits = std::bit_cast<uint32_t>(f);
            for (unsigned j = 0; j < 4; ++j) {
                const auto b = uint8_t(bits >> (j * 8));
                hex += digits[b >> 4];
                hex += digits[b & 15];
            }
        }
    }
    text += "    m_DataSize: " + std::to_string(offset * 3) + "\n    _typelessdata: " + hex +
            "\n  m_StreamData:\n    size: 0\n";
    for (auto c : text) {
        if (c == '\n') {
            r.files["Assets/plant.asset"].push_back('\r');
            r.files["Assets/plant.asset"].push_back('\r');
        }
        r.files["Assets/plant.asset"].push_back(uint8_t(c));
    }
    return r;
}
#endif
}  // namespace
TEST_CASE("asset.mesh.attributeCodecPreservesPackedBitsAndRejectsMalformedDescriptors") {
    auto m       = triangle();
    auto encoded = asset::encodeCanonicalMesh(m);
    REQUIRE(encoded.ok());
    REQUIRE_EQ(encoded.value()[7], uint8_t(3));
    auto decoded = asset::decodeCanonicalMesh(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().attributes.at("_UNITY_UV0").values, m.attributes.at("_UNITY_UV0").values);
    REQUIRE_EQ(decoded.value().attributes.at("COLOR_0").components, 4u);
    auto bad     = encoded.value();
    bad[32 + 68] = 1;  // Reserved descriptor word.
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad          = encoded.value();
    bad[32 + 64] = 5;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad         = encoded.value();
    bad[32 + 8] = 'X';  // Nonzero bytes after semantic terminator.
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad = encoded.value();
    bad.pop_back();
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    REQUIRE(!asset::decodeCanonicalMesh(encoded.value(), {3, 3, 64}).ok());
    m.attributes.at("_UNITY_UV0").values[2] = std::numeric_limits<float>::infinity();
    REQUIRE(!asset::encodeCanonicalMesh(m).ok());
    m.attributes.clear();
    auto legacy = asset::encodeCanonicalMesh(m);
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value()[7], uint8_t(2));
    REQUIRE(asset::decodeCanonicalMesh(legacy.value()).ok());
}
#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad") {
    auto                           request = unityMesh();
    asset_import::UnitySourceAsset source;
    source.path   = "Assets/plant.asset";
    source.guid   = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto imported = asset_import::prepareUnityNativeMesh(request, source);
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
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    class Factory               final : public graphics::IMeshResourceFactory {
    public:
        int                token = 0, vertices = 0, indices = 0;
        float              z = 0, t = 0;
        uint32_t           secondIndex    = 0;
        bool               tangentSupport = true;
        int                releases       = 0;
        std::vector<float> tangentFrame, bitangentFrame;
        Result<void>       setMeshTangentFrame(graphics::Mesh* mesh, std::span<const float> t,
                                                             std::span<const float> b) override {
            if (!tangentSupport) return IMeshResourceFactory::setMeshTangentFrame(mesh, t, b);
            tangentFrame.assign(t.begin(), t.end());
            bitangentFrame.assign(b.begin(), b.end());
            return Result<void>::success();
        }
        Result<graphics::Mesh*> uploadMesh(const float* p, const float*, const float* uv, int n, const uint32_t* index,
                                                         int count) override {
            vertices    = n;
            indices     = count;
            z           = p[2];
            t           = uv[1];
            secondIndex = index[1];
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } factory;
    auto loaded = asset_graphics::EvpackGraphicsLoader(reader, factory)
                      .loadMesh(imported.value().manifest.assets.front().asset, caps);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(factory.vertices, 3);
    REQUIRE_EQ(factory.indices, 3);
    REQUIRE_EQ(factory.z, -2.f);
    REQUIRE_EQ(factory.t, .7f);
    REQUIRE_EQ(factory.secondIndex, 2u);
    REQUIRE_EQ(factory.tangentFrame.size(), size_t(9));
    REQUIRE_EQ(factory.bitangentFrame.size(), size_t(9));
    REQUIRE_EQ(factory.tangentFrame[0], 1.f);
    REQUIRE_EQ(factory.bitangentFrame[2], 1.f);
    const auto& a = loaded.value().attributes;
    REQUIRE_EQ(a.at("_UNITY_UV0").components, 4u);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[1], .3f);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[2], 4194303.f);
    REQUIRE_EQ(a.at("_UNITY_UV0").values[3], 1048063.f);
    REQUIRE_EQ(a.at("_UNITY_UV3").values[2], 4.f);
    REQUIRE_EQ(a.at("COLOR_0").values[2], .75f);
    REQUIRE_EQ(a.at("TANGENT").values[3], -1.f);
    REQUIRE(factory.releaseMesh(loaded.value().mesh).ok());
    factory.tangentSupport = false;
    auto unsupported       = asset_graphics::EvpackGraphicsLoader(reader, factory)
                           .loadMesh(imported.value().manifest.assets.front().asset, caps);
    REQUIRE(!unsupported.ok());
    REQUIRE_EQ(factory.releases, 2);
    auto vegetation =
        asset_graphics::VegetationAsset::load(reader, imported.value().manifest.assets.front().asset, caps, {});
    REQUIRE(vegetation.ok());
    REQUIRE_EQ(vegetation.value()->vertexCount(), 3u);
    graphics::VegetationField  field;
    graphics::VegetationMotion motion;
    motion.bending = 0;
    motion.branch  = 0;
    motion.rolling = 0;
    motion.flutter = 0;
    auto rest      = vegetation.value()->evaluate(field, motion);
    REQUIRE(rest.ok());
    REQUIRE_EQ(rest.value().positions[0], 0.f);
    REQUIRE_EQ(rest.value().positions[1], 1.f);
    REQUIRE_EQ(rest.value().positions[2], -2.f);
    motion.bending = .4f;
    auto bent      = vegetation.value()->evaluate(field, motion);
    REQUIRE(bent.ok());
    REQUIRE(bent.value().positions != rest.value().positions);
    const float dx = bent.value().positions[0] - 2.f;
    const float dy = bent.value().positions[1] - 4.f;
    const float dz = bent.value().positions[2] + 3.f;
    REQUIRE(std::abs(dx * dx + dy * dy + dz * dz - 14.f) < 0.0001f);
    motion.bending = 0;
    auto restored  = vegetation.value()->evaluate(field, motion);
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().positions, rest.value().positions);
    auto* gfx = graphics::Graphics::create();
    gfx->initHeadless(16, 16);
    auto gpuMesh = vegetation.value()->createGpuFieldMesh(*gfx);
    REQUIRE(gpuMesh.ok());
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors().size(), size_t(27));
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[0], 2.f);
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[1], 4.f);
    REQUIRE_EQ(gpuMesh.value()->vegetationDeformationFactors()[2], -3.f);
    REQUIRE(gfx->releaseMesh(gpuMesh.value()));
}
TEST_CASE("asset.mesh.vegetationAdmissionRejectsMissingMalformedAndInvalidStreams") {
    auto mesh = triangle();
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh.normals                  = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.attributes["_UNITY_UV1"] = {4, std::vector<float>(12, 0.f)};
    mesh.attributes["_UNITY_UV3"] = {4, std::vector<float>(12, 0.f)};
    auto admitted                 = asset_graphics::VegetationAsset::fromCanonical(mesh);
    REQUIRE(admitted.ok());
    mesh.attributes["_UNITY_UV0"].values[2] = .5f;
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh.attributes["_UNITY_UV0"].values[2] = 4194303.f;
    mesh.normals[0]                         = std::numeric_limits<float>::infinity();
    REQUIRE(!asset_graphics::VegetationAsset::fromCanonical(mesh).ok());
    mesh = {};
    graphics::VegetationField field;
    auto                      geometry = admitted.value()->evaluate(field, {});
    REQUIRE(geometry.ok());
    REQUIRE_EQ(geometry.value().positions.size(), size_t(9));
}
#endif
}  // namespace test_asset_mesh_attributes
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_mesh_uv)
#include "PrivateTestScope.h"
#include <bit>
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/EvpackGraphicsLoader.h"
#endif
// asset_mesh_uv.cpp
namespace test_asset_mesh_uv {
#ifdef EVE_TEST_MESH_UPLOAD

#endif
using namespace eve;
namespace {
asset_import::GltfImportRequest uvMesh() {
    asset_import::GltfImportRequest r;
    r.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "uv.test", "1.0.0", {}};
    r.sourceName = "uv.gltf";
    auto& b      = r.externalResources["data.bin"];
    b.resize(78);
    // Three distinct UV representations, including sparse channel numbers.
    for (size_t i = 0; i < 6; ++i) {
        auto bits = std::bit_cast<uint32_t>(float(i) / 2.f);
        for (size_t k = 0; k < 4; ++k) b[36 + i * 4 + k] = uint8_t(bits >> (k * 8));
        uint16_t value = (i % 2) ? 65535 : 0;
        b[60 + i * 2]  = uint8_t(value);
        b[61 + i * 2]  = uint8_t(value >> 8);
        b[72 + i]      = uint8_t(i * 51);
    }
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data.bin","byteLength":78}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":12},{"buffer":0,"byteOffset":72,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5123,"normalized":true,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5121,"normalized":true,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_2":2,"TEXCOORD_7":3}}]}]})";
    r.documentBytes.assign(json.begin(), json.end());
    return r;
}
std::vector<uint8_t> meshBytes(const asset_import::PreparedAssetImport& imported) {
    for (const auto& e : imported.entries)
        if (e.path.ends_with("mesh.bin")) return e.bytes;
    return {};
}
}  // namespace
TEST_CASE("asset.mesh.multipleUvSetsSurviveImportAndCook") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.front().schemaVersion, SchemaVersion(2));
    auto decoded = asset::decodeCanonicalMesh(meshBytes(imported.value()));
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().texcoords.size(), size_t(3));
    REQUIRE_EQ(decoded.value().texcoords.at(0)[5], 2.5f);
    REQUIRE_EQ(decoded.value().texcoords.at(2)[1], 1.f);
    REQUIRE_EQ(decoded.value().texcoords.at(7)[5], 1.f);
    auto archiveBytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = asset::parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto payload = reader.read(imported.value().manifest.entrypoints.at("default"), "eve.mesh/3", caps, 1024 * 1024);
    REQUIRE(payload.ok());
    bool checked = false;
    for (const auto& c : payload.value().chunks)
        if (c.kind == asset::EvpackChunkKind::Bulk) {
            auto actual = asset::decodeCanonicalMesh(c.bytes);
            REQUIRE(actual.ok());
            REQUIRE_EQ(actual.value().texcoords, decoded.value().texcoords);
            checked = true;
        }
    REQUIRE(checked);
}
TEST_CASE("asset.mesh.rejectsMalformedUvStreamsAndBudgets") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    const auto original = meshBytes(imported.value());
    auto       bad      = original;
    bad[28]             = 0;  // Duplicate UV descriptor (0 instead of 2).
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad = original;
    bad.pop_back();
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad     = original;
    bad[20] = 255;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    bad     = original;
    bad[48] = 0;
    bad[49] = 0;
    bad[50] = 128;
    bad[51] = 127;
    REQUIRE(!asset::decodeCanonicalMesh(bad).ok());
    asset::CanonicalMeshLimits limits;
    limits.maximumDecodedBytes = 32;
    REQUIRE(!asset::decodeCanonicalMesh(original, limits).ok());
    auto request                       = uvMesh();
    request.limits.maximumDecodedBytes = 32;
    REQUIRE(!asset_import::prepareGltfImport(request).ok());
}
#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.mesh.graphicsSelectsUvSetWithoutSilentRemap") {
    auto imported = asset_import::prepareGltfImport(uvMesh());
    REQUIRE(imported.ok());
    auto bytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    class Factory               final : public graphics::IMeshResourceFactory {
    public:
        int                                    calls = 0, token = 0, releases = 0;
        std::vector<float>                     uv;
        bool                                   multiUv = false;
        std::map<uint32_t, std::vector<float>> channels;
        Result<void> setMeshTexcoords(graphics::Mesh* mesh, uint32_t set, std::span<const float> values) override {
            if (!multiUv) return IMeshResourceFactory::setMeshTexcoords(mesh, set, values);
            channels[set] = {values.begin(), values.end()};
            return Result<void>::success();
        }
        Result<graphics::Mesh*> uploadMesh(const float*, const float*, const float* t, int n, const uint32_t*,
                                                         int) override {
            ++calls;
            uv.assign(t, t + n * 2);
            return Result<graphics::Mesh*>::success(reinterpret_cast<graphics::Mesh*>(&token));
        }
        Result<void> releaseMesh(graphics::Mesh*) override {
            ++releases;
            return Result<void>::success();
        }
    } factory;
    asset_graphics::EvpackGraphicsLoader loader(reader, factory);
    const auto                           ref  = imported.value().manifest.entrypoints.at("default");
    auto                                 mesh = loader.loadMesh(ref, caps, {}, 2);
    REQUIRE(mesh.ok());
    REQUIRE_EQ(factory.uv, std::vector<float>({0, 1, 0, 1, 0, 1}));
    REQUIRE_EQ(mesh.value().texcoordSet, uint32_t(2));
    REQUIRE(factory.releaseMesh(mesh.value().mesh).ok());
    auto missing = loader.loadMesh(ref, caps, {}, 1);
    REQUIRE(!missing.ok());
    REQUIRE_EQ(factory.calls, 1);
    auto absent = loader.loadMesh(ref, caps, {}, 0, true);
    REQUIRE(!absent.ok());
    REQUIRE_EQ(factory.releases, 2);
    factory.multiUv = true;
    auto complete   = loader.loadMesh(ref, caps, {}, 0, true);
    REQUIRE(complete.ok());
    REQUIRE_EQ(factory.channels.size(), size_t(2));
    REQUIRE_EQ(factory.channels.at(2), std::vector<float>({0, 1, 0, 1, 0, 1}));
    REQUIRE(factory.releaseMesh(complete.value().mesh).ok());
}
#endif
TEST_CASE("asset.mesh.legacyBinaryDecodesToCanonicalUvSets") {
    auto request = uvMesh();
    auto doc     = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(doc.ok());
    auto& attrs = *doc.value()
                       .getIf<Value::Object>()
                       ->at("meshes")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("primitives")
                       .getIf<Value::Array>()
                       ->at(0)
                       .getIf<Value::Object>()
                       ->at("attributes")
                       .getIf<Value::Object>();
    attrs.erase("TEXCOORD_2");
    attrs.erase("TEXCOORD_7");
    auto json = doc.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = asset_import::prepareGltfImport(request);
    REQUIRE(imported.ok());
    auto bytes = meshBytes(imported.value());
    bytes[7]   = 1;
    bytes[16]  = 2;
    bytes[20]  = 0;
    bytes.erase(bytes.begin() + 24, bytes.begin() + 28);
    auto legacy = asset::decodeCanonicalMesh(bytes);
    REQUIRE(legacy.ok());
    REQUIRE_EQ(legacy.value().texcoords.size(), size_t(1));
    REQUIRE_EQ(legacy.value().texcoords.at(0)[5], 2.5f);
    bytes[7] = 3;
    REQUIRE(!asset::decodeCanonicalMesh(bytes).ok());
}
TEST_CASE("asset.mesh.rejectsInvalidSourceUvAccessors") {
    for (int kind = 0; kind < 3; ++kind) {
        auto request  = uvMesh();
        auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
        REQUIRE(document.ok());
        auto& root = *document.value().getIf<Value::Object>();
        auto& uv   = *root.at("accessors").getIf<Value::Array>()->at(2).getIf<Value::Object>();
        if (kind == 0) uv["normalized"] = Value(false);
        if (kind == 1) uv["count"] = Value(int64_t(2));
        if (kind == 2) {
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
            attrs["TEXCOORD_4294967296"] = Value(int64_t(2));
        }
        auto json = document.value().toJson();
        REQUIRE(json.ok());
        request.documentBytes.assign(json.value().begin(), json.value().end());
        REQUIRE(!asset_import::prepareGltfImport(request).ok());
    }
}

TEST_CASE("asset.mesh.encoderRoundTripsCanonicalVersion3WithVertexColors") {
    asset::CanonicalMeshData source;
    source.positions    = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    source.normals      = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    source.texcoords[0] = {0, 0, 1, 0, 0, 1};
    source.colors       = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0.5f};
    source.indices      = {0, 1, 2};
    auto encoded        = asset::encodeCanonicalMesh(source);
    REQUIRE(encoded.ok());
    auto decoded = asset::decodeCanonicalMesh(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().positions, source.positions);
    CHECK_EQ(decoded.value().normals, source.normals);
    CHECK_EQ(encoded.value()[7], std::uint8_t(3));
    CHECK_EQ(decoded.value().texcoords, source.texcoords);
    CHECK_EQ(decoded.value().colors, source.colors);
    CHECK_EQ(decoded.value().indices, source.indices);
    auto invalid = source;
    invalid.texcoords[0].pop_back();
    REQUIRE(!asset::encodeCanonicalMesh(invalid).ok());
    invalid = source;
    invalid.colors.pop_back();
    REQUIRE(!asset::encodeCanonicalMesh(invalid).ok());
    invalid            = source;
    invalid.indices[2] = 3;
    REQUIRE(!asset::encodeCanonicalMesh(invalid).ok());
}
}  // namespace test_asset_mesh_uv
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_migration)
#include "PrivateTestScope.h"
#include "asset/AssetMigration.h"
#include "data/HashFunction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_migration.cpp
namespace test_asset_migration {
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
    REQUIRE_EQ(migrated.value().manifest.assets.front().schemaVersion, SchemaVersion(3));
    REQUIRE_EQ(migrated.value().manifest.dependencies.front().expectedType, std::string("eve.mesh/3"));
    auto value = Value::fromJson(
        std::string(migrated.value().entries.front().bytes.begin(), migrated.value().entries.front().bytes.end()));
    REQUIRE(value.ok());
    const auto& object = *value.value().getIf<Value::Object>();
    REQUIRE(!object.contains("texcoord0"));
    REQUIRE_EQ(object.at("texcoordSets").getIf<Value::Array>()->at(0).asInt(), int64_t(0));
    REQUIRE_EQ(object.at("vendorExtension").asInt(), int64_t(42));
    REQUIRE(!object.at("colors").asBool());
    REQUIRE_EQ(object.at("schemaVersion").asInt(), int64_t(3));
    REQUIRE_EQ(original.entries.front().bytes, std::vector<uint8_t>(text.begin(), text.end()));
}
}  // namespace test_asset_migration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_procgen_terrain_detail_render)
#include "PrivateTestScope.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_scene_bindings)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include "asset/RuntimeDefinition.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_shader)
#include "PrivateTestScope.h"
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/ShaderAsset.h"
#include "asset/import/ShaderImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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

TEST_CASE("asset.shader decodes owning parameter and stage data") {
    auto result = decodeShaderAsset(shaderDefinition());
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().parameters.size(), std::size_t(1));
    REQUIRE_EQ(result.value().parameters[0].name, std::string("strength"));
    REQUIRE_EQ(result.value().parameters[0].defaults[0], 1.f);
    REQUIRE_EQ(result.value().vertex.size(), std::size_t(10));
}

TEST_CASE("asset.shader rejects future versions unknown fields and wrong stages") {
    auto  definition        = shaderDefinition();
    auto& fields            = *definition.getIf<Value::Object>();
    fields["schemaVersion"] = Value(std::int64_t(2));
    REQUIRE(!decodeShaderAsset(definition).ok());
    fields["schemaVersion"] = Value(std::int64_t(1));
    fields["unexpected"]    = Value(true);
    REQUIRE(!decodeShaderAsset(definition).ok());
    fields.erase("unexpected");
    fields["fragment"] = fields["vertex"];
    REQUIRE(!decodeShaderAsset(definition).ok());
}

TEST_CASE("asset.shader rejects duplicate parameters and exhausted ABI") {
    auto  definition = shaderDefinition();
    auto& fields     = *definition.getIf<Value::Object>();
    auto& parameters = *fields["parameters"].getIf<Value::Array>();
    parameters.push_back(parameters.front());
    REQUIRE(!decodeShaderAsset(definition).ok());
    parameters.clear();
    for (int i = 0; i < 33; ++i)
        parameters.emplace_back(
            Value::Object{{"name", Value("p" + std::to_string(i))}, {"default", Value(Value::Array{Value(0.0)})}});
    REQUIRE(!decodeShaderAsset(definition).ok());
}

TEST_CASE("asset.shader rejects truncated instructions and byte budgets") {
    auto              definition = shaderDefinition();
    ShaderAssetLimits limits;
    limits.maximumStageWords = 9;
    REQUIRE(!decodeShaderAsset(definition, limits).ok());
    auto& words = *definition.getIf<Value::Object>()->at("vertex").getIf<Value::Array>();
    words.pop_back();
    REQUIRE(!decodeShaderAsset(definition).ok());
}

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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_translucency)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <memory>
#include "asset/AssetCooker.h"
#include "asset/graphics/CookedMaterial.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/import/UnityImporter.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation_translucency.cpp
namespace test_graphics_vegetation_translucency {
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
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_medialoader_model_link)
#include "PrivateTestScope.h"
#include <assimp/scene.h>
#include "medialoader/model/ModelLoader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// medialoader_model_link.cpp
namespace test_medialoader_model_link {
TEST_CASE("MedialoaderModel.linkHeaders") {
    medialoader::ModelLoader loader;
    CHECK(sizeof(aiScene) > 0);
    CHECK(sizeof(loader) > 0);
}
}  // namespace test_medialoader_model_link
#endif
