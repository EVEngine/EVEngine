#pragma once
#include "common/Export.h"


#include "common/PcgPhotoModeApply.h"
#include "daynight/DayNight.h"

namespace eve::daynight {

/**
 * @brief Explicit Lighting-field owner for Pcg's six manual-sun controls.
 * @lifetime The borrowed DayNight target must outlive this authority or be detached first.
 * @thread Game thread only; assignments are synchronous and invoke no callbacks or scripts.
 */
class EVENGINE_API_WORLD PcgLightingSunPhotoMode final : public IPhotoModeFieldSink {
public:
    ~PcgLightingSunPhotoMode() override;
    /** @brief Attach a borrowed DayNight target and seed local state from it. */
    void setTarget(DayNight *target) noexcept;
    /** @brief Register or revoke this instance as owner of the six manual-sun fields. */
    void setAuthority(bool enabled);
    /** @brief Return Accepted only for the six Pcg manual-sun Lighting fields. */
    PhotoModeFieldAcceptance acceptsPhotoModeField(
        const PhotoModeAssignment &assignment) const noexcept override;
    /** @brief Validate and atomically project one field through DayNight's joint sun state. */
    [[nodiscard]] Result<void> applyPhotoModeField(
        const PhotoModeAssignment &assignment) override;

private:
    DayNight *target_ = nullptr;
    PcgManualSunState state_;
    bool authority_ = false;
};

}  // namespace eve::daynight
