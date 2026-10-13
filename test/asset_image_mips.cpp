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
