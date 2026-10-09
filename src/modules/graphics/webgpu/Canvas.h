#pragma once
#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "graphics/webgpu/Graphics.h"

#include <optional>

#include <glm/glm.hpp>

namespace eve::graphics::webgpu {

/**
 * @brief Offscreen render target (RGBA8Unorm color, optional Depth32Float).
 * 2D batches are flushed into the canvas texture by Graphics::flush2DToCanvas.
 */
class OffscreenCanvas final : public eve::graphics::Canvas {
public:
    /** @brief Constructs a OffscreenCanvas. */
    OffscreenCanvas(Graphics *gfx, int width, int height, bool hdr = false);
    /** @brief Releases OffscreenCanvas resources. */
    ~OffscreenCanvas() override;

    /** @brief Returns the width. */
    int getWidth() const override { return width; }
    /** @brief Returns the height. */
    int getHeight() const override { return height; }
    /** @brief Returns the texture. */
    Texture *getTexture() override { return &colorTex; }
    /** @brief True when hdr. */
    bool isHDR() const { return hdr; }

    /** @brief Clears . */
    void clear(std::optional<Color> color, std::optional<int> stencil,
               std::optional<double> depth) override;
    /** @brief Returns the pixel. */
    Color getPixel(int x, int y) override;
    /** @brief Creates a image data. @ownership Caller deletes unless documented otherwise. */
    image::ImageData *newImageData() override;
    /** @ownership The caller owns the returned HDR image data. */
    /** @brief Creates a hdr image data. @ownership Caller deletes unless documented otherwise. */
    image::ImageData *newHDRImageData() override;

    /** @brief Draws . */
    void draw(Canvas *, const glm::mat4 &) const override {}
    /** @brief Draws . */
    void draw(eve::graphics::Graphics *, const glm::mat4 &) const override {}

    bool clearRequested = false;
    Color clearColor{0.f, 0.f, 0.f, 1.f};

    friend class Graphics;

private:
    Graphics *gfx;
    int width = 0;
    int height = 0;
    wgpu::Texture color;
    wgpu::TextureView colorView;
    wgpu::Texture depth;
    wgpu::TextureView depthView;
    GpuTexture colorGpu;
    Texture colorTex;
    bool hdr = false;
};

}  // namespace eve::graphics::webgpu
