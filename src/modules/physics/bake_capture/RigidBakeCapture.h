#pragma once
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/PhysicsHandles.h"

namespace eve::physics {
class World;
class World3D;
struct RigidTrackFrame;
/** @brief Recording-only link from a persistent track ID to a generation-qualified live body.
 * @ownership Owns identities only. World owns the body; no lifetime is extended.
 * @lifetime Rebuild links after restoring/recreating a world. Never serialize runtime handles.
 * Destroying the source first makes capture fail; destroying these links never modifies the source.
 */
struct RigidBakeBinding {
    PersistentId       id;
    PhysicsWorldHandle world;
    PhysicsBodyHandle  body;
};
/** @brief Copy one owning frame from 2D bodies using pixel XY and an angle-derived Z quaternion.
 * @ownership Borrows the world and bindings only during this call; returns no pointers or runtime handles.
 * @thread Call on the world owner thread, outside stepping/callbacks; no callbacks or world mutation.
 * @cost Linear in bodies plus identity validation sorting, once per recorded sample outside playback.
 * @return Complete frame or checked stale/foreign/duplicate binding failure, with no partial frame exposed.
 * Time is explicit, finite and nonnegative. RigidPoseTrack::prepare validates the final temporal roster.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<RigidTrackFrame> captureRigidFrame(const World&                      world,
                                                                           std::span<const RigidBakeBinding> bindings,
                                                                           float                             seconds);
/** @brief Copy one owning frame from 3D bodies using metres and XYZW rotations.
 * @ownership Borrows the world and bindings for this call only; returned values survive world destruction.
 * @thread World owner thread outside stepping/callbacks. No callbacks or world mutation.
 * @cost Linear in bodies plus identity validation sorting, once per recorded sample outside playback.
 * @return Complete frame or checked stale/foreign/duplicate binding failure. Final recording admission
 * is performed by RigidPoseTrack::prepare; no simulation time, RNG or world step is generated here.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<RigidTrackFrame> captureRigidFrame(const World3D&                    world,
                                                                           std::span<const RigidBakeBinding> bindings,
                                                                           float                             seconds);
}  // namespace eve::physics
