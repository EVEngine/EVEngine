#include "graphics/Texture.h"

#include "graphics/Graphics.h"

#include <cmath>
#include <glm/glm.hpp>

namespace eve::graphics {

Texture::Texture() = default;

Texture::~Texture() {
    // GPU lifetime is owned by Graphics backend for now (tracked until Graphics destroy).
    gpuHandle = nullptr;
}

void Texture::draw(Graphics *, const glm::mat4 &) const {
    // Declarative path uses RenderSystem; immediate draw left for later.
}

void Texture::drawOcclusion(Graphics *gfx, const glm::mat4 &matrix) const {
    if (!gfx || !getCastOcclusion()) return;
    // Treat matrix as 2D affine: translation in column 3, scale from basis lengths.
    const float x = matrix[3][0];
    const float y = matrix[3][1];
    const float w = std::sqrt(matrix[0][0] * matrix[0][0] + matrix[0][1] * matrix[0][1]);
    const float h = std::sqrt(matrix[1][0] * matrix[1][0] + matrix[1][1] * matrix[1][1]);
    if (w <= 0.f || h <= 0.f) return;
    gfx->drawOcclusionTexture(const_cast<Texture *>(this), x, y, w, h);
}

}  // namespace eve::graphics
