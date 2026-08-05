#pragma once

#include "graphics/Drawable.h"
#include <cstdint>

namespace eve::graphics {

class Mesh : public Drawable {
public:
    int indexCount = 0;
    void *gpuHandle = nullptr; // vulkan::GpuMesh*

    void draw(Graphics * /*gfx*/, const glm::mat4 & /*matrix*/) const override {}
};

} // namespace eve::graphics
