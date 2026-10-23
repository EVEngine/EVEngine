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

/** @brief OffscreenCanvas public API. */
class OffscreenCanvas final : public eve::graphics::Canvas {
public:
    /** @brief Constructs a OffscreenCanvas. */
    OffscreenCanvas(Graphics *owner, int width, int height, bool hdr = false);
    /** @brief Releases OffscreenCanvas resources. */
    ~OffscreenCanvas() override;

    /** @brief Returns the width. */
    int getWidth() const override { return width; }
    /** @brief Returns the height. */
    int getHeight() const override { return height; }
    /** @brief Returns the texture. */
    Texture *getTexture() override { return &sampleTexture; }

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
    void draw(eve::graphics::Graphics *, const glm::mat4 &) const override {}
    /** @brief Draws . */
    void draw(Canvas *, const glm::mat4 &) const override {}

    /** @brief Framebuffer. */
    vk::Framebuffer framebuffer() const { return fb; }
    /** @brief Framebuffer 3 d. */
    vk::Framebuffer framebuffer3D() const { return fb3D; }
    /** @brief Color image. */
    vkb::ColorAttachmentImage &colorImage() { return color; }
    /** @brief Depth image. */
    vkb::DepthTarget &depthImage() { return depth; }
    /** @brief Pending clear color. */
    Color pendingClearColor() const { return clearColor; }
    /** @brief Take pending clear. */
    bool takePendingClear();
    /** @brief True when hdr. */
    bool isHDR() const { return hdr; }

    /** @brief Ensure a D32 depth attachment + 3D (color+depth) framebuffer exist. */
    void ensure3D();

private:
    void readAllPixels(std::vector<uint8_t> &outRgba);
    void readAllHDRPixels(std::vector<uint8_t> &outRgba16f);

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
    bool hdr = false;
};

}  // namespace eve::graphics::vulkan
