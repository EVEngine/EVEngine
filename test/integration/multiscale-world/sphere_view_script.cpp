// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "graphics_multiscale",
  "default": "script-binding"
}
*/
// clang-format on
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/multiscale/MultiscaleView.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.multiscale.scriptFrameOwnsValuesAndRejectsLossyAddresses") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::graphics::multiscale::MultiscaleView::expose(table);
    vm.run(vm.compileSource(R"(
        local api = eve.MultiscaleView();
        local origin = {cell=["9223372036854775807","0","0"], fraction=[0,0,0]};
        local input = {
            camera=origin,
            view={right=[1,0,0], up=[0,1,0], forward=[0,0,1],
                  tanHalfVerticalFov=1, aspect=1, cellExponent=900},
            spheres=[{center={cell=["9223372036854775807","0","10"], fraction=[0,0,0]},
                      radiusMantissa=2, radiusExponent=900, color=[1,0,0]}]
        };
        good <- api.prepareSphereFrame(input,1);
        overBudget <- api.prepareSphereFrame(input,0);
        input.spheres[0].center.cell[0] = "9223372036854775808";
        overflow <- api.prepareSphereFrame(input,1);
        input.spheres[0].center.cell[0] = 1;
        lossy <- api.prepareSphereFrame(input,1);
        input.spheres[0].center.cell[0] = "9223372036854775807";
        input.spheres[0].unknown <- 1;
        unknown <- api.prepareSphereFrame(input,1);
        delete input.spheres[0].unknown;
        input.spheres[0].center.fraction[0] = 0.5;
        fraction <- api.prepareSphereFrame(input,1);
        input.spheres.clear();
        empty <- api.prepareSphereFrame(input,1);
        oversized <- api.prepareSphereFrame(input,1000001);
        fractionalBudget <- api.prepareSphereFrame(input,1.5);
        noProvider <- api.newSphereView(null);
        wrongProvider <- api.newSphereView(good.value);
        local emptyView = eve.MultiscaleSphereView();
        wrongFrame <- emptyView.draw(api);
        noFrame <- emptyView.draw(null);
        wrongReceiver <- emptyView.draw.call(api, good.value);
        noBatchProvider <- api.prepareSphereBatch(null, good.value);
        wrongBatchProvider <- api.prepareSphereBatch(good.value, good.value);
        local emptyBatch = eve.MultiscaleSphereBatch();
        missingBatch <- emptyBatch.draw();
        wrongBatchReceiver <- emptyBatch.draw.call(api);
        // Frame storage survives mutation/destruction of the input object.
        input = null;
        collectgarbage();
        retained <- typeof good.value;
    )"));
    REQUIRE(vm.find("good").toTable().get<bool>("ok"));
    REQUIRE(vm.find("empty").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("retained").toString(), std::string("instance"));
    for (const char* name : {"overBudget", "overflow", "lossy", "unknown", "fraction", "noProvider", "wrongProvider",
                             "wrongFrame", "noFrame", "wrongReceiver", "oversized", "fractionalBudget",
                             "noBatchProvider", "wrongBatchProvider", "missingBatch", "wrongBatchReceiver"})
        REQUIRE(!vm.find(name).toTable().get<bool>("ok"));
}
