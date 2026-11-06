#pragma once
#include "common/Module.h"
namespace eve::physics {
/** @brief Optional VM bridge from native baked players to graphics presentation.
 * @ownership ModuleManager owns this stateless facade; calls borrow players and live targets synchronously.
 * @thread VM/render owner thread, excluding other target transform writers. No callbacks or implicit time advance.
 */
class EVENGINE_API_WORLD PhysicsBakeGraphics final : public Module {
public:
    Module_REG(PhysicsBakeGraphics);
};
}  // namespace eve::physics
