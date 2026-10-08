#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>

namespace eve::procgen {

/** @brief Stable semantic cell ids stored in Grid2D.cells (not tile GIDs). */
namespace Semantic {
constexpr uint32_t Empty    = 0;
constexpr uint32_t Wall     = 1;
constexpr uint32_t Floor    = 2;
constexpr uint32_t Corridor = 3;
constexpr uint32_t Water    = 4;
constexpr uint32_t Sand     = 5;
constexpr uint32_t Grass    = 6;
constexpr uint32_t Dirt     = 7;
constexpr uint32_t Stone    = 8;
constexpr uint32_t Snow     = 9;
constexpr uint32_t Door     = 10;
constexpr uint32_t Road     = 11;
}  // namespace Semantic

EVENGINE_API_DOMAINS const char *semanticName(uint32_t id);
EVENGINE_API_DOMAINS uint32_t    semanticId(const std::string &name);

}  // namespace eve::procgen
