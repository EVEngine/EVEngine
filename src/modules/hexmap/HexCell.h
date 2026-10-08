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
    /** @brief Constructs a HexValues. */
    constexpr HexValues() noexcept = default;
    /** @brief Constructs a HexValues. */
    explicit constexpr HexValues(std::uint32_t packed) noexcept : packed_(packed) {}

    /** @brief Raw. */
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return packed_; }

    /** @brief Elevation. */
    [[nodiscard]] constexpr std::int32_t elevation() const noexcept {
        return static_cast<std::int32_t>(get(31u, 0u)) - 15;
    }
    /** @brief Water level. */
    [[nodiscard]] constexpr std::int32_t waterLevel() const noexcept { return static_cast<std::int32_t>(get(31u, 5u)); }
    /** @brief Urban level. */
    [[nodiscard]] constexpr std::int32_t urbanLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 10u)); }
    /** @brief Farm level. */
    [[nodiscard]] constexpr std::int32_t farmLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 12u)); }
    /** @brief Plant level. */
    [[nodiscard]] constexpr std::int32_t plantLevel() const noexcept { return static_cast<std::int32_t>(get(3u, 14u)); }
    /** @brief Special index. */
    [[nodiscard]] constexpr std::int32_t specialIndex() const noexcept {
        return static_cast<std::int32_t>(get(255u, 16u));
    }
    /** @brief Terrain type. */
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
    /** @brief With water level. */
    [[nodiscard]] constexpr HexValues withWaterLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 31u, 31u, 5u)};
    }
    /** @brief With urban level. */
    [[nodiscard]] constexpr HexValues withUrbanLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 10u)};
    }
    /** @brief With farm level. */
    [[nodiscard]] constexpr HexValues withFarmLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 12u)};
    }
    /** @brief With plant level. */
    [[nodiscard]] constexpr HexValues withPlantLevel(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 3u, 3u, 14u)};
    }
    /** @brief With special index. */
    [[nodiscard]] constexpr HexValues withSpecialIndex(std::int32_t value) const noexcept {
        return HexValues{with(static_cast<std::uint32_t>(value) & 255u, 255u, 16u)};
    }
    /** @brief With terrain type. */
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
    /** @brief Constructs a HexFlags. */
    constexpr HexFlags() noexcept = default;
    /** @brief Constructs a HexFlags. */
    explicit constexpr HexFlags(std::uint32_t packed) noexcept : packed_(packed) {}

    /** @brief Raw. */
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return packed_; }

    /** @brief Direction. */
    [[nodiscard]] static constexpr HexFlags direction(HexDirection d) noexcept {
        return HexFlags{1u << static_cast<std::uint32_t>(d)};
    }

    /** @brief True when active. */
    [[nodiscard]] constexpr bool has(HexFlags other) const noexcept {
        return (packed_ & other.packed_) == other.packed_ && other.packed_ != 0u;
    }
    /** @brief True when none. */
    [[nodiscard]] constexpr bool     hasNone(HexFlags other) const noexcept { return (packed_ & other.packed_) == 0u; }
    /** @brief With. */
    [[nodiscard]] constexpr HexFlags with(HexFlags other) const noexcept { return HexFlags{packed_ | other.packed_}; }
    /** @brief Without. */
    [[nodiscard]] constexpr HexFlags without(HexFlags other) const noexcept {
        return HexFlags{packed_ & ~other.packed_};
    }

    /** @brief True when road. */
    [[nodiscard]] constexpr bool hasRoad(HexDirection d) const noexcept { return has(direction(d)); }
    /** @brief True when river in. */
    [[nodiscard]] constexpr bool hasRiverIn(HexDirection d) const noexcept {
        /** @brief True when active. */
        return has(HexFlags{direction(d).packed_ << 6});
    }
    /** @brief True when river out. */
    [[nodiscard]] constexpr bool hasRiverOut(HexDirection d) const noexcept {
        /** @brief True when active. */
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

    /** @brief True when walled. */
    [[nodiscard]] constexpr bool isWalled() const noexcept { return (packed_ & (1u << 18)) != 0u; }
    /** @brief True when explored. */
    [[nodiscard]] constexpr bool isExplored() const noexcept { return (packed_ & (1u << 20)) != 0u; }
    /** @brief True when explorable. */
    [[nodiscard]] constexpr bool isExplorable() const noexcept { return (packed_ & (1u << 21)) != 0u; }

    /** @brief With road. */
    [[nodiscard]] constexpr HexFlags withRoad(HexDirection d) const noexcept { return with(direction(d)); }
    /** @brief Without road. */
    [[nodiscard]] constexpr HexFlags withoutRoad(HexDirection d) const noexcept { return without(direction(d)); }
    /** @brief With river in. */
    [[nodiscard]] constexpr HexFlags withRiverIn(HexDirection d) const noexcept {
        /** @brief With. */
        return with(HexFlags{direction(d).packed_ << 6});
    }
    /** @brief Without river in. */
    [[nodiscard]] constexpr HexFlags withoutRiverIn(HexDirection d) const noexcept {
        /** @brief Without. */
        return without(HexFlags{direction(d).packed_ << 6});
    }
    /** @brief With river out. */
    [[nodiscard]] constexpr HexFlags withRiverOut(HexDirection d) const noexcept {
        /** @brief With. */
        return with(HexFlags{direction(d).packed_ << 12});
    }
    /** @brief Without river out. */
    [[nodiscard]] constexpr HexFlags withoutRiverOut(HexDirection d) const noexcept {
        /** @brief Without. */
        return without(HexFlags{direction(d).packed_ << 12});
    }
    /** @brief With walled. */
    [[nodiscard]] constexpr HexFlags withWalled(bool value) const noexcept {
        return value ? with(HexFlags{1u << 18}) : without(HexFlags{1u << 18});
    }
    /** @brief With explored. */
    [[nodiscard]] constexpr HexFlags withExplored(bool value) const noexcept {
        return value ? with(HexFlags{1u << 20}) : without(HexFlags{1u << 20});
    }
    /** @brief With explorable. */
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
