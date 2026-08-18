#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/PlacementSession.h"
#include "map/Map.h"
#include "map/TileLayer.h"

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

TEST_CASE("building.demo.townSandboxDefs") {
    // Mirrors examples/building/main.nut definitions: road / house / stall / dock / L-barn.
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    Building mod;
    int n = mod.registerBuildingsFromJson(R"([
      {"id":"road","displayName":"石板路","category":"infra",
       "footprintW":1,"footprintH":1,"tags":["road"],"cost":{"gold":2}},
      {"id":"house.wood","displayName":"木屋","category":"housing",
       "footprintW":2,"footprintH":2,"tags":["house","housing"],
       "requireTerrain":[1],"cost":{"gold":15,"wood":20}},
      {"id":"stall","displayName":"摊位","category":"commerce",
       "footprintW":1,"footprintH":1,"tags":["shop"],
       "requireTerrain":[1],"requireAdjacentTag":"road","cost":{"gold":8,"wood":6}},
      {"id":"dock","displayName":"码头","category":"infra",
       "footprintW":2,"footprintH":1,"tags":["dock"],
       "requireTerrain":[2],"rotationMode":"cardinal","cost":{"gold":25,"wood":30}},
      {"id":"barn.l","displayName":"L形仓","category":"storage",
       "footprintW":2,"footprintH":2,"tags":["barn"],
       "footprintMask":[1,1,1,0],"requireTerrain":[1],"cost":{"gold":18,"wood":24}}
    ])");
    CHECK_EQ(n, 5);

    PlacementWorld world(24, 18, 28.f);
    world.fillTerrain(1);
    for (int x = 0; x < 24; ++x) world.setTerrain(x, 11, 2);

    CHECK(world.placeAt("road", 5, 5, 0.f) > 0);
    CHECK(!world.canPlace("stall", 10, 5, 0.f));
    CHECK_EQ(world.canPlaceReason("stall", 10, 5, 0.f), "adjacency_tag");
    CHECK(world.canPlace("stall", 5, 4, 0.f));
    CHECK(world.placeAt("stall", 5, 4, 0.f) > 0);

    CHECK(!world.canPlace("dock", 1, 1, 0.f));
    CHECK_EQ(world.canPlaceReason("dock", 1, 1, 0.f), "terrain_mismatch");
    CHECK(world.canPlace("dock", 3, 11, 0.f));
    CHECK(world.placeAt("dock", 3, 11, 0.f) > 0);

    CHECK(world.placeAt("house.wood", 8, 5, 0.f) > 0);
    CHECK(world.placeAt("barn.l", 12, 5, 0.f) > 0);
    CHECK(world.isCellEmpty(13, 6));  // L mask hole
    CHECK_EQ(world.getBuildingCount(), 5);

    BuildingRegistry::clear();
}

TEST_CASE("building.grid.isometricPlacement") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.footprintW = 1;
    hut.footprintH = 1;
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(16, 16, 64.f);
    world.setGridLayout("isometric");
    world.setOrigin(0.f, 0.f);

    float px = 0.f, py = 0.f;
    world.cellToWorldPlane(1, 0, px, py);
    REQUIRE(approxEq(px, 32.f));
    REQUIRE(approxEq(py, 32.f));

    const int id = world.placeAt("hut", 1, 0, 0.f);
    REQUIRE(id > 0);
    REQUIRE(approxEq(world.getBuildingWorldX(id), 32.f));
    REQUIRE(approxEq(world.getBuildingWorldY(id), 32.f));
    REQUIRE(!world.canPlace("hut", 1, 0, 0.f));  // 已占用
    REQUIRE(world.canPlace("hut", 2, 0, 0.f));

    // 世界坐标吸附回格（iso 反投影）。
    Ghost ghost;
    ghost.setBuildingId("hut");
    ghost.setFromWorld(&world, 35.f, 40.f);
    REQUIRE_EQ(ghost.getCellX(), 1);
    REQUIRE_EQ(ghost.getCellY(), 0);

    BuildingRegistry::clear();
}

TEST_CASE("building.grid.hexRotation") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hexhut";
    hut.footprintW = 2;
    hut.footprintH = 1;
    hut.rotationMode = "hex";
    BuildingRegistry::registerBuilding(hut);

    REQUIRE(approxEq(PlacementSystem::normalizeRotation("hexhut", 10.f), 0.f));
    REQUIRE(approxEq(PlacementSystem::normalizeRotation("hexhut", 65.f), 60.f));
    REQUIRE(approxEq(PlacementSystem::normalizeRotation("hexhut", 375.f), 0.f));

    PlacementWorld world(12, 12, 32.f);
    world.setGridLayout("hexagon");
    world.setStagger("y", "odd");
    world.setHexSideLength(14.f);

    REQUIRE(world.placeAt("hexhut", 2, 2, 60.f) > 0);
    // 60° 旋转后的占地占 2 格；同一格再放冲突。
    REQUIRE(!world.canPlace("hexhut", 2, 2, 0.f));

    BuildingRegistry::clear();
}

TEST_CASE("building.channel.stackedOccupancy") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition floor;
    floor.id = "floor";
    floor.channel = "floor";
    floor.footprintW = 1;
    floor.footprintH = 1;
    BuildingRegistry::registerBuilding(floor);

    BuildingDefinition furniture;
    furniture.id = "furniture";
    furniture.channel = "furniture";
    furniture.footprintW = 1;
    furniture.footprintH = 1;
    BuildingRegistry::registerBuilding(furniture);

    PlacementWorld world(8, 8, 32.f);
    const int a = world.placeAt("floor", 3, 3, 0.f);
    const int b = world.placeAt("furniture", 3, 3, 0.f);
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    REQUIRE(world.isCellEmptyInChannel("", 3, 3));
    REQUIRE_EQ(world.getOccupantInChannel("floor", 3, 3), a);
    REQUIRE_EQ(world.getOccupantInChannel("furniture", 3, 3), b);
    REQUIRE(world.getAnyOccupant(3, 3) != 0);

    REQUIRE(world.removeBuilding(a));
    REQUIRE_EQ(world.getOccupantInChannel("floor", 3, 3), 0);
    REQUIRE_EQ(world.getOccupantInChannel("furniture", 3, 3), b);

    BuildingRegistry::clear();
}

TEST_CASE("building.tilemap.terrainFromGid") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition dock;
    dock.id = "dock";
    dock.footprintW = 1;
    dock.footprintH = 1;
    dock.requireTerrain = {2};
    BuildingRegistry::registerBuilding(dock);

    auto *mapMod = eve::map::Map::create();
    eve::map::TileLayer *layer = mapMod->newLayer(10, 10, 32.f, 32.f);
    REQUIRE(layer != nullptr);
    layer->fill(1);          // GID 1 = 陆地
    layer->setTile(4, 4, 2); // GID 2 = 水域

    PlacementWorld world(10, 10, 32.f);
    world.bindTileLayer(layer);
    world.setTerrainGid(1, 1);
    world.setTerrainGid(2, 2);

    REQUIRE_EQ(world.getTerrain(0, 0), 1);
    REQUIRE_EQ(world.getTerrain(4, 4), 2);
    REQUIRE(!world.canPlace("dock", 0, 0, 0.f));
    REQUIRE_EQ(world.canPlaceReason("dock", 0, 0, 0.f), "terrain_mismatch");
    REQUIRE(world.canPlace("dock", 4, 4, 0.f));

    // 手动覆盖优先于 GID。
    world.setTerrain(4, 4, 1);
    REQUIRE_EQ(world.getTerrain(4, 4), 1);
    REQUIRE(!world.canPlace("dock", 4, 4, 0.f));

    BuildingRegistry::clear();
}

TEST_CASE("building.surface.plane3D") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.footprintW = 1;
    hut.footprintH = 1;
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(10, 10, 1.f);
    world.setGridPlane("xz");
    world.setOrigin(0.f, 0.f);

    Ghost ghost;
    ghost.setBuildingId("hut");
    ghost.setFromSurface(&world, "plane", 4.2f, 7.8f);
    REQUIRE_EQ(ghost.getCellX(), 4);
    REQUIRE_EQ(ghost.getCellY(), 7);
    REQUIRE(approxEq(ghost.getElevation(), 0.f));
    REQUIRE(ghost.validate(&world));
    const int id = world.placeGhost(&ghost);
    REQUIRE(id > 0);
    REQUIRE(approxEq(world.getBuildingWorldX(id), 4.f));
    REQUIRE(approxEq(world.getBuildingWorldY(id), 0.f));   // XZ 平面：worldY = 高度
    REQUIRE(approxEq(world.getBuildingWorldZ(id), 7.f));
    REQUIRE(approxEq(world.getBuildingElevation(id), 0.f));

    PlacementSystem::setPlaneSurfaceHeight(1.5f);
    Ghost ghost2;
    ghost2.setBuildingId("hut");
    ghost2.setFromSurface(&world, "plane", 1.2f, 2.3f);
    REQUIRE(approxEq(ghost2.getElevation(), 1.5f));
    PlacementSystem::setPlaneSurfaceHeight(0.f);

    const int id2 = world.placeAtWorld3D("hut", 5.2f, 2.5f, 3.1f, 0.f);
    REQUIRE(id2 > 0);
    REQUIRE(approxEq(world.getBuildingWorldX(id2), 5.f));
    REQUIRE(approxEq(world.getBuildingWorldY(id2), 2.5f));
    REQUIRE(approxEq(world.getBuildingWorldZ(id2), 3.f));
    REQUIRE(approxEq(world.getBuildingElevation(id2), 2.5f));

    BuildingRegistry::clear();
}

TEST_CASE("building.session.placeAndRemove") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.footprintW = 1;
    hut.footprintH = 1;
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(8, 8, 32.f);
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, "hut"));
    REQUIRE(session.isActive());
    REQUIRE(session.updateFromWorld(&world, 80.f, 90.f));
    REQUIRE(session.isValid());
    const int id = session.execute();
    REQUIRE(id > 0);
    REQUIRE_EQ(world.getBuildingCount(), 1);

    session.setMode("remove");
    REQUIRE(session.updateFromWorld(&world, 80.f, 90.f));
    const int removed = session.execute();
    REQUIRE_EQ(removed, id);
    REQUIRE_EQ(world.getBuildingCount(), 0);

    session.stopPlacement();
    REQUIRE(!session.isActive());

    BuildingRegistry::clear();
}
