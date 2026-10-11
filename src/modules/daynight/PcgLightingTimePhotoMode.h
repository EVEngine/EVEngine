#pragma once

#include "common/PcgPhotoModeApply.h"

namespace eve::daynight {

class DayNight;

/**
 * @brief Explicit Lighting-field owner that projects Pcg time settings into DayNight.
 * @lifetime The borrowed DayNight target must outlive this authority or be detached before destruction.
 * @thread Game thread only; no callbacks or scripts are invoked.
 */
class PcgLightingTimePhotoMode final : public IPhotoModeFieldSink {
public:
    ~PcgLightingTimePhotoMode() override;
    /** @brief Attach a borrowed DayNight target. */
    void setTarget(DayNight* target) noexcept { target_ = target; }
    /** @brief Register or revoke this instance as owner of the three Pcg time fields. */
    void setAuthority(bool enabled);

    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;

private:
    DayNight* target_ = nullptr;
    bool authority_ = false;
};

}  // namespace eve::daynight
