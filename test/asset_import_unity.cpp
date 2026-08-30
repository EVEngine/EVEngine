#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset_import/UnityImporter.h"
#include "asset_import/TerrainImporter.h"
#include "asset_procgen/EvpackInstanceSetLoader.h"
#include "asset_procgen/EvpackTerrainLoader.h"
#include "asset_procgen/EvpackTerrainMaterialLoader.h"
#include "asset_scene/EvpackSceneTemplateLoader.h"

#include <algorithm>
#include <cmath>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {

std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

ImportPackageIdentity unityIdentity() {
    return {*PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"), "unity.terrain-prefab", "1.0.0",
            {{"provider", Value("unity-project")},
             {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
}

AssetRef reference(std::string_view text) {
    auto parsed = AssetRef::parse(text);
    REQUIRE(parsed.ok());
    return std::move(parsed).takeValue();
}

std::shared_ptr<const Evpack> runtimePack(std::string type, std::string definition,
                                         std::vector<std::uint8_t> bulk = {}) {
    const auto asset = reference("asset://550e8400-e29b-41d4-a716-446655440000");
    EvpackBuild build;
    build.packageId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants = {{"android", "arm64", "vulkan", {"astc"}, "spirv-1.6", "high", {}}};
    auto parsedDefinition = Value::fromJson(definition);
    REQUIRE(parsedDefinition.ok());
    auto encodedDefinition = encodeRuntimeDefinition(parsedDefinition.value());
    REQUIRE(encodedDefinition.ok());
    build.chunks.push_back({asset.id(), type, SchemaVersion(1), 0,
                            EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {},
                            std::move(encodedDefinition).takeValue()});
    if (!bulk.empty())
        build.chunks.push_back({asset.id(), type, SchemaVersion(1), 0,
                                EvpackChunkKind::Bulk, 1, EvpackCodec::None, 8, {},
                                std::move(bulk)});
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
    constexpr std::string_view layer = R"yaml(%YAML 1.1
--- !u!114 &11400000
TerrainLayer:
  m_Name: Grass
  m_DiffuseTexture: {fileID: 2800000, guid: cccccccccccccccccccccccccccccccc, type: 3}
  m_NormalMapTexture: {fileID: 2800000, guid: dddddddddddddddddddddddddddddddd, type: 3}
  m_TileSize: {x: 8, y: 8}
)yaml";
    constexpr std::string_view prefab = R"yaml(%YAML 1.1
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
    UnityProjectImportRequest request;
    request.package = unityIdentity();
    request.terrainDataPath = "Assets/Terrain/TerrainData.asset";
    request.prefabPath = "Assets/Terrain/Terrain.prefab";
    request.files = {
        {request.terrainDataPath, bytes(terrain)},
        {request.terrainDataPath + ".meta", bytes("fileFormatVersion: 2\nguid: 11111111111111111111111111111111\n")},
        {"Assets/Terrain/Grass.terrainlayer", bytes(layer)},
        {"Assets/Terrain/Grass.terrainlayer.meta", bytes("fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n")},
        {request.prefabPath, bytes(prefab)},
        {request.prefabPath + ".meta", bytes("fileFormatVersion: 2\nguid: ffffffffffffffffffffffffffffffff\n")}
    };
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
        return finding.feature.starts_with("MonoBehaviour") &&
               finding.disposition == ImportDisposition::Unsupported;
    }));

    auto evaBytes = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(evaBytes.ok());
    auto eva = parseEvaArchive(evaBytes.value());
    REQUIRE(eva.ok());
    AssetCookProfile profile{{"android", "arm64", "vulkan", {"astc"}, "spirv-1.6", "high", {}},
                             CookPublication::LocalInspection, 16};
    auto cooked = cookEvaToEvpack(eva.value(), profile);
    AssetCookProfile webProfile{{"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}},
                                CookPublication::LocalInspection, 16};
    auto webCooked = cookEvaToEvpack(eva.value(), webProfile);
    REQUIRE(cooked.ok());
    REQUIRE(webCooked.ok());
    auto runtime = parseEvpack(cooked.value().bytes);
    auto webRuntime = parseEvpack(webCooked.value().bytes);
    REQUIRE(runtime.ok());
    REQUIRE(webRuntime.ok());
    CHECK(runtime.value().chunks().size() >= prepared.value().manifest.assets.size());
    CHECK_EQ(runtime.value().chunks().size(), webRuntime.value().chunks().size());
    auto androidAdmitted = std::make_shared<const Evpack>(std::move(runtime).takeValue());
    auto webAdmitted = std::make_shared<const Evpack>(std::move(webRuntime).takeValue());
    EvpackResourceReader androidReader(androidAdmitted);
    EvpackResourceReader webReader(webAdmitted);
    for (const auto& sourceAsset : prepared.value().manifest.assets) {
        const std::string expected = sourceAsset.type + "/" +
                                     std::to_string(sourceAsset.schemaVersion.value());
        auto androidAsset = androidReader.read(
            sourceAsset.asset, expected,
            {"android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}},
            16 * 1024 * 1024);
        auto webAsset = webReader.read(
            sourceAsset.asset, expected,
            {"web", "wasm32", "webgpu", {"rgba8"}, {"wgsl-1"}, {"high"}, {}},
            16 * 1024 * 1024);
        REQUIRE(androidAsset.ok());
        REQUIRE(webAsset.ok());
        CHECK_EQ(androidAsset.value().chunks.front().bytes,
                 webAsset.value().chunks.front().bytes);
    }

    const auto terrainAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.terrain"; });
    const auto sceneAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.scene-template"; });
    const auto instancesAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.instance-set"; });
    const auto materialAsset = std::find_if(
        prepared.value().manifest.assets.begin(), prepared.value().manifest.assets.end(),
        [](const auto& asset) { return asset.type == "eve.terrain-material"; });
    REQUIRE(terrainAsset != prepared.value().manifest.assets.end());
    REQUIRE(sceneAsset != prepared.value().manifest.assets.end());
    REQUIRE(instancesAsset != prepared.value().manifest.assets.end());
    REQUIRE(materialAsset != prepared.value().manifest.assets.end());
    const EvpackCapabilities androidCapabilities{
        "android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    eve::asset_procgen::EvpackTerrainLoader terrainLoader(androidReader);
    auto loadedTerrain = terrainLoader.load(terrainAsset->asset, androidCapabilities);
    REQUIRE(loadedTerrain.ok());
    CHECK_EQ(loadedTerrain.value().heightmap.getWidth(), 3);
    CHECK_EQ(loadedTerrain.value().heightmap.getHeight(), 3);
    CHECK(std::abs(loadedTerrain.value().heightmap.height(1, 1) - 100.f) < 0.001f);
    auto sampled = loadedTerrain.value().spatial.sample(2.f, 42, 0.f);
    CHECK(sampled.getCount() > 0);

    eve::asset_scene::EvpackSceneTemplateLoader sceneLoader(androidReader);
    auto loadedScene = sceneLoader.load(sceneAsset->asset, androidCapabilities);
    REQUIRE(loadedScene.ok());
    REQUIRE_EQ(loadedScene.value().root.children.size(), std::size_t(1));
    const auto& prefabRoot = loadedScene.value().root.children.front();
    CHECK_EQ(prefabRoot.name, std::string("TerrainRoot"));
    CHECK(std::abs(prefabRoot.x - 1.f) < 0.0001f);
    CHECK(std::abs(prefabRoot.y - 2.f) < 0.0001f);
    CHECK(std::abs(prefabRoot.z + 3.f) < 0.0001f);
    const bool yawConverted = std::abs(prefabRoot.yaw + 90.f) < 0.01f ||
                              std::abs(prefabRoot.yaw - 90.f) < 0.01f;
    CHECK(yawConverted);

    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(androidReader);
    auto loadedInstances = instanceLoader.load(instancesAsset->asset, androidCapabilities);
    REQUIRE(loadedInstances.ok());
    REQUIRE_EQ(loadedInstances.value().instances.size(), std::size_t(1));
    const auto& tree = loadedInstances.value().instances.front();
    CHECK_EQ(tree.prototype,
             std::string("unity-guid:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
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
}

TEST_CASE("asset.import.unityRejectsBinarySerializationAndBadHeightCounts") {
    UnityProjectImportRequest binary;
    binary.package = unityIdentity();
    binary.terrainDataPath = "Assets/Terrain.asset";
    binary.files[binary.terrainDataPath] = {0, 1, 2};
    auto unsupported = prepareUnityProjectImport(binary);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(unsupported.error()->code(), DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.terrainRejectsInstancesThatRuntimeCouldNotAdmit") {
    CanonicalTerrainInput terrain;
    terrain.width = terrain.height = 2;
    terrain.heightsMeters = {0.f, 0.f, 0.f, 0.f};
    CanonicalTerrainInstance instance;
    instance.prototype = std::string("bad\0prototype", 13);
    terrain.instances.push_back(instance);
    auto invalidText = prepareCanonicalTerrainImport(
        unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidText.ok());
    CHECK_EQ(invalidText.error()->code(), DiagnosticCode::InvalidArgument);

    terrain.instances.front().prototype = "valid-prototype";
    terrain.instances.front().rotation[3] = 2.f;
    auto invalidRotation = prepareCanonicalTerrainImport(
        unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidRotation.ok());
    CHECK_EQ(invalidRotation.error()->code(), DiagnosticCode::InvalidArgument);

    terrain.instances.front().rotation[3] = 1.f;
    terrain.instances.front().scale[1] = 0.f;
    auto invalidScale = prepareCanonicalTerrainImport(
        unityIdentity(), terrain, "unity.terrain-prefab/1");
    REQUIRE(!invalidScale.ok());
    CHECK_EQ(invalidScale.error()->code(), DiagnosticCode::InvalidArgument);
}

TEST_CASE("asset.runtimeWorldLoadersRejectCrossChunkMismatchAndSceneCycles") {
    const EvpackCapabilities capabilities{
        "android", "arm64", "vulkan", {"astc"}, {"spirv-1.6"}, {"high"}, {}};
    std::vector<std::uint8_t> terrain = {'E', 'V', 'T', 'R', 'N', 0, 1, 0,
                                         1,   0,   0,   0,   1,   0, 0, 0,
                                         0,   0,   0x80, 0x3f, 0, 0, 0x80, 0x3f,
                                         0,   0,   0,   0};
    auto terrainPack = runtimePack(
        "eve.terrain",
        R"json({"schema":"eve.terrain","schemaVersion":1,"width":2,"height":1,"spacingX":1,"spacingZ":1,"coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","heightUnit":"meter"})json",
        std::move(terrain));
    EvpackResourceReader terrainReader(terrainPack);
    eve::asset_procgen::EvpackTerrainLoader terrainLoader(terrainReader);
    auto mismatched = terrainLoader.load(
        reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!mismatched.ok());
    CHECK_EQ(mismatched.error()->code(), DiagnosticCode::ParseError);

    auto scenePack = runtimePack(
        "eve.scene-template",
        R"json({"schema":"eve.scene-template","schemaVersion":1,"coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","nodes":[{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31040","sourceFileId":1,"parentSourceFileId":2,"name":"A","position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31041","sourceFileId":2,"parentSourceFileId":1,"name":"B","position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}]})json");
    EvpackResourceReader sceneReader(scenePack);
    eve::asset_scene::EvpackSceneTemplateLoader sceneLoader(sceneReader);
    auto cyclic = sceneLoader.load(
        reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!cyclic.ok());
    CHECK_EQ(cyclic.error()->code(), DiagnosticCode::Conflict);

    std::vector<std::uint8_t> shortInstances = {'E', 'V', 'I', 'N', 'S', 'T', 0, 1,
                                                 0xff, 0xff, 0xff, 0x7f, 0, 0, 0, 0};
    auto instancePack = runtimePack(
        "eve.instance-set",
        R"json({"schema":"eve.instance-set","schemaVersion":1,"count":2147483647,"partition":"single-cell"})json",
        std::move(shortInstances));
    EvpackResourceReader instanceReader(instancePack);
    eve::asset_procgen::EvpackInstanceSetLoader instanceLoader(instanceReader);
    auto oversized = instanceLoader.load(
        reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!oversized.ok());
    CHECK_EQ(oversized.error()->code(), DiagnosticCode::InvalidArgument);

    auto materialPack = runtimePack(
        "eve.terrain-material",
        R"json({"schema":"eve.terrain-material","schemaVersion":1,"layers":[{"name":"bad","diffuseSource":"","normalSource":"","weightSource":"","normalConvention":"opengl","tileSizeMeters":0}]})json");
    EvpackResourceReader materialReader(materialPack);
    eve::asset_procgen::EvpackTerrainMaterialLoader materialLoader(materialReader);
    auto invalidMaterial = materialLoader.load(
        reference("asset://550e8400-e29b-41d4-a716-446655440000"), capabilities);
    REQUIRE(!invalidMaterial.ok());
    CHECK_EQ(invalidMaterial.error()->code(), DiagnosticCode::InvalidArgument);
}
