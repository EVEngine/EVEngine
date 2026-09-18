#pragma once

#include <cstdint>
#include <string_view>

namespace eve::procgen {

/** @brief TileWorldCreator-compatible standard-grid tile categories. */
enum class TilePresetKind : std::uint8_t {
    None,
    DeadEnd,
    Single,
    Fill,
    CornerWay,
    CornerFill,
    InteriorCorner,
    DoubleCorner,
    EdgeWay,
    EdgeFill,
    ThreeWay,
    ThreeWayFill,
    EdgeCornerFill,
    ThreeCorner,
    FourWay,
};

/** @brief Stable build semantics resolved from one eight-neighbour autotile mask. */
struct TilePresetVariant {
    TilePresetKind kind = TilePresetKind::None;
    /** Clockwise rotation in degrees, restricted to 0, 90, 180 or 270. */
    int rotationDegrees = 0;
    /** Mirror the selected tile mesh along its local X axis after rotation. */
    bool mirrorX = false;
    /** Original TileWorldCreator 3x3 configuration value, including the center bit. */
    std::uint16_t configuration = 16;
};

/**
 * @brief Convert EVEngine's N/E/S/W/NE/SE/SW/NW mask to the reference 3x3 configuration.
 * @param neighbourMask Eight-neighbour occupancy mask produced by `grid.autotile`.
 * @return Nine-bit row-major configuration with the occupied center bit set.
 */
[[nodiscard]] std::uint16_t toTilePresetConfiguration(std::uint8_t neighbourMask) noexcept;

/**
 * @brief Resolve the exact TileWorldCreator 4.3.5 standard-grid category and transform tables.
 * @param neighbourMask Eight-neighbour occupancy mask produced by `grid.autotile`.
 * @return Deterministic tile category, rotation, mirror flag and reference configuration.
 * @note Configuration 350 is intentionally `None`, matching the reference table's unclassified entry.
 */
[[nodiscard]] TilePresetVariant resolveTilePreset(std::uint8_t neighbourMask) noexcept;

/** @brief Stable lowercase identifier used in mesh build-layer group names. */
[[nodiscard]] std::string_view tilePresetKindName(TilePresetKind kind) noexcept;

}  // namespace eve::procgen
