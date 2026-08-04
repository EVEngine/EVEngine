#include "graphics/Texture.h"

namespace eve::graphics {

Texture::Texture() = default;

Texture::~Texture() {
    // GPU lifetime is owned by Graphics backend for now (tracked until Graphics destroy).
    gpuHandle = nullptr;
}

void Texture::draw(Graphics *, const glm::mat4 &) const {
    // Declarative path uses RenderSystem; immediate draw left for later.
}

}  // namespace eve::graphics
