#pragma once

#include "graphics/vulkan/Graphics.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::gpgpu {

/** Resolve live Vulkan Graphics (throws if missing / not initialized). */
graphics::vulkan::Graphics *requireVulkanGraphics();

/** True if Vulkan Graphics device is ready. */
bool vulkanGraphicsReady();

vk::Queue computeQueue(graphics::vulkan::Graphics *vkg);
vk::CommandPool computeCommandPool(graphics::vulkan::Graphics *vkg);

std::vector<uint32_t> loadSpirvFile(const std::string &path);
std::vector<uint32_t> compileComputeGlsl(const std::string &glsl);

}  // namespace eve::gpgpu
