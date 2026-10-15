#pragma once

#include "common/Export.h"

namespace eve {
class ICameraObstructionQuery;
class IProcgenWorldQuery;
}  // namespace eve
namespace eve::physics {
class TargetingLineOfSightAdapter;

/** @brief Borrow the process-owned camera query on the simulation thread; lifetime is process-wide. */
EVENGINE_API_WORLD eve::ICameraObstructionQuery& cameraObstructionQuery();
/** @brief Borrow the process-owned LOS adapter on the simulation thread; worlds are weakly observed. */
EVENGINE_API_WORLD TargetingLineOfSightAdapter& targetingLineOfSightAdapter();
/** @brief Borrow the process-owned terrain projection query; no world ownership is transferred. */
EVENGINE_API_WORLD eve::IProcgenWorldQuery& procgenWorldQuery();
}  // namespace eve::physics
