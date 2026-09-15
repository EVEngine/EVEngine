#include "zeroerr/unittest.h"
#include "scene/PcgGrowth.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"
#include <limits>
using namespace eve::scene;
TEST_CASE("scene.pcgGrowth.linearVarianceAndDelayedDeath"){
 PcgGrowth g;REQUIRE(g.configure(.15f,1,.25f,5).ok());REQUIRE(g.start(10,77).ok());float end=g.getActualEndScale();CHECK_GE(end,1.f);CHECK_LT(end,1.25f);
 auto start=g.advance(10);REQUIRE(start.ok());CHECK_EQ(start.value(),.15f);auto mid=g.advance(12.5f);REQUIRE(mid.ok());CHECK_GT(mid.value(),.15f);CHECK_LT(mid.value(),end);auto done=g.advance(15);REQUIRE(done.ok());CHECK_EQ(done.value(),end);CHECK(g.getFinished());
 REQUIRE(g.die(20).ok());auto early=g.shouldDestroy(24.999f);REQUIRE(early.ok());CHECK(!early.value());auto due=g.shouldDestroy(25);REQUIRE(due.ok());CHECK(due.value());
}
TEST_CASE("scene.pcgGrowth.rejectsInvalidInputAtomically"){
 PcgGrowth g;REQUIRE(g.configure(.2f,1,.2f,2).ok());REQUIRE(g.start(4,9).ok());float before=g.getActualEndScale();CHECK(!g.configure(0,1,.2f,2).ok());CHECK_EQ(g.getActualEndScale(),before);CHECK(!g.advance(3).ok());CHECK_EQ(g.getScale(),.2f);CHECK(!g.die(std::numeric_limits<float>::quiet_NaN()).ok());
}TEST_CASE("scene.pcgGrowth.appliesToAndDestroysRealNode"){
 Scene*scene=Scene::create();scene->beginBuild();scene->beginNode("root");scene->addNode("plant");scene->end();REQUIRE(scene->mountBuildAs("pcg-growth-test"));
 SceneNodeRef node("pcg-growth-test","plant");REQUIRE(node.isValid());PcgGrowth g;REQUIRE(g.start(10,3).ok());auto applied=applyPcgGrowth(g,node,10);REQUIRE(applied.ok());CHECK(!applied.value());CHECK_EQ(node.getScaleX(),.15f);
 REQUIRE(g.die(11).ok());auto removed=applyPcgGrowth(g,node,16);REQUIRE(removed.ok());CHECK(removed.value());CHECK(!node.isValid());
}