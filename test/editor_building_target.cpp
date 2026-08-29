#include "editor/EditorBuildingTarget.h"

#include "building/BuildingDef.h"
#include "building/PlacementWorld.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

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
    REQUIRE(place.value);
    REQUIRE(target.applyDomainOperation(*place.value).accepted());
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
    placed.properties["skin"] = "blue";
    placed.garrison = {{"unit-1", "worker", {"builder"}}};
    placed.garrisonRevision = 4;
    auto place = target.makePlace(placed);
    REQUIRE(place.value);
    REQUIRE(target.applyDomainOperation(*place.value).accepted());
    REQUIRE(world.hasBuilding(77));
    CHECK_EQ(world.getBuildingProp(77, "skin"), "blue");

    auto move = target.makeMove(77, 6, 5, 179.0);
    REQUIRE(move.value);
    REQUIRE(target.applyDomainOperation(*move.value).accepted());
    CHECK_EQ(world.getBuildingCellX(77), 6);
    CHECK_EQ(world.getBuildingCellY(77), 5);
    CHECK_EQ(world.getBuildingRotation(77), 180.f);
    REQUIRE(target.applyDomainOperation(inverse(*move.value)).accepted());
    CHECK_EQ(world.getBuildingCellX(77), 1);
    CHECK_EQ(world.getBuildingCellY(77), 1);

    auto remove = target.makeRemove(77);
    REQUIRE(remove.value);
    REQUIRE(target.applyDomainOperation(*remove.value).accepted());
    CHECK(!world.hasBuilding(77));
    REQUIRE(target.applyDomainOperation(inverse(*remove.value)).accepted());
    REQUIRE(world.hasBuilding(77));
    const auto restored = target.instance(77);
    REQUIRE(restored.value);
    CHECK_EQ(restored.value->properties.at("skin"), "blue");
    CHECK_EQ(restored.value->garrison.size(), size_t{1});
    CHECK_EQ(restored.value->garrison[0].id, "unit-1");
    CHECK_EQ(restored.value->garrisonRevision, uint64_t{4});
}

TEST_CASE("editor.building.undo_restore_conflicts_instead_of_overwriting_occupancy") {
    registerHouse();
    eve::building::PlacementWorld world(8, 8, 10.f);
    BuildingPlacementTarget target("conflict-world", &world);
    auto first = target.makePlace(house(10, 1, 1));
    REQUIRE(first.value);
    REQUIRE(target.applyDomainOperation(*first.value).accepted());
    auto remove = target.makeRemove(10);
    REQUIRE(remove.value);
    REQUIRE(target.applyDomainOperation(*remove.value).accepted());
    auto replacement = target.makePlace(house(11, 1, 1));
    REQUIRE(replacement.value);
    REQUIRE(target.applyDomainOperation(*replacement.value).accepted());
    const auto conflict = target.applyDomainOperation(inverse(*remove.value));
    CHECK_EQ(static_cast<int>(conflict.status), static_cast<int>(EditorStatus::Rejected));
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
    REQUIRE(first.value);
    REQUIRE(second.value);
    REQUIRE(target.applyDomainOperation(*first.value).accepted());
    REQUIRE(target.applyDomainOperation(*second.value).accepted());
    const EditorValue snapshot = target.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root);
    CHECK_EQ(*root->at("worldId").getIf<std::string>(), "settlement");
    const auto* instances = root->at("instances").getIf<EditorValue::Array>();
    REQUIRE(instances);
    CHECK_EQ(instances->size(), size_t{2});
    CHECK_EQ(*instances->at(0).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{3});
    CHECK_EQ(*instances->at(1).getIf<EditorValue::Object>()->at("instanceId").getIf<int64_t>(), int64_t{9});
}
