#pragma once
#include "common/Export.h"


/** @file HexSerializer.h @brief Versioned payload codec for a hex map and its units. */

#include "common/Result.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexUnits.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/** @brief Magic prefix of a hex map payload; ASCII `EVEHEX\0\0`. */
inline constexpr std::uint8_t kHexSaveMagic[8] = {'E', 'V', 'E', 'H', 'E', 'X', 0, 0};

/** @brief Format version written by `saveHexMap`. */
inline constexpr std::uint32_t kHexSaveVersion = 1;

/**
 * @brief Serializes a map and its units into a self-describing byte payload.
 *
 * Every field is little-endian and written byte by byte:
 *
 * | offset | type          | field                                    |
 * |--------|---------------|------------------------------------------|
 * | 0      | `uint8[8]`    | `kHexSaveMagic`                          |
 * | 8      | `uint32`      | format version                           |
 * | 12     | `uint32`      | cell count X                             |
 * | 16     | `uint32`      | cell count Z                             |
 * | 20     | `uint32`      | generation seed                          |
 * | 24     | `uint64`      | map revision at save time (diagnostic)   |
 * | 32     | `uint32[2*N]` | per cell: packed `HexValues`, `HexFlags` |
 * | ...    | `uint32`      | unit count                               |
 * | ...    | `(int32,float)` | per unit: cell index, orientation      |
 *
 * The payload is a *snapshot format*, not a save-game format for arbitrary game
 * state: it carries the grid and the units that the map itself owns. Unknown
 * fields are not preserved because the version is rejected outright when it is
 * not `kHexSaveVersion`; a newer writer must therefore bump the version and add
 * its migration in `loadHexMap`.
 *
 * @param map Map to serialize.
 * @param units Unit states, in registry order.
 * @param out Receives the payload; cleared first.
 * @return Success, or InvalidArgument when the map holds no cells.
 * @cost Proportional to the cell count.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> saveHexMap(const HexMap& map, const std::vector<HexUnitState>& units,
                                      std::vector<std::uint8_t>& out);

/**
 * @brief Restores a map and its units from a payload produced by `saveHexMap`.
 *
 * The whole payload is validated before `map` is touched, so a rejected payload
 * leaves both `map` and `units` unchanged. On success `map` is rebuilt from
 * scratch through `HexMap::reset` — no partially applied cell writes are
 * observable.
 *
 * @param bytes Payload to decode.
 * @param map Receives the restored grid.
 * @param units Receives the restored unit states; cleared first.
 * @return Success, or InvalidArgument for a truncated, mis-magic'd, wrong-version
 *         or out-of-range payload.
 * @cost Proportional to the cell count.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> loadHexMap(const std::vector<std::uint8_t>& bytes, HexMap& map,
                                      std::vector<HexUnitState>& units);

}  // namespace eve::hexmap
