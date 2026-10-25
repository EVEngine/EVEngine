#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/FramePacing.h"

#include <cstdint>
#include <string>

namespace eve::os {

/**
 * @brief OS module — host OS / hardware / wall-clock / clipboard / optional GPU info.
 * Frame timing stays on Timer; this is host/environment queries.
 *
 * Script: `os <- eve.OS();`.
 */
class EVENGINE_API_FOUNDATION OS : public Module, public IFramePacing {
public:
    Module_REG(OS);
    /** @brief Os. */
    OS();
    /** @brief Os. */
    ~OS() override;
    /** @brief Sets the vertical sync count. */
    [[nodiscard]] Result<void> setVerticalSyncCount(int count) override;
    /** @brief Returns the vertical sync count. */
    [[nodiscard]] int getVerticalSyncCount() const noexcept override { return verticalSyncCount_; }
    /** @brief Sets the target frames per second. */
    [[nodiscard]] Result<void> setTargetFramesPerSecond(int target) override;
    /** @brief Returns the target frames per second. */
    [[nodiscard]] int getTargetFramesPerSecond() const noexcept override { return targetFramesPerSecond_; }
    /** @brief Limit frame. */
    void limitFrame() override;

    /** @brief Engine version string (e.g. "v0.1.0"). */
    std::string getEngineVersion() const;

    /** @brief Raw SDL platform string (e.g. "Mac OS X", "Windows"). */
    std::string getPlatform() const;

    /**
     * @brief Normalized OS id for games:
     * "Windows" | "OS X" | "Linux" | "Android" | "iOS" | "Unknown"
     */
    std::string getOS() const;

    /** @brief Returns the processor count. */
    int getProcessorCount() const;
    /** @brief CPU L1 cache line size in bytes (0 if unknown). */
    int getCPUCacheLineSize() const;

    /** @brief System RAM in megabytes (0 if unknown). */
    int getSystemRAM() const;
    /** @brief Current process resident memory in megabytes (0 if unknown). */
    int getProcessMemoryMB() const;

    /** @brief UTC wall-clock seconds since Unix epoch (float for script). */
    float getWallTime() const;

    /** @brief Sleep milliseconds. */
    void sleepMilliseconds(int ms);

    /**
     * @brief Power state string:
     * "unknown" | "on_battery" | "no_battery" | "charging" | "charged"
     */
    std::string getPowerState() const;
    /** @brief Estimated seconds of battery left (-1 if unknown). */
    int getPowerSecondsLeft() const;
    /** @brief Battery percent 0–100, or -1 if unknown. */
    int getPowerPercent() const;

    /** @brief Returns the clipboard text. */
    std::string getClipboardText() const;
    /** @brief Sets the clipboard text. */
    void        setClipboardText(const std::string &text);

    /** @brief GPU queries — empty/0 if Graphics not initialized yet. */
    std::string getGpuName() const;
    /** @brief Returns the gpu vendor. */
    std::string getGpuVendor() const;
    /** @brief "discrete" | "integrated" | "virtual" | "cpu" | "other" | "" */
    std::string getGpuDeviceType() const;
    /** @brief Sum of DEVICE_LOCAL heap sizes in MB (0 if unknown / not ready). */
    int getGpuMemoryTotalMB() const;
private:
    int      verticalSyncCount_      = 0;
    int      targetFramesPerSecond_  = -1;
    uint64_t lastFrameCounter_        = 0;
};

}  // namespace eve::os
