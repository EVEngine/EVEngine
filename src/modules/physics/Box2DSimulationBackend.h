#pragma once
#include "physics/backend/SimulationBackend.h"
namespace eve::physics::detail {
/**
 * @brief Creates the built-in Box2D CPU backend adapter.
 * @param world Borrowed Box2D world; it must outlive the returned backend.
 * @return An owning backend adapter.
 * @throws eve::Exception when `world` is null.
 *
 * This composition hook is intentionally independent of Graphics and Gpgpu.
 */
[[nodiscard("retain the backend until its borrowed world is destroyed")]]
EVENGINE_API_WORLD std::unique_ptr<ISimulationBackend> makeBox2DSimulationBackend(b2World* world);
}  // namespace eve::physics::detail
