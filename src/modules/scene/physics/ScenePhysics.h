#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "physics/PhysicsHandles.h"

namespace eve::physics {
class World3D;
}

namespace eve::scene_physics {

/** @brief Scene/runtime composition entry point for generated colliders in a shared World3D. */
class ScenePhysics final : public eve::Module {
public:
    Module_REG(ScenePhysics);

    /**
     * @brief Route future generated collider publications into a caller-owned gameplay world.
     * @ownership The caller owns world; this module retains only its stale-safe identity and a provider binding.
     * @lifetime The provider owns the binding. Unbind before destroying a non-empty publication set; world destruction
     * makes records observably stale.
     * @thread Owner-thread only; do not race publication or simulation stepping.
     */
    [[nodiscard]] eve::Result<void> bindGeneratedColliders(eve::physics::World3D& world);
    /** @brief Remove the current empty shared-world binding. */
    [[nodiscard]] eve::Result<void> unbindGeneratedColliders();
    /** @brief Current bound world identity, or invalid when unbound/stale. */
    [[nodiscard]] eve::physics::PhysicsWorldHandle boundWorld() const noexcept;

};

}  // namespace eve::scene_physics
