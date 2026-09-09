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
        "m_GameObject: {fileID: 1}\n  m_Enabled: 1\n  m_Materials:\n";
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
