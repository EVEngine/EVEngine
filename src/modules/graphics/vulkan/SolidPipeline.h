#pragma once
#include "graphics/vulkan/Graphics.h"
namespace eve::graphics::vulkan {
/** @brief Internal synchronous pipeline factory; caller destroys the returned Vulkan pipeline. */
vk::Pipeline createSolidColorPipeline(vkb::Device& device, const vkb::BuiltRenderPass& renderPass,
    vk::PipelineLayout layout, BlendMode mode = BlendMode::Opaque);
}
