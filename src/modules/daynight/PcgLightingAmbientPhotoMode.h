#pragma once
#include "common/Export.h"


#include "common/PcgPhotoModeApply.h"
#include "daynight/DayNight.h"

namespace eve::daynight {

/**
 * @brief Explicit Lighting owner for Pcg ambient colors and global light multiplier.
 * @lifetime The borrowed DayNight target must outlive this authority or be detached first.
 * @thread Game thread only; assignments are synchronous and invoke no callbacks or scripts.
 */
class EVENGINE_API_WORLD PcgLightingAmbientPhotoMode final : public IPhotoModeFieldSink {
public:
    ~PcgLightingAmbientPhotoMode() override;
    /** @brief Attach a borrowed DayNight target and seed local state from it. */
    void setTarget(DayNight *target) noexcept;
    /** @brief Register or revoke ownership of the five ambient-light fields. */
    void setAuthority(bool enabled);
    /** @brief Accept only Pcg ambient and global-light assignments. */
    PhotoModeFieldAcceptance acceptsPhotoModeField(
        const PhotoModeAssignment &assignment) const noexcept override;
    /** @brief Validate and atomically project one field through DayNight. */
    [[nodiscard]] Result<void> applyPhotoModeField(
        const PhotoModeAssignment &assignment) override;

private:
    DayNight *target_ = nullptr;
    PcgAmbientLightState baseline_;
    PcgAmbientLightState state_;
    bool authority_ = false;
};

}  // namespace eve::daynight
