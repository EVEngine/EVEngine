#pragma once
#include "graphics/Graphics.h"
#include "vkbuilder.hpp"

namespace eve::graphics::vulkan
{

class Graphics final : public eve::graphics::Graphics {
public:



  vkb::Instance inst;
  vkb::Device device;
  vkb::Swapchain swapchain;

  vk::RenderPass renderpass;
  vk::Pipeline pipeline;
  vkb::Present present;
  vkb::HostVertexBuffer buffer;
};

} // namespace eve::graphics::sd
