#pragma once
#include "common/Export.h"


/** @file HexCoordinates.h @brief Axial hex coordinates and world-space conversion. */

#include "hexmap/HexMetrics.h"

#include <cmath>
#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Axial coordinates of one hex cell.
 *
 * The third cube component is derived: `y = -x - z`. The map uses the
 * odd-row-offset-right layout of the reference hex-map project, so the axial
 * pair is directly the `(column, row)` pair with even rows starting at column 0
 * and odd rows shifted half a cell to the right.
 */
struct EVENGINE_API_WORLD HexCoordinates {
    std::int32_t x = 0;
    std::int32_t z = 0;

    constexpr HexCoordinates() noexcept = default;
    constexpr HexCoordinates(std::int32_t xValue, std::int32_t zValue) noexcept : x(xValue), z(zValue) {}

    /** @brief Cube Y component (`-x - z`). */
    [[nodiscard]] constexpr std::int32_t y() const noexcept { return -x - z; }

    [[nodiscard]] constexpr bool operator==(const HexCoordinates& other) const noexcept {
        return x == other.x && z == other.z;
    }
    [[nodiscard]] constexpr bool operator!=(const HexCoordinates& other) const noexcept { return !(*this == other); }

    /**
     * @brief Returns the neighbouring coordinates one step in `direction`.
     * @note The row offset is implicit in the axial pair, so stepping is a pure
     *       integer offset and never needs the odd-row correction.
     */
    [[nodiscard]] constexpr HexCoordinates step(HexDirection direction, std::int32_t distance = 1) const noexcept {
        switch (direction) {
            case HexDirection::NE: return HexCoordinates{x, z + distance};
            case HexDirection::E: return HexCoordinates{x + distance, z};
            case HexDirection::SE: return HexCoordinates{x + distance, z - distance};
            case HexDirection::SW: return HexCoordinates{x, z - distance};
            case HexDirection::W: return HexCoordinates{x - distance, z};
            case HexDirection::NW: return HexCoordinates{x - distance, z + distance};
        }
        return *this;
    }

    /** @brief Cube distance to another cell (wrapping is not supported). */
    [[nodiscard]] constexpr std::int32_t distanceTo(const HexCoordinates& other) const noexcept {
        const std::int32_t dx   = x - other.x;
        const std::int32_t dy   = y() - other.y();
        const std::int32_t dz   = z - other.z;
        const auto         absi = [](std::int32_t v) { return v < 0 ? -v : v; };
        return (absi(dx) + absi(dy) + absi(dz)) / 2;
    }

    /** @brief Hex-space X: east-west neighbour spacing is 1. */
    [[nodiscard]] constexpr float hexX() const noexcept {
        const std::int32_t half = floorDiv2(z);
        return static_cast<float>(x + half) + ((z & 1) == 0 ? 0.f : 0.5f);
    }

    /** @brief Hex-space Z: row spacing is `outerToInner`. */
    [[nodiscard]] constexpr float hexZ() const noexcept { return static_cast<float>(z) * HexMetrics::kOuterToInner; }

    /** @brief Chunk column this cell belongs to. */
    [[nodiscard]] constexpr std::int32_t chunkColumn() const noexcept {
        return (x + floorDiv2(z)) / HexMetrics::kChunkSizeX;
    }

    /** @brief Chunk row this cell belongs to. */
    [[nodiscard]] constexpr std::int32_t chunkRow() const noexcept { return z / HexMetrics::kChunkSizeZ; }

    /** @brief Converts an odd-row offset pair into axial coordinates. */
    [[nodiscard]] static constexpr HexCoordinates fromOffset(std::int32_t offsetX, std::int32_t offsetZ) noexcept {
        return HexCoordinates{offsetX - floorDiv2(offsetZ), offsetZ};
    }

    /** @brief Offset X of this cell inside the rectangular grid. */
    [[nodiscard]] constexpr std::int32_t offsetX() const noexcept { return x + floorDiv2(z); }

    /** @brief Offset Z (row) of this cell inside the rectangular grid. */
    [[nodiscard]] constexpr std::int32_t offsetZ() const noexcept { return z; }

    /**
     * @brief World-space centre of a cell before elevation and perturbation.
     *
     * The odd-row offset of an axial pair is `x + z/2`, so the reference layout
     * `(offsetX + (z odd ? 0.5 : 0)) * innerDiameter` collapses to
     * `(x + 0.5 * z) * innerDiameter` and the result is the exact inverse of
     * `fromWorldPosition`.
     *
     * @param coordinates Cell coordinates.
     * @return Centre position on the XZ plane; Y is always 0.
     */
    [[nodiscard]] static HexVec3 toWorldPosition(HexCoordinates coordinates) noexcept {
        const float diameter = HexMetrics::innerDiameter();
        const float fx       = static_cast<float>(coordinates.x) + 0.5f * static_cast<float>(coordinates.z);
        return HexVec3{fx * diameter, 0.f, static_cast<float>(coordinates.z) * HexMetrics::rowSpacing()};
    }

    /**
     * @brief Converts a world-space XZ position back to cell coordinates.
     *
     * Uses the cube-rounding correction so the result is the hex containing the
     * sample point rather than its nearest centre.
     *
     * @param position World position; only X and Z are read.
     * @return The containing cell coordinates (may be outside the grid bounds).
     */
    [[nodiscard]] static HexCoordinates fromWorldPosition(HexVec3 position) noexcept;

private:
    /** @brief Truncating-toward-negative-infinity halving, matching floor division. */
    [[nodiscard]] static constexpr std::int32_t floorDiv2(std::int32_t value) noexcept {
        return value >= 0 ? value / 2 : -(((-value) + 1) / 2);
    }
};

}  // namespace eve::hexmap
