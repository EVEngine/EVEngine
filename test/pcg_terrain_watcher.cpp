#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/TerrainStampScript.h"

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

TEST_CASE("procgen.pcgTerrainWatcher.bindsRealVm") {
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
        local w=eve.PcgTerrainWatcher();
        w.beginScan();assert(w.addTerrain("a").ok);assert(w.commitScan().value==0);
        w.beginScan();assert(w.addTerrain("b").ok);assert(w.commitScan().value==2);
        assert(w.getTerrainCount()==1 && w.getChangeCount()==2);
        assert(w.getChangeTerrainId(0).value=="b" && w.getChangeType(0).value==0);
        assert(w.getChangeTerrainId(1).value=="a" && w.getChangeType(1).value==1);
    )"));
}
