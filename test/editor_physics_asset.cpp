#include "editor/EditorPhysicsAsset.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
AssetRecord collider(const char* id, const char* uri, EditorValue metadata) {
    AssetRecord record;
    record.guid = AssetGuid(id); record.logicalUri = uri; record.typeId = "mesh";
    record.sourceUri = std::string("source/") + id; record.sourceHash = "hash";
    record.importerId = "physics-test"; record.artifacts = {std::string("artifact/") + id};
    record.metadata = std::move(metadata); return record;
}
}

TEST_CASE("editor.physics.asset_resolver_validates_triangle_geometry_and_uri_lookup") {
    MemoryAssetDatabase database;
    EditorValue::Array vertices{0.0,0.0,0.0, 1.0,0.0,0.0, 0.0,1.0,0.0};
    EditorValue::Array indices{int64_t{0},int64_t{1},int64_t{2}};
    CHECK(database.publish(collider("triangle", "physics/triangle", EditorValue::Object{
        {"kind","triangle-mesh"},{"vertices",vertices},{"indices",indices}})).isAccepted());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto resolved = resolver.resolve("physics/triangle", "triangle-mesh");
    REQUIRE(resolved.value);
    CHECK_EQ(resolved.value->vertices.size(), 9U);
    CHECK_EQ(resolved.value->indices.size(), 3U);
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/triangle", "convex-hull").status),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.physics.asset_resolver_rejects_out_of_range_indices_and_builds_height_metadata") {
    MemoryAssetDatabase database;
    CHECK(database.publish(collider("bad", "physics/bad", EditorValue::Object{
        {"kind","triangle-mesh"}, {"vertices",EditorValue::Array{0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0}},
        {"indices",EditorValue::Array{int64_t{0},int64_t{1},int64_t{9}}}})).isAccepted());
    CHECK(database.publish(collider("terrain", "physics/terrain", EditorValue::Object{
        {"kind","height-field"},{"countX",int64_t{2}},{"countZ",int64_t{2}},
        {"cellSizeX",1.0},{"cellSizeZ",2.0},{"heights",EditorValue::Array{0.0,1.0,2.0,3.0}}})).isAccepted());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    CHECK_EQ(static_cast<int>(resolver.resolve("bad", "triangle-mesh").status),
             static_cast<int>(EditorStatus::Rejected));
    auto height = resolver.resolve("terrain", "height-field"); REQUIRE(height.value);
    CHECK_EQ(height.value->countX, 2); CHECK_EQ(height.value->countZ, 2);
    CHECK_EQ(height.value->minimumHeight, 0.0F); CHECK_EQ(height.value->maximumHeight, 3.0F);
}

TEST_CASE("editor.physics.asset_resolver_validates_2d_polygon_and_chain_metadata") {
    MemoryAssetDatabase database;
    CHECK(database.publish(collider("polygon", "physics/polygon", EditorValue::Object{
        {"kind", "polygon"},
        {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 2.0, 1.0, 0.0, 1.0}}})).isAccepted());
    CHECK(database.publish(collider("chain", "physics/chain", EditorValue::Object{
        {"kind", "chain"}, {"loop", true},
        {"vertices", EditorValue::Array{0.0, 0.0, 1.0, 0.0, 1.0, 1.0}}})).isAccepted());
    AssetDatabasePhysicsColliderResolver resolver(&database);
    auto polygon = resolver.resolve("physics/polygon", "polygon");
    REQUIRE(polygon.value);
    CHECK_EQ(polygon.value->vertices.size(), size_t{8});
    auto chain = resolver.resolve("physics/chain", "chain");
    REQUIRE(chain.value);
    CHECK(chain.value->loop);

    CHECK(database.publish(collider("concave", "physics/concave", EditorValue::Object{
        {"kind", "polygon"},
        {"vertices", EditorValue::Array{0.0, 0.0, 2.0, 0.0, 1.0, 0.5, 2.0, 1.0,
                                         0.0, 1.0}}})).isAccepted());
    CHECK_EQ(static_cast<int>(resolver.resolve("physics/concave", "polygon").status),
             static_cast<int>(EditorStatus::Rejected));
}
