#pragma once
#include "common/Export.h"

#include "common/Module.h"
#include "common/Result.h"
#include "graphics/fog/FogSystem.h"

#include <memory>
#include <string>

namespace eve::graphics::fog {

/**
 * @brief Optional graphics satellite for realtime fog (MAC + raymarch + froxel + analytic lights).
 *
 * Script: `eve.RealtimeFog()`. Factories transfer FogSystem ownership to the caller.
 *
 * @ownership Module owns no live systems; `newSystem` transfers unique ownership.
 * @thread Main / simulation thread; not synchronized for concurrent writers.
 */
class EVENGINE_API_BACKENDS RealtimeFog final : public eve::Module {
public:
    Module_REG(RealtimeFog);

    /**
     * @brief Create an empty FogSystem with Enhanced quality defaults.
     * @ownership Caller owns the returned system.
     * @lifetime Until the caller deletes it (or the script VM releases it).
     */
    [[nodiscard]] Result<std::unique_ptr<FogSystem>> newSystem();

    /** @brief Script facade: create system or throw; VM owns the pointer. */
    FogSystem* newSystemScript();

    /** @brief Parse quality spelling used by scripts ("fast"|"enhanced"|"physical_reference"). */
    [[nodiscard]] static std::string qualityName(FogQuality quality);
};

}  // namespace eve::graphics::fog
