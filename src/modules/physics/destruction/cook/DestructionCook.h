#pragma once
#include "common/Module.h"
namespace eve::physics::destruction_cook {
/** @brief Offline script entrypoint for true convex Voronoi fracture.
 * Factories transfer owning assets/instances to the VM through canonical Result projections.
 */
class EVENGINE_API_DOMAINS DestructionCook final : public Module {
public:
    Module_REG(DestructionCook);
};
}  // namespace eve::physics::destruction_cook
