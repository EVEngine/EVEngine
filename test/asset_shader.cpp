#include "asset/AssetCooker.h"
#include "asset/EvpackResourceReader.h"
#include "asset/ShaderAsset.h"
#include "asset/import/ShaderImporter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

namespace {
Value shaderDefinition() {
    auto value = Value::fromJson(R"({"schema":"eve.shader","schemaVersion":1,
      "interface":"mesh3d","format":"spirv-1.6",
      "vertex":[119734787,67072,0,2,0,327695,0,1,1852399981,0],
      "fragment":[119734787,67072,0,2,0,327695,4,1,1852399981,0],
      "parameters":[{"name":"strength","default":[1.0]}]})");
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}
}  // namespace

TEST_CASE("asset.shader decodes owning parameter and stage data") {
    auto result = decodeShaderAsset(shaderDefinition());
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().parameters.size(), std::size_t(1));
    REQUIRE_EQ(result.value().parameters[0].name, std::string("strength"));
    REQUIRE_EQ(result.value().parameters[0].defaults[0], 1.f);
    REQUIRE_EQ(result.value().vertex.size(), std::size_t(10));
}

TEST_CASE("asset.shader rejects future versions unknown fields and wrong stages") {
    auto  definition        = shaderDefinition();
    auto& fields            = *definition.getIf<Value::Object>();
    fields["schemaVersion"] = Value(std::int64_t(2));
    REQUIRE(!decodeShaderAsset(definition).ok());
    fields["schemaVersion"] = Value(std::int64_t(1));
    fields["unexpected"]    = Value(true);
    REQUIRE(!decodeShaderAsset(definition).ok());
    fields.erase("unexpected");
    fields["fragment"] = fields["vertex"];
    REQUIRE(!decodeShaderAsset(definition).ok());
}

TEST_CASE("asset.shader rejects duplicate parameters and exhausted ABI") {
    auto  definition = shaderDefinition();
    auto& fields     = *definition.getIf<Value::Object>();
    auto& parameters = *fields["parameters"].getIf<Value::Array>();
    parameters.push_back(parameters.front());
    REQUIRE(!decodeShaderAsset(definition).ok());
    parameters.clear();
    for (int i = 0; i < 33; ++i)
        parameters.emplace_back(
            Value::Object{{"name", Value("p" + std::to_string(i))}, {"default", Value(Value::Array{Value(0.0)})}});
    REQUIRE(!decodeShaderAsset(definition).ok());
}

TEST_CASE("asset.shader rejects truncated instructions and byte budgets") {
    auto              definition = shaderDefinition();
    ShaderAssetLimits limits;
    limits.maximumStageWords = 9;
    REQUIRE(!decodeShaderAsset(definition, limits).ok());
    auto& words = *definition.getIf<Value::Object>()->at("vertex").getIf<Value::Array>();
    words.pop_back();
    REQUIRE(!decodeShaderAsset(definition).ok());
}

TEST_CASE("asset.shader cooks reopens and loads beside Unity schema versions") {
    auto id = PersistentId::parse("550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(id);
    auto reference = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(reference.ok());
    auto json = shaderDefinition().toJson();
    REQUIRE(json.ok());
    EvaArchive source;
    source.manifest.packageId      = *id;
    source.manifest.packageName    = "shader.test";
    source.manifest.packageVersion = "1.0.0";
    const std::string path         = "assets/" + id->format() + "/asset.json";
    source.manifest.assets.push_back({reference.value(), "eve.shader", SchemaVersion(1), path, {}, {}});
    source.manifest.entrypoints.emplace("default", reference.value());
    source.entries.push_back({path, {json.value().begin(), json.value().end()}});
    auto profile = assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto first = cookEvaToEvpack(source, profile.value());
    REQUIRE(first.ok());
    auto second = cookEvaToEvpack(source, profile.value());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().bytes, second.value().bytes);
    auto package = parseEvpack(first.value().bytes);
    REQUIRE(package.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(package).takeValue()));
    EvpackCapabilities   capabilities{"windows", "x86_64", "vulkan", {"bc", "rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                 loaded = loadShaderAsset(reader, reference.value(), capabilities);
    REQUIRE(loaded.ok());
    REQUIRE_EQ(loaded.value().parameters[0].name, std::string("strength"));
    capabilities.graphics = "webgpu";
    REQUIRE(!loadShaderAsset(reader, reference.value(), capabilities).ok());
    auto web = assetCookProfileForTarget("web-wasm32-webgpu");
    REQUIRE(web.ok());
    REQUIRE(!cookEvaToEvpack(source, web.value()).ok());
}

TEST_CASE("asset.shader import preserves identity and rejects invalid candidates") {
    auto id = PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    REQUIRE(id);
    asset_import::ImportPackageIdentity identity{*id, "shader.test", "1.0.0", {}};
    auto                                definition = shaderDefinition();
    auto                                json       = definition.toJson();
    REQUIRE(json.ok());
    auto first = asset_import::prepareShaderImport(identity, json.value());
    REQUIRE(first.ok());
    auto second = asset_import::prepareShaderImport(identity, json.value());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().manifest.assets[0].asset, second.value().manifest.assets[0].asset);
    REQUIRE_EQ(first.value().manifest.assets[0].asset.id(), id->child("shader:default"));
    auto archive = buildEvaArchive(first.value().manifest, first.value().entries);
    REQUIRE(archive.ok());
    auto reopened = parseEvaArchive(archive.value());
    REQUIRE(reopened.ok());
    REQUIRE(!asset_import::prepareShaderImport(identity, "{}").ok());
    REQUIRE(!asset_import::prepareShaderImport({}, json.value()).ok());
}
