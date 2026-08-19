#pragma once

#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "graphics/vulkan/Graphics.h"
#include "vkbuilder.hpp"
#include <memory>
#include <optional>
#include <vector>
#include <cstdint>

namespace eve::graphics::vulkan {

class OffscreenCanvas final : public eve::graphics::Canvas {
public:
    OffscreenCanvas(Graphics *owner, int width, int height);
    ~OffscreenCanvas() override;

    int getWidth() const override { return width; }
    int getHeight() const override { return height; }
    Texture *getTexture() override { return &sampleTexture; }

    void clear(std::optional<Color> color, std::optional<int> stencil,
               std::optional<double> depth) override;
    Color getPixel(int x, int y) override;
    image::ImageData *newImageData() override;

    void draw(eve::graphics::Graphics *, const glm::mat4 &) const override {}
    void draw(Canvas *, const glm::mat4 &) const override {}

    vk::Framebuffer framebuffer() const { return fb; }
    vk::Framebuffer framebuffer3D() const { return fb3D; }
    vkb::ColorAttachmentImage &colorImage() { return color; }
    vkb::DepthTarget &depthImage() { return depth; }
    Color pendingClearColor() const { return clearColor; }
    bool takePendingClear();

    /** @brief Ensure a D32 depth attachment + 3D (color+depth) framebuffer exist. */
    void ensure3D();

private:
    void readAllPixels(std::vector<uint8_t> &outRgba);

    Graphics *owner = nullptr;
    int width = 0;
    int height = 0;
    vkb::ColorAttachmentImage color;
    vk::Framebuffer fb = nullptr;
    vkb::DepthTarget depth;
    vk::Framebuffer fb3D = nullptr;
    GpuTexture sampleGpu;
    Texture sampleTexture;

    Color clearColor{0.f, 0.f, 0.f, 1.f};
    bool hasPendingClear = true;
};

}  // namespace eve::graphics::vulkan
