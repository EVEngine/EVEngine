#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "graphics/IImageResourceFactory.h"
#include "graphics/IMeshResourceFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset_import;

namespace {
UnityProjectImportRequest triangleFbx() {
    aiScene scene;
    scene.mRootNode                           = new aiNode("Root");
    scene.mRootNode->mNumChildren             = 1;
    scene.mRootNode->mChildren                = new aiNode*[1]{new aiNode("GroundTileModular.002")};
    scene.mRootNode->mChildren[0]->mParent    = scene.mRootNode;
    scene.mRootNode->mChildren[0]->mNumMeshes = 1;
    scene.mRootNode->mChildren[0]->mMeshes    = new unsigned[1]{0};
    scene.mNumMeshes                          = 1;
    scene.mMeshes                             = new aiMesh*[1]{new aiMesh};
    auto& mesh                                = *scene.mMeshes[0];
    mesh.mName.Set("Geometry");
    mesh.mNumVertices          = 3;
    mesh.mVertices             = new aiVector3D[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    mesh.mNormals              = new aiVector3D[3]{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
    mesh.mTextureCoords[0]     = new aiVector3D[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.mNumUVComponents[0]   = 2;
    mesh.mNumFaces             = 1;
    mesh.mFaces                = new aiFace[1];
    mesh.mFaces[0].mNumIndices = 3;
    mesh.mFaces[0].mIndices    = new unsigned[3]{0, 1, 2};
    scene.mNumMaterials        = 1;
    scene.mMaterials           = new aiMaterial*[1]{new aiMaterial};
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

TEST_CASE("asset.import.unityStaticPrefabResolvesAndReleasesGpuLeases") {
    auto request = triangleFbx();
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
    const auto                ref    = prepared.value().manifest.entrypoints.at("Assets/tile.prefab");
    auto                      loaded = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value()->drawCount(), std::size_t(1));
    REQUIRE_EQ(meshes.uploads, 1);
    meshes.reject = true;
    auto failed   = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(!failed.ok());
    REQUIRE_EQ(loaded.value()->drawCount(), std::size_t(1));
    meshes.reject      = false;
    images.reject      = true;
    auto textureFailed = asset_graphics::EvpackStaticPrefab::load(reader, meshes, images, ref, caps);
    REQUIRE(!textureFailed.ok());
    REQUIRE_EQ(meshes.releases, 1);
    REQUIRE_EQ(loaded.value()->drawCount(), std::size_t(1));
    auto released = loaded.value()->release();
    REQUIRE(released.ok());
    REQUIRE_EQ(meshes.releases, 2);
    REQUIRE_EQ(images.releases, 1);
    REQUIRE_EQ(loaded.value()->drawCount(), std::size_t(0));
}
}  // namespace

TEST_CASE("asset.import.unityFbxHashesNodeNameAndProducesCanonicalMesh") {
    auto             request = triangleFbx();
    UnitySourceAsset source{
        "Assets/tile.fbx", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", UnitySourceKind::Model, "ModelImporter", {}};
    auto prepared = prepareUnityFbx(request, source);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(1));
    REQUIRE_EQ(prepared.value().sourceMappings.front().sourceObject, std::string("-813904291765591093"));
    auto repeated = prepareUnityFbx(request, source);
    REQUIRE(repeated.ok());
    REQUIRE(prepared.value().sourceMappings.front().asset == repeated.value().sourceMappings.front().asset);
}

TEST_CASE("asset.import.unityFbxRejectsCorruptionAndLegacyIdentityWithoutGuessing") {
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
    auto legacy = prepareUnityFbx(request, source);
    REQUIRE(!legacy.ok());
    REQUIRE(legacy.error()->code() == DiagnosticCode::Unsupported);
}
