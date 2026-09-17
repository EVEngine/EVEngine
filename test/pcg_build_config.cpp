#include "zeroerr/unittest.h"
#include "scene/PcgBuildConfig.h"
using namespace eve::scene;
TEST_CASE("scene.pcgBuildConfig.preservesOrderedHistory") { PcgBuildConfig c; CHECK_EQ(c.getPublicationType(),1); REQUIRE(c.setPublicationType(0).ok()); REQUIRE(c.addHistory(4,"World",123456789).ok()); REQUIRE(c.addHistory(0,"World",123456790).ok()); CHECK_EQ(c.getHistoryCount(),uint64_t(2)); auto a=c.getHistoryCategory(0);auto n=c.getHistorySceneName(1);auto t=c.getHistoryTimestamp(1);REQUIRE(a.ok());REQUIRE(n.ok());REQUIRE(t.ok());CHECK_EQ(a.value(),4);CHECK_EQ(n.value(),"World");CHECK_EQ(t.value(),int64_t(123456790)); c.clearHistory();CHECK_EQ(c.getHistoryCount(),uint64_t(0)); }
TEST_CASE("scene.pcgBuildConfig.rejectsInvalidEnumsAndIndices") { PcgBuildConfig c;CHECK(!c.setPublicationType(2).ok());CHECK_EQ(c.getPublicationType(),1);CHECK(!c.addHistory(7,"",0).ok());CHECK_EQ(c.getHistoryCount(),uint64_t(0));CHECK(!c.getHistoryCategory(0).ok()); }
