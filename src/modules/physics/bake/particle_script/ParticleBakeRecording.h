#pragma once
#include "common/Module.h"
namespace eve::physics {
/** @brief Optional CPU particle journal and archive VM facade, independent of rigid/cloth/rope capture.
 * @ownership VM owns completed journals and archives; active logs belong to emitter Sim.
 * @thread Simulation/VM owner thread outside steps and sub-emitter callbacks; no retained emitter pointers.
 */
class EVENGINE_API_ORCHESTRATION ParticleBakeRecording final : public Module {
public:
    Module_REG(ParticleBakeRecording);
};
}  // namespace eve::physics
