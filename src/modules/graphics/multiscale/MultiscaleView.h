#pragma once
#include "common/Module.h"

namespace eve::graphics::multiscale {
/**
 * @brief Optional script composition facade for explicit multiscale sphere views.
 * @ownership Stateless module; script-owned views and frames own their resources and values.
 * @thread Main graphics/VM thread only; synchronous and non-reentrant.
 * @remarks Factories use the common Result projection. No scene, camera or simulation ownership.
 */
class EVENGINE_API_WORLD MultiscaleView final : public Module {
public:
    Module_REG(MultiscaleView);
};
}  // namespace eve::graphics::multiscale
