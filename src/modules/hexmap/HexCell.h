#pragma once

/** @file HexCell.h @brief Packed per-cell state records of the hex map. */

#include "hexmap/HexMetrics.h"

#include <cstdint>

namespace eve::hexmap {

/** @brief Terrain palette index of a hex cell. */
enum class HexTerrainType : std::int32_t {
    Sand  = 0,
    Grass = 1,
    Mud   = 2,
    Stone = 3,
    Snow  = 4,
};

/** @brief Number of entries in the terrain palette. */
inline constexpr std::int32_t kHexTerrainTypeCount = 5;

/** @brief Terrain type index clamped into the supported palette range. */
[[nodiscard]] constexpr std::int32_t clampTerrainType(std::int32_t index) noexcept {
    return index < 0 ? 0 : (index >= kHexTerrainTypeCount ? kHexTerrainTypeCount - 1 : index);
}

/**
 * @brief Packed numeric cell state.
 *
 * Bit layout (most significant first): `TTTTTTTT SSSSSSSS PPFFUUWW WWWEEEEE`
 * where `T` is the terrain type, `S` the special-feature index, `P` the plant
 * level, `F` the farm level, `U` the urban level, `W` the water level and `E`
 * the elevation (stored biased by 15 so five bits cover `[-15, 16]`).
 *
 * @note This is a transient in-memory record; it is not a persistence format.
 */
class HexValues {
public:
    constexpr HexValues() noexcept = default;
    explicit constexpr HexValues(std::uint32_t packed) noexcept : packed_(packed) {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return packed_; }

    [[nodiscard]] constexpr std::int32_t elevation() const noexcept {
        return static_cast<std::int32_t>(get(31u, 0u)) - 15;
    }
    [[nodiscard]] constexpr std::int32_t waterLevel() const noexcept { return static_cast<std::int32_t>(get(31u, 5u)); }
    [[nodiscard]] constexpr std::int32_t urbanLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 10u)); }
    [[nodiscard]] constexpr std::int32_t farmLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 12u)); }
    [[nodiscard]] constexpr std::int32_t plantLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 14u)); }
    [[nodiscard]] constexpr std::int32_t specialIndex() const noexcept {
        return static_cast<std::int32_t>(get(255u, 16u));
    }
    [[nodiscard]] constexpr std::int32_t terrainType() const noexcept {
        return static_cast<std::int32_t>(get(255u, 24u));
    }

    /** @brief Elevation used for line of sight: the higher of land and water. */
    [[nodiscard]] constexpr std::int32_t viewElevation() const noexcept {
        return elevation() > waterLevel() ? elevation() : waterLevel();
    }
    /** @brief Whether the cell is flooded above its terrain surface. */
    [[nodiscard]] constexpr bool isUnderwater() const noexcept { return waterLevel() > elevation(); }

    [[nodiscard]] constexpr HexValues withElevation(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value + 15) & 31u, 31u, 0u)};
    }
    [[nodiscard]] constexpr HexValues withWaterLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 31u, 31u, 5u)};
    }
    [[nodiscard]] constexpr HexValues withUrbanLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 10u)};
    }
    [[nodiscard]] constexpr HexValues withFarmLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 12u)};
    }
    [[nodiscard]] constexpr HexValues withPlantLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 14u)};
    }
    [[nodiscard]] constexpr HexValues withSpecialIndex(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 255u, 255u, 16u)};
    }
    [[nodiscard]] constexpr HexValues withTerrainType(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 255u, 255u, 24u)};
    }

private:
    [[nodiscard]] constexpr std::uint32_t get(std::uint32_t mask, std::uint32_t shift) const noexcept {
        return (packed_ >> shift) & mask;
    }
    [[nodiscard]] constexpr std::uint32_t with(std::uint32_t value, std::uint32_t mask,
                                               std::uint32_t shift) const noexcept {
        return (packed_ & ~(mask << shift)) | (value << shift);
    }

    std::uint32_t packed_ = 0;
};

/**
 * @brief Packed boolean cell state (roads, rivers, walls, exploration).
 *
 * Bits 0-5 are roads per direction, 6-11 incoming rivers, 12-17 outgoing
 * rivers, 18 walled, 20 explored and 21 explorable. Direction bit order matches
 * `HexDirection`, so the direction helpers are plain shifts.
 */
class HexFlags {
public:
    constexpr HexFlags() noexcept = default;
    explicit constexpr HexFlags(std::uint32_t packed) noexcept : packed_(packed) {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return packed_; }

    [[nodiscard]] static constexpr HexFlags direction(HexDirection d) noexcept {
        return HexFlags{1u << static_cast<std::uint32_t>(d)};
    }

    [[nodiscard]] constexpr bool has(HexFlags other) const noexcept {
        return (packed_ & other.packed_) == other.packed_ && other.packed_ != 0u;
    }
    [[nodiscard]] constexpr bool     hasNone(HexFlags other) const noexcept { return (packed_ & other.packed_) == 0u; }
    [[nodiscard]] constexpr HexFlags with(HexFlags other) const noexcept { return HexFlags{packed_ | other.packed_}; }
    [[nodiscard]] constexpr HexFlags without(HexFlags other) const noexcept {
        return HexFlags{packed_ & ~other.packed_};
    }

    [[nodiscard]] constexpr bool hasRoad(HexDirection d) const noexcept { return has(direction(d)); }
    [[nodiscard]] constexpr bool hasRiverIn(HexDirection d) const noexcept {
        return has(HexFlags{direction(d).packed_ << 6});
    }
    [[nodiscard]] constexpr bool hasRiverOut(HexDirection d) const noexcept {
        return has(HexFlags{direction(d).packed_ << 12});
    }
    /** @brief Whether any river flows into this cell. */
    [[nodiscard]] constexpr bool hasAnyRiverIn() const noexcept { return (packed_ & (0x3fu << 6)) != 0u; }
    /** @brief Whether any river leaves this cell. */
    [[nodiscard]] constexpr bool hasAnyRiverOut() const noexcept { return (packed_ & (0x3fu << 12)) != 0u; }
    /** @brief Whether this cell has any river connection at all. */
    [[nodiscard]] constexpr bool hasRiver() const noexcept { return hasAnyRiverIn() || hasAnyRiverOut(); }
    /** @brief Whether this cell has any road. */
    [[nodiscard]] constexpr bool hasRoad() const noexcept { return (packed_ & 0x3fu) != 0u; }
    /** @brief Whether a river enters and/or leaves through `d`. */
    [[nodiscard]] constexpr bool hasRiverThrough(HexDirection d) const noexcept {
        return hasRiverIn(d) || hasRiverOut(d);
    }
    /** @brief Whether the cell is a river source or sink (exactly one connection). */
    [[nodiscard]] constexpr bool hasRiverBeginOrEnd() const noexcept {
        const std::uint32_t in   = (packed_ >> 6) & 0x3fu;
        const std::uint32_t out  = (packed_ >> 12) & 0x3fu;
        const auto          bits = [](std::uint32_t v) {
            std::uint32_t n = 0;
            while (v) {
                n += v & 1u;
                v >>= 1;
            }
            return n;
        };
        return (bits(in) + bits(out)) == 1u;
    }

    [[nodiscard]] constexpr bool isWalled() const noexcept { return (packed_ & (1u << 18)) != 0u; }
    [[nodiscard]] constexpr bool isExplored() const noexcept { return (packed_ & (1u << 20)) != 0u; }
    [[nodiscard]] constexpr bool isExplorable() const noexcept { return (packed_ & (1u << 21)) != 0u; }

    [[nodiscard]] constexpr HexFlags withRoad(HexDirection d) const noexcept { return with(direction(d)); }
    [[nodiscard]] constexpr HexFlags withoutRoad(HexDirection d) const noexcept { return without(direction(d)); }
    [[nodiscard]] constexpr HexFlags withRiverIn(HexDirection d) const noexcept {
        return with(HexFlags{direction(d).packed_ << 6});
    }
    [[nodiscard]] constexpr HexFlags withoutRiverIn(HexDirection d) const noexcept {
        return without(HexFlags{direction(d).packed_ << 6});
    }
    [[nodiscard]] constexpr HexFlags withRiverOut(HexDirection d) const noexcept {
        return with(HexFlags{direction(d).packed_ << 12});
    }
    [[nodiscard]] constexpr HexFlags withoutRiverOut(HexDirection d) const noexcept {
        return without(HexFlags{direction(d).packed_ << 12});
    }
    [[nodiscard]] constexpr HexFlags withWalled(bool value) const noexcept {
        return value ? with(HexFlags{1u << 18}) : without(HexFlags{1u << 18});
    }
    [[nodiscard]] constexpr HexFlags withExplored(bool value) const noexcept {
        return value ? with(HexFlags{1u << 20}) : without(HexFlags{1u << 20});
    }
    [[nodiscard]] constexpr HexFlags withExplorable(bool value) const noexcept {
        return value ? with(HexFlags{1u << 21}) : without(HexFlags{1u << 21});
    }

    /** @brief Mask of every river bit. */
    [[nodiscard]] static constexpr HexFlags riverMask() noexcept { return HexFlags{(0x3fu << 6) | (0x3fu << 12)}; }
    /** @brief Mask of every road bit. */
    [[nodiscard]] static constexpr HexFlags roadMask() noexcept { return HexFlags{0x3fu}; }

private:
    std::uint32_t packed_ = 0;
};

/** @brief One cell of the hex map: packed values plus packed flags. */
struct HexCellData {
    HexValues values{};
    HexFlags  flags{};
};

/**
 * @brief Whether a cell record is in a state that can hold a unit.
 *
 * This is the single rule shared by pathfinding's destination test, the unit
 * registry's restore path and the save-payload validator: a unit may stand on a
 * cell that has been explored, may still be explored, and is not flooded.
 * Occupancy by another unit is deliberately *not* part of this predicate,
 * because a travelling unit reserves its own destination.
 *
 * @param cell Cell record to test.
 * @return True when the cell can hold a unit.
 */
[[nodiscard]] constexpr bool canHoldUnit(const HexCellData& cell) noexcept {
    return cell.flags.isExplored() && cell.flags.isExplorable() && !cell.values.isUnderwater();
}

}  // namespace eve::hexmap
