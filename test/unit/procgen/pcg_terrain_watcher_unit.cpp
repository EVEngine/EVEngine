#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

// pcg_terrain_watcher.cpp
using namespace eve::procgen;

TEST_CASE("procgen.pcgTerrainWatcher.bootstrapsAndOrdersChanges") {
    PcgTerrainWatcher watcher;
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("west").ok());
    REQUIRE(watcher.addTerrain("east").ok());
    auto initial = watcher.commitScan();
    REQUIRE(initial.ok());
    CHECK(initial.value() == 0);
    CHECK(watcher.getTerrainCount() == 2);

    watcher.beginScan();
    REQUIRE(watcher.addTerrain("east").ok());
    REQUIRE(watcher.addTerrain("north").ok());
    auto changed = watcher.commitScan();
    REQUIRE(changed.ok());
    CHECK(changed.value() == 2);
    REQUIRE(watcher.getChangeTerrainId(0).ok());
    CHECK(watcher.getChangeTerrainId(0).value() == "north");
    CHECK(watcher.getChangeType(0).value() == 0);
    CHECK(watcher.getChangeTerrainId(1).value() == "west");
    CHECK(watcher.getChangeType(1).value() == 1);
}

TEST_CASE("procgen.pcgTerrainWatcher.invalidAndCancelledScansPreservePublishedState") {
    PcgTerrainWatcher watcher;
    CHECK(!watcher.addTerrain("orphan").ok());
    CHECK(!watcher.commitScan().ok());
    watcher.beginScan();
    REQUIRE(watcher.addTerrain("stable").ok());
    CHECK(!watcher.addTerrain("stable").ok());
    watcher.cancelScan();
    CHECK(watcher.getTerrainCount() == 0);
    CHECK(watcher.getChangeCount() == 0);
    CHECK(!watcher.getChangeTerrainId(0).ok());
}


// pcg_tree_manager.cpp
using namespace eve::procgen;

TEST_CASE("procgen.pcgTreeManager.aggregatesWorldTreesAndUsesRectangularRange") {
    PcgTreeManager manager;
    REQUIRE(manager.reset(-10.f, -20.f, 30.f, 40.f).ok());
    REQUIRE(manager.addTree(-2.f, 3.f, 0).ok());
    PointSet tile;
    tile.add(1.f, 8.f, 1.f);
    tile.add(4.f, 9.f, 4.f);
    REQUIRE(manager.addTrees(tile, 2).ok());
    CHECK(manager.getCount() == 3);
    auto near = manager.countInRange(1.f, 1.f, 3.f);
    REQUIRE(near.ok());
    CHECK(near.value() == 3);  // inclusive square, including (4,4)
    CHECK(manager.countInRange(1.f, 1.f, 2.9f).value() == 1);
}

TEST_CASE("procgen.pcgTreeManager.failedBatchIsAtomic") {
    PcgTreeManager manager;
    CHECK(!manager.addTree(0, 0, 0).ok());
    REQUIRE(manager.reset(0, 0, 10, 10).ok());
    REQUIRE(manager.addTree(2, 2, 0).ok());
    PointSet invalid;
    invalid.add(3, 0, 3);
    invalid.add(10, 0, 3);
    CHECK(!manager.addTrees(invalid, 1).ok());
    CHECK(manager.getCount() == 1);
    CHECK(!manager.countInRange(0, 0, -1).ok());
    CHECK(!manager.addTree(std::numeric_limits<float>::infinity(), 0, 0).ok());
}
