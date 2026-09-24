#pragma once
#include "common/Export.h"


#include "common/PcgPhotoModeApply.h"
#include "daynight/DayNight.h"

namespace eve::daynight {

/**
 * @brief Explicit Lighting-field owner for Pcg's four skybox override controls.
 * @lifetime The borrowed DayNight target must outlive this authority or be detached first.
 * @thread Game thread only; assignments are synchronous and invoke no callbacks or scripts.
 */
class EVENGINE_API_WORLD PcgLightingSkyboxPhotoMode final : public IPhotoModeFieldSink {
public:
    ~PcgLightingSkyboxPhotoMode() override;
    /** @brief Attach a borrowed DayNight target and seed local state from it. */
    void setTarget(DayNight *target) noexcept;
    /** @brief Register or revoke ownership of the four Pcg skybox fields. */
    void setAuthority(bool enabled);
    /** @brief Accept only Pcg skybox Lighting assignments. */
    PhotoModeFieldAcceptance acceptsPhotoModeField(
        const PhotoModeAssignment &assignment) const noexcept override;
    /** @brief Validate and atomically project one field through DayNight. */
    [[nodiscard]] Result<void> applyPhotoModeField(
        const PhotoModeAssignment &assignment) override;

private:
    DayNight *target_ = nullptr;
    PcgSkyboxState state_;
    bool authority_ = false;
};

}  // namespace eve::daynight
