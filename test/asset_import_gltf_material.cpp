#ifdef EVE_TEST_MESH_UPLOAD
#include "asset/graphics/CookedMaterial.h"
#endif
#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/RuntimeDefinition.h"
#include "asset/import/AssetImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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
        REQUIRE_EQ(o->at("schemaVersion").asInt(), int64_t(2));
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
        reader.read(imported.value().manifest.entrypoints.at("material:0"), "eve.material/2", caps, 1024 * 1024);
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
        auto image = reader.read(imageRef.value(), "eve.image/2", caps, 1024 * 1024);
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
        auto& mat         = *document.value()
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
    auto& root          = *document.value().getIf<Value::Object>();
    auto& attrs         = *root.at("meshes")
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
