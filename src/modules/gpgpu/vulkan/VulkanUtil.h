#pragma once

#include "vkbuilder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics::vulkan {
class Graphics;
}

namespace eve::gpgpu {

/** @brief Resolve live Vulkan Graphics (throws if missing / not initialized). */
graphics::vulkan::Graphics *requireVulkanGraphics();

/** @brief True if Vulkan Graphics device is ready. */
bool vulkanGraphicsReady();

/** @brief Computes queue. */
vk::Queue computeQueue(graphics::vulkan::Graphics *vkg);
/** @brief Computes command pool. */
vk::CommandPool computeCommandPool(graphics::vulkan::Graphics *vkg);

/** @brief Loads spirv file. */
std::vector<uint32_t> loadSpirvFile(const std::string &path);
/** @brief Compiles compute glsl. */
std::vector<uint32_t> compileComputeGlsl(const std::string &glsl);

}  // namespace eve::gpgpu
