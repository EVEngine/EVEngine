#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/physics/GtsTerrainColliderRuntime.h"
#include <memory>
#include <zeroerr/unittest.h>
using namespace eve;
namespace {
procgen::MeshBuild terrainQuad(){procgen::MeshBuild m;m.setActiveGroup("terrain");m.addVertex(0,0,0,0,1,0,0,0);m.addVertex(2,0,0,0,1,0,1,0);m.addVertex(2,0,2,0,1,0,1,1);m.addVertex(0,0,2,0,1,0,0,1);m.addTriangle(0,1,2);m.addTriangle(0,2,3);return m;}
}
TEST_CASE("procgen.physics.gtsTerrainCollider.replacesAtomicallyAndSurvivesWorldFirstTeardown") {
    auto lods=procgen::buildGtsTerrainLods(terrainQuad(),1,0,procgen::GtsMeshPivot::CenterXZ,procgen::defaultGtsTerrainLodLevels()); REQUIRE(lods.ok());
    procgen_physics::GtsTerrainColliderRuntime runtime;
    auto world=std::make_unique<physics::World3D>(0.f,-9.8f,0.f,true);
    auto revision=runtime.replace(lods.value(),*world,0,10,2,20); REQUIRE(revision.ok()); CHECK_EQ(revision.value(),uint64_t(1));
    CHECK_EQ(runtime.getTileCount(),2); CHECK_EQ(world->getBodyCount(),2); CHECK_EQ(world->getShapeCount(),2);
    auto* first=runtime.getBody(0); REQUIRE(first); CHECK_EQ(first->getY(),2.f); CHECK(first->getX()>10.f);
    auto* preserved=runtime.getBody(1); REQUIRE(preserved);
    auto failed=runtime.replace(lods.value(),*world,99); REQUIRE(!failed.ok()); CHECK_EQ(runtime.getRevision(),uint64_t(1)); CHECK_EQ(runtime.getBody(1),preserved); CHECK_EQ(world->getBodyCount(),2);
    auto replaced=runtime.replace(lods.value(),*world,1,-5,1,-7); REQUIRE(replaced.ok()); CHECK_EQ(replaced.value(),uint64_t(2)); CHECK_EQ(world->getBodyCount(),2); CHECK(runtime.getBody(1)!=preserved);
    auto worldHandle=world->runtimeHandle(); CHECK_EQ(physics::World3D::findWorld(worldHandle),world.get());
    world.reset(); CHECK_EQ(physics::World3D::findWorld(worldHandle),nullptr); CHECK_EQ(runtime.getBody(0),nullptr);
    auto cleared=runtime.clear(); REQUIRE(cleared.ok()); CHECK_EQ(cleared.value(),0); CHECK_EQ(runtime.getRevision(),uint64_t(3));
}