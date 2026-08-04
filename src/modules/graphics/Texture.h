#pragma once

#include "graphics/Drawable.h"
#include "graphics/Quad.h"
#include <cstdint>
#include <vector>

namespace eve::graphics {

class Graphics;

/**
 * GPU texture created via Graphics::newTexture.
 * Owns GPU resources through an opaque backend handle.
 */
class Texture : public Drawable {
public:
    Texture();
    ~Texture() override;

    void draw(Graphics *gfx, const glm::mat4 &matrix) const override;

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getPixelWidth() const { return pixelWidth; }
    int getPixelHeight() const { return pixelHeight; }

    /** Backend-private GPU object (vulkan::GpuTexture*). */
    void *gpuHandle = nullptr;

    int width = 0;
    int height = 0;
    int depth = 1;
    int layers = 1;
    int mipmapCount = 1;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

}  // namespace eve::graphics
