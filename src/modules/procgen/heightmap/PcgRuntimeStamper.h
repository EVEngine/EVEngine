#pragma once

#include "common/Result.h"
#include "procgen/heightmap/Heightmap.h"

#include <string>

namespace eve::procgen {

/** @brief Observable lifecycle of Pcg's runtime stamper sample. */
enum class PcgRuntimeStampStatus { Idle = 0, AwaitingResource = 1, Loaded = 2, Completed = 3, Failed = 4 };

/**
 * @brief Native orchestration of Pcg RuntimeStamper's load, flatten, fit, mask, and stamp sequence.
 * @details The controller owns a copy of the loaded raster. execute() builds a complete terrain candidate and
 *          publishes it only on success. It retains no external pointer and invokes no callbacks.
 * @thread Game thread only; callers must serialize access to the target heightmap.
 */
class PcgRuntimeStamper {
public:
    /** @brief Set the resource address and presentation flags; backslashes are normalized to slashes. */
    [[nodiscard]] Result<void> configure(std::string stampAddress, bool showGui = true, bool showDebug = true);
    /** @brief Copy a decoded stamp resource into the controller. */
    [[nodiscard]] Result<void> loadStamp(const Heightmap& stamp, const std::string& resourceName);
    /** @brief Record the same missing-resource failure exposed by Pcg RuntimeStamper. */
    [[nodiscard]] Result<void> reportMissingStamp();
    /** @brief Flatten target to zero, fit the stamp to its complete grid, and apply height 6 with a linear radial mask. */
    [[nodiscard]] Result<int> execute(Heightmap& target, double originX = 0.0, double originZ = 0.0,
                                      double spacingX = 1.0, double spacingZ = 1.0);
    /** @brief Recompute Pcg's 300x20 bottom-centred progress-label rectangle. */
    [[nodiscard]] Result<void> updateLayout(double screenWidth, double screenHeight);

    /** @brief Return the normalized resource address. */
    [[nodiscard]] const std::string& stampAddress() const noexcept { return address_; }
    /** @brief Return the current user-facing progress text. */
    [[nodiscard]] const std::string& progressText() const noexcept { return progress_; }
    /** @brief Return the lifecycle state. */
    [[nodiscard]] PcgRuntimeStampStatus status() const noexcept { return status_; }
    /** @brief Return Pcg's incremental update allowance, 1/15 second. */
    [[nodiscard]] double updateTimeAllowed() const noexcept { return 1.0 / 15.0; }
    /** @brief Return progress-label center X. */
    [[nodiscard]] double labelCenterX() const noexcept { return labelX_; }
    /** @brief Return progress-label center Y. */
    [[nodiscard]] double labelCenterY() const noexcept { return labelY_; }

private:
    std::string address_ = "Pcg/Stamps/RuggedHills 1810 4";
    std::string progress_;
    Heightmap stamp_;
    PcgRuntimeStampStatus status_ = PcgRuntimeStampStatus::Idle;
    bool showGui_ = true;
    bool showDebug_ = true;
    double labelX_ = 150.0;
    double labelY_ = 10.0;
};

}  // namespace eve::procgen
