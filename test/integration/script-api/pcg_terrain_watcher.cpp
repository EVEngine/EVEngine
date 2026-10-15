#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTerrainWatcher.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

// pcg_terrain_watcher.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgTerrainWatcher.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
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

// pcg_tree_manager.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgTreeManager.bindsRealVm") {
    PointSet trees;
    trees.add(2, 0, 2);
    trees.add(8, 0, 8);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("trees", [&]() { return &trees; });
    vm.run(vm.compileSource(R"(
      local m=eve.PcgTreeManager();assert(m.reset(0.0,0.0,10.0,10.0).ok);
      assert(m.addTrees(trees(),4).value==2);assert(m.addTree(5.0,5.0,1).value==3);
      assert(m.getCount()==3 && m.countInRange(5.0,5.0,3.0).value==3);
    )"));
}
