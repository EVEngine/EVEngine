#include "procgen/GeneratedArtifact.h"
#include "procgen/road/RoadGeometrySources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
TEST_CASE("procgen.road.geometrySources.artifactRootSurvivesStoreRoundtrip") {
    Params params;
    params.setString("scene", "straight");
    params.setInt("lodCount", 2);
    auto id = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    REQUIRE(id.has_value());
    auto generated = generateRoadNetworkArtifact(params, *id);
    REQUIRE(generated.ok());
    REQUIRE(generated.value().metadata.at("geometrySourceSpanCount").asInt() > 0);
    const auto* composite = std::get_if<CompositeArtifact>(&generated.value().payload);
    REQUIRE(composite != nullptr);
    const auto* originalPart = composite->find("mesh");
    REQUIRE(originalPart != nullptr);
    const auto* originalMesh = std::get_if<MeshBuild>(&originalPart->payload);
    REQUIRE(originalMesh != nullptr);
    const auto originalColors = originalMesh->colors();
    REQUIRE(!originalColors.empty());
    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    auto state = store.snapshotState();
    REQUIRE(state.ok());
    auto json = state.value().toJson();
    REQUIRE(json.ok());
    auto decoded = eve::Value::fromJson(json.value());
    REQUIRE(decoded.ok());
    ArtifactStore restored;
    REQUIRE(restored.restoreState(decoded.value()).ok());
    auto* root = restored.findPart(id->child("mesh"));
    REQUIRE(root != nullptr);
    const auto* mesh = std::get_if<MeshBuild>(&root->payload);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->colors() == originalColors);
    auto prepared = PreparedRoadGeometrySources::prepare(*mesh);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().spans().empty());
}

namespace {
int editColorFields(eve::Value& value, bool legacy) {
    int count = 0;
    if (auto* object = value.getIf<eve::Value::Object>()) {
        if (auto it = object->find("colors"); it != object->end()) {
            ++count;
            if (legacy)
                object->erase(it);
            else
                it->second = eve::Value("invalid-color-stream");
        }
        for (auto& [key, child] : *object) count += editColorFields(child, legacy);
    } else if (auto* array = value.getIf<eve::Value::Array>()) {
        for (auto& child : *array) count += editColorFields(child, legacy);
    }
    return count;
}
}  // namespace
TEST_CASE("procgen.road.geometrySources.artifactColorsLegacyAndAtomicRejection") {
    Params params;
    params.setString("scene", "straight");
    auto id = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd192");
    REQUIRE(id.has_value());
    auto generated = generateRoadNetworkArtifact(params, *id);
    REQUIRE(generated.ok());
    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    auto state = store.snapshotState();
    REQUIRE(state.ok());
    auto baseline = state.value().toJson();
    REQUIRE(baseline.ok());
    auto malformed = state.value();
    REQUIRE(editColorFields(malformed, false) > 0);
    REQUIRE(!store.restoreState(malformed).ok());
    auto after = store.snapshotState();
    REQUIRE(after.ok());
    auto afterJson = after.value().toJson();
    REQUIRE(afterJson.ok());
    REQUIRE_EQ(afterJson.value(), baseline.value());
    auto legacy = state.value();
    REQUIRE(editColorFields(legacy, true) > 0);
    ArtifactStore restored;
    REQUIRE(restored.restoreState(legacy).ok());
    const auto* part = restored.findPart(id->child("mesh"));
    REQUIRE(part != nullptr);
    const auto* mesh = std::get_if<MeshBuild>(&part->payload);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->colors().empty());
    // Historical snapshots lost colors: the old digest must not be silently accepted.
    REQUIRE(!PreparedRoadGeometrySources::prepare(*mesh).ok());
}
