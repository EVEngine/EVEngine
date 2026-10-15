#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSession.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/fx/BuildingFx.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// building_placement_regression.cpp
using namespace eve::building;


// building_visual_placement.cpp


TEST_CASE("buildingfx.scriptMeshResolverValidatesAndCopiesResources") {
    eve::graphics::Mesh mesh;
    ssq::VM             vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto eveTable = vm.find("eve").toTable();
    eve::graphics::Graphics::expose(eveTable);
    vm.addFunc("testHouseMesh", [&]() { return &mesh; });
    vm.run(vm.compileSource(R"(
        b <- eve.Building();
        b.registerBuildingsFromJson("[{\"id\":\"script-house\",\"renderMode\":\"3d\",\"visual3d\":{\"mesh\":\"house.obj\"}}]");
        world <- b.newWorld(4, 4, 1.0);
        fx <- eve.BuildingFx();
        resources <- { "house.obj": testHouseMesh() };
        fx.setMeshResolver(resources);
        resources.clear();
        rejected <- false;
        try { fx.setMeshResolver({ "house.obj": world }); }
        catch (error) { rejected = true; }
        fx.attach(world);
        id <- world.placeAt("script-house", 1, 1, 0.0);
        fx.sync(world);
        resolved <- fx.getVisualFallbackReason(world, id) == "";
        fx.clearMeshResolver();
        fx.sync(world);
        missing <- fx.getVisualFallbackReason(world, id) == "resolver_unavailable";
        fx.detach(world);
        world.destroy();
    )"));
    REQUIRE(vm.find("rejected").toBool());
    REQUIRE(vm.find("resolved").toBool());
    REQUIRE(vm.find("missing").toBool());
    eve::building::BuildingRegistry::clear();
}
