#include "common/Value.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadGeometrySources.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <utility>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
MeshBuild bakedStraight() {
    auto network = RoadNetwork::makeStraight();
    REQUIRE(network.ok());
    auto baked = bakeRoadNetwork(network.value());
    REQUIRE(baked.ok());
    return std::move(baked.value().mesh);
}

eve::Value sourceDocument(const MeshBuild& mesh) {
    auto parsed = eve::Value::fromJson(mesh.getMeta("road.geometrySources", ""));
    REQUIRE(parsed.ok());
    return parsed.value();
}

void setSourceDocument(MeshBuild& mesh, const eve::Value& document) {
    auto json = document.toJson();
    REQUIRE(json.ok());
    mesh.setMeta("road.geometrySources", json.value());
}
}  // namespace

TEST_CASE("procgen.road.sources.bakedPartitionIsContiguousAndNamesAuthorObjects") {
    auto network = RoadNetwork::makeCross(48.f, 2);
    REQUIRE(network.ok());
    auto baked = bakeRoadNetwork(network.value());
    REQUIRE(baked.ok());
    auto prepared = PreparedRoadGeometrySources::prepare(baked.value().mesh);
    REQUIRE(prepared.ok());
    std::uint32_t next  = 0;
    int           edges = 0, junctions = 0, derived = 0;
    for (const auto& span : prepared.value().spans()) {
        REQUIRE(span.firstTriangle == next);
        REQUIRE(span.triangleCount > 0);
        next += span.triangleCount;
        switch (span.kind) {
            case RoadGeometrySourceKind::Edge:
                REQUIRE(network.value().edgeResult(span.id).ok());
                ++edges;
                break;
            case RoadGeometrySourceKind::Junction:
                REQUIRE(network.value().nodeResult(span.id).ok());
                ++junctions;
                break;
            case RoadGeometrySourceKind::NetworkDerived:
                REQUIRE(span.id == 0);
                ++derived;
                break;
        }
    }
    REQUIRE(next == baked.value().mesh.getIndexCount() / 3);
    REQUIRE(edges > 0);
    REQUIRE(junctions > 0);
    REQUIRE(derived > 0);
}

TEST_CASE("procgen.road.sources.preparedSnapshotOwnsIntervalsAfterMeshMutation") {
    auto mesh     = bakedStraight();
    auto prepared = PreparedRoadGeometrySources::prepare(mesh);
    REQUIRE(prepared.ok());
    const auto count = prepared.value().spans().size();
    REQUIRE(count > 0);
    mesh.positions()[0] += .25f;
    auto stale = PreparedRoadGeometrySources::prepare(mesh);
    REQUIRE(!stale.ok());
    REQUIRE(stale.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    mesh.clear();
    REQUIRE(prepared.value().spans().size() == count);
}

TEST_CASE("procgen.road.sources.schemaRejectsMalformedIntervalsWithoutChangingMetadata") {
    auto mesh     = bakedStraight();
    auto original = sourceDocument(mesh);
    for (int mode = 0; mode < 6; ++mode) {
        auto  document = original;
        auto* spans    = document.find("spans")->getIf<eve::Value::Array>();
        REQUIRE(spans != nullptr);
        REQUIRE(!spans->empty());
        if (mode == 0) document.set("version", 2);
        if (mode == 1) spans->front().set("first", 1);
        if (mode == 2) spans->front().set("id", 0);
        if (mode == 3) spans->front().set("count", 0);
        if (mode == 4) spans->front().set("kind", 99);
        if (mode == 5) document.set("triangles", 0);
        setSourceDocument(mesh, document);
        const auto encoded = mesh.getMeta("road.geometrySources", "");
        REQUIRE(!PreparedRoadGeometrySources::prepare(mesh).ok());
        REQUIRE(mesh.getMeta("road.geometrySources", "") == encoded);
    }
}

TEST_CASE("procgen.road.sources.unknownFieldsAreAllowedButMissingMetadataIsUnsupported") {
    auto mesh     = bakedStraight();
    auto document = sourceDocument(mesh);
    document.set("futureField", 17);
    setSourceDocument(mesh, document);
    REQUIRE(PreparedRoadGeometrySources::prepare(mesh).ok());
    auto group = mesh.copyGroup(0);
    REQUIRE(group);
    REQUIRE(!PreparedRoadGeometrySources::prepare(*group).ok());
    MeshBuild bare;
    auto      missing = PreparedRoadGeometrySources::prepare(bare);
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().diagnostics().front().code() == eve::DiagnosticCode::Unsupported);
}

TEST_CASE("procgen.road.sources.recipeAttributionSurvivesMeshCopyAndDetectsBadIndices") {
    Params params;
    params.setString("scene", "straight");
    params.setBool("navigation", false);
    auto recipe = bakeRoadNetworkRecipe(params);
    REQUIRE(recipe.ok());
    MeshBuild copy     = recipe.value().mesh;
    auto prepared = PreparedRoadGeometrySources::prepare(copy);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().spans().empty());
    copy.indices()[0] = static_cast<std::uint32_t>(copy.getVertexCount());
    REQUIRE(!PreparedRoadGeometrySources::prepare(copy).ok());
}
