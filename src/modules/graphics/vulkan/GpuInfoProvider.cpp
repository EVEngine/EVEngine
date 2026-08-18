// Publishes the Vulkan physical-device description as the IGpuInfo capability,
// so the system module can report it without including graphics.

#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"

#include <cstdio>
#include <string>

namespace eve::graphics::vulkan {
namespace {

std::string vendorNameFromId(uint32_t vendorID) {
    switch (vendorID) {
        case 0x1002: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x1010: return "ImgTec";
        case 0x106B: return "Apple";
        default: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%04X", vendorID);
            return buf;
        }
    }
}

const char *deviceTypeName(vk::PhysicalDeviceType t) {
    switch (t) {
        case vk::PhysicalDeviceType::eDiscreteGpu: return "discrete";
        case vk::PhysicalDeviceType::eIntegratedGpu: return "integrated";
        case vk::PhysicalDeviceType::eVirtualGpu: return "virtual";
        case vk::PhysicalDeviceType::eCpu: return "cpu";
        default: return "other";
    }
}

class VulkanGpuInfo : public eve::caps::IGpuInfo {
public:
    bool gpuReady() const override { return device() != nullptr; }

    std::string gpuName() const override {
        auto *gfx = device();
        if (!gfx) return {};
        return gfx->getDevice().physical_device.properties.deviceName.data();
    }

    std::string gpuVendor() const override {
        auto *gfx = device();
        if (!gfx) return {};
        return vendorNameFromId(gfx->getDevice().physical_device.properties.vendorID);
    }

    std::string gpuDeviceType() const override {
        auto *gfx = device();
        if (!gfx) return {};
        return deviceTypeName(gfx->getDevice().physical_device.properties.deviceType);
    }

    int gpuMemoryTotalMB() const override {
        auto *gfx = device();
        if (!gfx) return 0;
        const auto &mem = gfx->getDevice().physical_device.memory_properties;
        uint64_t bytes = 0;
        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
            if (mem.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
                bytes += mem.memoryHeaps[i].size;
        }
        return static_cast<int>(bytes / (1024ull * 1024ull));
    }

private:
    /** The live backend, or nullptr before a physical device is selected. */
    static Graphics *device() {
        auto *base = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        auto *gfx = dynamic_cast<Graphics *>(base);
        if (!gfx) return nullptr;
        vk::PhysicalDevice pd = gfx->getDevice().physical_device;
        if (static_cast<VkPhysicalDevice>(pd) == VK_NULL_HANDLE) return nullptr;
        return gfx;
    }
};

// Registered at static-init so the capability is available before any module
// is constructed; the provider itself resolves Graphics lazily on each call.
struct Register {
    Register() {
        static VulkanGpuInfo info;
        eve::cap::provide<eve::caps::IGpuInfo>(&info);
    }
} g_register;

}  // namespace
}  // namespace eve::graphics::vulkan
