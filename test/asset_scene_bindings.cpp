#include "asset/RuntimeDefinition.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>

using namespace eve;
using namespace eve::asset;

namespace {
constexpr const char*    objectId = "018f6f22-2490-7ad2-bf58-4f1dbca31041";
constexpr const char*    assetId  = "018f6f22-2490-7ad2-bf58-4f1dbca31040";
const EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};

Value::Object definition() {
    return {{"schema", "eve.scene-template"},
            {"schemaVersion", 2},
            {"coordinateSystem", "right-handed-x-right-y-up-minus-z-forward"},
            {"nodes", Value::Array{Value::Object{{"objectId", objectId},
                                                 {"sourceFileId", 1},
                                                 {"parentSourceFileId", 0},
                                                 {"name", "Node"},
                                                 {"position", Value::Array{0, 0, 0}},
                                                 {"rotation", Value::Array{0, 0, 0, 1}},
                                                 {"scale", Value::Array{1, 1, 1}}}}},
            {"renderers", Value::Array{Value::Object{{"objectId", objectId},
                                                     {"mesh", std::string("asset://") + assetId},
                                                     {"material", std::string("asset://") + assetId},
                                                     {"enabled", true}}}}};
}

Result<asset_scene::LoadedSceneTemplate> load(Value::Object root, unsigned version = 2) {
    auto encoded = encodeRuntimeDefinition(Value(std::move(root)));
    REQUIRE(encoded.ok());
    EvpackBuild build;
    build.packageId = *PersistentId::parse(assetId);
    build.buildId   = *PersistentId::parse(objectId);
    build.variants  = {{"windows", "x86_64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}}};
    build.chunks.push_back({build.packageId,
                            "eve.scene-template",
                            SchemaVersion(version),
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(encoded).takeValue()});
    auto packed = buildEvpack(std::move(build));
    REQUIRE(packed.ok());
    auto parsed = parseEvpack(packed.value());
    REQUIRE(parsed.ok());
    EvpackResourceReader reader(std::make_shared<const Evpack>(std::move(parsed).takeValue()));
    auto                 ref = AssetRef::fromId(*PersistentId::parse(assetId));
    REQUIRE(ref.ok());
    return asset_scene::EvpackSceneTemplateLoader(reader).load(ref.value(), caps);
}

std::array<double, 4> yxz(double yaw, double pitch, double roll) {
    const double y = yaw / 2, x = pitch / 2, z = roll / 2;
    return {std::cos(y) * std::sin(x) * std::cos(z) + std::sin(y) * std::cos(x) * std::sin(z),
            std::sin(y) * std::cos(x) * std::cos(z) - std::cos(y) * std::sin(x) * std::sin(z),
            std::cos(y) * std::cos(x) * std::sin(z) - std::sin(y) * std::sin(x) * std::cos(z),
            std::cos(y) * std::cos(x) * std::cos(z) + std::sin(y) * std::sin(x) * std::sin(z)};
}
}  // namespace

TEST_CASE("asset.sceneBindingsRejectDanglingDuplicateAndUnversionedBindings") {
    auto valid = load(definition());
    REQUIRE(valid.ok());
    REQUIRE_EQ(valid.value().renderers.size(), std::size_t(1));
    auto root                                                                                 = definition();
    (*root.at("renderers").getIf<Value::Array>()->front().getIf<Value::Object>())["objectId"] = assetId;
    auto invalid                                                                              = load(root);
    REQUIRE(!invalid.ok());
    root                                                = definition();
    auto duplicate                                      = root.at("nodes").getIf<Value::Array>()->front();
    (*duplicate.getIf<Value::Object>())["sourceFileId"] = 2;
    root.at("nodes").getIf<Value::Array>()->push_back(duplicate);
    auto repeated = load(root);
    REQUIRE(!repeated.ok());
    root                  = definition();
    root["schemaVersion"] = 1;
    auto unversioned      = load(root, 1);
    REQUIRE(!unversioned.ok());
    root.erase("renderers");
    auto legacy = load(root, 1);
    REQUIRE(legacy.ok());
    REQUIRE(legacy.value().renderers.empty());
}

TEST_CASE("asset.sceneBindingsPreserveCombinedAndGimbalRotations") {
    constexpr double radians = 0.017453292519943295;
    for (double pitch : {37.0, 90.0, -90.0}) {
        const auto expected = yxz(23 * radians, pitch * radians, -19 * radians);
        auto       root     = definition();
        (*root.at("nodes").getIf<Value::Array>()->front().getIf<Value::Object>())["rotation"] =
            Value::Array{expected[0], expected[1], expected[2], expected[3]};
        auto loaded = load(root);
        REQUIRE(loaded.ok());
        const auto& node   = loaded.value().root.children.front();
        const auto  actual = yxz(node.yaw * radians, node.pitch * radians, node.roll * radians);
        double      dot    = 0;
        for (std::size_t i = 0; i < 4; ++i) dot += expected[i] * actual[i];
        REQUIRE(std::abs(std::abs(dot) - 1.0) < 1e-6);
    }
}
