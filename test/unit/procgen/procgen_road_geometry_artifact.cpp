#include "procgen/GeneratedArtifact.h"
#include "procgen/road/RoadGeometrySources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <utility>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
ArtifactId artifactId() {
    const auto parsed = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    REQUIRE(parsed.has_value());
    return *parsed;
}

Params straightRoad() {
    Params params;
    params.setString("scene", "straight");
    params.setInt("lodCount", 2);
    return params;
}

const MeshBuild* rootMesh(const GeneratedArtifact& artifact) {
    const auto* composite = std::get_if<CompositeArtifact>(&artifact.payload);
    if (!composite) return nullptr;
    const auto* part = composite->find("mesh");
    return part ? std::get_if<MeshBuild>(&part->payload) : nullptr;
}

int changeColors(eve::Value& value, bool remove) {
    int changed = 0;
    if (auto* object = value.getIf<eve::Value::Object>()) {
        if (auto found = object->find("colors"); found != object->end()) {
            ++changed;
            if (remove)
                object->erase(found);
            else
                found->second = eve::Value("invalid-color-stream");
        }
        for (auto& [key, child] : *object) changed += changeColors(child, remove);
    } else if (auto* array = value.getIf<eve::Value::Array>()) {
        for (auto& child : *array) changed += changeColors(child, remove);
    }
    return changed;
}
}  // namespace

TEST_CASE("procgen.road.artifact_generation_contains_preparable_root_attribution") {
    auto generated = generateRoadNetworkArtifact(straightRoad(), artifactId());
    REQUIRE(generated.ok());
    REQUIRE(generated.value().metadata.at("geometrySourceSpanCount").asInt() > 0);
    const auto* mesh = rootMesh(generated.value());
    REQUIRE(mesh != nullptr);
    REQUIRE(!mesh->colors().empty());
    const auto prepared = PreparedRoadGeometrySources::prepare(*mesh);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().spans().empty());
    std::size_t covered = 0;
    for (const auto& span : prepared.value().spans()) {
        CHECK_EQ(span.firstTriangle, covered);
        REQUIRE(span.triangleCount > 0);
        covered += span.triangleCount;
    }
    CHECK_EQ(covered, static_cast<std::size_t>(mesh->getIndexCount() / 3));
}

TEST_CASE("procgen.road.artifact_store_round_trip_preserves_mesh_and_source_spans") {
    const auto id        = artifactId();
    auto       generated = generateRoadNetworkArtifact(straightRoad(), id);
    REQUIRE(generated.ok());
    const auto* mesh = rootMesh(generated.value());
    REQUIRE(mesh != nullptr);
    const auto originalColors  = mesh->colors();
    const auto originalSources = PreparedRoadGeometrySources::prepare(*mesh);
    REQUIRE(originalSources.ok());

    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    const auto state = store.snapshotState();
    REQUIRE(state.ok());
    const auto json = state.value().toJson();
    REQUIRE(json.ok());
    const auto decoded = eve::Value::fromJson(json.value());
    REQUIRE(decoded.ok());
    ArtifactStore restored;
    REQUIRE(restored.restoreState(decoded.value()).ok());
    const auto* restoredPart = restored.findPart(id.child("mesh"));
    REQUIRE(restoredPart != nullptr);
    const auto* restoredMesh = std::get_if<MeshBuild>(&restoredPart->payload);
    REQUIRE(restoredMesh != nullptr);
    CHECK_EQ(restoredMesh->colors(), originalColors);
    const auto restoredSources = PreparedRoadGeometrySources::prepare(*restoredMesh);
    REQUIRE(restoredSources.ok());
    REQUIRE_EQ(restoredSources.value().spans().size(), originalSources.value().spans().size());
    for (std::size_t i = 0; i < originalSources.value().spans().size(); ++i) {
        CHECK_EQ(restoredSources.value().spans()[i].firstTriangle, originalSources.value().spans()[i].firstTriangle);
        CHECK_EQ(restoredSources.value().spans()[i].triangleCount, originalSources.value().spans()[i].triangleCount);
    }
}

TEST_CASE("procgen.road.artifact_restore_rejects_malformed_color_stream_atomically") {
    auto generated = generateRoadNetworkArtifact(straightRoad(), artifactId());
    REQUIRE(generated.ok());
    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    const auto state = store.snapshotState();
    REQUIRE(state.ok());
    const auto before = state.value().toJson();
    REQUIRE(before.ok());

    auto malformed = state.value();
    REQUIRE(changeColors(malformed, false) > 0);
    CHECK(!store.restoreState(malformed).ok());
    const auto after = store.snapshotState();
    REQUIRE(after.ok());
    const auto afterJson = after.value().toJson();
    REQUIRE(afterJson.ok());
    CHECK_EQ(afterJson.value(), before.value());
}

TEST_CASE("procgen.road.artifact_legacy_color_omission_cannot_validate_stale_digest") {
    const auto id        = artifactId();
    auto       generated = generateRoadNetworkArtifact(straightRoad(), id);
    REQUIRE(generated.ok());
    ArtifactStore store;
    REQUIRE(store.publish(std::move(generated).takeValue()).ok());
    auto state = store.snapshotState();
    REQUIRE(state.ok());
    auto legacy = state.value();
    REQUIRE(changeColors(legacy, true) > 0);

    ArtifactStore restored;
    REQUIRE(restored.restoreState(legacy).ok());
    const auto* part = restored.findPart(id.child("mesh"));
    REQUIRE(part != nullptr);
    const auto* mesh = std::get_if<MeshBuild>(&part->payload);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->colors().empty());
    const auto prepared = PreparedRoadGeometrySources::prepare(*mesh);
    CHECK(!prepared.ok());
}
