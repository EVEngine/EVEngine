#include "common/Value.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadGeometrySources.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
TEST_CASE("procgen.road.geometrySources.partitionCoversEdgesJunctionAndDerivedTriangles") {
    auto network = RoadNetwork::makeCross(48, 2);
    REQUIRE(network.ok());
    auto baked = bakeRoadNetwork(network.value());
    REQUIRE(baked.ok());
    auto prepared = PreparedRoadGeometrySources::prepare(baked.value().mesh);
    REQUIRE(prepared.ok());
    std::uint32_t cursor = 0;
    int           edges = 0, junctions = 0, derived = 0;
    for (std::size_t i = 0; i < prepared.value().spans().size(); ++i) {
        const auto& s = prepared.value().spans()[i];
        REQUIRE_EQ(s.firstTriangle, cursor);
        REQUIRE(s.triangleCount > 0);
        cursor += s.triangleCount;
        if (s.kind == RoadGeometrySourceKind::Edge) {
            ++edges;
            REQUIRE(network.value().edgeResult(s.id).ok());
        } else if (s.kind == RoadGeometrySourceKind::Junction) {
            ++junctions;
            REQUIRE(network.value().nodeResult(s.id).ok());
        } else {
            ++derived;
            REQUIRE_EQ(s.id, 0u);
        }
    }
    REQUIRE(edges > 0);
    REQUIRE(junctions > 0);
    REQUIRE(derived > 0);
    REQUIRE_EQ(cursor, static_cast<std::uint32_t>(baked.value().mesh.getIndexCount() / 3));
    const auto size = prepared.value().spans().size();
    baked.value().mesh.positions()[0] += .25f;
    auto stale = PreparedRoadGeometrySources::prepare(baked.value().mesh);
    REQUIRE(!stale.ok());
    REQUIRE(stale.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    baked.value().mesh.clear();
    REQUIRE_EQ(prepared.value().spans().size(), size);
}
TEST_CASE("procgen.road.geometrySources.schemaAndPartitionRejectBeforePublication") {
    auto network = RoadNetwork::makeStraight();
    REQUIRE(network.ok());
    auto baked = bakeRoadNetwork(network.value());
    REQUIRE(baked.ok());
    const auto original = baked.value().mesh.getMeta("road.geometrySources", "");
    auto       parsed   = eve::Value::fromJson(original);
    REQUIRE(parsed.ok());
    for (int mode = 0; mode < 5; ++mode) {
        auto  root  = parsed.value();
        auto* spans = root.find("spans")->getIf<eve::Value::Array>();
        REQUIRE(spans != nullptr);
        REQUIRE(!spans->empty());
        if (mode == 0) root.set("version", 2);
        if (mode == 1) spans->front().set("first", 1);
        if (mode == 2) spans->front().set("id", 0);
        if (mode == 3) spans->front().set("count", 0);
        if (mode == 4) spans->front().set("kind", 99);
        auto json = root.toJson();
        REQUIRE(json.ok());
        baked.value().mesh.setMeta("road.geometrySources", json.value());
        REQUIRE(!PreparedRoadGeometrySources::prepare(baked.value().mesh).ok());
        REQUIRE_EQ(baked.value().mesh.getMeta("road.geometrySources", ""), json.value());
    }
    auto root = parsed.value();
    root.set("futureField", 17);
    auto json = root.toJson();
    REQUIRE(json.ok());
    baked.value().mesh.setMeta("road.geometrySources", json.value());
    REQUIRE(PreparedRoadGeometrySources::prepare(baked.value().mesh).ok());
    auto group = baked.value().mesh.copyGroup(0);
    REQUIRE(group != nullptr);
    REQUIRE(!PreparedRoadGeometrySources::prepare(*group).ok());
    MeshBuild old;
    auto      missing = PreparedRoadGeometrySources::prepare(old);
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().diagnostics().front().code() == eve::DiagnosticCode::Unsupported);
}
TEST_CASE("procgen.road.geometrySources.recipeMeshPreservesPortableAttribution") {
    Params params;
    params.setString("scene", "straight");
    params.setBool("navigation", false);
    auto recipe = bakeRoadNetworkRecipe(params);
    REQUIRE(recipe.ok());
    auto json = eve::Value::fromJson(recipe.value().mesh.getMeta("road.geometrySources", ""));
    REQUIRE(json.ok());
    auto encoded = json.value().toJson();
    REQUIRE(encoded.ok());
    MeshBuild copy = recipe.value().mesh;
    copy.setMeta("road.geometrySources", encoded.value());
    auto prepared = PreparedRoadGeometrySources::prepare(copy);
    REQUIRE(prepared.ok());
    REQUIRE(!prepared.value().spans().empty());
    copy.indices()[0] = static_cast<std::uint32_t>(copy.getVertexCount());
    REQUIRE(!PreparedRoadGeometrySources::prepare(copy).ok());
}
