#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "common/Module.h"
#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("GraphicsPrimitives.scriptSpatialShapesAndReleasedProxy") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        gfx <- eve.Graphics();
        disk <- gfx.newPrimitiveDisk3D(0.0,0.0,-2.0,0.0,1.0,0.0,1.0,1.0,0.5,0.1,1.0,2.0);
        shape <- disk.value;
        fill <- shape.setPaintMode("fill-stroke");
        cap <- shape.setLineCap("round");
        join <- shape.setLineJoin("bevel");
        width <- shape.setWidthSpace("world");
        blend <- shape.setBlendMode("alpha");
        cull <- shape.setCullMode("back");
        layer <- shape.setLayer(4);
        badMode <- shape.setPaintMode("invalid");
        badMatrix <- shape.setTransform([1.0]);
        matrix <- shape.setTransform([1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,
                                      0.0,0.0,1.0,0.0, 2.0,0.0,0.0,1.0]);
        route <- gfx.newPrimitivePolyline3D([0.0,0.0,-2.0,1.0,1.0,-3.0],false,1.0,1.0,1.0,1.0,2.0);
        badRoute <- gfx.newPrimitivePolyline3D([0.0,0.0],false,1.0,1.0,1.0,1.0,2.0);
        routeUpdate <- route.value.setPolyline([0.0,0.0,-2.0,2.0,1.0,-3.0],false);
        cylinder <- gfx.newPrimitiveCylinder3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,1.0,1.0,1.0,1.0,2.0);
        capsule <- gfx.newPrimitiveCapsule3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,1.0,1.0,1.0,1.0,2.0);
        cone <- gfx.newPrimitiveCone3D(0.0,2.0,0.0,0.0,-1.0,0.0,2.0,0.5,1.0,1.0,1.0,1.0,2.0);
        arrow <- gfx.newPrimitiveArrow3D(0.0,0.0,0.0,0.0,2.0,0.0,0.5,0.2,1.0,1.0,1.0,1.0,2.0);
        obb <- gfx.newPrimitiveObb3D([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0],1.0,1.0,1.0,1.0,2.0);
        grid <- gfx.newPrimitiveGrid3D([0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0],4,4,1.0,1.0,1.0,1.0,2.0);
        arc <- gfx.newPrimitiveArc3D([0.0,0.0,0.0,0.0,1.0,0.0,1.0,0.0,0.0],1.0,0.0,1.5,1.0,1.0,1.0,1.0,2.0);
        removed <- shape.remove();
        stale <- shape.isStale();
        afterRemove <- shape.setLineCap("round");
    )"));
    for (const char* name : {"disk", "fill", "cap", "join", "width", "blend", "cull", "layer", "matrix", "route", "removed",
                             "routeUpdate", "cylinder", "capsule", "cone", "arrow", "obb", "grid", "arc"})
        CHECK(vm.find(name).toTable().get<bool>("ok"));
    for (const char* name : {"badMode", "badMatrix", "badRoute", "afterRemove"})
        CHECK(!vm.find(name).toTable().get<bool>("ok"));
    CHECK(vm.find("stale").toBool());
}
