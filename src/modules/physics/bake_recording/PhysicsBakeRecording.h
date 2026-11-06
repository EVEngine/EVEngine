#pragma once
#include "common/Module.h"
namespace eve::physics {
/** @brief Compatibility-only facade for Physics script recording; not an independently registered module.
 * @ownership The process owns the stateless facade; VM owns sessions, which retain only captured values.
 * @thread Simulation/VM owner thread outside source stepping; no callbacks or retained source pointers.
 */
class EVENGINE_API_WORLD PhysicsBakeRecording final {
public:
    SSQ_REG
    /** @brief Borrow the stateless compatibility facade for the Squirrel factory ABI.
     * @ownership Process-owned; no ownership transfers and no source state is retained.
     * @lifetime Valid for the process lifetime. @nullable No.
     * @thread Call from the VM owner thread; no callbacks or module registration.
     * @return The always-present compatibility facade; use the subsystem service in scripts.
     */
    [[nodiscard]] static PhysicsBakeRecording* create();
    static const char*                         name;
};
}  // namespace eve::physics
