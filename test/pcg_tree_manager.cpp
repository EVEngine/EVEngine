#include "zeroerr/unittest.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/PointSet.h"
#include "procgen/heightmap/PcgTreeManager.h"
#include "procgen/heightmap/TerrainStampScript.h"

using namespace eve::procgen;

TEST_CASE("procgen.pcgTreeManager.aggregatesWorldTreesAndUsesRectangularRange") {
    PcgTreeManager manager;
    REQUIRE(manager.reset(-10.f,-20.f,30.f,40.f).ok());
    REQUIRE(manager.addTree(-2.f,3.f,0).ok());
    PointSet tile;
    tile.add(1.f,8.f,1.f);
    tile.add(4.f,9.f,4.f);
    REQUIRE(manager.addTrees(tile,2).ok());
    CHECK(manager.getCount()==3);
    auto near=manager.countInRange(1.f,1.f,3.f);
    REQUIRE(near.ok());
    CHECK(near.value()==3); // inclusive square, including (4,4)
    CHECK(manager.countInRange(1.f,1.f,2.9f).value()==1);
}

TEST_CASE("procgen.pcgTreeManager.failedBatchIsAtomic") {
    PcgTreeManager manager;
    CHECK(!manager.addTree(0,0,0).ok());
    REQUIRE(manager.reset(0,0,10,10).ok());
    REQUIRE(manager.addTree(2,2,0).ok());
    PointSet invalid;
    invalid.add(3,0,3);
    invalid.add(10,0,3);
    CHECK(!manager.addTrees(invalid,1).ok());
    CHECK(manager.getCount()==1);
    CHECK(!manager.countInRange(0,0,-1).ok());
    CHECK(!manager.addTree(std::numeric_limits<float>::infinity(),0,0).ok());
}

TEST_CASE("procgen.pcgTreeManager.bindsRealVm") {
    PointSet trees;trees.add(2,0,2);trees.add(8,0,8);
    ssq::VM vm(1024);auto table=vm.addTable("eve");exposeHeightmap(table);
    vm.addFunc("trees",[&](){return &trees;});
    vm.run(vm.compileSource(R"(
      local m=eve.PcgTreeManager();assert(m.reset(0.0,0.0,10.0,10.0).ok);
      assert(m.addTrees(trees(),4).value==2);assert(m.addTree(5.0,5.0,1).value==3);
      assert(m.getCount()==3 && m.countInRange(5.0,5.0,3.0).value==3);
    )"));
}
