// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-offscreen",
  "module": "fluids"
}
*/
// clang-format on

#include "common/Runtime.h"
#include "fluids/ShallowWaterField.h"
#include "graphics/Graphics.h"
#include "graphics/Water.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("fluids.shore.gpuMatchesCpuOnSlopingWetDryBed") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    eve::fluids::ShallowWaterSettings settings;
    settings.resolutionX = 32;
    settings.resolutionZ = 16;
    settings.cellSize    = 0.1f;
    settings.preferGpu   = false;
    auto madeCpu         = eve::fluids::ShallowWaterField::create(settings);
    REQUIRE(madeCpu.ok());
    auto cpu           = std::move(madeCpu).takeValue();
    settings.preferGpu = true;
    auto madeGpu       = eve::fluids::ShallowWaterField::create(settings);
    REQUIRE(madeGpu.ok());
    auto               gpu = std::move(madeGpu).takeValue();
    std::vector<float> bed(512);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 32; ++x) bed[size_t(z * 32 + x)] = std::max(0.f, ((float(x) + 0.5f) * 0.1f - 1.f) * 0.35f);
    for (auto* field : {cpu.get(), gpu.get()}) {
        REQUIRE(field->setBed(bed).ok());
        REQUIRE(field->fillSurface(0.4f).ok());
        REQUIRE(field->disturbAt(1.7f, 0.8f, 0.3f, 0.04f, 0.12f).ok());
    }
    for (int tick = 0; tick < 120; ++tick) {
        REQUIRE(cpu->step(1.f / 120.f).ok());
        REQUIRE(gpu->step(1.f / 120.f).ok());
    }
    REQUIRE(gpu->usingGpu());
    REQUIRE(!gpu->usedBackendFallback());
    for (size_t i = 0; i < bed.size(); ++i) {
        REQUIRE(std::abs(cpu->depths()[i] - gpu->depths()[i]) < 0.002f);
        REQUIRE(std::abs(cpu->momentumX()[i] - gpu->momentumX()[i]) < 0.003f);
        REQUIRE(std::abs(cpu->momentumZ()[i] - gpu->momentumZ()[i]) < 0.003f);
    }
}

TEST_CASE("fluids.shore.scriptSurfaceSyncChecksInputsAndPublishesMesh") {
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    eve::Runtime runtime(8192, ssq::Libs::ALL);
    runtime.initialize();
    auto& vm = runtime.vm();
    vm.set("testGraphics", graphics);
    auto* water = graphics->newWater();
    REQUIRE(water != nullptr);
    water->createPlane(0.8f, 0.8f, 7, 7);
    REQUIRE(water->getMesh() != nullptr);
    vm.set("testMesh", water->getMesh());
    vm.run(vm.compileSource(R"(
        bindingError <- "";
        try {
        local graphics = testGraphics;
        local fluids = eve.Fluids();
        local field = fluids.newShallowWater(8,8,0.1,false);
        local bed=[];for(local i=0;i<64;++i) bed.append(0.1);
        if(!field.setBed(bed).ok || !field.fillSurface(0.4).ok) throw "lake initialization failed";


        gainRejected <- !field.syncSurfaceMesh(graphics,testMesh,0.0).ok;
        syncAccepted <- field.syncSurfaceMesh(graphics,testMesh,1.0).ok;
        unchanged <- field.depthAt(0.4,0.4)>0.299 && field.depthAt(0.4,0.4)<0.301;
        } catch(error) {bindingError=error.tostring();}
    )"));
    REQUIRE(vm.find("bindingError").toString() == std::string{});
    REQUIRE(vm.find("gainRejected").toBool());
    REQUIRE(vm.find("syncAccepted").toBool());
    REQUIRE(vm.find("unchanged").toBool());
}
