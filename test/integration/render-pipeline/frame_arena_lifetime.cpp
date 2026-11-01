// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"graphics","default":"render-window","modules":["graphics","window"]}
*/
// clang-format on
#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <cstring>
#include "Fixtures.h"
#include "graphics/vulkan/Graphics.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.frameArena.ordinaryFramesHaveIndependentStorage") {
    GfxFixture fixture;
    auto*      gfx = dynamic_cast<eve::graphics::vulkan::Graphics*>(fixture.gfx);
    REQUIRE(gfx != nullptr);
    // This contract also applies when optional GPU-driven rendering is absent.
    std::printf("gpu-driven available: %d\n", int(gfx->gpuDrivenCaps().gpuDrivenAvailable()));
    vk::Buffer first{};
    bool       distinct = false;
    for (int frame = 0; frame < 8; ++frame) {
        gfx->begin3DFrame();
        auto& arena      = gfx->currentFrameArena();
        auto  allocation = arena.alloc(64, 16);
        REQUIRE(allocation.mapped != nullptr);
        REQUIRE(arena.buffer());
        if (frame == 0)
            first = arena.buffer();
        else
            distinct = distinct || arena.buffer() != first;
        std::memset(allocation.mapped, frame, size_t(allocation.size));
        gfx->present();
    }
    REQUIRE(distinct);
}
#endif
