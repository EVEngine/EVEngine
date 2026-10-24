#pragma once
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
class Rope3D;
class Cloth3D;
struct DeformFrame;
/** @brief Capture actual rope positions and active connectivity in metres at explicit seconds.
 * @ownership Borrows source only during the call; returns owning snapshot without solver pointers.
 * @thread Simulation owner thread excluding concurrent source mutation; no callbacks or world stepping.
 * @return Complete snapshot or invalid time/positions/topology; empty active connectivity is rejected.
 * @cost Linear in particles and connectivity with allocations; record outside playback.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<DeformFrame> captureDeformFrame(const Rope3D& source, float seconds);
/** @brief Capture cloth positions and surviving triangles in metres at explicit seconds.
 * @ownership Borrows source only during the call; snapshots survive source destruction, and do not own it.
 * @thread Simulation owner thread excluding concurrent mutation; no callbacks or world stepping.
 * @return Complete snapshot or invalid time/positions/topology; empty surviving connectivity is rejected.
 * @cost Linear in particles and triangles with allocations; record outside playback.
 * @remarks UV is absent in this geometry capture; no grid reconstruction that would restore torn triangles.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<DeformFrame> captureDeformFrame(const Cloth3D& source, float seconds);
}  // namespace eve::physics
