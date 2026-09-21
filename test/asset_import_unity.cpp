#include "asset/AssetCooker.h"
#include "asset/AssetMigration.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/import/TerrainImporter.h"
#include "asset/import/UnityImporter.h"
#include "asset/procgen/EvpackInstanceSetLoader.h"
#include "asset/procgen/EvpackTerrainLoader.h"
#include "asset/procgen/EvpackTerrainMaterialLoader.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorDocumentService.h"
#include "procgen/editing/HeightmapTarget.h"
#include "procgen/heightmap_target/TerrainDocumentCodec.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    request.package         = unityIdentity();
    request.terrainDataPath = "Assets/Terrain.asset";
    const std::string adjacentDetails = request.terrainDataPath + ".eve-details.json";
    request.files              = {
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
    const auto expectedResource = AssetRef::fromId(
        unityIdentity().packageId.child("unity-prefab:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
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
    auto request = UnityProjectImportRequest{unityIdentity()};
    constexpr std::string_view scene = R"yaml(%YAML 1.1
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
    request.files["Assets/Demo.unity"] = bytes(scene);
    request.files["Assets/Demo.unity.meta"] = bytes("fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    auto imported = prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    REQUIRE_EQ(imported.value().manifest.assets.size(), std::size_t(2));
    CHECK_EQ(imported.value().manifest.assets.front().type, std::string("eve.vegetation-scene"));
    CHECK(std::any_of(imported.value().manifest.assets.begin(), imported.value().manifest.assets.end(),
                      [](const auto& asset) { return asset.type == "eve.scene-template"; }));
    CHECK(std::any_of(imported.value().manifest.dependencies.begin(), imported.value().manifest.dependencies.end(),
                      [](const auto& dependency) { return dependency.path == "vegetationScene"; }));
    const auto vegetationAsset = imported.value().manifest.assets.front().asset.id().format();
    auto definition = std::find_if(imported.value().entries.begin(), imported.value().entries.end(),
                                   [&](const auto& entry) {
                                       return entry.path == "assets/" + vegetationAsset + "/asset.json";
                                   });
    REQUIRE(definition != imported.value().entries.end());
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(definition->bytes.data()),
                                                   definition->bytes.size()));
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

    auto legacyScene = std::string(scene);
    const auto legacyBegin = legacyScene.find("  renderColors:");
    const auto legacyEnd = legacyScene.find("--- !u!4 &32", legacyBegin);
    REQUIRE(legacyBegin != std::string::npos);
    REQUIRE(legacyEnd != std::string::npos);
    legacyScene.replace(legacyBegin, legacyEnd - legacyBegin,
                        "  renderColors: 1024\n  renderExtras: 512\n  renderMotion: 256\n  renderVertex: 128\n");
    request.files["Assets/Demo.unity"] = bytes(legacyScene);
    auto legacyImported = prepareUnityProjectImport(request);
    REQUIRE(legacyImported.ok());
    const auto legacyDefinition = std::find_if(legacyImported.value().entries.begin(), legacyImported.value().entries.end(),
                                               [](const auto& entry) { return entry.path.ends_with("asset.json"); });
    REQUIRE(legacyDefinition != legacyImported.value().entries.end());
    auto legacyParsed = Value::fromJson(std::string_view(
        reinterpret_cast<const char*>(legacyDefinition->bytes.data()), legacyDefinition->bytes.size()));
    REQUIRE(legacyParsed.ok());
    const auto& legacyVolume = *legacyParsed.value().getIf<Value::Object>()->at("volume").getIf<Value::Object>();
    CHECK_EQ(legacyVolume.at("extras").getIf<Value::Array>()->at(1).asInt(), std::int64_t(512));
    CHECK_EQ(legacyVolume.at("extras").getIf<Value::Array>()->at(2).asInt(), std::int64_t(512));

    request.files["Assets/Demo.unity"] = bytes(scene.substr(0, scene.find("--- !u!114 &4")));
    auto incomplete = prepareUnityProjectImport(request);
    REQUIRE(!incomplete.ok());
    CHECK_EQ(incomplete.error()->code(), DiagnosticCode::ParseError);
}

TEST_CASE("asset.runtimeInstanceSetV3LoadsAndValidatesTerrainDetailResourceAsset") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string resource = "asset://550e8400-e29b-41d4-a716-446655440099";
    const std::string definition =
        std::string(R"json({"schema":"eve.instance-set","schemaVersion":3,"count":0,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":true,"minWidth":0.5,"maxWidth":1.5,"minHeight":1,"maxHeight":2,"noiseSeed":23,"noiseSpread":0.25,"density":0.75,"alignToGround":0.4,"positionJitter":0.2,"resourceAsset":")json") +
        resource + R"json("}]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pack = runtimePack("eve.instance-set", definition, bulk, SchemaVersion(3));
    EvpackResourceReader reader(pack);
    eve::asset_procgen::EvpackInstanceSetLoader loader(reader);
    auto loaded = loader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().prototypes.size(), std::size_t(1));
    CHECK_EQ(loaded.value().prototypes.front().resourceAsset, resource);

    const auto invalidDefinition = std::regex_replace(definition, std::regex("asset://550e8400-e29b-41d4-a716-446655440099"),
                                                      "project://not-an-asset");
    auto invalidPack = runtimePack("eve.instance-set", invalidDefinition, std::move(bulk), SchemaVersion(3));
    EvpackResourceReader invalidReader(invalidPack);
    eve::asset_procgen::EvpackInstanceSetLoader invalidLoader(invalidReader);
    auto rejected = invalidLoader.load(reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);
}

TEST_CASE("asset.runtimeInstanceSetV4LoadsTerrainDetailAppearance") {
    const EvpackCapabilities capabilities{"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    const std::string definition =
        R"json({"schema":"eve.instance-set","schemaVersion":4,"count":0,"partition":"single-cell","prototypes":[{"prototype":"unity-texture-guid:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","renderMode":"GrassBillboard","usePrototypeMesh":false,"useInstancing":false,"minWidth":0.5,"maxWidth":1.5,"minHeight":1,"maxHeight":2,"noiseSeed":23,"noiseSpread":0.25,"density":0.75,"alignToGround":0.4,"positionJitter":0.2,"resourceAsset":"asset://550e8400-e29b-41d4-a716-446655440099","healthyColor":[0.1,0.8,0.2,1],"dryColor":[0.7,0.4,0.1,1],"bendFactor":0.3,"holeEdgePadding":0.2,"useDensityScaling":false}]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pack = runtimePack("eve.instance-set", definition, std::move(bulk), SchemaVersion(4));
    EvpackResourceReader reader(pack);
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
    const std::string definition =
        R"json({"schema":"eve.instance-set","schemaVersion":5,"count":0,"partition":"single-cell","wavingGrass":{"amount":0.4,"speed":0.6,"strength":0.8,"tint":[0.3,0.7,0.4,1]},"prototypes":[]})json";
    std::vector<std::uint8_t> bulk{'E', 'V', 'I', 'N', 'S', 'T', 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pack = runtimePack("eve.instance-set", definition, std::move(bulk), SchemaVersion(5));
    EvpackResourceReader reader(pack);
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
