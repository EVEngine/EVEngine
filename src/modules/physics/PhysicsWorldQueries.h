#pragma once

namespace eve {
class ICameraObstructionQuery;
class IProcgenWorldQuery;
}  // namespace eve
namespace eve::physics {
class TargetingLineOfSightAdapter;

/** @brief Borrow the process-owned camera query on the simulation thread; lifetime is process-wide. */
eve::ICameraObstructionQuery& cameraObstructionQuery();
/** @brief Borrow the process-owned LOS adapter on the simulation thread; worlds are weakly observed. */
TargetingLineOfSightAdapter& targetingLineOfSightAdapter();
/** @brief Borrow the process-owned terrain projection query; no world ownership is transferred. */
eve::IProcgenWorldQuery& procgenWorldQuery();
}  // namespace eve::physics
