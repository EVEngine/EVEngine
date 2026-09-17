#include "zeroerr/unittest.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/TerrainStampScript.h"

using namespace eve::procgen;

TEST_CASE("procgen.pcgBiomeController.matchesPcgLoadingBoundsAndTiers") {
    PcgBiomeController controller;
    REQUIRE(controller.configure(100.0,20.0,-40.0,50.0,200.0,
                                 PcgBiomeLoadMode::RuntimeAlways).ok());
    CHECK_EQ(controller.regularBounds().sizeX,99.5);
    CHECK_EQ(controller.impostorBounds().sizeX,299.5);
    CHECK_EQ(controller.tierAt(149.75,20.0,-40.0),PcgTerrainStreamingTier::Regular);
    CHECK_EQ(controller.tierAt(200.0,20.0,-40.0),PcgTerrainStreamingTier::Impostor);
    CHECK_EQ(controller.tierAt(250.0,20.0,-40.0),PcgTerrainStreamingTier::Unloaded);

    REQUIRE(controller.configure(1.0,2.0,3.0,5.0,0.0,PcgBiomeLoadMode::EditorAlways).ok());
    CHECK_EQ(controller.impostorBounds().sizeX,0.0);
    CHECK_EQ(controller.tierAt(7.0,2.0,3.0),PcgTerrainStreamingTier::Unloaded);
}

TEST_CASE("procgen.pcgBiomeController.fitsTerrainWithDistinctYRulesAndIsAtomic") {
    PcgBiomeController controller;
    REQUIRE(controller.configure(0,0,0,10,20,PcgBiomeLoadMode::RuntimeAlways).ok());
    REQUIRE(controller.fitToTerrain(-100,7,-50,200,80,100).ok());
    CHECK_EQ(controller.regularBounds().centerX,0.0);
    CHECK_EQ(controller.regularBounds().centerY,7.0);
    CHECK_EQ(controller.regularBounds().centerZ,0.0);
    CHECK_EQ(controller.range(),100.0);

    REQUIRE(controller.fitToAllTerrains(-100,-10,-50,300,80,100).ok());
    CHECK_EQ(controller.regularBounds().centerX,50.0);
    CHECK_EQ(controller.regularBounds().centerY,30.0);
    CHECK_EQ(controller.range(),150.0);
    const auto before=controller.regularBounds();
    CHECK(!controller.fitToTerrain(0,0,0,-1,1,1).ok());
    CHECK_EQ(controller.regularBounds().centerX,before.centerX);
    CHECK(!controller.configure(0,0,0,std::numeric_limits<double>::infinity(),0,
                                PcgBiomeLoadMode::Disabled).ok());
}

TEST_CASE("procgen.pcgBiomeController.bindsRealVm") {
    ssq::VM vm(1024);auto table=vm.addTable("eve");exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
      local b=eve.PcgBiomeController();
      assert(b.configure(10.0,20.0,30.0,50.0,200.0,3).ok);
      assert(b.getLoadMode()==3 && b.getRange()==50.0);
      assert(b.getRegularCenterX()==10.0 && b.getRegularCenterY()==20.0 && b.getRegularCenterZ()==30.0);
      assert(b.getRegularSize()==99.5 && b.getImpostorSize()==299.5);
      assert(b.tierAt(10.0,20.0,30.0)==0 && b.tierAt(110.0,20.0,30.0)==1);
      assert(b.fitToTerrain(-100.0,7.0,-50.0,200.0,80.0,100.0).ok);
      assert(b.getRegularCenterX()==0.0 && b.getRegularCenterY()==7.0 && b.getRange()==100.0);
    )"));
}
