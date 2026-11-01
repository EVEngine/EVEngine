#include "graphics/vulkan/Graphics.h"
extern "C" void probeWaitIdle(eve::graphics::Graphics* gfx) {
    auto* backend=dynamic_cast<eve::graphics::vulkan::Graphics*>(gfx);
    if (!backend) throw std::runtime_error("probe expected Vulkan");
    backend->getDevice()->waitIdle();
}
