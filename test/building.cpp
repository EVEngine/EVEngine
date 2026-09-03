#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/HeightfieldSurface.h"
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
       "requireTerrain":[2],"tags":["dock"],"rotationMode":"cardinal",
       "maxSurfaceSlopeDegrees":22.5,"maxSurfaceHeightDelta":0.75,
       "freeFootprintWidthCells":2.5,"freeFootprintHeightCells":0.75,
       "freeFootprintVertices":[-1,0,0,-0.25,1,0,0,0.25]}
    ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(BuildingRegistry::count(), 3);
    CHECK_EQ(BuildingRegistry::find("dock")->requireTerrain.size(), size_t(1));
    CHECK_EQ(BuildingRegistry::find("dock")->requireTerrain[0], 2);
    CHECK(approxEq(BuildingRegistry::find("dock")->maxSurfaceSlopeDegrees, 22.5f));
    CHECK(approxEq(BuildingRegistry::find("dock")->maxSurfaceHeightDelta, 0.75f));
    CHECK(approxEq(BuildingRegistry::find("dock")->freeFootprintWidthCells, 2.5f));
    CHECK(approxEq(BuildingRegistry::find("dock")->freeFootprintHeightCells, 0.75f));
    CHECK_EQ(BuildingRegistry::find("dock")->freeFootprintVertices.size(), size_t{8});

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

TEST_CASE("building.surface.structuredProviderFrameAndAttachment") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "surface-hut";
    hut.validateRule = "surfaceMetadata";
    BuildingRegistry::registerBuilding(hut);

    PlacementSystem::registerValidateRule(
        "surfaceMetadata", [](const PlacementWorld &, const PlacementQuery &query,
                              std::string *reason) {
            if (query.surfaceId != "terrain.chunk.4" || query.surfaceRevision != 17 ||
                !approxEq(query.surfaceNormalY, 0.7071067f)) {
                if (reason) *reason = "surface_metadata_missing";
                return false;
            }
            return true;
        });

    PlacementWorld world(10, 10, 1.f);
    world.setGridPlane("xz");

    PlacementSystem::registerSurfaceProvider(
        "terrain.test", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 2.5f;
            hit.worldZ = y;
            hit.normalX = 0.f;
            hit.normalY = 2.f;
            hit.normalZ = 2.f;
            hit.tangentX = 3.f;
            hit.tangentY = 0.f;
            hit.tangentZ = 0.f;
            hit.surfaceId = "terrain.chunk.4";
            hit.surfaceRevision = 17;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });

    auto sample = PlacementSystem::sampleSurface(world, "terrain.test", 3.2f, 4.7f);
    REQUIRE(sample.ok());
    REQUIRE_EQ(sample.value().surfaceId, "terrain.chunk.4");
    REQUIRE_EQ(sample.value().surfaceRevision, uint64_t(17));
    REQUIRE(approxEq(sample.value().normalY, 0.7071067f));
    REQUIRE(approxEq(sample.value().normalZ, 0.7071067f));
    REQUIRE(approxEq(sample.value().tangentX, 1.f));

    Ghost ghost;
    ghost.setBuildingId("surface-hut");
    ghost.setFromSurface(&world, "terrain.test", 3.2f, 4.7f);
    REQUIRE_EQ(ghost.getSurfaceId(), "terrain.chunk.4");
    REQUIRE_EQ(ghost.getSurfaceRevision(), int64_t(17));
    REQUIRE(approxEq(ghost.getSurfaceNormalY(), 0.7071067f));
    REQUIRE(ghost.validate(&world));

    const int id = world.placeGhost(&ghost);
    REQUIRE(id > 0);
    REQUIRE_EQ(world.getBuildingSurfaceId(id), "terrain.chunk.4");
    REQUIRE_EQ(world.getBuildingSurfaceRevision(id), int64_t(17));
    REQUIRE(approxEq(world.getBuildingSurfaceNormalY(id), 0.7071067f));

    auto missing = PlacementSystem::sampleSurface(world, "missing.surface", 0.f, 0.f);
    REQUIRE(!missing.ok());
    REQUIRE_EQ(missing.code(), eve::StatusCode::NotFound);

    PlacementSystem::registerSurfaceProvider(
        "invalid.normal", [](const PlacementWorld &, float, float) {
            PlacementSystem::PlacementHit hit;
            hit.normalX = 0.f;
            hit.normalY = 0.f;
            hit.normalZ = 0.f;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    auto invalid = PlacementSystem::sampleSurface(world, "invalid.normal", 0.f, 0.f);
    REQUIRE(!invalid.ok());
    REQUIRE_EQ(invalid.code(), eve::StatusCode::Rejected);

    PlacementSystem::unregisterSurface("terrain.test");
    PlacementSystem::unregisterSurface("invalid.normal");
    PlacementSystem::unregisterValidateRule("surfaceMetadata");
    BuildingRegistry::clear();
}

TEST_CASE("building.surface.patchSamplesFootprintAndEnforcesLimits") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition platform;
    platform.id = "surface-platform";
    platform.footprintW = 2;
    platform.footprintH = 2;
    platform.maxSurfaceSlopeDegrees = 10.f;
    platform.maxSurfaceHeightDelta = 0.25f;
    BuildingRegistry::registerBuilding(platform);

    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    int patchSampleCalls = 0;
    PlacementSystem::registerSurfaceProvider(
        "terrain.patch", [&](const PlacementWorld &, float x, float y) {
            ++patchSampleCalls;
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 0.1f * (x + y);
            hit.worldZ = y;
            hit.normalY = 1.f;
            hit.surfaceId = "terrain.main";
            hit.surfaceRevision = 9;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });

    auto patch = PlacementSystem::sampleSurfacePatch(world, platform.id, "terrain.patch", 2.2f,
                                                     2.2f, 0.f);
    REQUIRE(patch.ok());
    REQUIRE_EQ(patch.value().samples.size(), size_t(4));
    REQUIRE(approxEq(patch.value().heightDelta, 0.2f));
    REQUIRE(approxEq(patch.value().maxSlopeDegrees, 0.f));

    Ghost ghost;
    ghost.setBuildingId(platform.id);
    ghost.setFromSurface(&world, "terrain.patch", 2.2f, 2.2f);
    REQUIRE_EQ(ghost.getSurfaceSampleCount(), 4);
    REQUIRE(approxEq(ghost.getSurfaceHeightDelta(), 0.2f));
    REQUIRE(ghost.validate(&world));
    REQUIRE_EQ(patchSampleCalls, 8);
    ghost.rotateBy(90.f);
    REQUIRE(ghost.validate(&world));
    REQUIRE_EQ(patchSampleCalls, 12);

    platform.maxSurfaceHeightDelta = 0.05f;
    BuildingRegistry::registerBuilding(platform);
    REQUIRE(!ghost.validate(&world));
    REQUIRE_EQ(ghost.getReason(), "surface_height_delta");

    PlacementSystem::registerSurfaceProvider(
        "terrain.split", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldZ = y;
            hit.surfaceId = x < 3.f ? "terrain.left" : "terrain.right";
            hit.surfaceRevision = 1;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    auto split = PlacementSystem::sampleSurfacePatch(world, platform.id, "terrain.split", 2.2f,
                                                     2.2f, 0.f);
    REQUIRE(!split.ok());
    REQUIRE_EQ(split.code(), eve::StatusCode::Conflict);

    PlacementSystem::unregisterSurface("terrain.patch");
    PlacementSystem::unregisterSurface("terrain.split");
    BuildingRegistry::clear();
}

TEST_CASE("building.surface.heightfieldValidatesInterpolatesAndOwnsRegistration") {
    HeightfieldSurface::Config invalidConfig;
    invalidConfig.width = 1;
    invalidConfig.height = 2;
    auto invalidDimensions = HeightfieldSurface::create(invalidConfig, {0.f, 1.f});
    REQUIRE(!invalidDimensions.ok());
    REQUIRE_EQ(invalidDimensions.code(), eve::StatusCode::Rejected);

    HeightfieldSurface::Config config;
    config.width = 3;
    config.height = 3;
    config.originX = 10.f;
    config.originY = 20.f;
    config.spacingX = 2.f;
    config.spacingY = 4.f;
    config.heightScale = 2.f;
    config.heightOffset = 3.f;
    config.surfaceId = "heightfield.chunk.7";
    config.surfaceRevision = 11;
    config.tags = {"terrain", "buildable"};
    auto created = HeightfieldSurface::create(
        config, {0.f, 1.f, 2.f, 2.f, 3.f, 4.f, 4.f, 5.f, 6.f});
    REQUIRE(created.ok());
    auto surface = std::move(created).takeValue();
    std::weak_ptr<const HeightfieldSurface> lifetime = surface;

    PlacementWorld xzWorld(32, 32, 1.f);
    xzWorld.setGridPlane("xz");
    auto registered =
        PlacementSystem::registerHeightfieldSurface("heightfield.test", surface);
    REQUIRE(registered.ok());
    surface.reset();
    REQUIRE(!lifetime.expired());

    auto xz = PlacementSystem::sampleSurface(xzWorld, "heightfield.test", 11.f, 21.f);
    REQUIRE(xz.ok());
    REQUIRE(approxEq(xz.value().worldX, 11.f));
    REQUIRE(approxEq(xz.value().worldY, 5.f));
    REQUIRE(approxEq(xz.value().worldZ, 21.f));
    const float normalLength = std::sqrt(3.f);
    REQUIRE(approxEq(xz.value().normalX, -1.f / normalLength));
    REQUIRE(approxEq(xz.value().normalY, 1.f / normalLength));
    REQUIRE(approxEq(xz.value().normalZ, -1.f / normalLength));
    REQUIRE_EQ(xz.value().surfaceId, "heightfield.chunk.7");
    REQUIRE_EQ(xz.value().surfaceRevision, uint64_t(11));
    REQUIRE_EQ(xz.value().primitiveId, uint64_t(0));
    REQUIRE_EQ(xz.value().tags.size(), size_t(2));

    PlacementWorld xyWorld(32, 32, 1.f);
    xyWorld.setGridPlane("xy");
    auto xy = PlacementSystem::sampleSurface(xyWorld, "heightfield.test", 11.f, 21.f);
    REQUIRE(xy.ok());
    REQUIRE(approxEq(xy.value().worldX, 11.f));
    REQUIRE(approxEq(xy.value().worldY, 21.f));
    REQUIRE(approxEq(xy.value().worldZ, 5.f));
    REQUIRE(approxEq(xy.value().normalX, -1.f / normalLength));
    REQUIRE(approxEq(xy.value().normalY, -1.f / normalLength));
    REQUIRE(approxEq(xy.value().normalZ, 1.f / normalLength));

    auto boundary = PlacementSystem::sampleSurface(xzWorld, "heightfield.test", 14.f, 28.f);
    REQUIRE(boundary.ok());
    REQUIRE(approxEq(boundary.value().worldY, 15.f));
    REQUIRE_EQ(boundary.value().primitiveId, uint64_t(3));
    auto outside = PlacementSystem::sampleSurface(xzWorld, "heightfield.test", 14.01f, 28.f);
    REQUIRE(!outside.ok());
    REQUIRE_EQ(outside.code(), eve::StatusCode::NotFound);

    HeightfieldSurface::Config smoothConfig;
    smoothConfig.width = 3;
    smoothConfig.height = 2;
    smoothConfig.surfaceId = "heightfield.smooth";
    auto smoothCreated =
        HeightfieldSurface::create(smoothConfig, {0.f, 0.f, 4.f, 0.f, 2.f, 4.f});
    REQUIRE(smoothCreated.ok());
    auto smoothRegistered = PlacementSystem::registerHeightfieldSurface(
        "heightfield.smooth", std::move(smoothCreated).takeValue());
    REQUIRE(smoothRegistered.ok());
    auto leftOfBoundary =
        PlacementSystem::sampleSurface(xzWorld, "heightfield.smooth", 0.9999f, 0.5f);
    auto rightOfBoundary =
        PlacementSystem::sampleSurface(xzWorld, "heightfield.smooth", 1.0001f, 0.5f);
    REQUIRE(leftOfBoundary.ok());
    REQUIRE(rightOfBoundary.ok());
    REQUIRE(approxEq(leftOfBoundary.value().normalX, rightOfBoundary.value().normalX, 0.001f));
    REQUIRE(approxEq(leftOfBoundary.value().normalY, rightOfBoundary.value().normalY, 0.001f));
    REQUIRE(approxEq(leftOfBoundary.value().normalZ, rightOfBoundary.value().normalZ, 0.001f));
    PlacementSystem::unregisterSurface("heightfield.smooth");

    PlacementSystem::unregisterSurface("heightfield.test");
    REQUIRE(lifetime.expired());
}

TEST_CASE("building.surface.heightfieldFeedsFootprintsAndCurves") {
    BuildingRegistry::clear();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition platform;
    platform.id = "heightfield-platform";
    platform.footprintW = 2;
    platform.footprintH = 2;
    platform.maxSurfaceSlopeDegrees = 50.f;
    platform.maxSurfaceHeightDelta = 3.f;
    BuildingRegistry::registerBuilding(platform);

    BuildingDefinition wall;
    wall.id = "heightfield-wall";
    wall.placementKind = "edge";
    BuildingRegistry::registerBuilding(wall);

    HeightfieldSurface::Config config;
    config.width = 8;
    config.height = 8;
    config.surfaceId = "heightfield.integration";
    config.surfaceRevision = 4;
    std::vector<float> samples;
    samples.reserve(64);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) samples.push_back(0.25f * float(x + y));
    }
    auto created = HeightfieldSurface::create(config, std::move(samples));
    REQUIRE(created.ok());
    auto registered = PlacementSystem::registerHeightfieldSurface(
        "heightfield.integration", std::move(created).takeValue());
    REQUIRE(registered.ok());

    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    auto patch = PlacementSystem::sampleSurfacePatch(
        world, platform.id, "heightfield.integration", 2.2f, 2.2f);
    REQUIRE(patch.ok());
    REQUIRE_EQ(patch.value().samples.size(), size_t(4));
    REQUIRE(approxEq(patch.value().heightDelta, 0.5f));
    REQUIRE_EQ(patch.value().anchor.surfaceId, "heightfield.integration");

    Ghost ghost;
    ghost.setBuildingId(platform.id);
    ghost.setFromSurface(&world, "heightfield.integration", 2.2f, 2.2f);
    REQUIRE(ghost.validate(&world));
    const int platformId = world.placeGhost(&ghost);
    REQUIRE(platformId > 0);
    REQUIRE_EQ(world.getBuildingSurfaceRevision(platformId), int64_t(4));

    const std::vector<PlacementSystem::EdgeCurvePoint> controls{
        {1.f, 1.f}, {2.f, 1.f}, {3.f, 2.f}, {4.f, 2.f}};
    auto curveSurface = PlacementSystem::sampleEdgeCurveSurface(
        world, "heightfield.integration", controls, 8);
    REQUIRE(curveSurface.ok());
    REQUIRE_EQ(curveSurface.value().surfaceId, "heightfield.integration");
    REQUIRE_EQ(curveSurface.value().surfaceRevision, uint64_t(4));
    REQUIRE_EQ(curveSurface.value().samples.size(), size_t(9));
    auto curve = PlacementSystem::placeEdgeCubicBezierOnSurface(
        &world, wall.id, controls, 8, "heightfield.integration");
    REQUIRE(curve.ok());
    REQUIRE(!curve.value().instanceIds.empty());
    auto group = world.edgeCurveGroupForInstance(curve.value().instanceIds.front());
    REQUIRE(group.ok());
    REQUIRE_EQ(group.value().surfaceProviderName, "heightfield.integration");
    REQUIRE_EQ(group.value().surfaceId, "heightfield.integration");
    REQUIRE_EQ(group.value().surfaceRevision, uint64_t(4));
    REQUIRE_EQ(group.value().surfaceSamples.size(), size_t(9));

    PlacementSystem::unregisterSurface("heightfield.integration");
    BuildingRegistry::clear();
}

TEST_CASE("building.edge.canonicalOccupancyAndTopology") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();

    BuildingDefinition wall;
    wall.id = "stone-wall";
    wall.placementKind = "edge";
    wall.connectionGroup = "stone";
    wall.channel = "walls";
    BuildingRegistry::registerBuilding(wall);

    BuildingDefinition gate = wall;
    gate.id = "stone-gate";
    BuildingRegistry::registerBuilding(gate);

    PlacementWorld world(4, 3, 10.f);
    CHECK(world.canPlaceEdge("stone-wall", 1, 1, "north"));
    const int first = world.placeEdge("stone-wall", 1, 1, "north");
    REQUIRE(first > 0);
    CHECK_EQ(world.getEdgeOccupant("walls", 1, 0, "south"), first);
    CHECK(!world.canPlaceEdge("stone-gate", 1, 0, "south"));
    CHECK_EQ(world.canPlaceEdgeReason("stone-gate", 1, 0, "south"), "edge_occupied");

    const int second = world.placeEdge("stone-wall", 2, 1, "north");
    const int branch = world.placeEdge("stone-gate", 1, 0, "east");
    REQUIRE(second > 0);
    REQUIRE(branch > 0);
    CHECK((world.getEdgeConnectionMask(first) & (1 << 1)) != 0);
    CHECK((world.getEdgeConnectionMask(first) & 0x3c) != 0);

    CHECK_EQ(world.placeAt("stone-wall", 0, 0), 0);
    CHECK_EQ(world.canPlaceReason("stone-wall", 0, 0), "edge_requires_edge_address");
    CHECK(world.removeBuilding(first));
    CHECK(world.isEdgeEmpty("walls", 1, 1, "north"));

    PlacedBuilding snapshot = world.buildings().at(second);
    CHECK(world.removeBuilding(second));
    std::string reason;
    CHECK_EQ(static_cast<int>(PlacementSystem::restoreExact(&world, snapshot, &reason)),
             static_cast<int>(PlacementRestoreStatus::Restored));
    CHECK_EQ(world.getEdgeOccupant("walls", 2, 1, "north"), second);

    world.clearBuildings();
    CHECK_EQ(world.getBuildingCount(), 0);
    CHECK(world.isEdgeEmpty("walls", 1, 0, "east"));

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, "stone-wall"));
    REQUIRE_EQ(static_cast<int>(session.updateEdge(&world, 0, 0, "east")),
               static_cast<int>(EdgeUpdateStatus::Updated));
    REQUIRE(session.isValid());
    REQUIRE_EQ(session.getGhost()->getPlacementKind(), "edge");
    REQUIRE_EQ(session.getGhost()->getEdgeAxis(), "vertical");
    const int sessionWall = session.execute();
    REQUIRE(sessionWall > 0);
    CHECK_EQ(world.getEdgeOccupant("walls", 0, 0, "east"), sessionWall);
}

TEST_CASE("building.edgeLine.atomicRollbackAndVariants") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();

    BuildingDefinition wall;
    wall.id = "line-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(5, 5, 1.f);

    const int blocker = world.placeEdge(wall.id, 1, 0, "north");
    REQUIRE(blocker > 0);
    PlacementSystem::clearEvents();
    const int countBefore = world.getBuildingCount();
    auto rejected = PlacementSystem::placeEdgeLine(&world, wall.id, 0, 0, 3, 0);
    CHECK(!rejected.ok());
    CHECK_EQ(world.getBuildingCount(), countBefore);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 0);
    CHECK(world.isEdgeEmpty("walls", 0, 0, "north"));
    CHECK(world.isEdgeEmpty("walls", 2, 0, "north"));

    REQUIRE(world.removeBuilding(blocker));
    PlacementSystem::clearEvents();
    auto placed = PlacementSystem::placeEdgeLine(&world, wall.id, 3, 0, 0, 0);
    REQUIRE(placed.ok());
    const auto ids = std::move(placed).takeValue().instanceIds;
    REQUIRE_EQ(static_cast<int>(ids.size()), 3);
    REQUIRE_EQ(static_cast<int>(PlacementSystem::events().size()), 3);
    CHECK_EQ(world.getEdgeVariant(ids[0]), "end");
    CHECK_EQ(world.getEdgeVariant(ids[1]), "straight");
    CHECK_EQ(world.getEdgeVariant(ids[2]), "end");

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, wall.id));
    const EdgeLineExecuteStatus lineStatus = session.executeEdgeLine(0, 2, 0, 5);
    REQUIRE_EQ(static_cast<int>(lineStatus),
               static_cast<int>(EdgeLineExecuteStatus::Placed));
    REQUIRE_EQ(session.getLastPlacedCount(), 3);
    CHECK(session.getLastPlacedId(0) > 0);
    CHECK_EQ(session.getLastPlacedId(3), 0);
}

TEST_CASE("building.edgePathCommitsTurnsAtomicallyAndRejectsDuplicateEdges") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    BuildingDefinition wall;
    wall.id = "path-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    wall.connectionGroup = "path-wall";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(8, 8, 1.f);

    const int blocker = world.placeEdge(wall.id, 3, 1, "west");
    REQUIRE(blocker > 0);
    PlacementSystem::clearEvents();
    const int countBefore = world.getBuildingCount();
    auto rejected = PlacementSystem::placeEdgePath(
        &world, wall.id, {{0, 0}, {3, 0}, {3, 2}});
    CHECK(!rejected.ok());
    CHECK_EQ(world.getBuildingCount(), countBefore);
    CHECK(PlacementSystem::events().empty());
    REQUIRE(world.removeBuilding(blocker));

    auto duplicate = PlacementSystem::placeEdgePath(
        &world, wall.id, {{0, 0}, {2, 0}, {0, 0}});
    CHECK(!duplicate.ok());
    CHECK_EQ(world.getBuildingCount(), 0);

    PlacementSystem::clearEvents();
    auto placed = PlacementSystem::placeEdgePath(
        &world, wall.id, {{0, 0}, {3, 0}, {3, 2}});
    REQUIRE(placed.ok());
    const auto ids = std::move(placed).takeValue().instanceIds;
    REQUIRE_EQ(ids.size(), size_t{5});
    CHECK_EQ(PlacementSystem::events().size(), size_t{5});
    CHECK_EQ(world.getEdgeVariant(ids[2]), "corner");
    CHECK_EQ(world.getEdgeVariant(ids[3]), "corner");

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, wall.id));
    CHECK_EQ(static_cast<int>(session.beginEdgePath(5, 1)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendEdgePathVertex(5, 4)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendEdgePathVertex(7, 4)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(session.getEdgePathVertexCount(), 3);
    CHECK_EQ(static_cast<int>(session.executeEdgePath()),
             static_cast<int>(EdgeLineExecuteStatus::Placed));
    CHECK_EQ(session.getLastPlacedCount(), 5);
    CHECK_EQ(session.getEdgePathVertexCount(), 0);
    BuildingRegistry::clear();
}

TEST_CASE("building.edgeCubicBezierSamplesDeterministicallyAndCommitsAtomically") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    BuildingDefinition wall;
    wall.id = "curve-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    wall.connectionGroup = "curve-wall";
    BuildingRegistry::registerBuilding(wall);
    PlacementWorld world(10, 10, 1.f);
    const std::vector<PlacementSystem::EdgeCurvePoint> controls{
        {1.f, 1.f}, {1.f, 6.f}, {7.f, 6.f}, {7.f, 1.f}};

    auto sampled = PlacementSystem::sampleEdgeCubicBezier(controls, 24);
    REQUIRE(sampled.ok());
    REQUIRE(sampled.value().size() > size_t{7});
    CHECK_EQ(sampled.value().front().x, 1);
    CHECK_EQ(sampled.value().front().y, 1);
    CHECK_EQ(sampled.value().back().x, 7);
    CHECK_EQ(sampled.value().back().y, 1);
    for (size_t i = 1; i < sampled.value().size(); ++i) {
        const int distance = std::abs(sampled.value()[i].x - sampled.value()[i - 1].x) +
                             std::abs(sampled.value()[i].y - sampled.value()[i - 1].y);
        CHECK_EQ(distance, 1);
    }
    auto sampledAgain = PlacementSystem::sampleEdgeCubicBezier(controls, 24);
    REQUIRE(sampledAgain.ok());
    CHECK(sampledAgain.value() == sampled.value());
    CHECK(!PlacementSystem::sampleEdgeCubicBezier(controls, 1).ok());

    auto preview = PlacementSystem::previewEdgeCubicBezier(&world, wall.id, controls, 24);
    REQUIRE(preview.ok());
    REQUIRE(!preview.value().edges.empty());
    const EdgeAddress blockedEdge = preview.value().edges[preview.value().edges.size() / 2];
    const int blocker = world.placeEdge(
        wall.id, blockedEdge.x, blockedEdge.y,
        blockedEdge.axis == EdgeAxis::Horizontal ? "north" : "west");
    REQUIRE(blocker > 0);
    PlacementSystem::clearEvents();
    auto rejected = PlacementSystem::placeEdgeCubicBezier(&world, wall.id, controls, 24);
    CHECK(!rejected.ok());
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK(PlacementSystem::events().empty());
    REQUIRE(world.removeBuilding(blocker));

    auto placed = PlacementSystem::placeEdgeCubicBezier(&world, wall.id, controls, 24);
    REQUIRE(placed.ok());
    CHECK_EQ(placed.value().instanceIds.size(), preview.value().edges.size());
    CHECK_EQ(PlacementSystem::events().size(), preview.value().edges.size());
    CHECK_EQ(world.getEdgeCurveGroupCount(), 1);
    auto group = world.edgeCurveGroupForInstance(placed.value().instanceIds.front());
    REQUIRE(group.ok());
    CHECK(group.value().id);
    CHECK_EQ(group.value().buildingId, wall.id);
    CHECK_EQ(group.value().controlPoints, controls);
    CHECK_EQ(group.value().subdivisions, 24);
    CHECK_EQ(group.value().instanceIds, placed.value().instanceIds);
    auto cloned = world.cloneState();
    REQUIRE(static_cast<bool>(cloned));
    auto clonedGroup = cloned->edgeCurveGroup(group.value().id);
    REQUIRE(clonedGroup.ok());
    CHECK_EQ(clonedGroup.value(), group.value());

    const int removedMember = placed.value().instanceIds[placed.value().instanceIds.size() / 2];
    REQUIRE(world.removeBuilding(removedMember));
    CHECK_EQ(world.getEdgeCurveGroupCount(), 0);
    CHECK(!world.edgeCurveGroup(group.value().id).ok());
    for (int instanceId : placed.value().instanceIds) {
        if (instanceId == removedMember) continue;
        CHECK(!world.edgeCurveGroupForInstance(instanceId).ok());
    }

    PlacementWorld sessionWorld(10, 10, 1.f);
    PlacementSession session;
    REQUIRE(session.startPlacement(&sessionWorld, wall.id));
    CHECK_EQ(static_cast<int>(session.beginEdgeCurve(1.f, 1.f)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendEdgeCurveControlPoint(1.f, 6.f)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 6.f)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendEdgeCurveControlPoint(7.f, 1.f)),
             static_cast<int>(EdgePathUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.executeEdgeCurve(24)),
             static_cast<int>(EdgeLineExecuteStatus::Placed));
    CHECK(session.getLastPlacedCount() > 0);

    PlacementWorld surfaceSessionWorld(10, 10, 10.f);
    surfaceSessionWorld.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "session.curve-terrain", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = x * 0.05f;
            hit.worldZ = y;
            hit.normalX = -0.05f;
            hit.normalY = 1.f;
            hit.surfaceId = "terrain.session";
            hit.surfaceRevision = 3;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    PlacementSession surfaceSession;
    REQUIRE(surfaceSession.startPlacement(&surfaceSessionWorld, wall.id));
    REQUIRE(static_cast<int>(surfaceSession.beginEdgeCurve(1.f, 1.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(surfaceSession.appendEdgeCurveControlPoint(1.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(surfaceSession.appendEdgeCurveControlPoint(7.f, 6.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(surfaceSession.appendEdgeCurveControlPoint(7.f, 1.f)) ==
            static_cast<int>(EdgePathUpdateStatus::Updated));
    REQUIRE(static_cast<int>(surfaceSession.executeEdgeCurveOnSurface(
                24, "session.curve-terrain")) ==
            static_cast<int>(EdgeLineExecuteStatus::Placed));
    REQUIRE(surfaceSession.getLastPlacedCount() > 0);
    auto surfaceGroup = surfaceSessionWorld.edgeCurveGroupForInstance(
        surfaceSession.getLastPlacedId(0));
    REQUIRE(surfaceGroup.ok());
    CHECK_EQ(surfaceGroup.value().surfaceProviderName, "session.curve-terrain");
    CHECK_EQ(surfaceGroup.value().surfaceId, "terrain.session");
    CHECK_EQ(surfaceGroup.value().surfaceRevision, uint64_t{3});
    PlacementSystem::unregisterSurface("session.curve-terrain");
    BuildingRegistry::clear();
}

TEST_CASE("building.edit.moveReplaceTransactions") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();

    BuildingDefinition chair;
    chair.id = "edit-chair";
    chair.channel = "furniture";
    chair.tags = {"seat"};
    BuildingRegistry::registerBuilding(chair);
    BuildingDefinition bench = chair;
    bench.id = "edit-bench";
    bench.footprintW = 2;
    bench.tags = {"bench"};
    BuildingRegistry::registerBuilding(bench);
    BuildingDefinition wall;
    wall.id = "edit-wall";
    wall.placementKind = "edge";
    wall.channel = "walls";
    BuildingRegistry::registerBuilding(wall);
    BuildingDefinition gate = wall;
    gate.id = "edit-gate";
    gate.tags = {"gate"};
    BuildingRegistry::registerBuilding(gate);

    PlacementWorld world(6, 6, 1.f);
    const int source = world.placeAt(chair.id, 1, 1);
    const int blocker = world.placeAt(chair.id, 2, 1);
    REQUIRE(source > 0);
    REQUIRE(blocker > 0);
    world.buildings().at(source).setProp("owner", "player");
    world.buildings().at(source).garrison.push_back({"worker-1", "worker", {"builder"}});

    PlacementSystem::clearEvents();
    auto rejectedMove = PlacementSystem::moveBuildingResult(&world, source, 2, 1, 0.f);
    CHECK(!rejectedMove.ok());
    CHECK_EQ(world.getBuildingCellX(source), 1);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 0);

    auto moved = PlacementSystem::moveBuildingResult(&world, source, 1, 2, 0.f);
    REQUIRE(moved.ok());
    PlacementEditReceipt moveReceipt = std::move(moved).takeValue();
    CHECK_EQ(moveReceipt.before.originCellY, 1);
    CHECK_EQ(moveReceipt.after.originCellY, 2);
    CHECK_EQ(world.getBuildingCellY(source), 2);
    REQUIRE_EQ(static_cast<int>(PlacementSystem::events().size()), 1);
    CHECK_EQ(PlacementSystem::events().back().action, "move");

    REQUIRE(world.moveBuilding(blocker, 2, 2));
    PlacementSystem::clearEvents();
    auto rejectedReplace = PlacementSystem::replaceBuildingResult(&world, source, bench.id);
    CHECK(!rejectedReplace.ok());
    CHECK_EQ(world.buildings().at(source).buildingId, chair.id);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 0);

    REQUIRE(world.moveBuilding(blocker, 4, 4));
    PlacementSystem::clearEvents();
    auto replaced = PlacementSystem::replaceBuildingResult(&world, source, bench.id);
    REQUIRE(replaced.ok());
    PlacementEditReceipt replaceReceipt = std::move(replaced).takeValue();
    CHECK_EQ(replaceReceipt.before.buildingId, chair.id);
    CHECK_EQ(replaceReceipt.after.buildingId, bench.id);
    CHECK_EQ(replaceReceipt.after.instanceId, source);
    CHECK_EQ(world.buildings().at(source).getProp("owner"), "player");
    REQUIRE_EQ(static_cast<int>(world.buildings().at(source).garrison.size()), 1);
    REQUIRE_EQ(static_cast<int>(PlacementSystem::events().size()), 1);
    CHECK_EQ(PlacementSystem::events().back().action, "replace");
    CHECK_EQ(PlacementSystem::events().back().otherBuildingId, chair.id);

    const int edge = world.placeEdge(wall.id, 0, 0, "north");
    const int edgeBlocker = world.placeEdge(wall.id, 2, 0, "north");
    REQUIRE(edge > 0);
    REQUIRE(edgeBlocker > 0);
    PlacementSystem::clearEvents();
    auto rejectedEdgeMove = PlacementSystem::moveEdgeResult(&world, edge, 2, 0, "north");
    CHECK(!rejectedEdgeMove.ok());
    CHECK_EQ(world.getEdgeOccupant("walls", 0, 0, "north"), edge);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 0);

    auto movedEdge = PlacementSystem::moveEdgeResult(&world, edge, 1, 0, "east");
    REQUIRE(movedEdge.ok());
    CHECK_EQ(static_cast<int>(std::move(movedEdge).takeValue().after.edge.axis),
             static_cast<int>(EdgeAxis::Vertical));
    CHECK_EQ(world.getEdgeOccupant("walls", 1, 0, "east"), edge);
    auto replacedEdge = PlacementSystem::replaceBuildingResult(&world, edge, gate.id);
    REQUIRE(replacedEdge.ok());
    CHECK_EQ(std::move(replacedEdge).takeValue().after.instanceId, edge);
    CHECK_EQ(world.buildings().at(edge).buildingId, gate.id);

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, chair.id));
    REQUIRE(session.updateFromWorld(&world, 0.f, 3.f));
    CHECK_EQ(static_cast<int>(session.executeMove(source)),
             static_cast<int>(PlacementEditExecuteStatus::Committed));
    CHECK_EQ(session.getLastEditedId(), source);
    CHECK_EQ(world.getBuildingCellX(source), 0);
    CHECK_EQ(world.getBuildingCellY(source), 3);
    CHECK_EQ(static_cast<int>(session.executeReplace(source)),
             static_cast<int>(PlacementEditExecuteStatus::Committed));
    CHECK_EQ(world.buildings().at(source).buildingId, chair.id);

    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
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

TEST_CASE("building.area.previewAndAtomicCommit") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    BuildingDefinition tile;
    tile.id = "area-tile";
    tile.channel = "floor";
    BuildingRegistry::registerBuilding(tile);
    PlacementWorld world(5, 5, 1.f);
    REQUIRE(world.placeAt(tile.id, 1, 1) > 0);
    PlacementSystem::clearEvents();

    auto previewResult = PlacementSystem::previewRectangle(&world, tile.id, 0, 0, 2, 1);
    REQUIRE(previewResult.ok());
    auto preview = std::move(previewResult).takeValue();
    CHECK_EQ(preview.acceptedCount, 5);
    CHECK_EQ(preview.rejectedCount, 1);
    CHECK_EQ(preview.cells[4].reason, "occupied");
    auto rejected = PlacementSystem::placeRectangle(&world, tile.id, 0, 0, 2, 1);
    CHECK(!rejected.ok());
    CHECK_EQ(world.getBuildingCount(), 1);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 0);

    auto brushPreview = PlacementSystem::previewBrush(&world, tile.id, 3, 3, 1);
    REQUIRE(brushPreview.ok());
    CHECK_EQ(static_cast<int>(brushPreview.value().cells.size()), 5);
    auto brush = PlacementSystem::placeBrush(&world, tile.id, 3, 3, 1);
    REQUIRE(brush.ok());
    CHECK_EQ(static_cast<int>(std::move(brush).takeValue().instanceIds.size()), 5);
    CHECK_EQ(world.getBuildingCount(), 6);
    CHECK_EQ(static_cast<int>(PlacementSystem::events().size()), 5);

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, tile.id));
    CHECK_EQ(static_cast<int>(session.previewRectangle(0, 3, 1, 4)),
             static_cast<int>(AreaExecuteStatus::Accepted));
    CHECK_EQ(session.getAreaPreviewCount(), 4);
    CHECK(session.getAreaPreviewAccepted(0));
    CHECK_EQ(static_cast<int>(session.executeRectangle(0, 3, 1, 4)),
             static_cast<int>(AreaExecuteStatus::Accepted));
    CHECK_EQ(session.getLastPlacedCount(), 4);
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
}

TEST_CASE("building.pattern.unifies_edge_area_outline_and_session_commit") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    BuildingDefinition tile;
    tile.id = "pattern-tile";
    tile.channel = "pattern-floor";
    BuildingRegistry::registerBuilding(tile);
    BuildingDefinition wall;
    wall.id = "pattern-wall";
    wall.channel = "pattern-wall";
    wall.placementKind = "edge";
    BuildingRegistry::registerBuilding(wall);

    PlacementWorld areaWorld(12, 12, 1.f);
    PlacementSystem::PatternRequest request;
    request.kind = PlacementSystem::PatternKind::RectangleOutline;
    request.points = {{1.f, 1.f}, {4.f, 3.f}};
    auto outline = PlacementSystem::previewPattern(&areaWorld, tile.id, request);
    REQUIRE(outline.ok());
    CHECK_EQ(outline.value().anchorCount(), 10);
    CHECK_EQ(outline.value().area.acceptedCount, 10);
    auto placedOutline = PlacementSystem::placePattern(&areaWorld, tile.id, request);
    REQUIRE(placedOutline.ok());
    CHECK_EQ(static_cast<int>(placedOutline.value().instanceIds.size()), 10);
    CHECK_EQ(areaWorld.getOccupantAtLevel("pattern-floor", 2, 2, 0), 0);

    PlacementSystem::PatternRequest brush;
    brush.kind = PlacementSystem::PatternKind::CircleBrush;
    brush.points = {{8.f, 8.f}};
    brush.radius = 1;
    auto brushPreview = PlacementSystem::previewPattern(&areaWorld, tile.id, brush);
    REQUIRE(brushPreview.ok());
    CHECK_EQ(brushPreview.value().anchorCount(), 5);

    PlacementWorld edgeWorld(16, 16, 1.f);
    PlacementSystem::PatternRequest line;
    line.kind = PlacementSystem::PatternKind::EdgeLine;
    line.points = {{1.f, 1.f}, {5.f, 1.f}};
    auto linePreview = PlacementSystem::previewPattern(&edgeWorld, wall.id, line);
    REQUIRE(linePreview.ok());
    CHECK_EQ(linePreview.value().anchorCount(), 4);

    PlacementSystem::PatternRequest path;
    path.kind = PlacementSystem::PatternKind::EdgePath;
    path.points = {{1.f, 2.f}, {5.f, 2.f}, {5.f, 5.f}};
    auto pathPreview = PlacementSystem::previewPattern(&edgeWorld, wall.id, path);
    REQUIRE(pathPreview.ok());
    CHECK_EQ(pathPreview.value().anchorCount(), 7);

    PlacementSystem::PatternRequest curve;
    curve.kind = PlacementSystem::PatternKind::EdgeCubicBezier;
    curve.points = {{1.f, 7.f}, {1.f, 12.f}, {8.f, 12.f}, {8.f, 7.f}};
    curve.subdivisions = 24;
    auto curvePreview = PlacementSystem::previewPattern(&edgeWorld, wall.id, curve);
    REQUIRE(curvePreview.ok());
    CHECK(curvePreview.value().anchorCount() > 7);

    PlacementWorld sessionWorld(12, 12, 1.f);
    PlacementSession session;
    REQUIRE(session.startPlacement(&sessionWorld, tile.id));
    CHECK_EQ(static_cast<int>(session.beginPattern("rectangle_outline")),
             static_cast<int>(PatternUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendPatternPoint(2.f, 2.f)),
             static_cast<int>(PatternUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.appendPatternPoint(5.f, 4.f)),
             static_cast<int>(PatternUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.previewPattern()),
             static_cast<int>(PatternUpdateStatus::Updated));
    CHECK_EQ(session.getPatternPreviewCount(), 10);
    CHECK(session.getPatternPreviewAccepted(0));
    CHECK_EQ(session.getPatternPreviewAxis(0), std::string{});
    CHECK_EQ(static_cast<int>(session.executePattern()),
             static_cast<int>(PatternExecuteStatus::Placed));
    CHECK_EQ(session.getLastPlacedCount(), 10);
    CHECK_EQ(sessionWorld.getBuildingCount(), 10);

    CHECK_EQ(static_cast<int>(session.beginPattern("not-a-pattern")),
             static_cast<int>(PatternUpdateStatus::Rejected));
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
}

TEST_CASE("building.levelsIsolateCellAndEdgeOccupancy") {
    BuildingRegistry::clear();
    BuildingDefinition tile;
    tile.id = "level-tile";
    tile.channel = "floor";
    BuildingRegistry::registerBuilding(tile);
    BuildingDefinition wall;
    wall.id = "level-wall";
    wall.channel = "walls";
    wall.placementKind = "edge";
    BuildingRegistry::registerBuilding(wall);

    PlacementWorld world(4, 4, 1.f);
    world.setFloorHeight(2.5f);
    const int groundTile = world.placeAt(tile.id, 1, 1);
    const int groundWall = world.placeEdge(wall.id, 1, 1, "north");
    REQUIRE(groundTile > 0);
    REQUIRE(groundWall > 0);
    CHECK_EQ(world.getOccupantAtLevel("floor", 1, 1, 0), groundTile);
    CHECK_EQ(world.getEdgeOccupantAtLevel("walls", 1, 1, "north", 0), groundWall);

    world.setActiveLevel(1);
    const int upperTile = world.placeAt(tile.id, 1, 1);
    const int upperWall = world.placeEdge(wall.id, 1, 1, "north");
    REQUIRE(upperTile > 0);
    REQUIRE(upperWall > 0);
    CHECK_EQ(world.getBuildingLevel(upperTile), 1);
    CHECK_EQ(world.getOccupantInChannel("floor", 1, 1), upperTile);
    CHECK_EQ(world.getOccupantAtLevel("floor", 1, 1, 0), groundTile);
    CHECK_EQ(world.getEdgeOccupant("walls", 1, 1, "north"), upperWall);
    CHECK_EQ(world.getEdgeOccupantAtLevel("walls", 1, 1, "north", 0), groundWall);
    CHECK_EQ(world.placeAt(tile.id, 1, 1), 0);
    CHECK_EQ(world.placeEdge(wall.id, 1, 1, "north"), 0);
    CHECK_EQ(world.getEdgeConnectionMask(groundWall), 0);

    auto clone = world.cloneState();
    REQUIRE(static_cast<bool>(clone));
    CHECK_EQ(clone->getActiveLevel(), 1);
    CHECK(std::abs(clone->getFloorHeight() - 2.5f) < 0.0001f);
    CHECK_EQ(clone->getOccupantAtLevel("floor", 1, 1, 1), upperTile);
    BuildingRegistry::clear();
}

TEST_CASE("building.structuralSupportRequiresTaggedCellBelow") {
    BuildingRegistry::clear();
    BuildingDefinition foundation;
    foundation.id = "foundation";
    foundation.channel = "floor";
    foundation.tags = {"structural.support"};
    BuildingRegistry::registerBuilding(foundation);
    BuildingDefinition roof;
    roof.id = "roof";
    roof.channel = "roof";
    roof.structuralRole = "roof";
    roof.supportMode = "cell_below";
    roof.supportTag = "structural.support";
    BuildingRegistry::registerBuilding(roof);
    BuildingDefinition reinforced = foundation;
    reinforced.id = "reinforced-foundation";
    BuildingRegistry::registerBuilding(reinforced);
    BuildingDefinition decorative = foundation;
    decorative.id = "decorative-floor";
    decorative.tags.clear();
    BuildingRegistry::registerBuilding(decorative);

    PlacementWorld world(4, 4, 1.f);
    world.setActiveLevel(1);
    CHECK_EQ(world.canPlaceReason(roof.id, 2, 2), "support_missing");
    CHECK_EQ(world.placeAt(roof.id, 2, 2), 0);
    world.setActiveLevel(0);
    const int foundationId = world.placeAt(foundation.id, 2, 2);
    REQUIRE(foundationId > 0);
    world.setActiveLevel(1);
    const int roofId = world.placeAt(roof.id, 2, 2);
    REQUIRE(roofId > 0);
    CHECK_EQ(world.getBuildingLevel(roofId), 1);
    CHECK_EQ(world.getBuildingSupportCount(roofId), 1);
    CHECK_EQ(world.getBuildingSupportAt(roofId, 0), foundationId);
    CHECK_EQ(world.getBuildingDependentCount(foundationId), 1);
    CHECK_EQ(world.canRemoveBuildingReason(foundationId), "support_in_use");
    CHECK(!world.removeBuilding(foundationId));
    auto moveSupport = PlacementSystem::moveBuildingResult(&world, foundationId, 1, 1);
    CHECK(!moveSupport.ok());
    auto badReplace =
        PlacementSystem::replaceBuildingResult(&world, foundationId, decorative.id);
    CHECK(!badReplace.ok());
    auto preservingReplace =
        PlacementSystem::replaceBuildingResult(&world, foundationId, reinforced.id);
    REQUIRE(preservingReplace.ok());
    CHECK_EQ(world.getBuildingDependentCount(foundationId), 1);
    auto cascade = PlacementSystem::removeBuildingCascadeResult(&world, foundationId);
    REQUIRE(cascade.ok());
    REQUIRE_EQ(cascade.value().removed.size(), size_t(2));
    CHECK_EQ(cascade.value().removed[0].instanceId, roofId);
    CHECK_EQ(cascade.value().removed[1].instanceId, foundationId);
    CHECK(!world.hasBuilding(foundationId));
    CHECK(!world.hasBuilding(roofId));

    std::string error;
    CHECK_EQ(BuildingRegistry::loadFromJson(
                 R"([{"id":"json-roof","structuralRole":"roof","supportMode":"cell_below","supportTag":"structural.support"}])",
                 &error),
             1);
    const BuildingDefinition *parsed = BuildingRegistry::find("json-roof");
    REQUIRE(parsed != nullptr);
    CHECK_EQ(parsed->structuralRole, "roof");
    CHECK_EQ(parsed->supportMode, "cell_below");
    CHECK_EQ(parsed->supportTag, "structural.support");
    BuildingRegistry::clear();
}

TEST_CASE("building.structuralCascadeIsTransitiveAndRejectsCorruptCycles") {
    BuildingRegistry::clear();
    BuildingDefinition support;
    support.id = "support";
    support.channel = "structure";
    support.tags = {"structural.support"};
    support.supportMode = "cell_below";
    support.supportTag = "structural.support";
    BuildingRegistry::registerBuilding(support);

    PlacementWorld world(2, 2, 1.f);
    world.setActiveLevel(0);
    BuildingDefinition ground = support;
    ground.id = "ground-support";
    ground.supportMode = "none";
    BuildingRegistry::registerBuilding(ground);
    const int level0 = world.placeAt(ground.id, 0, 0);
    world.setActiveLevel(1);
    const int level1 = world.placeAt(support.id, 0, 0);
    world.setActiveLevel(2);
    const int level2 = world.placeAt(support.id, 0, 0);
    REQUIRE(level0 > 0);
    REQUIRE(level1 > 0);
    REQUIRE(level2 > 0);

    int removalHooks = 0;
    bool hookObservedPartialCommit = false;
    PlacementSystem::registerChangeHook("structural-atomic-observer",
                                        [&](const BuildingChangeEvent &event) {
                                            if (event.action != "remove") return;
                                            ++removalHooks;
                                            hookObservedPartialCommit =
                                                hookObservedPartialCommit ||
                                                world.hasBuilding(level0) ||
                                                world.hasBuilding(level1) ||
                                                world.hasBuilding(level2);
                                        });
    auto cascade = PlacementSystem::removeBuildingCascadeResult(&world, level0);
    REQUIRE(cascade.ok());
    REQUIRE_EQ(cascade.value().removed.size(), size_t(3));
    CHECK_EQ(cascade.value().removed[0].instanceId, level2);
    CHECK_EQ(cascade.value().removed[1].instanceId, level1);
    CHECK_EQ(cascade.value().removed[2].instanceId, level0);
    CHECK_EQ(removalHooks, 3);
    CHECK(!hookObservedPartialCommit);
    PlacementSystem::unregisterChangeHook("structural-atomic-observer");

    world.setActiveLevel(0);
    const int a = world.placeAt(ground.id, 0, 0);
    const int b = world.placeAt(ground.id, 1, 0);
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    world.buildings().at(a).supportInstanceIds = {b};
    world.buildings().at(b).supportInstanceIds = {a};
    auto corrupt = PlacementSystem::removeBuildingCascadeResult(&world, a);
    CHECK(!corrupt.ok());
    CHECK(world.hasBuilding(a));
    CHECK(world.hasBuilding(b));
    BuildingRegistry::clear();
}

TEST_CASE("building.structuralLinksRebuildAtomicallyAfterDefinitionReload") {
    BuildingRegistry::clear();
    BuildingDefinition base;
    base.id = "reload-base";
    base.channel = "base";
    base.tags = {"load-bearing"};
    BuildingRegistry::registerBuilding(base);
    BuildingDefinition upper;
    upper.id = "reload-upper";
    upper.channel = "upper";
    upper.supportMode = "cell_below";
    upper.supportTag = "load-bearing";
    BuildingRegistry::registerBuilding(upper);

    PlacementWorld world(2, 2, 1.f);
    const int baseId = world.placeAt(base.id, 0, 0);
    world.setActiveLevel(1);
    const int upperId = world.placeAt(upper.id, 0, 0);
    REQUIRE(baseId > 0);
    REQUIRE(upperId > 0);
    world.buildings().at(upperId).supportInstanceIds.clear();

    auto rebuilt = PlacementSystem::rebuildStructuralLinksResult(&world);
    REQUIRE(rebuilt.ok());
    CHECK_EQ(rebuilt.value().inspectedCount, 2);
    CHECK_EQ(rebuilt.value().changedCount, 1);
    CHECK_EQ(world.getBuildingSupportAt(upperId, 0), baseId);

    BuildingDefinition incompatible = base;
    incompatible.tags.clear();
    BuildingRegistry::registerBuilding(incompatible);
    const auto linksBeforeFailure = world.buildings().at(upperId).supportInstanceIds;
    auto rejected = PlacementSystem::rebuildStructuralLinksResult(&world);
    CHECK(!rejected.ok());
    CHECK_EQ(world.buildings().at(upperId).supportInstanceIds, linksBeforeFailure);
    CHECK(world.hasBuilding(baseId));
    CHECK(world.hasBuilding(upperId));
    BuildingRegistry::clear();
}

TEST_CASE("building.cornerDomainOwnsVerticesAndSupportsVerticalStructures") {
    BuildingRegistry::clear();
    BuildingDefinition post;
    post.id = "corner-post";
    post.placementKind = "corner";
    post.channel = "posts";
    post.tags = {"post-support"};
    BuildingRegistry::registerBuilding(post);
    BuildingDefinition reinforced = post;
    reinforced.id = "corner-post-reinforced";
    BuildingRegistry::registerBuilding(reinforced);
    BuildingDefinition decorative = post;
    decorative.id = "corner-decoration";
    decorative.tags.clear();
    BuildingRegistry::registerBuilding(decorative);
    BuildingDefinition upper = post;
    upper.id = "corner-upper";
    upper.channel = "upper-posts";
    upper.supportMode = "corner_below";
    upper.supportTag = "post-support";
    BuildingRegistry::registerBuilding(upper);
    BuildingDefinition tile;
    tile.id = "corner-domain-cell";
    tile.channel = "posts";
    BuildingRegistry::registerBuilding(tile);

    PlacementWorld world(2, 2, 1.f);
    REQUIRE(world.placeAt(tile.id, 2 - 1, 2 - 1) > 0);
    auto base = PlacementSystem::placeCornerResult(&world, post.id, 2, 2);
    REQUIRE(base.ok());
    const int baseId = base.value().instanceId;
    CHECK_EQ(world.getCornerOccupant("posts", 2, 2), baseId);
    CHECK_EQ(world.getOccupantInChannel("posts", 1, 1) != 0, true);
    CHECK_EQ(world.canPlaceReason(post.id, 0, 0), "corner_requires_corner_address");
    CHECK_EQ(world.canPlaceCornerReason(post.id, 3, 2), "corner_out_of_bounds");
    CHECK_EQ(world.canPlaceCornerReason(post.id, 2, 2), "corner_occupied");

    world.setActiveLevel(1);
    CHECK_EQ(world.canPlaceCornerReason(upper.id, 2, 2), std::string{});
    auto top = PlacementSystem::placeCornerResult(&world, upper.id, 2, 2);
    REQUIRE(top.ok());
    const int topId = top.value().instanceId;
    CHECK_EQ(world.getBuildingSupportAt(topId, 0), baseId);
    CHECK_EQ(world.getCornerOccupantAtLevel("posts", 2, 2, 0), baseId);
    CHECK_EQ(world.getCornerOccupantAtLevel("upper-posts", 2, 2, 1), topId);

    auto invalidReplace =
        PlacementSystem::replaceBuildingResult(&world, baseId, decorative.id);
    CHECK(!invalidReplace.ok());
    auto validReplace =
        PlacementSystem::replaceBuildingResult(&world, baseId, reinforced.id);
    REQUIRE(validReplace.ok());
    auto removed = PlacementSystem::removeBuildingCascadeResult(&world, baseId);
    REQUIRE(removed.ok());
    REQUIRE_EQ(removed.value().removed.size(), size_t(2));
    CHECK_EQ(removed.value().removed[0].instanceId, topId);

    std::string reason;
    CHECK_EQ(static_cast<int>(
                 PlacementSystem::restoreExact(&world, removed.value().removed[1], &reason)),
             static_cast<int>(PlacementRestoreStatus::Restored));
    CHECK_EQ(static_cast<int>(
                 PlacementSystem::restoreExact(&world, removed.value().removed[0], &reason)),
             static_cast<int>(PlacementRestoreStatus::Restored));
    CHECK_EQ(world.getBuildingSupportAt(topId, 0), baseId);

    world.setActiveLevel(0);
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, post.id));
    CHECK_EQ(static_cast<int>(session.updateCorner(&world, 0, 2)),
             static_cast<int>(CornerUpdateStatus::Updated));
    const int sessionPost = session.execute();
    REQUIRE(sessionPost > 0);
    CHECK_EQ(world.getCornerOccupant("posts", 0, 2), sessionPost);
    CHECK_EQ(static_cast<int>(session.updateCorner(&world, 1, 2)),
             static_cast<int>(CornerUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.executeMove(sessionPost)),
             static_cast<int>(PlacementEditExecuteStatus::Committed));
    CHECK_EQ(world.getCornerOccupant("posts", 0, 2), 0);
    CHECK_EQ(world.getCornerOccupant("posts", 1, 2), sessionPost);
    session.setMode("remove");
    CHECK_EQ(session.execute(), sessionPost);
    CHECK(!world.hasBuilding(sessionPost));
    BuildingRegistry::clear();
}

TEST_CASE("building.freeDomainPreservesExactAnchorsAndRejectsCircularOverlap") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    BuildingDefinition prop;
    prop.id = "free-prop";
    prop.placementKind = "free";
    prop.snapMode = "free";
    prop.rotationMode = "free";
    prop.channel = "decor";
    prop.freeRadiusCells = 0.25f;
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(8, 8, 10.f);

    auto first = PlacementSystem::placeFreeResult(&world, prop.id, 12.25f, 17.75f, 1.5f, 37.f);
    REQUIRE(first.ok());
    const int firstId = first.value().instanceId;
    CHECK_EQ(world.getBuildingWorldX(firstId), 12.25f);
    CHECK_EQ(world.getBuildingElevation(firstId), 1.5f);
    CHECK_EQ(world.getBuildingRotation(firstId), 37.f);
    CHECK_EQ(world.getFreeOccupant("decor", 12.25f, 17.75f), firstId);
    CHECK_EQ(world.canPlaceFreeReason(prop.id, 16.f, 17.75f), "free_overlap");
    CHECK(world.canPlaceFree(prop.id, 18.f, 17.75f));

    Ghost ghost;
    ghost.setBuildingId(prop.id);
    ghost.setFree(&world, 31.125f, 22.875f, 0.5f);
    ghost.setRotationDeg(83.f);
    REQUIRE(ghost.validate(&world));
    const int secondId = world.placeGhost(&ghost);
    REQUIRE(secondId > 0);
    CHECK_EQ(world.getBuildingWorldX(secondId), 31.125f);

    PlacementSession session;
    REQUIRE(session.startPlacement(&world, prop.id));
    CHECK_EQ(static_cast<int>(session.updateFree(&world, 44.5f, 26.25f, 2.f)),
             static_cast<int>(FreeUpdateStatus::Updated));
    CHECK_EQ(static_cast<int>(session.executeMove(secondId)),
             static_cast<int>(PlacementEditExecuteStatus::Committed));
    CHECK_EQ(world.getBuildingWorldX(secondId), 44.5f);
    CHECK_EQ(world.getBuildingElevation(secondId), 2.f);

    PlacedBuilding snapshot = world.buildings().at(secondId);
    REQUIRE(world.removeBuilding(secondId));
    std::string reason;
    CHECK_EQ(static_cast<int>(PlacementSystem::restoreExact(&world, snapshot, &reason)),
             static_cast<int>(PlacementRestoreStatus::Restored));
    CHECK_EQ(world.getBuildingWorldX(secondId), 44.5f);
    BuildingRegistry::clear();
}

TEST_CASE("building.freeDomainCommitsSurfaceAttachmentAndLimits") {
    BuildingRegistry::clear();
    BuildingDefinition prop;
    prop.id = "free-surface-prop";
    prop.placementKind = "free";
    prop.snapMode = "free";
    prop.rotationMode = "free";
    prop.maxSurfaceSlopeDegrees = 50.f;
    prop.maxSurfaceHeightDelta = 1.f;
    BuildingRegistry::registerBuilding(prop);
    PlacementWorld world(8, 8, 1.f);
    world.setGridPlane("xz");
    PlacementSystem::registerSurfaceProvider(
        "free.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 2.5f;
            hit.worldZ = y;
            hit.normalY = 1.f;
            hit.normalZ = 1.f;
            hit.tangentX = 1.f;
            hit.surfaceId = "terrain/free";
            hit.surfaceRevision = 42;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    Ghost ghost;
    ghost.setBuildingId(prop.id);
    ghost.setFromSurface(&world, "free.surface", 2.25f, 3.75f);
    REQUIRE(ghost.validate(&world));
    const int instanceId = world.placeGhost(&ghost);
    REQUIRE(instanceId > 0);
    CHECK_EQ(world.getBuildingWorldX(instanceId), 2.25f);
    CHECK_EQ(world.getBuildingWorldY(instanceId), 2.5f);
    CHECK_EQ(world.getBuildingWorldZ(instanceId), 3.75f);
    CHECK_EQ(world.getBuildingSurfaceId(instanceId), "terrain/free");
    CHECK_EQ(world.getBuildingSurfaceRevision(instanceId), int64_t{42});
    CHECK_EQ(world.buildings().at(instanceId).surfaceSampleCount, 1);

    PlacementSystem::registerSurfaceProvider(
        "free.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 4.f;
            hit.worldZ = y;
            hit.normalY = 1.f;
            hit.tangentX = 1.f;
            hit.surfaceId = "terrain/free-next";
            hit.surfaceRevision = 77;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    PlacementSession session;
    REQUIRE(session.startPlacement(&world, prop.id));
    REQUIRE(session.updateFromSurface(&world, "free.surface", 5.5f, 6.25f));
    PlacementSystem::clearEvents();
    CHECK_EQ(static_cast<int>(session.executeMove(instanceId)),
             static_cast<int>(PlacementEditExecuteStatus::Committed));
    CHECK_EQ(world.getBuildingWorldX(instanceId), 5.5f);
    CHECK_EQ(world.getBuildingWorldY(instanceId), 4.f);
    CHECK_EQ(world.getBuildingWorldZ(instanceId), 6.25f);
    CHECK_EQ(world.getBuildingSurfaceId(instanceId), "terrain/free-next");
    CHECK_EQ(world.getBuildingSurfaceRevision(instanceId), int64_t{77});
    CHECK_EQ(PlacementSystem::events().size(), size_t{1});

    const PlacedBuilding beforeRejectedMove = world.buildings().at(instanceId);
    PlacementSystem::registerSurfaceProvider(
        "free.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = 8.f;
            hit.worldZ = y;
            hit.normalX = 1.f;
            hit.normalY = 0.f;
            hit.tangentZ = 1.f;
            hit.surfaceId = "terrain/too-steep";
            hit.surfaceRevision = 99;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    REQUIRE(session.updateFromSurface(&world, "free.surface", 7.f, 7.f));
    CHECK(!session.isValid());
    CHECK_EQ(session.getReason(), "surface_slope");
    CHECK_EQ(static_cast<int>(session.executeMove(instanceId)),
             static_cast<int>(PlacementEditExecuteStatus::Rejected));
    const PlacedBuilding &afterRejectedMove = world.buildings().at(instanceId);
    CHECK_EQ(afterRejectedMove.worldX, beforeRejectedMove.worldX);
    CHECK_EQ(afterRejectedMove.surfaceId, beforeRejectedMove.surfaceId);
    CHECK_EQ(afterRejectedMove.surfaceRevision, beforeRejectedMove.surfaceRevision);
    CHECK_EQ(PlacementSystem::events().size(), size_t{1});
    PlacementSystem::unregisterSurface("free.surface");
    BuildingRegistry::clear();
}

TEST_CASE("building.freeObbFootprintsUseRotationForCollisionMoveAndPointQuery") {
    BuildingRegistry::clear();
    BuildingDefinition bench;
    bench.id = "free-bench";
    bench.placementKind = "free";
    bench.snapMode = "free";
    bench.rotationMode = "free";
    bench.channel = "decor";
    bench.freeFootprintWidthCells = 2.f;
    bench.freeFootprintHeightCells = 0.5f;
    BuildingRegistry::registerBuilding(bench);
    PlacementWorld world(8, 8, 10.f);

    auto first = PlacementSystem::placeFreeResult(&world, bench.id, 20.f, 20.f, 0.f, 0.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().freeHalfWidth, 10.f);
    CHECK_EQ(first.value().freeHalfHeight, 2.5f);
    BuildingDefinition otherChannel = bench;
    otherChannel.id = "free-bench-overlay";
    otherChannel.channel = "overlay";
    BuildingRegistry::registerBuilding(otherChannel);
    REQUIRE(PlacementSystem::placeFreeResult(&world, otherChannel.id, 20.f, 20.f, 0.f, 90.f)
                .ok());

    auto rotatedConflict =
        PlacementSystem::placeFreeResult(&world, bench.id, 20.f, 29.f, 0.f, 90.f);
    CHECK(!rotatedConflict.ok());
    REQUIRE(rotatedConflict.error());
    CHECK_EQ(rotatedConflict.error()->path(), "free_overlap");

    auto second = PlacementSystem::placeFreeResult(&world, bench.id, 20.f, 29.f, 0.f, 0.f);
    REQUIRE(second.ok());
    const int secondId = second.value().instanceId;
    CHECK_EQ(world.getFreeOccupant("decor", 29.f, 20.f), first.value().instanceId);
    CHECK_EQ(world.getFreeOccupant("decor", 20.f, 29.f), secondId);
    CHECK_EQ(world.getFreeOccupant("decor", 20.f, 35.f), 0);

    const PlacedBuilding beforeRejectedMove = world.buildings().at(secondId);
    auto rejectedMove =
        PlacementSystem::moveFreeResult(&world, secondId, 20.f, 29.f, 0.f, 90.f);
    CHECK(!rejectedMove.ok());
    REQUIRE(rejectedMove.error());
    CHECK_EQ(rejectedMove.error()->path(), "free_overlap");
    CHECK_EQ(world.buildings().at(secondId).rotationDeg, beforeRejectedMove.rotationDeg);
    CHECK_EQ(world.buildings().at(secondId).worldY, beforeRejectedMove.worldY);

    PlacementSystem::registerSurfaceProvider(
        "obb.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = y;
            hit.worldZ = 0.f;
            hit.normalZ = 1.f;
            hit.normalY = 0.f;
            hit.surfaceId = "floor/a";
            hit.surfaceRevision = 3;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    auto patch = PlacementSystem::sampleSurfacePatch(world, bench.id, "obb.surface", 40.f,
                                                     40.f, 30.f);
    REQUIRE(patch.ok());
    CHECK_EQ(patch.value().samples.size(), size_t{5});
    PlacementSystem::registerSurfaceProvider(
        "obb.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = y;
            hit.worldZ = 0.f;
            hit.normalZ = 1.f;
            hit.normalY = 0.f;
            hit.surfaceId = x > 45.f ? "floor/b" : "floor/a";
            hit.surfaceRevision = 3;
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    CHECK(!PlacementSystem::sampleSurfacePatch(world, bench.id, "obb.surface", 40.f, 40.f, 0.f)
               .ok());
    PlacementSystem::unregisterSurface("obb.surface");
    BuildingRegistry::clear();
}

TEST_CASE("building.freeConvexPolygonFootprintsValidateAndDriveCollision") {
    BuildingRegistry::clear();
    BuildingDefinition invalid;
    invalid.id = "free-concave";
    invalid.placementKind = "free";
    invalid.freeFootprintVertices = {0.f, 0.f, 1.f, 0.f, 0.5f,
                                     0.25f, 1.f, 1.f, 0.f, 1.f};
    BuildingRegistry::registerBuilding(invalid);
    REQUIRE(BuildingRegistry::find(invalid.id));
    CHECK(BuildingRegistry::find(invalid.id)->freeFootprintVertices.empty());

    BuildingDefinition diamond;
    diamond.id = "free-diamond";
    diamond.placementKind = "free";
    diamond.snapMode = "free";
    diamond.rotationMode = "free";
    diamond.channel = "decor";
    diamond.freeFootprintVertices = {-1.f, 0.f, 0.f, -0.25f,
                                     1.f, 0.f, 0.f, 0.25f};
    BuildingRegistry::registerBuilding(diamond);
    PlacementWorld world(8, 8, 10.f);
    auto first = PlacementSystem::placeFreeResult(&world, diamond.id, 20.f, 20.f, 0.f, 0.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().freeFootprintVertices.size(), size_t{8});
    CHECK_EQ(first.value().freeHalfWidth, 10.f);
    CHECK_EQ(first.value().freeHalfHeight, 2.5f);
    CHECK_EQ(world.getFreeOccupant("decor", 29.f, 20.f), first.value().instanceId);
    CHECK_EQ(world.getFreeOccupant("decor", 20.f, 23.f), 0);

    auto rotatedConflict =
        PlacementSystem::placeFreeResult(&world, diamond.id, 20.f, 29.f, 0.f, 90.f);
    CHECK(!rotatedConflict.ok());
    REQUIRE(PlacementSystem::placeFreeResult(&world, diamond.id, 20.f, 29.f, 0.f, 0.f).ok());

    BuildingDefinition circle;
    circle.id = "free-circle-mixed";
    circle.placementKind = "free";
    circle.snapMode = "free";
    circle.channel = "decor";
    circle.freeRadiusCells = 0.2f;
    BuildingRegistry::registerBuilding(circle);
    CHECK(!PlacementSystem::placeFreeResult(&world, circle.id, 31.f, 20.f).ok());
    PlacementSystem::registerSurfaceProvider(
        "polygon.surface", [](const PlacementWorld &, float x, float y) {
            PlacementSystem::PlacementHit hit;
            hit.worldX = x;
            hit.worldY = y;
            hit.normalY = 0.f;
            hit.normalZ = 1.f;
            hit.surfaceId = "floor/polygon";
            return eve::Result<PlacementSystem::PlacementHit>::success(std::move(hit));
        });
    auto patch = PlacementSystem::sampleSurfacePatch(world, diamond.id, "polygon.surface",
                                                     50.f, 50.f, 15.f);
    REQUIRE(patch.ok());
    CHECK_EQ(patch.value().samples.size(), size_t{5});
    PlacementSystem::unregisterSurface("polygon.surface");
    BuildingRegistry::clear();
}
