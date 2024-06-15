#pragma once
#include "graphics/Graphics.h"
#include "vkbuilder.hpp"

namespace eve::graphics::vulkan {

class Graphics final : public eve::graphics::Graphics {
public:
    void present() override;

    /**
     * Sets the current graphics display viewport dimensions.
     **/
    void setViewportSize(int width, int height, int pixelwidth, int pixelheight) override;

    void draw(eve::graphics::Graphics* gfx, const glm::mat4& matrix) const override;
    void draw(Canvas* C, const glm::mat4& matrix) const override;
    void clear(std::optional<Color> color, std::optional<int> stencil, std::optional<double> depth) override;
    Color getPixel(int x, int y) override;
    vkb::Instance  inst;
    vkb::Device    device;
    vkb::Swapchain swapchain;

    vk::RenderPass        renderpass;
    vk::Pipeline          pipeline;
    vkb::Present          present_model;
    vkb::HostVertexBuffer buffer;
};


}  // namespace eve::graphics::vulkan
