#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"

#include <cmath>
#include <string>
#include <vector>

using namespace eve::building;

namespace {
bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
}  // namespace

TEST_CASE("building.registry.registerAndJson") {
    BuildingRegistry::clear();

    BuildingDefinition house;
    house.id = "house.wood";
    house.displayName = "Wood House";
    house.footprintW = 2;
    house.footprintH = 2;
    house.tags = {"house", "housing"};
    house.cost["wood"] = 20;
    house.extra["mesh"] = "models/house.glb";
    BuildingRegistry::registerBuilding(house);

    CHECK(BuildingRegistry::count() == 1);
    CHECK(BuildingRegistry::find("house.wood") != nullptr);
    CHECK_EQ(BuildingRegistry::find("house.wood")->footprintW, 2);
    CHECK(BuildingRegistry::find("house.wood")->hasTag("house"));
    CHECK_EQ(BuildingRegistry::find("house.wood")->getCost("wood"), 20);
    CHECK_EQ(BuildingRegistry::find("house.wood")->getExtra("mesh"), "models/house.glb");

    int n = BuildingRegistry::loadFromJson(R"([
      {"id":"road.straight","displayName":"Road","footprintW":1,"footprintH":1,
       "tags":["road"],"category":"infra","cost":{"gold":1}},
      {"id":"dock","displayName":"Dock","footprintW":3,"footprintH":2,
       "requireTerrain":[2],"tags":["dock"],"rotationMode":"cardinal"}
    ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(BuildingRegistry::count(), 3);
    CHECK_EQ(BuildingRegistry::find("dock")->requireTerrain.size(), size_t(1));
    CHECK_EQ(BuildingRegistry::find("dock")->requireTerrain[0], 2);

    BuildingRegistry::clear();
    CHECK_EQ(BuildingRegistry::count(), 0);
}

TEST_CASE("building.world.placeOccupyConflict") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.footprintW = 2;
    hut.footprintH = 2;
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(8, 8, 32.f);
    world.setId("map1");

    CHECK(world.canPlace("hut", 1, 1, 0.f));
    int id = world.placeAt("hut", 1, 1, 0.f);
    CHECK(id > 0);
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK(!world.isCellEmpty(1, 1));
    CHECK(!world.isCellEmpty(2, 2));
    CHECK(world.isCellEmpty(3, 1));

    CHECK(!world.canPlace("hut", 2, 2, 0.f));
    CHECK_EQ(world.canPlaceReason("hut", 2, 2, 0.f), "occupied");
    CHECK(world.canPlace("hut", 3, 1, 0.f));

    CHECK_EQ(PlacementSystem::events().size(), size_t(1));
    CHECK_EQ(PlacementSystem::events()[0].action, "place");
    CHECK_EQ(PlacementSystem::events()[0].buildingId, "hut");

    BuildingRegistry::clear();
}

TEST_CASE("building.world.snapAndGhost") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition tower;
    tower.id = "tower";
    tower.footprintW = 1;
    tower.footprintH = 1;
    tower.snapMode = "grid";
    BuildingRegistry::registerBuilding(tower);

    PlacementWorld world(10, 10, 32.f);
    world.setOrigin(0.f, 0.f);

    Ghost ghost;
    ghost.setBuildingId("tower");
    ghost.setFromWorld(&world, 40.f, 70.f);
    CHECK_EQ(ghost.getCellX(), 1);  // floor(40/32)=1
    CHECK_EQ(ghost.getCellY(), 2);  // floor(70/32)=2
    CHECK(approxEq(ghost.getWorldX(), 32.f));
    CHECK(approxEq(ghost.getWorldY(), 64.f));
    CHECK(ghost.validate(&world));
    int id = world.placeGhost(&ghost);
    CHECK(id > 0);
    CHECK_EQ(world.getBuildingCellX(id), 1);
    CHECK_EQ(world.getBuildingCellY(id), 2);

    BuildingRegistry::clear();
}

TEST_CASE("building.world.rotationFootprint") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hall;
    hall.id = "hall";
    hall.footprintW = 3;
    hall.footprintH = 1;
    hall.rotationMode = "cardinal";
    BuildingRegistry::registerBuilding(hall);

    PlacementWorld world(6, 6, 1.f);

    // 0°: occupies (0,0)(1,0)(2,0)
    CHECK(world.canPlace("hall", 0, 0, 0.f));
    int id = world.placeAt("hall", 0, 0, 0.f);
    CHECK(id > 0);
    CHECK(!world.isCellEmpty(2, 0));
    CHECK(world.isCellEmpty(0, 2));
    world.removeBuilding(id);

    // 90°: W/H swap conceptually — local (0,0)(1,0)(2,0) → (0,2)(0,1)(0,0) after 90 CCW remap
    CHECK(world.canPlace("hall", 0, 0, 90.f));
    id = world.placeAt("hall", 0, 0, 90.f);
    CHECK(id > 0);
    CHECK(!world.isCellEmpty(0, 0));
    CHECK(!world.isCellEmpty(0, 1));
    CHECK(!world.isCellEmpty(0, 2));
    CHECK(world.isCellEmpty(1, 0));
    CHECK(approxEq(world.getBuildingRotation(id), 90.f));

    BuildingRegistry::clear();
}

TEST_CASE("building.world.terrainAndAdjacency") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition dock;
    dock.id = "dock";
    dock.footprintW = 1;
    dock.footprintH = 1;
    dock.requireTerrain = {2};  // water
    BuildingRegistry::registerBuilding(dock);

    BuildingDefinition stall;
    stall.id = "stall";
    stall.footprintW = 1;
    stall.footprintH = 1;
    stall.requireAdjacentTag = "road";
    BuildingRegistry::registerBuilding(stall);

    BuildingDefinition road;
    road.id = "road";
    road.footprintW = 1;
    road.footprintH = 1;
    road.tags = {"road"};
    BuildingRegistry::registerBuilding(road);

    PlacementWorld world(5, 5, 1.f);
    world.fillTerrain(1);  // land
    world.setTerrain(2, 2, 2);  // water

    CHECK(!world.canPlace("dock", 1, 1, 0.f));
    CHECK_EQ(world.canPlaceReason("dock", 1, 1, 0.f), "terrain_mismatch");
    CHECK(world.canPlace("dock", 2, 2, 0.f));
    CHECK(world.placeAt("dock", 2, 2, 0.f) > 0);

    CHECK(!world.canPlace("stall", 0, 0, 0.f));
    CHECK_EQ(world.canPlaceReason("stall", 0, 0, 0.f), "adjacency_tag");
    CHECK(world.placeAt("road", 0, 1, 0.f) > 0);
    CHECK(world.canPlace("stall", 0, 0, 0.f));
    CHECK(world.placeAt("stall", 0, 0, 0.f) > 0);

    BuildingRegistry::clear();
}

TEST_CASE("building.world.moveRemoveMask") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition L;
    L.id = "L";
    L.footprintW = 2;
    L.footprintH = 2;
    // mask: ## / #.  (row-major)
    L.footprintMask = {1, 1, 1, 0};
    BuildingRegistry::registerBuilding(L);

    PlacementWorld world(5, 5, 1.f);
    int id = world.placeAt("L", 1, 1, 0.f);
    CHECK(id > 0);
    CHECK(!world.isCellEmpty(1, 1));
    CHECK(!world.isCellEmpty(2, 1));
    CHECK(!world.isCellEmpty(1, 2));
    CHECK(world.isCellEmpty(2, 2));  // mask hole

    CHECK(world.moveBuilding(id, 2, 2, 0.f));
    CHECK(world.isCellEmpty(1, 1));
    CHECK(!world.isCellEmpty(2, 2));
    CHECK_EQ(world.getBuildingCellX(id), 2);

    CHECK(world.removeBuilding(id));
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK(world.isCellEmpty(2, 2));

    BuildingRegistry::clear();
}

TEST_CASE("building.system.customValidateSnapHook") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition barn;
    barn.id = "barn";
    barn.footprintW = 1;
    barn.footprintH = 1;
    barn.validateRule = "needGold";
    barn.snapMode = "half";
    BuildingRegistry::registerBuilding(barn);

    int gold = 0;
    PlacementSystem::registerValidateRule(
        "needGold", [&](const PlacementWorld &, const PlacementQuery &, std::string *reason) {
            if (gold < 10) {
                if (reason) *reason = "not_enough_gold";
                return false;
            }
            return true;
        });
    PlacementSystem::registerSnapRule(
        "half", [](const PlacementWorld &world, float worldX, float worldY) {
            SnapResult r;
            const float cs = world.getCellSize();
            r.worldX = std::floor(worldX / (cs * 0.5f)) * (cs * 0.5f);
            r.worldY = std::floor(worldY / (cs * 0.5f)) * (cs * 0.5f);
            r.cellX = world.worldToCellX(r.worldX);
            r.cellY = world.worldToCellY(r.worldY);
            return r;
        });

    int hookCount = 0;
    PlacementSystem::registerChangeHook("count",
                                        [&](const BuildingChangeEvent &) { ++hookCount; });

    PlacementWorld world(8, 8, 32.f);
    CHECK(!world.canPlace("barn", 0, 0, 0.f));
    CHECK_EQ(world.canPlaceReason("barn", 0, 0, 0.f), "not_enough_gold");

    gold = 10;
    CHECK(world.canPlace("barn", 0, 0, 0.f));
    int id = world.placeAt("barn", 0, 0, 0.f);
    CHECK(id > 0);
    CHECK_EQ(hookCount, 1);

    SnapResult s = PlacementSystem::snap(world, "barn", 40.f, 40.f);
    CHECK(approxEq(s.worldX, 32.f));  // floor(40/16)*16 = 32

    PlacementSystem::unregisterValidateRule("needGold");
    PlacementSystem::unregisterSnapRule("half");
    PlacementSystem::unregisterChangeHook("count");
    BuildingRegistry::clear();
}

TEST_CASE("building.facade.endToEnd") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();

    Building mod;
    int n = mod.registerBuildingsFromJson(
        R"({"id":"farm","displayName":"Farm","footprintW":2,"footprintH":2,"tags":["farm"],
            "cost":{"gold":50},"extra":{"icon":"farm.png"}})");
    CHECK_EQ(n, 1);
    CHECK(mod.hasBuildingDefinition("farm"));
    CHECK_EQ(mod.getBuildingDisplayName("farm"), "Farm");
    CHECK_EQ(mod.getBuildingCost("farm", "gold"), 50);
    CHECK_EQ(mod.getBuildingExtra("farm", "icon"), "farm.png");
    CHECK(mod.hasValidateRule("default"));
    CHECK(mod.hasSnapRule("grid"));

    PlacementWorld *world = mod.newWorld(16, 16, 16.f);
    world->setId("w1");
    Ghost *ghost = mod.newGhost();
    ghost->setBuildingId("farm");
    ghost->setFromWorld(world, 33.f, 17.f);
    CHECK(ghost->validate(world));
    int id = world->placeGhost(ghost);
    CHECK(id > 0);
    CHECK_EQ(mod.getChangeEventCount(), 1);
    CHECK_EQ(mod.getChangeEventAction(0), "place");
    CHECK_EQ(mod.getChangeEventBuildingId(0), "farm");
    CHECK_EQ(mod.getChangeEventWorldId(0), "w1");

    world->destroy();
    ghost->destroy();
    mod.clearBuildingDefinitions();
    mod.clearChangeEvents();
}
