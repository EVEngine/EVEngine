#pragma once
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"
namespace eve::building {
class PlacementWorld;
/**
 * @brief Execute the version-1 house authoring protocol used by scripts and tools.
 * @ownership Synchronously borrows the world and request; returns an owning payload.
 * @thread World owner thread only; validation callbacks must be pure.
 * @reentrancy Mutation publishes complete state before observer callbacks.
 * @cost Capture/save copy selected/world state; preview/mutations stage a whole world once per gesture.
 * @return Structured result; mutations return exact before/after receipt endpoints.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<Value> executeHouseRequest(PlacementWorld& world, const Value& request);
}  // namespace eve::building
