#include "asset/AssetCooker.h"
#include "asset/EvaArchive.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySource.h"
#include "asset_import/UnityPackageFixture.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_import;

namespace {
std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

UnityProjectImportRequest collection() {
    UnityProjectImportRequest request;
    request.package = {
        *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040"),
        "unity.collection",
        "1.0.0",
        {{"provider", Value("local")}, {"license", Value(Value::Object{{"redistribution", Value("project-only")}})}}};
    request.files["Assets/pixel.png"] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    request.files["Assets/pixel.png.meta"] = bytes(
        "fileFormatVersion: 2\nguid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "TextureImporter:\n  sRGBTexture: 0\n  spriteMode: 1\n");
    request.files["Assets/prop.prefab"]      = bytes(R"yaml(%YAML 1.1
--- !u!1 &1
GameObject:
  m_Name: Prop
--- !u!4 &4
Transform:
  m_GameObject: {fileID: 1}
  m_Father: {fileID: 0}
  m_LocalPosition: {x: 1, y: 2, z: 3}
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalScale: {x: 1, y: 1, z: 1}
--- !u!23 &23
MeshRenderer:
  m_GameObject: {fileID: 1}
)yaml");
    request.files["Assets/prop.prefab.meta"] = bytes("guid: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\nPrefabImporter:\n");
    request.files["Assets/walk.anim"]        = bytes("%YAML 1.1\n--- !u!74 &7400000\nAnimationClip:\n  m_Name: Walk\n");
    request.files["Assets/walk.anim.meta"]   = bytes("guid: cccccccccccccccccccccccccccccccc\nNativeFormatImporter:\n");
    return request;
}
}  // namespace

TEST_CASE("asset.import.unityCollectionAcceptsCrLfMetadata") {
    auto request = collection();
    for (auto& [path, data] : request.files) {
        if (!path.ends_with(".meta")) continue;
        std::string crlf;
        for (const auto byte : data) {
            if (byte == '\n') crlf += '\r';
            crlf += static_cast<char>(byte);
        }
        data = bytes(crlf);
    }
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
}

TEST_CASE("asset.import.unityCollectionReportsNegativeComponentIdsWithoutLosingHierarchy") {
    auto       request  = collection();
    const auto collider = bytes("\n--- !u!65 &-4356345918748047990\nBoxCollider:\n  m_GameObject: {fileID: 1}\n");
    auto&      prefab   = request.files.at("Assets/prop.prefab");
    prefab.insert(prefab.end(), collider.begin(), collider.end());
    auto prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.feature == "Prefab.component:65:-4356345918748047990" &&
               f.disposition == ImportDisposition::Unsupported;
    }));
}

TEST_CASE("asset.import.unityCollectionCombinesAssetsPreservesSourcesAndCooks") {
    const auto request  = collection();
    auto       prepared = prepareUnityProjectImport(request);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(prepared.value().manifest.assets.size(), std::size_t(2));
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/prop.prefab"));
    REQUIRE(prepared.value().manifest.entrypoints.contains("Assets/pixel.png"));
    for (const auto& [path, data] : request.files) {
        auto found = std::find_if(prepared.value().entries.begin(), prepared.value().entries.end(),
                                  [&](const auto& entry) { return entry.path == "sources/unity/" + path; });
        REQUIRE(found != prepared.value().entries.end());
        REQUIRE(found->bytes == data);
    }
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.feature.starts_with("Prefab.component:23") && f.disposition == ImportDisposition::Unsupported;
    }));
    REQUIRE(std::any_of(prepared.value().findings.begin(), prepared.value().findings.end(), [](const auto& f) {
        return f.sourcePath == "Assets/walk.anim" && f.disposition == ImportDisposition::Unsupported;
    }));
    auto encoded = buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(encoded.ok());
    auto archive = parseEvaArchive(encoded.value());
    REQUIRE(archive.ok());
    AssetCookProfile profile{
        {"win32", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}, CookPublication::LocalInspection, 16};
    auto cooked = cookEvaToEvpack(archive.value(), profile);
    REQUIRE(cooked.ok());
    auto pack = parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    REQUIRE_EQ(pack.value().chunks().size(), std::size_t(3));
    auto repeated = prepareUnityProjectImport(request);
    REQUIRE(repeated.ok());
    auto repeatedBytes = buildEvaArchive(repeated.value().manifest, repeated.value().entries);
    REQUIRE(repeatedBytes.ok());
    REQUIRE(encoded.value() == repeatedBytes.value());
}

TEST_CASE("asset.import.unityCollectionRetainsIdentityAfterMoveAndAddsDistinctImages") {
    auto request = collection();
    auto before  = prepareUnityProjectImport(request);
    REQUIRE(before.ok());
    const auto originalImage               = before.value().manifest.entrypoints.at("Assets/pixel.png");
    const auto originalPrefab              = before.value().manifest.entrypoints.at("Assets/prop.prefab");
    request.files["Assets/moved.png"]      = std::move(request.files.at("Assets/pixel.png"));
    request.files["Assets/moved.png.meta"] = std::move(request.files.at("Assets/pixel.png.meta"));
    request.files.erase("Assets/pixel.png");
    request.files.erase("Assets/pixel.png.meta");
    request.files["Assets/renamed.prefab"]      = std::move(request.files.at("Assets/prop.prefab"));
    request.files["Assets/renamed.prefab.meta"] = std::move(request.files.at("Assets/prop.prefab.meta"));
    request.files.erase("Assets/prop.prefab");
    request.files.erase("Assets/prop.prefab.meta");
    request.files["Assets/other.png"]      = request.files.at("Assets/moved.png");
    request.files["Assets/other.png.meta"] = bytes("guid: dddddddddddddddddddddddddddddddd\nTextureImporter:\n");
    auto after                             = prepareUnityProjectImport(request);
    REQUIRE(after.ok());
    REQUIRE_EQ(after.value().manifest.assets.size(), std::size_t(3));
    REQUIRE(after.value().manifest.entrypoints.at("Assets/moved.png") == originalImage);
    REQUIRE(after.value().manifest.entrypoints.at("Assets/renamed.prefab") == originalPrefab);
    REQUIRE(after.value().manifest.entrypoints.at("Assets/other.png") != originalImage);
}

TEST_CASE("asset.import.unityCollectionRejectsCorruptConvertibleAndUnsupportedOnlyInputs") {
    auto request = collection();
    request.files.at("Assets/pixel.png")[45] ^= 1;
    auto corrupt = prepareUnityProjectImport(request);
    REQUIRE(!corrupt.ok());
    request = collection();
    request.files.erase("Assets/pixel.png");
    request.files.erase("Assets/pixel.png.meta");
    request.files.erase("Assets/prop.prefab");
    request.files.erase("Assets/prop.prefab.meta");
    auto unsupported = prepareUnityProjectImport(request);
    REQUIRE(!unsupported.ok());
    REQUIRE(unsupported.error()->code() == DiagnosticCode::Unsupported);
}

TEST_CASE("asset.import.unityCollectionTracksMetadataChangesAndGuidCase") {
    auto request = collection();
    auto before  = prepareUnityProjectImport(request);
    REQUIRE(before.ok());
    const auto oldKey                        = before.value().manifest.provenance.at("importKey").asString();
    const auto oldPrefab                     = before.value().manifest.entrypoints.at("Assets/prop.prefab");
    request.files["Assets/prop.prefab.meta"] = bytes("guid: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\nPrefabImporter:\n");
    auto&      meta                          = request.files.at("Assets/pixel.png.meta");
    const auto settings                      = bytes("  spritePixelsToUnits: 64\n");
    meta.insert(meta.end(), settings.begin(), settings.end());
    auto after = prepareUnityProjectImport(request);
    REQUIRE(after.ok());
    REQUIRE(after.value().manifest.entrypoints.at("Assets/prop.prefab") == oldPrefab);
    REQUIRE(after.value().manifest.provenance.at("importKey").asString() != oldKey);
}

TEST_CASE("asset.import.unityCollectionRejectsMalformedNestedPrefabBeforePublication") {
    auto request                        = collection();
    request.files["Assets/prop.prefab"] = bytes("%YAML 1.1\n--- !u!1001 &123\nPrefabInstance:\n");
    auto result                         = prepareUnityProjectImport(request);
    REQUIRE(!result.ok());
}

TEST_CASE("asset.import.unityPackageAndDirectoryProduceIdenticalCanonicalArchive") {
    auto request = collection();
    auto indexed = indexUnitySources(request.files);
    REQUIRE(indexed.ok());
    std::vector<std::uint8_t> tar;
    for (const auto& source : indexed.value().assets) {
        unity_test::appendTar(tar, source.guid + "/asset", request.files.at(source.path));
        unity_test::appendTar(tar, source.guid + "/asset.meta", request.files.at(source.path + ".meta"));
        unity_test::appendTar(tar, source.guid + "/pathname", bytes(source.path));
    }
    tar.resize(tar.size() + 1024);
    auto unpacked = readUnityPackage(unity_test::gzip(tar));
    REQUIRE(unpacked.ok());
    REQUIRE(unpacked.value() == request.files);
    auto direct = prepareUnityProjectImport(request);
    REQUIRE(direct.ok());
    request.files = std::move(unpacked).takeValue();
    auto packaged = prepareUnityProjectImport(request);
    REQUIRE(packaged.ok());
    auto directArchive = buildEvaArchive(direct.value().manifest, direct.value().entries);
    REQUIRE(directArchive.ok());
    auto packagedArchive = buildEvaArchive(packaged.value().manifest, packaged.value().entries);
    REQUIRE(packagedArchive.ok());
    REQUIRE(directArchive.value() == packagedArchive.value());
}
