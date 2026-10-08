#include "os/OS.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "common/Capability.h"
#include "common/GpuInfo.h"
#include "common/config.h"

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

namespace eve::os {
namespace {

/** The graphics backend's GPU description, or nullptr when none is linked. */
eve::caps::IGpuInfo *gpuInfoOrNull() {
    auto *info = eve::cap::query<eve::caps::IGpuInfo>();
    return (info && info->gpuReady()) ? info : nullptr;
}

}  // namespace

Module_IMPL(OS, new OS());

OS::OS() {
    SDL_InitSubSystem(SDL_INIT_TIMER);
    cap::provide<IFramePacing>(this);
}

OS::~OS() { cap::revoke<IFramePacing>(this); }

Result<void> OS::setVerticalSyncCount(int count) {
    if (count < 0 || count > 2)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "VSync count must be in [0,2]", "count",
                                                       {}, "os.framePacing"));
    verticalSyncCount_ = count;
    lastFrameCounter_  = 0;
    return Result<void>::success();
}

Result<void> OS::setTargetFramesPerSecond(int target) {
    if (target < -1 || target > 240)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "target FPS must be in [-1,240]", "target",
                                                       {}, "os.framePacing"));
    targetFramesPerSecond_ = target;
    lastFrameCounter_      = 0;
    return Result<void>::success();
}

void OS::limitFrame() {
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    if (lastFrameCounter_ == 0 || frequency == 0) {
        lastFrameCounter_ = now;
        return;
    }
    if (verticalSyncCount_ == 0 && targetFramesPerSecond_ > 0) {
        const double target = static_cast<double>(frequency) / static_cast<double>(targetFramesPerSecond_);
        const double elapsed = static_cast<double>(now - lastFrameCounter_);
        if (elapsed < target) {
            const uint32_t milliseconds =
                static_cast<uint32_t>((target - elapsed) * 1000.0 / static_cast<double>(frequency));
            if (milliseconds > 0) SDL_Delay(milliseconds);
        }
    }
    lastFrameCounter_ = SDL_GetPerformanceCounter();
}

std::string OS::getEngineVersion() const { return EVENGINE_VERSION; }

std::string OS::getPlatform() const {
    const char *p = SDL_GetPlatform();
    return p ? p : "Unknown";
}

std::string OS::getOS() const {
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

int OS::getProcessorCount() const {
    int n = SDL_GetCPUCount();
    return n > 0 ? n : 1;
}

int OS::getCPUCacheLineSize() const {
    int n = SDL_GetCPUCacheLineSize();
    return n > 0 ? n : 0;
}

int OS::getSystemRAM() const {
    int mb = SDL_GetSystemRAM();
    return mb > 0 ? mb : 0;
}

int OS::getProcessMemoryMB() const {
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

float OS::getWallTime() const {
    using clock = std::chrono::system_clock;
    auto tp     = clock::now().time_since_epoch();
    auto us     = std::chrono::duration_cast<std::chrono::microseconds>(tp).count();
    return float(double(us) / 1'000'000.0);
}

void OS::sleepMilliseconds(int ms) {
    if (ms < 0) throw Exception("OS.sleepMilliseconds: ms must be >= 0");
    if (ms == 0) return;
    SDL_Delay(static_cast<Uint32>(ms));
}

std::string OS::getPowerState() const {
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

int OS::getPowerSecondsLeft() const {
    int secs = -1, pct = -1;
    SDL_GetPowerInfo(&secs, &pct);
    return secs;
}

int OS::getPowerPercent() const {
    int secs = -1, pct = -1;
    SDL_GetPowerInfo(&secs, &pct);
    return pct;
}

std::string OS::getClipboardText() const {
    if (!SDL_HasClipboardText()) return {};
    char *t = SDL_GetClipboardText();
    if (!t) return {};
    std::string out(t);
    SDL_free(t);
    return out;
}

void OS::setClipboardText(const std::string &text) {
    if (SDL_SetClipboardText(text.c_str()) != 0)
        throw Exception("OS.setClipboardText failed: %s", SDL_GetError());
}

std::string OS::getGpuName() const {
    auto *info = gpuInfoOrNull();
    return info ? info->gpuName() : std::string();
}

std::string OS::getGpuVendor() const {
    auto *info = gpuInfoOrNull();
    return info ? info->gpuVendor() : std::string();
}

std::string OS::getGpuDeviceType() const {
    auto *info = gpuInfoOrNull();
    return info ? info->gpuDeviceType() : std::string();
}

int OS::getGpuMemoryTotalMB() const {
    auto *info = gpuInfoOrNull();
    return info ? info->gpuMemoryTotalMB() : 0;
}

void OS::expose(ssq::Table &table) {
    auto cls = table.addClass("OS", OS::create, false);
    expose(cls);
}

void OS::expose(ssq::Class &cls) {
    cls.addFunc("getName", &OS::getName);
    cls.addFunc("getEngineVersion", &OS::getEngineVersion);
    cls.addFunc("getPlatform", &OS::getPlatform);
    cls.addFunc("getOS", &OS::getOS);
    cls.addFunc("getProcessorCount", &OS::getProcessorCount);
    cls.addFunc("getCPUCacheLineSize", &OS::getCPUCacheLineSize);
    cls.addFunc("getSystemRAM", &OS::getSystemRAM);
    cls.addFunc("getProcessMemoryMB", &OS::getProcessMemoryMB);
    cls.addFunc("getWallTime", &OS::getWallTime);
    cls.addFunc("sleepMilliseconds", &OS::sleepMilliseconds);
    cls.addFunc("limitFrame", &OS::limitFrame);
    cls.addFunc("getVerticalSyncCount", [](const OS* os) { return os->getVerticalSyncCount(); });
    cls.addFunc("getTargetFramesPerSecond", [](const OS* os) { return os->getTargetFramesPerSecond(); });
    cls.addFunc("setTargetFramesPerSecond", [](OS* os, int target) {
        auto result = os->setTargetFramesPerSecond(target);
        if (!result.ok()) throw Exception("OS.setTargetFramesPerSecond: target must be in [-1,240]");
    });
    cls.addFunc("getPowerState", &OS::getPowerState);
    cls.addFunc("getPowerSecondsLeft", &OS::getPowerSecondsLeft);
    cls.addFunc("getPowerPercent", &OS::getPowerPercent);
    cls.addFunc("getClipboardText", &OS::getClipboardText);
    cls.addFunc("setClipboardText", &OS::setClipboardText);
    cls.addFunc("getGpuName", &OS::getGpuName);
    cls.addFunc("getGpuVendor", &OS::getGpuVendor);
    cls.addFunc("getGpuDeviceType", &OS::getGpuDeviceType);
    cls.addFunc("getGpuMemoryTotalMB", &OS::getGpuMemoryTotalMB);
}

}  // namespace eve::os
