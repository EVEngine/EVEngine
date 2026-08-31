#pragma once

#include "common/Result.h"
#include "map/MapObject.h"

#include <span>
#include <string_view>

namespace eve::map {

/**
 * @brief Validate map objects against an owning JSON content contract.
 * @param objects Objects borrowed for this call.
 * @param contractJson JSON using schema `eve.map.object-contract`, version 1.
 * @return Success when every object is admitted; otherwise a path-aware diagnostic.
 * @remarks Version 1 supports object types, unknown-type/property policies, required properties,
 * property kinds (`string`, `int`, `number`, `bool`), numeric bounds, and string enums.
 * @thread Thread-safe when the borrowed objects are not concurrently mutated.
 * @reentrancy No callbacks are invoked and no engine state is mutated.
 */
[[nodiscard]] eve::Result<void> validateMapObjects(std::span<const MapObject> objects,
                                                   std::string_view contractJson);

}  // namespace eve::map
