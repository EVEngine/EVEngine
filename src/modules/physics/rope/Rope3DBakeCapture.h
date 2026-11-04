#pragma once
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
class Rope3D;
struct DeformFrame;
/** @brief Copy a complete owning deformation frame; source-owner thread outside stepping.
 * @ownership Borrows source only during this call; returned positions/connectivity outlive it.
 * @cost Linear in particles and connectivity per recording sample, with allocation.
 * @return Snapshot or invalid time/position/connectivity diagnostic; source is unchanged. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<DeformFrame> captureDeformFrame(const Rope3D& source, float seconds);
}  // namespace eve::physics
