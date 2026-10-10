#include <fstream>
#include <iterator>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "common/Module.h"
#include "common/config.h"
#include "zeroerr/unittest.h"

TEST_CASE("daynight.sky script prepares an owned profile runtime and checks time commands") {
    GfxFixture fixture(32, 32, true);
    ssq::VM    vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
#ifdef EVENGINE_WEBGPU
    vm.set("vulkanSupported", false);
#else
    vm.set("vulkanSupported", true);
#endif
#if !defined(EVENGINE_WEBGPU) && defined(EVE_TEST_RESOURCE_VALIDATION)
    vm.set("skySupported", true);
#else
    vm.set("skySupported", false);
#endif
    std::ifstream file(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(file.good());
    vm.set("profileJson", std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()));
    vm.run(vm.compileSource(R"(
        local module = eve.DayNight();
        local graphics = eve.Graphics();
        assert(graphics.getSceneToneMapping() == "aces");
        assert(!graphics.setSceneToneMapping("unknown").ok);
        assert(graphics.getScenePhotographicVignette() == 0.0);
        assert(!graphics.setScenePhotographicVignette(-1.0).ok);
        assert(!module.prepareSky(graphics, "{}").ok);
        local made = module.prepareSky(graphics, profileJson);
        if (!skySupported) {
            assert(!made.ok);
            assert(made.code == "unsupported");
            assert(made.diagnostics.len() == 1);
            assert(made.diagnostics[0].message == (vulkanSupported
                ? "SPIR-V validation provider is unavailable"
                : "Atmosphere resource program requires Vulkan"));
            assert(graphics.setSceneToneMapping("filmic").ok == vulkanSupported);
            assert(graphics.setScenePhotographicVignette(0.4).ok == vulkanSupported);
            return;
        }
        if (!made.ok) foreach (diagnostic in made.diagnostics) print(diagnostic.message + "\n");
        assert(made.ok && made.hasValue && made.ownership == "owned");
        assert(graphics.setSceneToneMapping("filmic").ok);
        assert(graphics.getSceneToneMapping() == "filmic");
        assert(graphics.setScenePhotographicVignette(0.4).ok);
        assert(graphics.getScenePhotographicVignette() > 0.39);
        assert(graphics.setScenePhotographicVignette(0.0).ok);
        assert(graphics.setSceneToneMapping("aces").ok);
        local sky = made.value;
        assert(sky.attach().ok);
        assert(sky.attach().ok);
        assert(sky.frame().value.hour == 12);
        assert(!sky.advanceSeconds(-1.0).ok);
        assert(sky.frame().value.elapsedSeconds == 0);
        assert(sky.advanceSeconds(0.25).ok);
        assert(sky.frame().value.elapsedSeconds == 0.25);
        assert(sky.frame().value.hour == 12);
        assert(sky.frame().value.sunDirection[0] < -0.49);
        assert(sky.detach().ok);
        assert(sky.detach().ok);
        sky = null;
        made = null;
        collectgarbage();
    )"));
}
