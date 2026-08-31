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
    OffscreenCanvas(Graphics *gfx, int width, int height, bool hdr = false);
    ~OffscreenCanvas() override;

    int getWidth() const override { return width; }
    int getHeight() const override { return height; }
    Texture *getTexture() override { return &colorTex; }
    bool isHDR() const { return hdr; }

    void clear(std::optional<Color> color, std::optional<int> stencil,
               std::optional<double> depth) override;
    Color getPixel(int x, int y) override;
    image::ImageData *newImageData() override;
    /** @ownership The caller owns the returned HDR image data. */
    image::ImageData *newHDRImageData() override;

    void draw(Canvas *, const glm::mat4 &) const override {}
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
