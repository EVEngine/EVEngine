#pragma once

#include "common/Module.h"

#include <cstdint>
#include <string>

namespace eve::system {

/**
 * @brief System module — OS / hardware / wall-clock / clipboard / optional GPU info.
 * Frame timing stays on Timer; this is host/environment queries.
 *
 * Script: `sys <- eve.HostSystem();`. `eve.System` is reserved for the script
 * ECS system base class.
 */
class System : public Module {
public:
    Module_REG(System);
    System();
    ~System() override = default;

    /** @brief Engine version string (e.g. "v0.1.0"). */
    std::string getEngineVersion() const;

    /** @brief Raw SDL platform string (e.g. "Mac OS X", "Windows"). */
    std::string getPlatform() const;

    /**
     * @brief Normalized OS id for games:
     * "Windows" | "OS X" | "Linux" | "Android" | "iOS" | "Unknown"
     */
    std::string getOS() const;

    int getProcessorCount() const;
    /** @brief CPU L1 cache line size in bytes (0 if unknown). */
    int getCPUCacheLineSize() const;

    /** @brief System RAM in megabytes (0 if unknown). */
    int getSystemRAM() const;
    /** @brief Current process resident memory in megabytes (0 if unknown). */
    int getProcessMemoryMB() const;

    /** @brief UTC wall-clock seconds since Unix epoch (float for script). */
    float getWallTime() const;

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

    std::string getClipboardText() const;
    void        setClipboardText(const std::string &text);

    /** @brief GPU queries — empty/0 if Graphics not initialized yet. */
    std::string getGpuName() const;
    std::string getGpuVendor() const;
    /** @brief "discrete" | "integrated" | "virtual" | "cpu" | "other" | "" */
    std::string getGpuDeviceType() const;
    /** @brief Sum of DEVICE_LOCAL heap sizes in MB (0 if unknown / not ready). */
    int getGpuMemoryTotalMB() const;
};

}  // namespace eve::system
