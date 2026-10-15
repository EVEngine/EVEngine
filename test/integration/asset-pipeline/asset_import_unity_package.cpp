#include "asset/import/UnitySource.h"
#include "asset_import/UnityPackageFixture.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdio>

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
