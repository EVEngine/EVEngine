#include "building_editing/BuildingTarget.h"

#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "editing/EditingAuthority.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::building_editing;

namespace {

void registerHouse() {
    eve::building::BuildingDefinition definition;
    definition.id = "house";
    definition.displayName = "House";
    definition.footprintW = 2;
    definition.footprintH = 2;
    definition.rotationMode = "cardinal";
    definition.tags = {"building", "home"};
    eve::building::BuildingRegistry::registerBuilding(definition);
}

void registerEdges() {
    eve::building::BuildingDefinition fence;
    fence.id = "editor-fence";
    fence.displayName = "Fence";
    fence.placementKind = "edge";
    fence.channel = "boundary";
    fence.tags = {"building", "fence"};
    eve::building::BuildingRegistry::registerBuilding(fence);
    eve::building::BuildingDefinition gate = fence;
    gate.id = "editor-gate";
    gate.displayName = "Gate";
    gate.tags = {"building", "gate"};
    eve::building::BuildingRegistry::registerBuilding(gate);
}

void registerAreaTile() {
    eve::building::BuildingDefinition tile;
    tile.id = "editor-area-tile";
    tile.displayName = "Area Tile";
    tile.footprintW = 1;
    tile.footprintH = 1;
    tile.channel = "floor";
    tile.tags = {"building", "floor"};
    eve::building::BuildingRegistry::registerBuilding(tile);
}

void registerCorners() {
    eve::building::BuildingDefinition post;
    post.id = "editor-post";
    post.placementKind = "corner";
    post.channel = "posts";
    post.tags = {"building", "post"};
    eve::building::BuildingRegistry::registerBuilding(post);
    eve::building::BuildingDefinition reinforced = post;
    reinforced.id = "editor-reinforced-post";
    reinforced.tags = {"building", "post", "reinforced"};
    eve::building::BuildingRegistry::registerBuilding(reinforced);
}

void registerFreeProps() {
    eve::building::BuildingDefinition prop;
    prop.id = "editor-free-prop";
    prop.placementKind = "free";
    prop.snapMode = "free";
    prop.rotationMode = "free";
    prop.channel = "decor";
    prop.freeRadiusCells = 0.2f;
    prop.freeFootprintWidthCells = 0.8f;
    prop.freeFootprintHeightCells = 0.4f;
    prop.freeFootprintVertices = {-0.4f, -0.2f, 0.4f, -0.2f,
                                  0.4f, 0.2f, -0.4f, 0.2f};
    eve::building::BuildingRegistry::registerBuilding(prop);
}

DomainOperation inverse(const DomainOperation& source) {
    DomainOperation result = source;
    result.type = source.inverseType;
    result.payload = source.inverse;
    return result;
}

BuildingInstanceSnapshot house(int id, int x, int y) {
    BuildingInstanceSnapshot result;
    result.instanceId = id;
    result.buildingId = "house";
    result.cellX = x;
    result.cellY = y;
    result.worldX = x * 10.0;
    result.worldY = y * 10.0;
    result.tags = {"building", "home"};
    return result;
}

}  // namespace

TEST_CASE("editor.building.preview_reports_snap_footprint_and_occupancy") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("build-world", &world);
    const auto preview = target.preview("house", 21.0, 31.0, 0.0, 91.0);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.snappedCellX, 2);
    CHECK_EQ(preview.snappedCellY, 3);
    CHECK_EQ(preview.normalizedRotation, 90.0);
    CHECK_EQ(preview.cells.size(), size_t{4});
    for (const BuildingFootprintCell& cell : preview.cells) {
        CHECK(cell.inBounds);
        CHECK_EQ(cell.occupant, 0);
    }

    auto place = target.makePlace(house(target.nextAvailableInstanceId(), 2, 3));
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    const auto occupied = target.preview("house", 21.0, 31.0, 0.0, 0.0);
    CHECK_EQ(static_cast<int>(occupied.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!occupied.diagnostics.empty());
    CHECK(occupied.cells[0].occupant > 0);
}

TEST_CASE("editor.building.place_move_remove_are_exactly_reversible") {
    registerHouse();
    eve::building::PlacementWorld world(12, 12, 10.f);
    BuildingPlacementTarget target("reversible-world", &world);
    BuildingInstanceSnapshot placed = house(77, 1, 1);
    placed.level = 2;
    placed.properties["skin"] = "blue";
    placed.garrison = {{"unit-1", "worker", {"builder"}}};
    placed.garrisonRevision = 4;
    auto place = target.makePlace(placed);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    REQUIRE(world.hasBuilding(77));
    CHECK_EQ(world.getBuildingProp(77, "skin"), "blue");

    auto move = target.makeMove(77, 6, 5, 179.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getBuildingCellX(77), 6);
    CHECK_EQ(world.getBuildingCellY(77), 5);
    CHECK_EQ(world.getBuildingRotation(77), 180.f);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    CHECK_EQ(world.getBuildingCellX(77), 1);
    CHECK_EQ(world.getBuildingCellY(77), 1);

    auto remove = target.makeRemove(77);
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK(!world.hasBuilding(77));
    REQUIRE(target.applyDomainOperation(inverse(remove.value())).ok());
    REQUIRE(world.hasBuilding(77));
    const auto restored = target.instance(77);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().properties.at("skin"), "blue");
    CHECK_EQ(restored.value().garrison.size(), size_t{1});
    CHECK_EQ(restored.value().garrison[0].id, "unit-1");
    CHECK_EQ(restored.value().garrisonRevision, uint64_t{4});
    CHECK_EQ(restored.value().level, 2);
    CHECK_EQ(world.getOccupantAtLevel("", 1, 1, 2), 77);
}

TEST_CASE("editor.building.undo_restore_conflicts_instead_of_overwriting_occupancy") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("conflict-world", &world);
    auto first = target.makePlace(house(10, 1, 1));
    REQUIRE(first.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    auto remove = target.makeRemove(10);
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    auto replacement = target.makePlace(house(11, 1, 1));
    REQUIRE(replacement.ok());
    REQUIRE(target.applyDomainOperation(replacement.value()).ok());
    const auto conflict = target.applyDomainOperation(inverse(remove.value()));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(!world.hasBuilding(10));
    CHECK(world.hasBuilding(11));
}

TEST_CASE("editor.building.snapshot_is_deterministic_and_complete") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    world.setId("settlement");
    BuildingPlacementTarget target("snapshot-world", &world);
    auto first = target.makePlace(house(3, 0, 0));
    auto second = target.makePlace(house(9, 4, 4));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(target.applyDomainOperation(first.value()).ok());
    REQUIRE(target.applyDomainOperation(second.value()).ok());
    const EditorValue snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    CHECK_EQ(*root->at("schemaVersion").getIf<int64_t>(), int64_t{8});
    CHECK_EQ(*root->at("worldId").getIf<std::string>(), "settlement");
    const auto* instances = root->at("instances").getIf<EditorValue::Array>();
    REQUIRE(instances);
    CHECK_EQ(instances->size(), size_t{2});
    const auto* curveGroups = root->at("edgeCurveGroups").getIf<EditorValue::Array>();
    REQUIRE(curveGroups);
    CHECK(curveGroups->empty());
    CHECK_EQ(*instances->at(0).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{3});
    CHECK_EQ(*instances->at(1).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{9});
}

TEST_CASE("editor.building.corner_place_move_replace_and_undo_preserve_vertex_domain") {
    registerCorners();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("corner-world", &world);
    BuildingInstanceSnapshot post;
    post.instanceId = 81;
    post.buildingId = "editor-post";
    post.placementKind = "corner";
    post.cornerX = 2;
    post.cornerY = 3;
    post.cellX = 2;
    post.cellY = 3;
    post.worldX = 20.0;
    post.worldY = 30.0;
    post.channel = "posts";
    post.tags = {"building", "post"};

    auto place = target.makePlace(post);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 81);

    auto move = target.makeMove(81, 5, 6, 0.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 5, 6), 81);
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 0);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    CHECK_EQ(world.getCornerOccupant("posts", 2, 3), 81);

    auto replace = target.makeReplace(81, "editor-reinforced-post");
    REQUIRE(replace.ok());
    REQUIRE(target.applyDomainOperation(replace.value()).ok());
    CHECK_EQ(world.getBuildingId(81), "editor-reinforced-post");
    REQUIRE(target.applyDomainOperation(inverse(replace.value())).ok());
    CHECK_EQ(world.getBuildingId(81), "editor-post");
    const auto restored = target.instance(81);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().placementKind, "corner");
    CHECK_EQ(restored.value().cornerX, 2);
    CHECK_EQ(restored.value().cornerY, 3);
}

TEST_CASE("editor.building.free_snapshot_move_and_undo_preserve_exact_anchor") {
    registerFreeProps();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("free-world", &world);
    BuildingInstanceSnapshot prop;
    prop.instanceId = 91;
    prop.buildingId = "editor-free-prop";
    prop.placementKind = "free";
    prop.worldX = 12.25;
    prop.worldY = 17.75;
    prop.elevation = 0.5;
    prop.rotationDegrees = 23.0;
    prop.channel = "decor";
    prop.freeRadius = 2.0;
    prop.freeHalfWidth = 4.0;
    prop.freeHalfHeight = 2.0;
    prop.freeFootprintVertices = {-4.0, -2.0, 4.0, -2.0, 4.0, 2.0, -4.0, 2.0};
    auto place = target.makePlace(prop);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    CHECK_EQ(world.getBuildingWorldX(91), 12.25f);

    auto move = target.makeMoveFree(91, 43.125, 51.875, 2.5, 71.0);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    CHECK_EQ(world.getBuildingWorldX(91), 43.125f);
    CHECK_EQ(world.getBuildingElevation(91), 2.5f);
    CHECK_EQ(world.getBuildingRotation(91), 71.f);
    REQUIRE(target.applyDomainOperation(inverse(move.value())).ok());
    const auto restored = target.instance(91);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().placementKind, "free");
    CHECK_EQ(restored.value().worldX, 12.25);
    CHECK_EQ(restored.value().worldY, 17.75);
    CHECK_EQ(restored.value().freeRadius, 2.0);
    CHECK_EQ(restored.value().freeHalfWidth, 4.0);
    CHECK_EQ(restored.value().freeHalfHeight, 2.0);
    CHECK_EQ(restored.value().freeFootprintVertices.size(), size_t{8});
    CHECK_EQ(restored.value().freeFootprintVertices[0], -4.0);
}

TEST_CASE("editor.building.replace_commits_undoes_and_redoes_through_authority") {
    registerHouse();
    eve::building::BuildingDefinition upgraded;
    upgraded.id = "upgraded-house";
    upgraded.displayName = "Upgraded House";
    upgraded.footprintW = 2;
    upgraded.footprintH = 2;
    upgraded.rotationMode = "cardinal";
    upgraded.tags = {"building", "upgraded"};
    eve::building::BuildingRegistry::registerBuilding(upgraded);

    eve::building::PlacementWorld world(10, 10, 10.f);
    BuildingPlacementTarget target("replace-world", &world);
    auto place = target.makePlace(house(42, 2, 2));
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    world.setBuildingProp(42, "owner", "player");

    auto replacement = target.makeReplace(42, upgraded.id);
    REQUIRE(replacement.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.replace.1");
    transaction.label = "Replace building";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{replacement.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    eve::building::PlacementSystem::clearEvents();
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingId(42), upgraded.id);
    CHECK_EQ(world.getBuildingProp(42, "owner"), "player");
    const size_t committedEventCount = eve::building::PlacementSystem::events().size();

    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    CHECK_EQ(world.getBuildingId(42), "house");
    CHECK_EQ(world.getBuildingProp(42, "owner"), "player");
    CHECK_EQ(eve::building::PlacementSystem::events().size(), committedEventCount);

    transaction.id = eve::editing::TransactionId("building.replace.2");
    transaction.baseRevision = target.revision();
    auto redoPlan = authority.preflight(transaction, operations);
    REQUIRE(redoPlan.ok());
    auto redone = authority.commit(redoPlan.value());
    REQUIRE(redone.ok());
    CHECK_EQ(world.getBuildingId(42), upgraded.id);
}

TEST_CASE("editor.building.edge_replace_and_undo_preserve_canonical_address") {
    registerEdges();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("edge-replace-world", &world);
    BuildingInstanceSnapshot edge;
    edge.instanceId = 51;
    edge.buildingId = "editor-fence";
    edge.placementKind = "edge";
    edge.edgeX = 3;
    edge.edgeY = 4;
    edge.edgeAxis = "vertical";
    edge.cellX = 3;
    edge.cellY = 4;
    edge.worldX = 30.0;
    edge.worldY = 45.0;
    edge.rotationDegrees = 90.0;
    edge.channel = "boundary";
    edge.properties["owner"] = "player";
    auto place = target.makePlace(edge);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());

    auto replacement = target.makeReplace(51, "editor-gate");
    REQUIRE(replacement.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.edge.replace.1");
    transaction.label = "Replace edge building";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{replacement.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingId(51), "editor-gate");
    auto replaced = target.instance(51);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value().edgeX, 3);
    CHECK_EQ(replaced.value().edgeY, 4);
    CHECK_EQ(replaced.value().edgeAxis, "vertical");
    CHECK_EQ(replaced.value().properties.at("owner"), "player");

    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingId(51), "editor-fence");
    auto restored = target.instance(51);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().edgeAxis, "vertical");
    CHECK_EQ(restored.value().properties.at("owner"), "player");
}

TEST_CASE("editor.building.surface_attachment_survives_replace_and_undo") {
    registerHouse();
    eve::building::BuildingDefinition upgraded = *eve::building::BuildingRegistry::find("house");
    upgraded.id = "surface-upgraded-house";
    eve::building::BuildingRegistry::registerBuilding(upgraded);
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("surface-replace-world", &world);
    BuildingInstanceSnapshot placed = house(61, 2, 2);
    placed.surfaceId = "terrain/main";
    placed.surfaceRevision = 27;
    placed.surfaceNormalX = 0.25;
    placed.surfaceNormalY = 0.95;
    placed.surfaceNormalZ = 0.1;
    placed.surfaceTangentX = 0.9;
    placed.surfaceTangentY = -0.2;
    placed.surfaceTangentZ = 0.3;
    placed.surfaceSampleCount = 4;
    placed.surfaceMaxSlopeDegrees = 14.0;
    placed.surfaceHeightDelta = 0.6;
    auto place = target.makePlace(placed);
    REQUIRE(place.ok());
    REQUIRE(target.applyDomainOperation(place.value()).ok());
    auto replacement = target.makeReplace(61, upgraded.id);
    REQUIRE(replacement.ok());
    REQUIRE(target.applyDomainOperation(replacement.value()).ok());
    REQUIRE(target.applyDomainOperation(inverse(replacement.value())).ok());
    const auto restored = target.instance(61);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().surfaceId, "terrain/main");
    CHECK_EQ(restored.value().surfaceRevision, uint64_t{27});
    CHECK_EQ(restored.value().surfaceSampleCount, 4);
    CHECK_EQ(restored.value().surfaceMaxSlopeDegrees, 14.0);
    CHECK_EQ(restored.value().surfaceHeightDelta, 0.6);
}

TEST_CASE("editor.building.set_preflights_final_definition_and_pose_atomically") {
    registerHouse();
    eve::building::BuildingDefinition upgraded = *eve::building::BuildingRegistry::find("house");
    upgraded.id = "atomic-upgraded-house";
    eve::building::BuildingRegistry::registerBuilding(upgraded);
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("atomic-set-world", &world);
    auto source = target.makePlace(house(71, 0, 0));
    auto blocker = target.makePlace(house(72, 4, 4));
    REQUIRE(source.ok());
    REQUIRE(blocker.ok());
    REQUIRE(target.applyDomainOperation(source.value()).ok());
    REQUIRE(target.applyDomainOperation(blocker.value()).ok());
    auto replacement = target.makeReplace(71, upgraded.id);
    REQUIRE(replacement.ok());
    DomainOperation conflicting = replacement.value();
    auto* payload = conflicting.payload.getIf<EditorValue::Object>();
    REQUIRE(payload);
    (*payload)["cellX"] = int64_t{4};
    (*payload)["cellY"] = int64_t{4};
    const auto rejected = target.applyDomainOperation(conflicting);
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(world.getBuildingId(71), "house");
    CHECK_EQ(world.getBuildingCellX(71), 0);
    CHECK_EQ(world.getBuildingCellY(71), 0);
}

TEST_CASE("editor.building.rectangle_is_one_atomic_undoable_gesture") {
    registerAreaTile();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("area-rectangle-world", &world);
    auto rectangle = target.makeRectangle("editor-area-tile", 1, 2, 3, 3, 0.0);
    REQUIRE(rectangle.ok());
    CHECK_EQ(rectangle.value().affectedObjects.size(), size_t{6});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.area.rectangle.1");
    transaction.label = "Paint rectangle";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{rectangle.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(), 6);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
}

TEST_CASE("editor.building.edge_path_is_one_atomic_undoable_gesture") {
    registerEdges();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("edge-path-world", &world);
    auto path = target.makeEdgePath("editor-fence", {{1, 1}, {4, 1}, {4, 3}});
    REQUIRE(path.ok());
    CHECK_EQ(path.value().affectedObjects.size(), size_t{5});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.edge-path.1");
    transaction.label = "Place edge path";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{path.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(), 5);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    CHECK(world.getEdgeOccupant("boundary", 3, 1, "north") > 0);
    CHECK(world.getEdgeOccupant("boundary", 4, 2, "west") > 0);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
}

TEST_CASE("editor.building.edge_curve_is_one_atomic_undoable_gesture") {
    registerEdges();
    eve::building::PlacementWorld world(10, 10, 10.f);
    BuildingPlacementTarget target("edge-curve-world", &world);
    auto curve = target.makeEdgeCubicBezier(
        "editor-fence", {{1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}}, 24);
    REQUIRE(curve.ok());
    CHECK(curve.value().affectedObjects.size() > size_t{7});
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.edge-curve.1");
    transaction.label = "Place cubic edge curve";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{curve.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    const auto revisionBefore = target.revision();
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getBuildingCount(),
             static_cast<int>(curve.value().affectedObjects.size()));
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    const int firstMember = world.getBuildingInstanceAt(0);
    auto group = world.edgeCurveGroupForInstance(firstMember);
    REQUIRE(group.ok());
    CHECK_EQ(group.value().subdivisions, 24);
    CHECK_EQ(group.value().controlPoints.size(), size_t{4});
    const auto memberBeforeUndo = world.buildings().at(firstMember);
    CHECK_EQ(target.revision(), revisionBefore + 1);
    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK_EQ(world.getEdgeCurveGroupCount(), 0);
    CHECK_EQ(target.revision(), revisionBefore + 2);
    transaction.baseRevision = target.revision();
    const int blocker = world.placeEdge(
        "editor-fence", memberBeforeUndo.edge.x, memberBeforeUndo.edge.y,
        memberBeforeUndo.edge.axis == eve::building::EdgeAxis::Horizontal ? "north" : "west");
    REQUIRE(blocker > 0);
    auto rejectedRedo = authority.preflight(transaction, operations);
    CHECK(!rejectedRedo.ok());
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK_EQ(world.getEdgeCurveGroupCount(), 0);
    REQUIRE(world.removeBuilding(blocker));
    auto redoPlan = authority.preflight(transaction, operations);
    REQUIRE(redoPlan.ok());
    REQUIRE(authority.commit(redoPlan.value()).ok());
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    auto restoredGroup = world.edgeCurveGroupForInstance(firstMember);
    REQUIRE(restoredGroup.ok());
    CHECK_EQ(restoredGroup.value(), group.value());
    const EditorValue snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    const auto* curveGroups = root->at("edgeCurveGroups").getIf<EditorValue::Array>();
    REQUIRE(curveGroups);
    CHECK_EQ(curveGroups->size(), size_t{1});
}

TEST_CASE("editor.building.edge_curve_handles_preview_and_update_group_atomically") {
    registerEdges();
    eve::building::PlacementWorld world(12, 12, 10.f);
    BuildingPlacementTarget target("edge-curve-handle-world", &world);
    const std::vector<BuildingEdgeCurvePoint> original{
        {1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}};
    auto draft = target.previewEdgeCubicBezier("editor-fence", original, 24);
    CHECK_EQ(static_cast<int>(draft.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(draft.controlPoints.size(), size_t{4});
    CHECK(draft.edges.size() > size_t{7});
    auto gizmo = target.edgeCubicBezierGizmo("editor-fence", original, 24);
    CHECK_EQ(static_cast<int>(gizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK(gizmo.primitives.size() > draft.sampledVertices.size());
    CHECK_EQ(gizmo.primitives.front().id, "curve.control.0");
    CHECK_EQ(gizmo.primitives.front().kind, "sphere");

    auto initial = target.makeEdgeCubicBezier("editor-fence", original, 24);
    REQUIRE(initial.ok());
    REQUIRE(target.applyDomainOperation(initial.value()).ok());
    const int member = world.getBuildingInstanceAt(0);
    auto before = world.edgeCurveGroupForInstance(member);
    REQUIRE(before.ok());
    const std::vector<BuildingEdgeCurvePoint> dragged{
        {1.0, 1.0}, {2.0, 7.0}, {8.0, 5.0}, {8.0, 1.0}};
    auto liveDraft =
        target.previewEdgeCubicBezier("editor-fence", dragged, 32, member);
    CHECK_EQ(static_cast<int>(liveDraft.status),
             static_cast<int>(EditorStatus::Applied));
    auto update = target.makeUpdateEdgeCubicBezier(member, dragged, 32);
    REQUIRE(update.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec transaction;
    transaction.id = eve::editing::TransactionId("building.edge-curve.drag.1");
    transaction.label = "Drag cubic edge curve handle";
    transaction.target = target.targetId();
    transaction.baseRevision = target.revision();
    const std::vector<DomainOperation> operations{update.value()};
    auto plan = authority.preflight(transaction, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    auto after = world.edgeCurveGroup(before.value().id);
    REQUIRE(after.ok());
    CHECK_EQ(after.value().subdivisions, 32);
    CHECK(after.value().controlPoints != before.value().controlPoints);
    REQUIRE(authority.compensate(committed.value()).ok());
    auto undone = world.edgeCurveGroup(before.value().id);
    REQUIRE(undone.ok());
    CHECK_EQ(undone.value(), before.value());
}

TEST_CASE("editor.building.surface_curve_create_preview_and_gizmo_share_frames") {
    registerEdges();
    eve::building::PlacementWorld world(12, 12, 10.f);
    world.setGridPlane("xz");
    eve::building::PlacementSystem::registerSurfaceProvider(
        "editor.curve-terrain", [](const eve::building::PlacementWorld&, float x, float y) {
            eve::building::PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = x * 0.1f;
            hit.worldZ = y;
            hit.normalX = -0.1f;
            hit.normalY = 1.f;
            hit.surfaceId = "terrain.editor";
            hit.surfaceRevision = 21;
            return eve::Result<eve::building::PlacementSystem::PlacementHit>::success(
                std::move(hit));
        });
    BuildingPlacementTarget target("surface-curve-editor-world", &world);
    const std::vector<BuildingEdgeCurvePoint> controls{
        {1.0, 1.0}, {1.0, 6.0}, {7.0, 6.0}, {7.0, 1.0}};
    auto preview = target.previewEdgeCubicBezier(
        "editor-fence", controls, 16, 0, "editor.curve-terrain");
    REQUIRE(static_cast<int>(preview.status) ==
            static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.surfaceProviderName, "editor.curve-terrain");
    CHECK_EQ(preview.surfaceId, "terrain.editor");
    CHECK_EQ(preview.surfaceRevision, uint64_t{21});
    CHECK_EQ(preview.surfaceSamples.size(), size_t{17});
    auto gizmo = target.edgeCubicBezierGizmo(
        "editor-fence", controls, 16, 0, 0, "editor.curve-terrain");
    REQUIRE(static_cast<int>(gizmo.status) ==
            static_cast<int>(EditorStatus::Applied));
    bool foundElevatedPath = false;
    for (const auto& primitive : gizmo.primitives)
        if (primitive.id.rfind("curve.path.", 0) == 0 && primitive.position[1] > 1.0)
            foundElevatedPath = true;
    CHECK(foundElevatedPath);

    auto operation = target.makeEdgeCubicBezierOnSurface(
        "editor-fence", controls, 16, "editor.curve-terrain");
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(world.getEdgeCurveGroupCount() == 1);
    auto committed = world.edgeCurveGroupForInstance(world.getBuildingInstanceAt(0));
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value().surfaceProviderName, "editor.curve-terrain");
    CHECK_EQ(committed.value().surfaceId, "terrain.editor");
    CHECK_EQ(committed.value().surfaceRevision, uint64_t{21});
    CHECK_EQ(committed.value().surfaceSamples.size(), size_t{17});

    BuildingEdgeCurveDragSession drag(&target, "editor-fence",
                                      committed.value().instanceIds.front(), controls, 16,
                                      "editor.curve-terrain");
    auto missed = drag.beginDrag(100.0, 100.0, 100.0, 0.0, 0.0, -1.0);
    CHECK(!missed.ok());
    CHECK(!drag.isDragging());
    auto begun = drag.beginDrag(10.0, 1.0, 30.0, 0.0, 0.0, -1.0);
    REQUIRE(begun.ok());
    CHECK(drag.isDragging());
    CHECK_EQ(drag.activeControlPointIndex(), 0);
    auto moved = drag.updateDrag(20.0, 2.0, 30.0, 0.0, 0.0, -1.0);
    REQUIRE(moved.ok());
    CHECK_EQ(static_cast<int>(moved.value().status),
             static_cast<int>(EditorStatus::Applied));
    CHECK(std::abs(moved.value().curve.controlPoints[0].x - 2.0) < 0.0001);
    auto draggedOperation = drag.finishDrag();
    REQUIRE(draggedOperation.ok());
    CHECK(!drag.isDragging());
    REQUIRE(target.applyDomainOperation(draggedOperation.value()).ok());
    auto afterDrag = world.edgeCurveGroup(committed.value().id);
    REQUIRE(afterDrag.ok());
    CHECK(std::abs(afterDrag.value().controlPoints[0].x - 2.f) < 0.0001f);
    CHECK_EQ(afterDrag.value().surfaceId, "terrain.editor");
    std::vector<BuildingEdgeCurvePoint> afterDragControls;
    for (const auto& point : afterDrag.value().controlPoints)
        afterDragControls.push_back({point.x, point.y});
    BuildingEdgeCurveDragSession staleDrag(
        &target, "editor-fence", afterDrag.value().instanceIds.front(), afterDragControls, 16,
        "editor.curve-terrain");
    REQUIRE(staleDrag.beginDrag(20.0, 2.0, 30.0, 0.0, 0.0, -1.0).ok());
    REQUIRE(target.commitDomainState(target.cloneDomainState()).ok());
    auto staleFinish = staleDrag.finishDrag();
    CHECK(!staleFinish.ok());
    CHECK_EQ(static_cast<int>(staleFinish.code()),
             static_cast<int>(EditorStatus::Conflict));
    CHECK(!staleDrag.isDragging());

    auto rejected = target.previewEdgeCubicBezier(
        "editor-fence", controls, 16, 0, "editor.missing-terrain");
    CHECK_EQ(static_cast<int>(rejected.status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(!rejected.diagnostics.empty());
    auto rejectedGizmo = target.edgeCubicBezierGizmo(
        "editor-fence", controls, 16, 0, 0, "editor.missing-terrain");
    CHECK_EQ(static_cast<int>(rejectedGizmo.status),
             static_cast<int>(EditorStatus::Rejected));
    bool foundRejectedPath = false;
    for (const auto& primitive : rejectedGizmo.primitives)
        if (primitive.id.rfind("curve.path.", 0) == 0 && primitive.color[0] == 0.95)
            foundRejectedPath = true;
    CHECK(foundRejectedPath);
    eve::building::PlacementSystem::unregisterSurface("editor.curve-terrain");
    eve::building::BuildingRegistry::clear();
}

TEST_CASE("editor.building.brush_conflict_discards_complete_staging_candidate") {
    registerAreaTile();
    eve::building::PlacementWorld world(9, 9, 10.f);
    BuildingPlacementTarget target("area-conflict-world", &world);
    auto brush = target.makeBrush("editor-area-tile", 4, 4, 1, 0.0);
    REQUIRE(brush.ok());
    CHECK_EQ(brush.value().affectedObjects.size(), size_t{5});
    BuildingInstanceSnapshot blockerSnapshot;
    blockerSnapshot.instanceId = 99;
    blockerSnapshot.buildingId = "editor-area-tile";
    blockerSnapshot.cellX = 4;
    blockerSnapshot.cellY = 5;
    blockerSnapshot.worldX = 40.0;
    blockerSnapshot.worldY = 50.0;
    blockerSnapshot.channel = "floor";
    auto blockerPlacement = target.makePlace(blockerSnapshot);
    REQUIRE(blockerPlacement.ok());
    REQUIRE(target.applyDomainOperation(blockerPlacement.value()).ok());
    const int countBefore = world.getBuildingCount();
    const auto revisionBefore = target.revision();
    eve::building::PlacementSystem::clearEvents();
    const auto rejected = target.applyDomainOperation(brush.value());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(world.getBuildingCount(), countBefore);
    CHECK(world.hasBuilding(99));
    CHECK_EQ(target.revision(), revisionBefore);
    CHECK(eve::building::PlacementSystem::events().empty());
}
