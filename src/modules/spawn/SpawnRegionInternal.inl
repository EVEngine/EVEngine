#pragma once
#include "spawn/SpawnMap.h"
namespace eve::spawn::detail {
// Internal entry: region already admitted by compileMap; no borrowed data is retained.
Result<double> distanceToValidatedRegion(const Region& region, Point point);
}  // namespace eve::spawn::detail
