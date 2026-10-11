#include "asset/AssetCooker.h"
#include "asset/AssetMigration.h"
#include "asset/CanonicalMesh.h"
#include "asset/graphics/VegetationAsset.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"
#include "asset/import/VegetationPreset.h"
#include "zeroerr/unittest.h"

#include <chrono>

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
