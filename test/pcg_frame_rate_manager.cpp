#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "procgen/PcgFrameRateManager.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <limits>
using namespace eve::procgen;
TEST_CASE("procgen.pcgFrameRateManager.matchesSamplingAndQualityPolicy"){
 PcgFrameRateManager low;REQUIRE(low.configure(60,.5f,0,5,2).ok());
 for(int i=0;i<6;++i)REQUIRE(low.update(.1f,1).ok());
 CHECK_EQ(low.getQuality(),1);CHECK(low.getQualityChanged());CHECK_EQ(low.getPreset().treeDistance,500.f);
 PcgFrameRateManager high;REQUIRE(high.configure(60,.5f,0,5,2).ok());
 for(int i=0;i<51;++i)REQUIRE(high.update(.01f,1).ok());
 CHECK_EQ(high.getQuality(),3);CHECK_GE(high.getFps(),99.f);CHECK_EQ(high.getPreset().detailObjectDensity,.7f);
}
TEST_CASE("procgen.pcgFrameRateManager.manualAndFailureAreAtomic"){
 PcgFrameRateManager m;REQUIRE(m.configure(60,10,1,4,2).ok());REQUIRE(m.selectManualQuality(4).ok());
 CHECK(!m.getAutomatic());CHECK_EQ(m.getQuality(),4);CHECK(!m.selectManualQuality(5).ok());CHECK_EQ(m.getQuality(),4);
 CHECK(!m.update(std::numeric_limits<float>::quiet_NaN(),1).ok());CHECK_EQ(m.getQuality(),4);
}TEST_CASE("procgen.pcgFrameRateManager.squirrelContract"){
 ssq::VM vm(2048,ssq::Libs::ALL);auto table=vm.addTable("eve");eve::script::exposeResultBindings(table);exposePcgFrameRateManagerBindings(table);
 vm.run(vm.compileSource(R"(
  local m=eve.PcgFrameRateManager();assert(m.configure(60,0.5,0,5,2).ok);
  for(local i=0;i<6;i++)assert(m.update(0.1,1.0).ok);
  assert(m.getQuality()==1&&m.getQualityChanged());local p=m.getPreset();
  assert(p.treeDistance==500.0&&p.detailObjectDensity==0.25&&p.heightmapMaximumLod==1);
 )"));
}
