#include "editor/EditorMapDocument.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
class RoadSink final : public IMapRoadMeshSink {
public:
    EditorResult<void> publishRoad(const std::string&, const StableId& road,
                                   Revision revision, const EditorValue& mesh) override {
        published = road; publishedRevision = revision; value = mesh;
        return EditorResult<void>::applied();
    }
    EditorResult<void> removeRoad(const std::string&, const StableId&) override {
        return EditorResult<void>::applied();
    }
    StableId published;
    Revision publishedRevision = 0;
    EditorValue value;
};
}

TEST_CASE("editor.map.road_mesh_publication_and_object_import_are_revision_safe") {
    MapDocumentTarget map("city");
    auto roadLayer = map.makeCreateLayer({StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    REQUIRE(roadLayer.value); CHECK(map.applyDomainOperation(*roadLayer.value).accepted());
    auto objectLayer = map.makeCreateLayer({StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    REQUIRE(objectLayer.value); CHECK(map.applyDomainOperation(*objectLayer.value).accepted());
    MapRoadRecord road{StableId("main"), StableId("roads"), "Main", "asphalt", false,
        {{StableId("a"), 0.0, 0.0, 0.0, 4.0}, {StableId("b"), 10.0, 0.0, 0.0, 4.0}}};
    auto setRoad = map.makeSetRoad(road); REQUIRE(setRoad.value); CHECK(map.applyDomainOperation(*setRoad.value).accepted());
    RoadSink sink; MapRoadMeshPublisher publisher;
    CHECK(publisher.publish(map, StableId("main"), map.revision(), sink).accepted());
    CHECK_EQ(sink.published.value(), "main"); CHECK_EQ(sink.publishedRevision, map.revision());
    CHECK_EQ(static_cast<int>(publisher.publish(map, StableId("main"), map.revision() - 1, sink).status),
             static_cast<int>(EditorStatus::Conflict));

    MapObjectImporter importer;
    const auto imported = importer.plan(map, StableId("objects"),
        {{"tree-01", "asset://tree", 2.0, 0.0, 3.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0}},
        "import/forest");
    CHECK_EQ(static_cast<int>(imported.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(imported.operations.size(), 1U); CHECK(map.applyDomainOperation(imported.operations[0]).accepted());
    CHECK_EQ(map.mapPlacements()[0].id.value(), "import/forest/tree-01");
}

namespace {

DomainOperation inverse(const DomainOperation& source) {
    DomainOperation result = source;
    result.type = source.inverseType;
    result.payload = source.inverse;
    return result;
}

void createLayer(MapDocumentTarget& map, MapLayerRecord layer) {
    auto operation = map.makeCreateLayer(layer);
    REQUIRE(operation.value);
    REQUIRE(map.applyDomainOperation(*operation.value).accepted());
}

MapRoadRecord road() {
    return {StableId("main-road"), StableId("roads"), "Main Road", "asset://materials/asphalt",
            false, {{StableId("p0"), 0.0, 0.0, 0.0, 4.0},
                    {StableId("p1"), 10.0, 0.0, 2.0, 5.0},
                    {StableId("p2"), 20.0, 0.0, 0.0, 4.0}}};
}

}  // namespace

TEST_CASE("editor.map.layers_are_ordered_reversible_and_protect_nonempty_content") {
    MapDocumentTarget map("world-map");
    createLayer(map, {StableId("ground"), "Ground", "tile", true, false, 1.0, 0});
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 0.9, 10});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 20});
    REQUIRE_EQ(map.mapLayers().size(), size_t{3});
    CHECK_EQ(map.mapLayers()[0].id, StableId("ground"));
    CHECK_EQ(map.mapLayers()[2].id, StableId("objects"));

    MapLayerRecord changed = map.mapLayers()[2];
    changed.name = "Props";
    changed.visible = false;
    auto set = map.makeSetLayer(changed);
    REQUIRE(set.value);
    REQUIRE(map.applyDomainOperation(*set.value).accepted());
    CHECK_EQ(map.mapLayers()[2].name, "Props");
    REQUIRE(map.applyDomainOperation(inverse(*set.value)).accepted());
    CHECK_EQ(map.mapLayers()[2].name, "Objects");

    auto addRoad = map.makeSetRoad(road());
    REQUIRE(addRoad.value);
    REQUIRE(map.applyDomainOperation(*addRoad.value).accepted());
    CHECK_EQ(static_cast<int>(map.makeDeleteLayer(StableId("roads")).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(map.makeCreateLayer({StableId("duplicate-order"), "Duplicate", "tile",
                                                  true, false, 1.0, 10}).status),
             static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.map.road_spline_and_placements_support_create_update_undo") {
    MapDocumentTarget map("content-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    auto addRoad = map.makeSetRoad(road());
    REQUIRE(addRoad.value);
    REQUIRE(map.applyDomainOperation(*addRoad.value).accepted());
    CHECK_EQ(map.mapRoads()[0].points.size(), size_t{3});
    const auto preview = map.previewRoad(StableId("main-road"));
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK(preview.centerlineLength > 20.0);
    const auto* mesh = preview.mesh.getIf<EditorValue::Object>();
    REQUIRE(mesh);
    CHECK_EQ(mesh->at("vertices").getIf<EditorValue::Array>()->size(), size_t{6});
    CHECK_EQ(mesh->at("indices").getIf<EditorValue::Array>()->size(), size_t{12});
    CHECK_EQ(static_cast<int>(map.previewRoad(StableId("main-road"), 1).status),
             static_cast<int>(EditorStatus::Rejected));

    MapRoadRecord changed = map.mapRoads()[0];
    changed.points[1].width = 8.0;
    changed.closed = true;
    auto updateRoad = map.makeSetRoad(changed);
    REQUIRE(updateRoad.value);
    REQUIRE(map.applyDomainOperation(*updateRoad.value).accepted());
    CHECK_EQ(map.mapRoads()[0].points[1].width, 8.0);
    REQUIRE(map.applyDomainOperation(inverse(*updateRoad.value)).accepted());
    CHECK_EQ(map.mapRoads()[0].points[1].width, 5.0);

    MapPlacementRecord tree{StableId("tree-1"), StableId("objects"), "asset://models/tree.evm",
                            4.0, 0.0, 8.0, 0.0, 30.0, 0.0, 1.0, 1.0, 1.0};
    auto place = map.makeSetPlacement(tree);
    REQUIRE(place.value);
    REQUIRE(map.applyDomainOperation(*place.value).accepted());
    CHECK_EQ(map.mapPlacements()[0].asset, "asset://models/tree.evm");
    REQUIRE(map.applyDomainOperation(inverse(*place.value)).accepted());
    CHECK(map.mapPlacements().empty());
}

TEST_CASE("editor.map.locked_layers_reject_content_edits_and_batch_staging_is_atomic") {
    MapDocumentTarget map("locked-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, true, 1.0, 0});
    CHECK_EQ(static_cast<int>(map.makeSetRoad(road()).status), static_cast<int>(EditorStatus::Rejected));

    auto staged = map.cloneDomainState();
    auto* candidate = dynamic_cast<MapDocumentTarget*>(staged.get());
    REQUIRE(candidate);
    auto unlock = candidate->makeSetLayer({StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    REQUIRE(unlock.value);
    REQUIRE(candidate->applyDomainOperation(*unlock.value).accepted());
    auto addRoad = candidate->makeSetRoad(road());
    REQUIRE(addRoad.value);
    REQUIRE(candidate->applyDomainOperation(*addRoad.value).accepted());
    REQUIRE(map.commitDomainState(std::move(staged)).accepted());
    CHECK_EQ(map.mapRoads().size(), size_t{1});
}

TEST_CASE("editor.map.snapshot_round_trip_and_geometry_diagnostics") {
    MapDocumentTarget map("snapshot-map");
    createLayer(map, {StableId("roads"), "Roads", "road", true, false, 1.0, 0});
    createLayer(map, {StableId("objects"), "Objects", "object", true, false, 1.0, 1});
    MapRoadRecord zero = road();
    zero.materialAsset.clear();
    zero.points[1].x = zero.points[0].x;
    zero.points[1].z = zero.points[0].z;
    zero.points[2].x = zero.points[0].x;
    zero.points[2].z = zero.points[0].z;
    auto add = map.makeSetRoad(zero);
    REQUIRE(add.value);
    REQUIRE(map.applyDomainOperation(*add.value).accepted());
    CHECK_EQ(map.validate().size(), size_t{2});

    const EditorValue snapshot = map.snapshotValue();
    MapDocumentTarget restored("snapshot-map");
    REQUIRE(restored.loadSnapshot(snapshot).accepted());
    CHECK_EQ(restored.snapshotValue(), snapshot);
    const Revision before = restored.revision();
    EditorValue broken = snapshot;
    auto* root = broken.getIf<EditorValue::Object>();
    REQUIRE(root);
    (*root)["schemaVersion"] = int64_t{9};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(broken).status),
             static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(restored.revision(), before);
    CHECK_EQ(restored.snapshotValue(), snapshot);
}
