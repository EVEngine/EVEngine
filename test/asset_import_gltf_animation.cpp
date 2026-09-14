#include "asset/AssetCooker.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <bit>

using namespace eve;
using namespace eve::asset_import;

namespace {
GltfImportRequest animatedTriangle() {
    GltfImportRequest request;
    request.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "animated.test", "1.0.0", {}};
    request.sourceName = "triangle.gltf";
    auto& bytes        = request.externalResources["data.bin"];
    auto  scalar       = [&](float value) {
        auto bits = std::bit_cast<std::uint32_t>(value);
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    };
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) scalar(value);
    for (int i = 0; i < 12; ++i) bytes.push_back(0);
    for (int v = 0; v < 3; ++v)
        for (int i = 0; i < 4; ++i) scalar(i == 0 ? 1.f : 0.f);
    scalar(0);
    scalar(1);
    for (float value : {0.f, 0.f, 0.f, 0.f, 2.f, 0.f}) scalar(value);
    const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data.bin","byteLength":128}],
      "bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":8},{"buffer":0,"byteOffset":104,"byteLength":24}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"VEC3"}],
      "nodes":[{"name":"root","children":[1]},{"name":"joint"},{"mesh":0,"skin":0}],"skins":[{"joints":[1]}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],
      "animations":[{"name":"move","samplers":[{"input":3,"output":4,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
    request.documentBytes.assign(json.begin(), json.end());
    return request;
}
}  // namespace

TEST_CASE("asset.import.gltfAnimationSurvivesArchiveAndCook") {
    auto imported = prepareGltfImport(animatedTriangle());
    REQUIRE(imported.ok());
    std::vector<std::string> types;
    for (const auto& asset : imported.value().manifest.assets) types.push_back(asset.type);
    REQUIRE(std::find(types.begin(), types.end(), "eve.skeleton") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "eve.animation-clip") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "eve.skin") != types.end());
    REQUIRE_EQ(imported.value().manifest.dependencies.size(), std::size_t(3));
    for (const auto& definition : imported.value().manifest.assets) {
        const auto base  = "assets/" + definition.asset.id().format() + "/data.bin";
        auto       found = std::find_if(imported.value().entries.begin(), imported.value().entries.end(),
                                        [&](const auto& e) { return e.path == base; });
        if (definition.type == "eve.skin") {
            REQUIRE(found != imported.value().entries.end());
            // Header + one joint/identity inverse bind + three vertices * four
            // (joint,weight) pairs. The joint maps to bone 2 after parent ordering.
            REQUIRE_EQ(found->bytes.size(), std::size_t(188));
            REQUIRE_EQ(found->bytes[24], std::uint8_t(2));
            REQUIRE_EQ(found->bytes[92 + 6], std::uint8_t(128));  // FLOAT 1.0
        }
        if (definition.type == "eve.animation-clip") {
            REQUIRE(found != imported.value().entries.end());
            REQUIRE_EQ(found->bytes.size(), std::size_t(76));
            REQUIRE_EQ(found->bytes[28], std::uint8_t(2));   // remapped target bone
            REQUIRE_EQ(found->bytes[71], std::uint8_t(64));  // final Y = 2.0
        }
    }
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto parsed = asset::parseEvaArchive(archive.value());
    REQUIRE(parsed.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(parsed.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    REQUIRE_EQ(pack.value().chunks().size(), std::size_t(8));
}

TEST_CASE("asset.import.gltfAnimationRejectsInvalidJointWithoutPartialSuccess") {
    auto request                              = animatedTriangle();
    request.externalResources["data.bin"][36] = 1;
    auto imported                             = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRejectsUnsupportedInterpolation") {
    auto        request = animatedTriangle();
    std::string json(request.documentBytes.begin(), request.documentBytes.end());
    json.replace(json.find("LINEAR"), 6, "UNKNOWN");
    request.documentBytes.assign(json.begin(), json.end());
    auto imported = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRejectsMissingTargetAndCycles") {
    for (const auto& change :
         std::vector<std::pair<std::string, std::string>>{{"\"target\":", "\"missing\":"},
                                                          {"\"name\":\"joint\"", "\"name\":\"joint\",\"children\":[0]"},
                                                          {"\"joints\":[1]", "\"joints\":[9]"}}) {
        auto        request = animatedTriangle();
        std::string json(request.documentBytes.begin(), request.documentBytes.end());
        json.replace(json.find(change.first), change.first.size(), change.second);
        request.documentBytes.assign(json.begin(), json.end());
        auto imported = prepareGltfImport(request);
        REQUIRE(!imported.ok());
    }
}

TEST_CASE("asset.import.gltfAnimationHonorsDecodedBudget") {
    auto request                       = animatedTriangle();
    request.limits.maximumDecodedBytes = 128;
    auto imported                      = prepareGltfImport(request);
    REQUIRE(!imported.ok());
}

TEST_CASE("asset.import.gltfAnimationRetainsAndNormalizesMultipleQuantizedWeightSets") {
    auto  request = animatedTriangle();
    auto& bytes   = request.externalResources["data.bin"];
    for (std::size_t i = 48; i < 96; ++i) bytes[i] = 0;
    for (std::size_t vertex = 0; vertex < 3; ++vertex) bytes[48 + vertex * 4] = 127;
    for (int vertex = 0; vertex < 3; ++vertex) bytes.insert(bytes.end(), {1, 0, 0, 0});
    std::string json(request.documentBytes.begin(), request.documentBytes.end());
    auto        replace = [&](std::string from, std::string to) {
        const auto pos = json.find(from);
        REQUIRE(pos != std::string::npos);
        json.replace(pos, from.size(), to);
    };
    replace("\"byteLength\":128", "\"byteLength\":140");
    replace("\"byteOffset\":104,\"byteLength\":24}",
            "\"byteOffset\":104,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}");
    replace("\"bufferView\":2,\"componentType\":5126", "\"bufferView\":2,\"normalized\":true,\"componentType\":5121");
    replace("\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}",
            "\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},{\"bufferView\":5,"
            "\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"}");
    replace("\"joints\":[1]", "\"joints\":[1,2]");
    replace("\"WEIGHTS_0\":2", "\"WEIGHTS_0\":2,\"JOINTS_1\":5,\"WEIGHTS_1\":2");
    request.documentBytes.assign(json.begin(), json.end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& entry : imported.value().entries) {
        if (entry.bytes.size() != 352 || entry.bytes[2] != 'S') continue;
        found = true;
        REQUIRE_EQ(entry.bytes[16], std::uint8_t(8));
        REQUIRE_EQ(entry.bytes[167], std::uint8_t(63));  // first set weight = 0.5
        REQUIRE_EQ(entry.bytes[199], std::uint8_t(63));  // second set weight = 0.5
    }
    REQUIRE(found);
    REQUIRE(std::any_of(imported.value().findings.begin(), imported.value().findings.end(),
                        [](const auto& finding) { return finding.disposition == ImportDisposition::Baked; }));
}
