#include "system/System.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "common/config.h"
#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_clipboard.h>
#include <SDL2/SDL_cpuinfo.h>
#include <SDL2/SDL_power.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace eve::system {
namespace {

eve::graphics::vulkan::Graphics *vulkanGraphicsOrNull() {
    auto *base = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!base) return nullptr;
    return dynamic_cast<eve::graphics::vulkan::Graphics *>(base);
}

bool gpuReady(eve::graphics::vulkan::Graphics *gfx) {
    if (!gfx) return false;
    vk::PhysicalDevice pd = gfx->getDevice().physical_device;
    return static_cast<VkPhysicalDevice>(pd) != VK_NULL_HANDLE;
}

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

}  // namespace

Module_IMPL(System, new System());

System::System() { SDL_InitSubSystem(SDL_INIT_TIMER); }

std::string System::getEngineVersion() const { return EVENGINE_VERSION; }

std::string System::getPlatform() const {
    const char *p = SDL_GetPlatform();
    return p ? p : "Unknown";
}

std::string System::getOS() const {
#if defined(EVENGINE_WINDOWS)
    return "Windows";
#elif defined(EVENGINE_IOS)
    return "iOS";
#elif defined(EVENGINE_MACOSX)
    return "OS X";
#elif defined(EVENGINE_ANDROID)
    return "Android";
#elif defined(EVENGINE_LINUX)
    return "Linux";
#else
    return getPlatform();
#endif
}

int System::getProcessorCount() const {
    int n = SDL_GetCPUCount();
    return n > 0 ? n : 1;
}

int System::getCPUCacheLineSize() const {
    int n = SDL_GetCPUCacheLineSize();
    return n > 0 ? n : 0;
}

int System::getSystemRAM() const {
    int mb = SDL_GetSystemRAM();
    return mb > 0 ? mb : 0;
}

int System::getProcessMemoryMB() const {
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<int>(pmc.WorkingSetSize / (1024ull * 1024ull));
    return 0;
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS) {
        return static_cast<int>(info.phys_footprint / (1024ull * 1024ull));
    }
    return 0;
#elif defined(__linux__) || defined(EVENGINE_ANDROID) || defined(EVENGINE_LINUX)
    std::ifstream in("/proc/self/status");
    if (!in) return 0;
    std::string key;
    while (in >> key) {
        if (key == "VmRSS:") {
            long kb = 0;
            in >> kb;
            return static_cast<int>(kb / 1024);
        }
        std::string rest;
        std::getline(in, rest);
    }
    return 0;
#else
    return 0;
#endif
}

float System::getWallTime() const {
    using clock = std::chrono::system_clock;
    auto tp     = clock::now().time_since_epoch();
    auto us     = std::chrono::duration_cast<std::chrono::microseconds>(tp).count();
    return float(double(us) / 1'000'000.0);
}

void System::sleepMilliseconds(int ms) {
    if (ms < 0) throw Exception("System.sleepMilliseconds: ms must be >= 0");
    if (ms == 0) return;
    SDL_Delay(static_cast<Uint32>(ms));
}

std::string System::getPowerState() const {
    int secs = 0, pct = 0;
    SDL_PowerState st = SDL_GetPowerInfo(&secs, &pct);
    switch (st) {
        case SDL_POWERSTATE_ON_BATTERY: return "on_battery";
        case SDL_POWERSTATE_NO_BATTERY: return "no_battery";
        case SDL_POWERSTATE_CHARGING: return "charging";
        case SDL_POWERSTATE_CHARGED: return "charged";
        default: return "unknown";
    }
}

int System::getPowerSecondsLeft() const {
    int secs = -1, pct = -1;
    SDL_GetPowerInfo(&secs, &pct);
    return secs;
}

int System::getPowerPercent() const {
    int secs = -1, pct = -1;
    SDL_GetPowerInfo(&secs, &pct);
    return pct;
}

std::string System::getClipboardText() const {
    if (!SDL_HasClipboardText()) return {};
    char *t = SDL_GetClipboardText();
    if (!t) return {};
    std::string out(t);
    SDL_free(t);
    return out;
}

void System::setClipboardText(const std::string &text) {
    if (SDL_SetClipboardText(text.c_str()) != 0)
        throw Exception("System.setClipboardText failed: %s", SDL_GetError());
}

std::string System::getGpuName() const {
    auto *gfx = vulkanGraphicsOrNull();
    if (!gpuReady(gfx)) return {};
    return gfx->getDevice().physical_device.properties.deviceName.data();
}

std::string System::getGpuVendor() const {
    auto *gfx = vulkanGraphicsOrNull();
    if (!gpuReady(gfx)) return {};
    return vendorNameFromId(gfx->getDevice().physical_device.properties.vendorID);
}

std::string System::getGpuDeviceType() const {
    auto *gfx = vulkanGraphicsOrNull();
    if (!gpuReady(gfx)) return {};
    return deviceTypeName(gfx->getDevice().physical_device.properties.deviceType);
}

int System::getGpuMemoryTotalMB() const {
    auto *gfx = vulkanGraphicsOrNull();
    if (!gpuReady(gfx)) return 0;
    const auto &mem = gfx->getDevice().physical_device.memory_properties;
    uint64_t bytes  = 0;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
            bytes += mem.memoryHeaps[i].size;
    }
    return static_cast<int>(bytes / (1024ull * 1024ull));
}

void System::expose(ssq::Table &table) {
    auto cls = table.addClass(name, System::create, false);
    expose(cls);
}

void System::expose(ssq::Class &cls) {
    cls.addFunc("getName", &System::getName);
    cls.addFunc("getEngineVersion", &System::getEngineVersion);
    cls.addFunc("getPlatform", &System::getPlatform);
    cls.addFunc("getOS", &System::getOS);
    cls.addFunc("getProcessorCount", &System::getProcessorCount);
    cls.addFunc("getCPUCacheLineSize", &System::getCPUCacheLineSize);
    cls.addFunc("getSystemRAM", &System::getSystemRAM);
    cls.addFunc("getProcessMemoryMB", &System::getProcessMemoryMB);
    cls.addFunc("getWallTime", &System::getWallTime);
    cls.addFunc("sleepMilliseconds", &System::sleepMilliseconds);
    cls.addFunc("getPowerState", &System::getPowerState);
    cls.addFunc("getPowerSecondsLeft", &System::getPowerSecondsLeft);
    cls.addFunc("getPowerPercent", &System::getPowerPercent);
    cls.addFunc("getClipboardText", &System::getClipboardText);
    cls.addFunc("setClipboardText", &System::setClipboardText);
    cls.addFunc("getGpuName", &System::getGpuName);
    cls.addFunc("getGpuVendor", &System::getGpuVendor);
    cls.addFunc("getGpuDeviceType", &System::getGpuDeviceType);
    cls.addFunc("getGpuMemoryTotalMB", &System::getGpuMemoryTotalMB);
}

}  // namespace eve::system
