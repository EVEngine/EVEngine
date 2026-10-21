#pragma once
#include "common/Module.h"
namespace eve::physics {
/** @brief Optional VM capture facade; playback never depends on this live-source module.
 * @ownership ModuleManager owns the facade; VM owns sessions, which retain only captured values.
 * @thread Simulation/VM owner thread outside source stepping; no callbacks or retained source pointers.
 */
class EVENGINE_API_ORCHESTRATION PhysicsBakeRecording final : public Module {
public:
    Module_REG(PhysicsBakeRecording);
};
}  // namespace eve::physics
