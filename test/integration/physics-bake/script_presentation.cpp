#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderTransformProjection.h"
#include "physics/bake/RigidPlayback.h"
#include "physics/bake/graphics_script/PhysicsBakeGraphics.h"
#include "physics/bake/script/PhysicsBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("physics.bake.scriptRigidPresentationBorrowsTargetsAndPreservesFailures") {
    using namespace eve;
    using namespace eve::physics;
    PersistentId::Bytes bytes{};
    bytes.back() = 7;
    const PersistentId                 id(bytes);
    const std::vector<RigidTrackFrame> frames{{0, {{id, {10, 0, 0, 0, 0, 0, 1}}}}, {1, {{id, {20, 0, 0, 0, 0, 0, 1}}}}};
    RigidPlayback planar(RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels, frames).expect("planar"));
    RigidPlayback spatial(RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, frames).expect("spatial"));
    struct Targets {
        graphics::Renderable2D* planar  = graphics::Renderable2D::create();
        graphics::Renderable3D* spatial = graphics::Renderable3D::create();
        ~Targets() {
            ecs::DestroyEntity(planar);
            ecs::DestroyEntity(spatial);
        }
    } targets;
    REQUIRE(targets.planar != nullptr);
    REQUIRE(targets.spatial != nullptr);
    ssq::VM vm(2048);
    auto    table = vm.addTable("eve");
    PhysicsBake::expose(table);
    PhysicsBakeGraphics::expose(table);
    graphics::Graphics::expose(table);
    vm.addFunc("planarPlayer", [&] { return &planar; });
    vm.addFunc("spatialPlayer", [&] { return &spatial; });
    vm.addFunc("planarTarget", [&] { return targets.planar; });
    vm.addFunc("spatialTarget", [&] { return targets.spatial; });
    vm.addFunc("identity", [&] { return id.format(); });
    vm.addFunc("planarX", [&] { return targets.planar->getX(); });
    vm.addFunc("spatialX", [&] { return graphics::detail::modelFromTransform(*targets.spatial->transform())[3].x; });
    vm.run(vm.compileSource(R"(
        local graphics = eve.PhysicsBakeGraphics();
        local p2 = planarPlayer(); local p3 = spatialPlayer();
        assert(p2.seek(0.5).ok && p3.seek(0.5).ok);
        assert(p2.setPlacement(2.0,0.0,0.0,0.0,0.0,0.0,1.0).ok);
        assert(graphics.submitRigid2D(p2,identity(),planarTarget()).ok);
        assert(graphics.submitRigid3D(p3,identity(),spatialTarget()).ok);
        assert(planarX() == 17.0 && spatialX() == 15.0);
        assert(p2.time() == 0.5 && p3.time() == 0.5);
        assert(!graphics.submitRigid2D(null,identity(),planarTarget()).ok);
        assert(!graphics.submitRigid2D(p2,identity(),null).ok);
        assert(!graphics.submitRigid2D(p2,identity(),spatialTarget()).ok);
        assert(!graphics.submitRigid3D(p3,identity(),planarTarget()).ok);
        assert(!graphics.submitRigid2D(p3,identity(),planarTarget()).ok);
        assert(!graphics.submitRigid3D(p2,identity(),spatialTarget()).ok);
        assert(!graphics.submitRigid2D(p2,"invalid",planarTarget()).ok);
        assert(!graphics.submitRigid3D(p3,"00000000-0000-0000-0000-000000000008",spatialTarget()).ok);
        assert(planarX() == 17.0 && spatialX() == 15.0);
        assert(p2.seek(1.0).ok && p3.seek(0.0).ok);
        assert(graphics.submitRigid2D(p2,identity(),planarTarget()).ok);
        assert(graphics.submitRigid3D(p3,identity(),spatialTarget()).ok);
        assert(planarX() == 22.0 && spatialX() == 10.0);
    )"));
}
