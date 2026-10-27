#pragma once
#include "common/Module.h"
namespace eve::particles {
/** @brief Compatibility-only Particles recording facade; independent of rigid/cloth/rope and not a module.
 * @ownership VM owns completed journals and archives; active logs belong to emitter Sim.
 * @thread Simulation/VM owner thread outside steps and sub-emitter callbacks; no retained emitter pointers.
 */
class EVENGINE_API_DOMAINS ParticleBakeRecording final {
public:
    SSQ_REG
    /** @brief Borrow the stateless compatibility facade for the Squirrel factory ABI.
     * @ownership Process-owned; no ownership transfers and no source state is retained.
     * @lifetime Valid for the process lifetime. @nullable No.
     * @thread Call from the VM owner thread; no callbacks or module registration.
     * @return The always-present compatibility facade; use the subsystem service in scripts.
     */
    [[nodiscard]] static ParticleBakeRecording* create();
    static const char*                          name;
};
}  // namespace eve::particles
