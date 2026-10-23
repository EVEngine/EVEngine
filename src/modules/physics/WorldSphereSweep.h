#pragma once
#include "common/Export.h"
namespace eve {
class IWorldSphereSweepFactory;
}
namespace eve::physics {
/** @brief Borrow the process-owned optional sweep factory on the simulation thread. */
EVENGINE_API_WORLD IWorldSphereSweepFactory& worldSphereSweepFactory();
}  // namespace eve::physics
