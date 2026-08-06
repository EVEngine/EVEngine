#include "gpgpu/VulkanUtil.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace eve::gpgpu {
namespace {

std::vector<uint32_t> loadSpirvBytes(const void *data, size_t size) {
    if (!data || size < 4 || (size % 4) != 0)
        throw Exception("Gpgpu SPIR-V: invalid size %zu", size);
    const auto *words = static_cast<const uint32_t *>(data);
    if (words[0] != 0x07230203)
        throw Exception("Gpgpu SPIR-V: bad magic (expected 0x07230203)");
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
    // Prefer dedicated/separate compute; fall back to graphics (MoltenVK / unified).
    vk::Queue q = vkg->getDevice().getQueue(vkb::QueueType::compute);
    if (!q) q = vkg->getDevice().getQueue(vkb::QueueType::graphics);
    return q;
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
    if (glsl.empty()) throw Exception("Gpgpu.newShader: empty GLSL");
#if defined(_WIN32)
    (void)glsl;
    throw Exception("Gpgpu.newShader: GLSL via glslc is not supported on Windows; "
                    "use newShaderFromSpvFile");
#else
    char inPath[] = "/tmp/eve_comp_XXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) throw Exception("Gpgpu.newShader: mkstemp failed");
    std::string outPath = std::string(inPath) + ".spv";
    {
        ssize_t n = write(fd, glsl.data(), glsl.size());
        close(fd);
        if (n < 0 || size_t(n) != glsl.size()) {
            unlink(inPath);
            throw Exception("Gpgpu.newShader: failed to write temp GLSL");
        }
    }

    std::string cmd =
        std::string("glslc -fshader-stage=comp \"") + inPath + "\" -o \"" + outPath + "\" 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    std::string err;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) err += buf;
        int status = pclose(pipe);
        unlink(inPath);
        if (status != 0) {
            unlink(outPath.c_str());
            throw Exception("Gpgpu.newShader: glslc failed:\n%s", err.c_str());
        }
    } else {
        unlink(inPath);
        throw Exception("Gpgpu.newShader: glslc not available (popen failed)");
    }

    FILE *f = fopen(outPath.c_str(), "rb");
    if (!f) {
        unlink(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to open compiled SPIR-V");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0 && fread(bytes.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        fclose(f);
        unlink(outPath.c_str());
        throw Exception("Gpgpu.newShader: failed to read compiled SPIR-V");
    }
    fclose(f);
    unlink(outPath.c_str());
    return loadSpirvBytes(bytes.data(), bytes.size());
#endif
}

}  // namespace eve::gpgpu
