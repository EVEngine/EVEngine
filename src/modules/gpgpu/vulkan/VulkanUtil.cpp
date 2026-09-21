#include "gpgpu/vulkan/VulkanUtil.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/vulkan/GlslCompiler.h"
#include "graphics/vulkan/Graphics.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace eve::gpgpu {
namespace {

// Compiler failures must not call Exception's global render-tracer callback:
// this path also executes on CPU workers without a Graphics lifetime.
std::runtime_error compileError(const char *message) {
    return std::runtime_error(message);
}

template <class... Args>
std::runtime_error compileError(const char *format, Args... args) {
    const int count = std::snprintf(nullptr, 0, format, args...);
    if (count < 0) return std::runtime_error("Compute compilation failed");
    std::string text(static_cast<size_t>(count) + 1, '\0');
    std::snprintf(text.data(), text.size(), format, args...);
    text.pop_back();
    return std::runtime_error(text);
}

std::vector<uint32_t> loadSpirvBytes(const void *data, size_t size) {
    if (!data || size < 4 || (size % 4) != 0) throw compileError("Gpgpu SPIR-V: invalid size %zu", size);
    const auto *words = static_cast<const uint32_t *>(data);
    if (words[0] != 0x07230203) throw compileError("Gpgpu SPIR-V: bad magic (expected 0x07230203)");
    return std::vector<uint32_t>(words, words + size / 4);
}

}  // namespace

graphics::vulkan::Graphics *requireVulkanGraphics() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    auto *vkg = dynamic_cast<graphics::vulkan::Graphics *>(gfx);
    if (!vkg) throw Exception("Gpgpu: requires Vulkan Graphics backend");
    if (!static_cast<VkDevice>(vkg->getDevice().instance))
        throw Exception("Gpgpu: Graphics device not initialized (create a window first)");
    return vkg;
}

bool vulkanGraphicsReady() {
    try {
        auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (!gfx) return false;
        auto *vkg = dynamic_cast<graphics::vulkan::Graphics *>(gfx);
        if (!vkg) return false;
        return static_cast<VkDevice>(vkg->getDevice().instance) != VK_NULL_HANDLE;
    } catch (...) {
        return false;
    }
}

vk::Queue computeQueue(graphics::vulkan::Graphics *vkg) {
    // Must match computeCommandPool (uploadPool is graphics-family). Only use a
    // dedicated compute queue when it shares that family; otherwise submit on graphics.
    auto &device = vkg->getDevice();
    const uint32_t graphicsFamily = device.get_queue_index(vkb::QueueType::graphics);
    vk::Queue compute = device.getQueue(vkb::QueueType::compute);
    if (compute && device.get_queue_index(vkb::QueueType::compute) == graphicsFamily)
        return compute;
    return device.getQueue(vkb::QueueType::graphics);
}

vk::CommandPool computeCommandPool(graphics::vulkan::Graphics *vkg) {
    return vkg->getUploadPool();
}

std::vector<uint32_t> loadSpirvFile(const std::string &path) {
    auto *fs = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> fd(fs->read(path));
    if (!fd) throw Exception("Gpgpu.newShaderFromSpvFile: failed to read '%s'", path.c_str());
    return loadSpirvBytes(fd->getData(), fd->getSize());
}

std::vector<uint32_t> compileComputeGlsl(const std::string &glsl) {
    if (glsl.empty()) throw compileError("Gpgpu.newShader: empty GLSL");
    try {
        return graphics::compileGlslToSpirv(glsl, graphics::GlslStage::eCompute, "eve_compute.comp");
    } catch (const std::exception &error) {
        // The shared compiler helper reports plain runtime errors (it also runs on
        // CPU workers); keep the Gpgpu.newShader prefix callers rely on.
        throw compileError("Gpgpu.newShader: %s", error.what());
    }
}

}  // namespace eve::gpgpu
