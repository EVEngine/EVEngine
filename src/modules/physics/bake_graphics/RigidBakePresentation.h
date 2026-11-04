#pragma once
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
namespace eve::graphics {
class Renderable3D;
class Renderable2D;
}  // namespace eve::graphics
namespace eve::physics {
class RigidPlayback;
/** @brief Sample a metre-space rigid player into a live renderer's world matrix, preserving mesh scale.
 * @ownership Borrows player and target only during this call; retains no pointers, handles or callbacks.
 * @lifetime Caller must resolve a live target each call. Either object may be destroyed after return.
 * @thread ECS/render owner thread, outside concurrent mutation; caller must exclude other transform writers.
 * @return Success or invalid space, missing identity or nonrepresentable transform; failure preserves target.
 * @remarks No physics step, entity creation, GPU upload, time advance or Scene link is performed.
 * Direct renderer TRS setters subsequently replace the submitted matrix. Rebinding after restore is caller-owned.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> submitRigidPlayback(const RigidPlayback& player, PersistentId identity,
                                                                  graphics::Renderable3D& target);
/** @brief Submit planar pixel playback to a live 2D renderer, converting quaternion rotation to degrees.
 * @ownership Borrows both objects only during the call; no persistent link or callback.
 * @lifetime Caller resolves a live target each call; either object may be destroyed after return.
 * @thread ECS/render owner thread, excluding other transform writers.
 * @return Success or invalid space/identity/sample. Failure preserves the target; scale and sprite remain unchanged.
 * @remarks Coordinates keep their recorded XY axes; shortest rotation does not reconstruct multiple turns.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> submitRigidPlayback(const RigidPlayback& player, PersistentId identity,
                                                                  graphics::Renderable2D& target);
}  // namespace eve::physics
