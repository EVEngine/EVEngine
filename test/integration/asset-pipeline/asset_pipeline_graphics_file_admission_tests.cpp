// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "asset-pipeline",
  "module": "asset",
  "modules": [
    "asset",
    "asset_import",
    "data"
  ],
  "origins": [
    "asset_import_gltf_animation.cpp",
    "asset_import_gltf_material.cpp",
    "asset_import_tga.cpp",
    "asset_import_tiff.cpp",
    "asset_mesh_attributes.cpp",
    "asset_mesh_uv.cpp",
    "asset_migration.cpp"
  ],
  "cases": {
    "render-offscreen": [
      "asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad"
    ]
  }
}
*/
// clang-format on

// Cohesive asset workflow contracts share one physical translation unit.

// Contract section: asset import gltf animation
#include <algorithm>
#include <bit>
#include "PrivateTestScope.h"
#include "asset/AssetCooker.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// asset_import_gltf_animation.cpp
namespace test_asset_import_gltf_animation {
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
}  // namespace test_asset_import_gltf_animation


// Contract section: asset import gltf material
#include "PrivateTestScope.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/CookedMaterial.h"
#endif
#include "PrivateTestScope.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/import/ImportCommon.h"

#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/CookedMaterial.h"
#endif
// asset_import_gltf_material.cpp
namespace test_asset_import_gltf_material {
#ifdef EVE_TEST_MESH_UPLOAD

#endif


using namespace eve;
using namespace eve::asset_import;
namespace {
GltfImportRequest materialTriangle() {
    GltfImportRequest r;
    r.package    = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "material.test", "1.0.0", {}};
    r.sourceName = "material.gltf";
    r.externalResources["vertices.bin"].resize(36);
    r.externalResources["uv.bin"].resize(24);
    r.externalResources["pixel.png"] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0,    0,    0,    0x0d, 0x49, 0x48, 0x44, 0x52, 0, 0,
        0,    1,    0,    0,    0,    1,    8,    6,    0,    0,    0,    0x1f, 0x15, 0xc4, 0x89, 0,    0, 0,
        0x0d, 0x49, 0x44, 0x41, 0x54, 8,    0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0,    5,    0,    1, 0xff,
        0x72, 0x9c, 0x52, 0x67, 0,    0,    0,    0,    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::string json =
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"vertices.bin","byteLength":36},{"uri":"uv.bin","byteLength":24}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":1,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.3,0.4,0.5],"metallicFactor":0.7,"roughnessFactor":0.8,"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}},"normalTexture":{"index":0,"scale":0.6},"occlusionTexture":{"index":0,"strength":0.4},"emissiveTexture":{"index":0},"emissiveFactor":[0.1,0.2,0.3],"alphaMode":"MASK","alphaCutoff":0.3,"doubleSided":true}],"textures":[{"source":0}],"images":[{"uri":"pixel.png"}]})";
    r.documentBytes.assign(json.begin(), json.end());
    return r;
}
}  // namespace
TEST_CASE("asset.import.gltfMaterialAndImagesSurviveCook") {
    auto result = prepareGltfImport(materialTriangle());
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().manifest.assets.size(), size_t(7));
    REQUIRE_EQ(result.value().manifest.dependencies.size(), size_t(6));
    bool found = false;
    for (const auto& a : result.value().manifest.assets)
        if (a.type == "eve.material") {
            found = true;
            for (const auto& e : result.value().entries)
                if (e.path == a.definition) {
                    std::string text(e.bytes.begin(), e.bytes.end());
                    REQUIRE(text.find("metallicRoughnessTexture") != std::string::npos);
                    REQUIRE(text.find("normalTexture") != std::string::npos);
                    REQUIRE(text.find("masked") != std::string::npos);
                    auto decoded = Value::fromJson(text);
                    REQUIRE(decoded.ok());
                    const auto* object = decoded.value().getIf<Value::Object>();
                    REQUIRE(object != nullptr);
                    REQUIRE_EQ(object->at("metallic").asDouble(), 0.7);
                    REQUIRE_EQ(object->at("roughness").asDouble(), 0.8);
                    REQUIRE_EQ(object->at("normalScale").asDouble(), 0.6);
                    REQUIRE_EQ(object->at("occlusionStrength").asDouble(), 0.4);
                    REQUIRE_EQ(object->at("alphaCutoff").asDouble(), 0.3);
                }
        }
    REQUIRE(found);
    auto bytes = asset::buildEvaArchive(result.value().manifest, result.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto pack = asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(pack.ok());
}
TEST_CASE("asset.import.gltfMissingTextureFailsAtomically") {
    auto request = materialTriangle();
    request.externalResources.erase("pixel.png");
    REQUIRE(!prepareGltfImport(request).ok());
}

#ifdef EVE_TEST_MESH_UPLOAD
TEST_CASE("asset.import.materialV14ColorParametersSurviveV15CookWithImageDependency") {
    auto imported = prepareGltfImport(materialTriangle());
    REQUIRE(imported.ok());
    auto bytes = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(bytes.ok());
    auto archive = asset::parseEvaArchive(bytes.value());
    REQUIRE(archive.ok());
    const auto ref = imported.value().manifest.entrypoints.at("material:0");
    // Stage an authored N-1 definition. Cooking migrates and rehashes the owning candidate.
    for (auto& material : archive.value().manifest.assets) {
        if (material.asset != ref) continue;
        material.schemaVersion = SchemaVersion(14);
        for (auto& entry : archive.value().entries) {
            if (entry.path != material.definition) continue;
            auto decoded = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
            REQUIRE(decoded.ok());
            auto& root            = *decoded.value().getIf<Value::Object>();
            root["schemaVersion"] = 14;
            root.erase("alphaToCoverage");
            root["albedoTextureStrength"] = .25;
            root["colorMask"] =
                Value::Object{{"secondary", Value::Array{2.0, .1, .3}}, {"minimum", .2}, {"maximum", .8}};
            auto json = decoded.value().toJson();
            REQUIRE(json.ok());
            entry.bytes.assign(json.value().begin(), json.value().end());
            material.contentHash = asset_import::detail::sha256(entry.bytes);
        }
    }
    for (auto& dependency : archive.value().manifest.dependencies)
        if (dependency.to == ref) dependency.expectedType = "eve.material/14";
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(archive.value(), profile.value());
    if (!cooked) std::cerr << cooked.error()->message() << '\n';
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                        runtime = asset_graphics::detail::readCookedMaterial(reader, ref, caps);
    REQUIRE(runtime.ok());
    REQUIRE(runtime.value().surface.colorMaskEnabled);
    REQUIRE_EQ(runtime.value().surface.colorMaskSecondary[0], 2.f);
    REQUIRE_EQ(runtime.value().surface.albedoTextureStrength, .25f);
    REQUIRE(runtime.value().images[1].has_value());
    auto image = reader.read(*runtime.value().images[1], "eve.image/3", caps, 1024 * 1024);
    REQUIRE(image.ok());
}
#endif

TEST_CASE("asset.import.gltfMaterialSupportsIdentityTransformAndRejectsBudget") {
    auto        request = materialTriangle();
    std::string text(request.documentBytes.begin(), request.documentBytes.end());
    auto        at = text.find("\"baseColorTexture\":{\"index\":0}");
    REQUIRE(at != std::string::npos);
    text.replace(at, std::string("\"baseColorTexture\":{\"index\":0}").size(),
                 "\"baseColorTexture\":{\"index\":0,\"extensions\":{\"KHR_texture_transform\":{}}}");
    request.documentBytes.assign(text.begin(), text.end());
    REQUIRE(prepareGltfImport(request).ok());
    request                      = materialTriangle();
    request.limits.maximumAssets = 2;
    REQUIRE(!prepareGltfImport(request).ok());
}

TEST_CASE("asset.import.gltfMaterialExtensionsRetainParametersAndTextures") {
    auto request = materialTriangle();
    auto parsed  = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(parsed.ok());
    auto& root = *parsed.value().getIf<Value::Object>();
    auto& mat  = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    auto  ext  = Value::fromJson(
        R"({"KHR_materials_specular":{"specularFactor":0.4,"specularColorFactor":[2,1,0.5],"specularTexture":{"index":0},"specularColorTexture":{"index":0}},"KHR_materials_anisotropy":{"anisotropyStrength":0.7,"anisotropyRotation":-1.5,"anisotropyTexture":{"index":0}},"KHR_materials_clearcoat":{"clearcoatFactor":0.8,"clearcoatRoughnessFactor":0.2,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0},"clearcoatNormalTexture":{"index":0,"scale":0.3}},"KHR_materials_emissive_strength":{"emissiveStrength":3},"KHR_materials_ior":{"ior":1.7}})");
    REQUIRE(ext.ok());
    mat["extensions"]          = std::move(ext).takeValue();
    root["extensionsRequired"] = Value(Value::Array{Value("KHR_materials_specular"), Value("KHR_materials_ior")});
    auto text                  = parsed.value().toJson();
    REQUIRE(text.ok());
    request.documentBytes.assign(text.value().begin(), text.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool checked = false;
    for (const auto& entry : imported.value().entries) {
        if (!entry.path.ends_with("/asset.json")) continue;
        auto value = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
        REQUIRE(value.ok());
        auto* o = value.value().getIf<Value::Object>();
        if (o->at("schema").asString() != "eve.material") continue;
        REQUIRE_EQ(o->at("schemaVersion").asInt(), int64_t(15));
        REQUIRE(!o->at("alphaToCoverage").asBool());
        REQUIRE_EQ(o->at("specularFactor").asDouble(), 0.4);
        REQUIRE_EQ(o->at("anisotropyRotation").asDouble(), -1.5);
        REQUIRE_EQ(o->at("emissiveStrength").asDouble(), 3.0);
        REQUIRE_EQ(o->at("ior").asDouble(), 1.7);
        REQUIRE(o->contains("clearcoatNormalTexture"));
        REQUIRE_EQ(o->at("clearcoatNormalScale").asDouble(), 0.3);
        checked = true;
    }
    REQUIRE(checked);
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto decoded = asset::parseEvaArchive(archive.value());
    REQUIRE(decoded.ok());
    auto profile = asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = asset::cookEvaToEvpack(decoded.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    asset::EvpackResourceReader reader(std::make_shared<const asset::Evpack>(std::move(pack).takeValue()));
    asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                        payload =
        reader.read(imported.value().manifest.entrypoints.at("material:0"), "eve.material/15", caps, 1024 * 1024);
    REQUIRE(payload.ok());
#ifdef EVE_TEST_MESH_UPLOAD
    auto runtime = eve::asset_graphics::detail::readCookedMaterial(
        reader, imported.value().manifest.entrypoints.at("material:0"), caps);
    REQUIRE(runtime.ok());
    REQUIRE_EQ(runtime.value().surface.specularFactor, .4f);
    REQUIRE_EQ(runtime.value().surface.specularColor[0], 2.f);
    REQUIRE_EQ(runtime.value().surface.anisotropyStrength, .7f);
    REQUIRE_EQ(runtime.value().surface.clearcoatNormalScale, .3f);
    REQUIRE(runtime.value().images[10].has_value());
    REQUIRE(!runtime.value().surface.textures[6].srgbDecode);
#endif
    bool retained = false;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Definition) continue;
        auto value = asset::decodeRuntimeDefinition(chunk.bytes);
        REQUIRE(value.ok());
        auto& material = *value.value().getIf<Value::Object>();
        REQUIRE_EQ(material.at("specularColorFactor").getIf<Value::Array>()->at(0).asDouble(), 2.0);
        REQUIRE_EQ(material.at("clearcoatNormalScale").asDouble(), 0.3);
        auto imageRef = AssetRef::parse(material.at("specularColorTexture").asString());
        REQUIRE(imageRef.ok());
        auto image = reader.read(imageRef.value(), "eve.image/3", caps, 1024 * 1024);
        REQUIRE(image.ok());
        retained = true;
    }
    REQUIRE(retained);
}

TEST_CASE("asset.import.gltfExtensionsRejectInvalidInputs") {
    for (const char* ext :
         {R"({"KHR_materials_ior":{"ior":0.5}})", R"({"KHR_materials_specular":{"specularFactor":2}})",
          R"({"KHR_materials_anisotropy":{"anisotropyStrength":-1}})",
          R"({"KHR_materials_clearcoat":{"clearcoatTexture":{"index":99}}})",
          R"({"KHR_materials_emissive_strength":{"emissiveStrength":-1}})",
          R"({"KHR_materials_unlit":[],"KHR_materials_specular":{}})",
          R"({"KHR_materials_unlit":{},"KHR_materials_specular":{}})", R"({"EXT_unknown":{}})"}) {
        auto request  = materialTriangle();
        request.mode  = GltfImportMode::Strict;
        auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
        REQUIRE(document.ok());
        auto extension = Value::fromJson(ext);
        REQUIRE(extension.ok());
        auto& mat = *document.value()
                         .getIf<Value::Object>()
                         ->at("materials")
                         .getIf<Value::Array>()
                         ->at(0)
                         .getIf<Value::Object>();
        mat["extensions"] = std::move(extension).takeValue();
        auto text         = document.value().toJson();
        REQUIRE(text.ok());
        request.documentBytes.assign(text.value().begin(), text.value().end());
        REQUIRE(!prepareGltfImport(request).ok());
    }
}
TEST_CASE("asset.import.gltfUnlitAndTextureTransformSurvive") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& mat =
        *document.value().getIf<Value::Object>()->at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    mat["extensions"]      = Value(Value::Object{{"KHR_materials_unlit", Value(Value::Object{})}});
    auto textureDefinition = Value::fromJson(
        R"({"index":0,"texCoord":1,"extensions":{"KHR_texture_transform":{"offset":[-2,3],"scale":[2,-4],"rotation":-1.2,"texCoord":0}}})");
    REQUIRE(textureDefinition.ok());
    mat.at("pbrMetallicRoughness").getIf<Value::Object>()->at("baseColorTexture") =
        std::move(textureDefinition).takeValue();
    auto text = document.value().toJson();
    REQUIRE(text.ok());
    request.documentBytes.assign(text.value().begin(), text.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material") {
            for (const auto& entry : imported.value().entries)
                if (entry.path == a.definition) {
                    auto value = Value::fromJson(std::string(entry.bytes.begin(), entry.bytes.end()));
                    REQUIRE(value.ok());
                    auto& o = *value.value().getIf<Value::Object>();
                    REQUIRE_EQ(o.at("shadingModel").asString(), std::string("unlit"));
                    auto& transform = *o.at("baseColorTextureTransform").getIf<Value::Object>();
                    REQUIRE_EQ(transform.at("rotation").asDouble(), -1.2);
                    REQUIRE_EQ(transform.at("offset").getIf<Value::Array>()->at(0).asDouble(), -2.0);
                    REQUIRE_EQ(transform.at("scale").getIf<Value::Array>()->at(1).asDouble(), -4.0);
                    found = true;
                }
        }
    REQUIRE(found);
}

TEST_CASE("asset.import.gltfTextureTransformRejectsUnavailableUvSet") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& root    = *document.value().getIf<Value::Object>();
    auto& mat     = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    auto  texture = Value::fromJson(R"({"index":0,"extensions":{"KHR_texture_transform":{"texCoord":1}}})");
    REQUIRE(texture.ok());
    mat.at("pbrMetallicRoughness").getIf<Value::Object>()->at("baseColorTexture") = std::move(texture).takeValue();
    auto json                                                                     = document.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto rejected = prepareGltfImport(request);
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.error()->message().find("UV set") != std::string::npos);
}
TEST_CASE("asset.import.gltfMaterialSelectsNonzeroUvWithoutTransform") {
    auto request  = materialTriangle();
    auto document = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(document.ok());
    auto& root  = *document.value().getIf<Value::Object>();
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
    attrs["TEXCOORD_2"] = Value(int64_t(1));
    auto& material      = *root.at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    material.at("pbrMetallicRoughness")
        .getIf<Value::Object>()
        ->at("baseColorTexture")
        .getIf<Value::Object>()
        ->emplace("texCoord", Value(int64_t(2)));
    auto json = document.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool found = false;
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material")
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto d = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(d.ok());
                    REQUIRE_EQ(d.value().getIf<Value::Object>()->at("baseColorTextureTexCoord").asInt(), int64_t(2));
                    found = true;
                }
    REQUIRE(found);
}

TEST_CASE("asset.import.gltfPermissiveClampsSpecularAndWarns") {
    auto request = materialTriangle();
    auto doc     = Value::fromJson(std::string(request.documentBytes.begin(), request.documentBytes.end()));
    REQUIRE(doc.ok());
    auto& mat =
        *doc.value().getIf<Value::Object>()->at("materials").getIf<Value::Array>()->at(0).getIf<Value::Object>();
    mat["extensions"] =
        Value(Value::Object{{"KHR_materials_specular", Value(Value::Object{{"specularFactor", Value(38406.7422)}})}});
    auto json = doc.value().toJson();
    REQUIRE(json.ok());
    request.documentBytes.assign(json.value().begin(), json.value().end());
    auto imported = prepareGltfImport(request);
    REQUIRE(imported.ok());
    bool warned = false, clamped = false;
    for (const auto& finding : imported.value().findings)
        if (finding.severity == ImportSeverity::Warning) {
            REQUIRE(finding.feature.find("specularFactor") != std::string::npos);
            warned = true;
        }
    for (const auto& a : imported.value().manifest.assets)
        if (a.type == "eve.material")
            for (const auto& e : imported.value().entries)
                if (e.path == a.definition) {
                    auto d = Value::fromJson(std::string(e.bytes.begin(), e.bytes.end()));
                    REQUIRE(d.ok());
                    REQUIRE_EQ(d.value().getIf<Value::Object>()->at("specularFactor").asDouble(), 1.0);
                    clamped = true;
                }
    REQUIRE(warned);
    REQUIRE(clamped);
    auto archive = asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(archive.ok());
    auto decoded = asset::parseEvaArchive(archive.value());
    REQUIRE(decoded.ok());
    bool persisted = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path.find("report") != std::string::npos) {
            std::string report(entry.bytes.begin(), entry.bytes.end());
            if (report.find("warning") != std::string::npos && report.find("specularFactor") != std::string::npos)
                persisted = true;
        }
    REQUIRE(persisted);
    request.mode = GltfImportMode::Strict;
    REQUIRE(!prepareGltfImport(request).ok());
}
}  // namespace test_asset_import_gltf_material


// Contract section: asset import tga
#include "PrivateTestScope.h"
#include "asset/CanonicalImageCook.h"
#include "asset/SourceTga.h"

// asset_import_tga.cpp
namespace test_asset_import_tga {
using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(bool rle, unsigned channels, unsigned orientation) {
    std::vector<std::uint8_t> bytes(18, 0);
    bytes[2]  = rle ? 10 : 2;
    bytes[12] = 2;
    bytes[14] = 2;
    bytes[16] = std::uint8_t(channels * 8);
    bytes[17] = std::uint8_t(orientation | (channels == 4 ? 8 : 0));
    if (rle) bytes.push_back(0x81);
    const unsigned copies = rle ? 1 : 2;
    for (unsigned i = 0; i < copies; ++i) {
        bytes.insert(bytes.end(), {30, 20, 10});
        if (channels == 4) bytes.push_back(40);
    }
    if (rle) bytes.push_back(1);
    bytes.insert(bytes.end(), {70, 60, 50});
    if (channels == 4) bytes.push_back(80);
    bytes.insert(bytes.end(), {110, 100, 90});
    if (channels == 4) bytes.push_back(120);
    return bytes;
}
}  // namespace
TEST_CASE("asset.import.tgaRawAndRlePreserveOrientationAndAlpha") {
    for (bool rle : {false, true})
        for (unsigned channels : {3u, 4u})
            for (unsigned orientation : {0u, 16u, 32u, 48u}) {
                auto decoded = asset::detail::decodeTgaRgba8(fixture(rle, channels, orientation), 1024);
                REQUIRE(decoded.ok());
                const std::vector<std::uint8_t> source{10, 20, 30, 40, 10, 20,  30,  40,
                                                       50, 60, 70, 80, 90, 100, 110, 120};
                for (unsigned i = 0; i < 4; ++i) {
                    const auto x = (orientation & 16) ? 1 - i % 2 : i % 2, y = (orientation & 32) ? i / 2 : 1 - i / 2;
                    for (unsigned c = 0; c < 4; ++c)
                        REQUIRE_EQ(decoded.value().rgba[(y * 2 + x) * 4 + c],
                                   channels == 3 && c == 3 ? std::uint8_t(255) : source[i * 4 + c]);
                }
            }
}
TEST_CASE("asset.import.tgaRejectsBadPacketsAndCooksLinearPixels") {
    auto source = fixture(true, 4, 32);
    for (std::size_t size = 0; size < source.size(); ++size)
        REQUIRE(!asset::detail::decodeTgaRgba8(std::span(source).first(size), 1024).ok());
    REQUIRE(!asset::detail::decodeTgaRgba8(source, 39).ok());
    auto overflow = source;
    overflow[18]  = 255;
    REQUIRE(!asset::detail::decodeTgaRgba8(overflow, 1024).ok());
    auto interleaved = source;
    interleaved[17] |= 64;
    REQUIRE(!asset::detail::decodeTgaRgba8(interleaved, 1024).ok());
    auto footer = source;
    footer.insert(footer.end(), {255, 255, 255, 255, 0, 0, 0, 0});
    const std::string signature("TRUEVISION-XFILE.\0", 18);
    footer.insert(footer.end(), signature.begin(), signature.end());
    REQUIRE(!asset::detail::decodeTgaRgba8(footer, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tga.test", "1.0.0", {}};
    request.sourceName   = "mask.tga";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    bool checked = false;
    for (const auto& entry : imported.value().entries)
        if (entry.path == imported.value().manifest.assets.front().definition) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk[28], std::uint8_t(10));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[43], std::uint8_t(120));
            checked = true;
        }
    REQUIRE(checked);
}
}  // namespace test_asset_import_tga


// Contract section: asset import tiff
#include <zlib.h>
#include <array>
#include "PrivateTestScope.h"
#include "asset/SourceTiff.h"

// asset_import_tiff.cpp
namespace test_asset_import_tiff {
using namespace eve;
namespace {
std::vector<std::uint8_t> fixture(unsigned compression, unsigned bits, bool little, bool reverse = false) {
    std::vector<std::uint8_t> raw;
    const auto                put = [&](std::vector<std::uint8_t>& v, unsigned n, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) v.push_back(std::uint8_t(n >> ((little ? i : bytes - 1 - i) * 8)));
    };
    for (auto value : {10u, 20u, 30u, 40u, 90u, 90u, 90u, 90u}) put(raw, bits == 8 ? value : value * 257, bits / 8);
    std::vector<std::uint8_t> encoded;
    if (compression == 1)
        encoded = raw;
    else if (compression == 8) {
        uLongf length = compressBound(uLong(raw.size()));
        encoded.resize(length);
        REQUIRE_EQ(compress2(encoded.data(), &length, raw.data(), uLong(raw.size()), 6), Z_OK);
        encoded.resize(length);
    } else {
        std::vector<unsigned> codes{256};
        for (auto v : raw) codes.push_back(v);
        codes.push_back(257);
        unsigned acc = 0, count = 0;
        for (auto code : codes) {
            acc = (acc << 9) | code;
            count += 9;
            while (count >= 8) {
                count -= 8;
                encoded.push_back(std::uint8_t(acc >> count));
            }
        }
        if (count) encoded.push_back(std::uint8_t(acc << (8 - count)));
    }
    constexpr unsigned        tags = 12, bitsOffset = 8 + 2 + tags * 12 + 4, stripOffset = bitsOffset + 8;
    std::vector<std::uint8_t> out{std::uint8_t(little ? 'I' : 'M'), std::uint8_t(little ? 'I' : 'M')};
    put(out, 42, 2);
    put(out, 8, 4);
    put(out, tags, 2);
    auto tag = [&](unsigned id, unsigned type, unsigned n, unsigned value) {
        put(out, id, 2);
        put(out, type, 2);
        put(out, n, 4);
        if (type == 3 && n == 1) {
            put(out, value, 2);
            put(out, 0, 2);
        } else
            put(out, value, 4);
    };
    tag(256, 4, 1, 2);
    tag(257, 4, 1, 1);
    tag(258, 3, 4, bitsOffset);
    tag(259, 3, 1, compression);
    tag(262, 3, 1, 2);
    tag(273, 4, 1, stripOffset);
    tag(274, 3, 1, reverse ? 2 : 1);
    tag(277, 3, 1, 4);
    tag(278, 4, 1, 1);
    tag(279, 4, 1, unsigned(encoded.size()));
    tag(317, 3, 1, 2);
    tag(338, 3, 1, 0);
    put(out, 0, 4);
    for (int i = 0; i < 4; ++i) put(out, bits, 2);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}
}  // namespace
TEST_CASE("asset.import.tiffPreservesFourthChannelPredictorAndEndian") {
    for (auto compression : {1u, 5u, 8u})
        for (auto bits : {8u, 16u})
            for (bool little : {false, true}) {
                auto source = fixture(compression, bits, little);
                auto result = asset::detail::decodeTiffRgba8(source, 1024);
                REQUIRE(result.ok());
                REQUIRE_EQ(result.value().rgba, (std::vector<std::uint8_t>{10, 20, 30, 40, 100, 110, 120, 130}));
                auto reversed = asset::detail::decodeTiffRgba8(fixture(compression, bits, little, true), 1024);
                REQUIRE(reversed.ok());
                REQUIRE_EQ(reversed.value().rgba, (std::vector<std::uint8_t>{100, 110, 120, 130, 10, 20, 30, 40}));
            }
}
TEST_CASE("asset.import.tiffRejectsTruncationAndBudgetAndCooksPixels") {
    const auto source = fixture(5, 8, true);
    for (std::size_t n = 0; n < source.size(); ++n) {
        auto truncated = asset::detail::decodeTiffRgba8(std::span(source).first(n), 1024);
        REQUIRE(!truncated.ok());
    }
    REQUIRE(!asset::detail::decodeTiffRgba8(source, 31).ok());
    auto malformed = source;
    malformed[4]   = 255;
    REQUIRE(!asset::detail::decodeTiffRgba8(malformed, 1024).ok());
    asset_import::ImageImportRequest request;
    request.package      = {*PersistentId::parse("11111111-2222-4333-8444-555555555555"), "tiff.test", "1.0.0", {}};
    request.sourceName   = "mask.tif";
    request.encodedBytes = source;
    request.colorSpace   = asset_import::ImageColorSpace::Linear;
    request.usage        = "mask";
    auto imported        = asset_import::prepareImageImport(request);
    REQUIRE(imported.ok());
    const auto path = imported.value().manifest.assets.front().definition;
    for (const auto& entry : imported.value().entries)
        if (entry.path == path) {
            auto cooked = asset::cookCanonicalImageRgba8(entry.bytes, source, 1024);
            REQUIRE(cooked.ok());
            REQUIRE_EQ(cooked.value().bulk.size(), std::size_t(36));
            REQUIRE_EQ(cooked.value().bulk[31], std::uint8_t(40));
            REQUIRE_EQ(cooked.value().bulk[35], std::uint8_t(130));
            const auto json =
                Value::fromJson(std::string(cooked.value().definition.begin(), cooked.value().definition.end()));
            REQUIRE(json.ok());
            REQUIRE_EQ(json.value().getIf<Value::Object>()->at("sourceEncoding").asString(), std::string("tiff"));
        }
}
}  // namespace test_asset_import_tiff


// Contract section: asset mesh attributes
#include <cmath>
#include <limits>
#include "PrivateTestScope.h"
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
#include "PrivateTestScope.h"

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


// Contract section: asset mesh uv
#include "PrivateTestScope.h"
#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/EvpackGraphicsLoader.h"
#endif
#include "PrivateTestScope.h"

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


}  // namespace test_asset_mesh_uv


// Contract section: asset migration
#include "PrivateTestScope.h"
#include "asset/AssetMigration.h"
#include "data/HashFunction.h"

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
