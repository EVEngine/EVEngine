#pragma once
#include "common/Export.h"

namespace ssq {
class Object;
class Table;
}  // namespace ssq
namespace eve::physics {
class World3D;
struct PhysicsVector3D;
/**
 * @brief Project an explicit contact impulse through the canonical script Result boundary.
 * @param world Borrowed nullable script receiver; used only during this call, never retained.
 * @param event Borrowed contact event; its VM owns the returned table, and must outlive the call.
 * @param impulse Owning world-space impulse in N s.
 * @param point Owning world-space application point in meters.
 * @thread VM/physics owner only; no callbacks or reentrancy.
 * @return Owning VM table describing the same checked native operation, including null-receiver failure.
 */
[[nodiscard]] EVENGINE_API_WORLD ssq::Table projectSweepContactImpulse(World3D* world, const ssq::Object& event,
                                                                       PhysicsVector3D impulse, PhysicsVector3D point);
}  // namespace eve::physics
