#pragma once
#include "common/Export.h"


#include "common/PcgPhotoModeApply.h"
#include "daynight/DayNight.h"

namespace eve::daynight {

/**
 * @brief Explicit owner for Pcg's fourteen Lighting fog and density-volume fields.
 * @lifetime The borrowed DayNight target must outlive this authority or be detached first.
 * @thread Game thread only; assignments are synchronous and invoke no callbacks or scripts.
 */
class EVENGINE_API_WORLD PcgLightingFogPhotoMode final : public IPhotoModeFieldSink {
public:
    ~PcgLightingFogPhotoMode() override;
    /** @brief Attach a borrowed DayNight target and seed local state from it. */
    void setTarget(DayNight *target) noexcept;
    /** @brief Register or revoke ownership of Pcg fog fields. */
    void setAuthority(bool enabled);
    /** @brief Accept only Pcg fog and density-volume Lighting assignments. */
    PhotoModeFieldAcceptance acceptsPhotoModeField(
        const PhotoModeAssignment &assignment) const noexcept override;
    /** @brief Validate and atomically project one field through the joint DayNight state. */
    [[nodiscard]] Result<void> applyPhotoModeField(
        const PhotoModeAssignment &assignment) override;

private:
    DayNight *target_ = nullptr;
    PcgFogState state_;
    bool authority_ = false;
};

}  // namespace eve::daynight
