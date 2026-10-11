#pragma once
#include "common/Export.h"

/**
 * @file GridConfig.h
 * @brief Module-neutral grid topology and sizing (no building/map dependency).
 *
 * Layout, cell size/gap, origin, stagger, and plane axes live here.
 * Coordinate conversion is in GridProjection.h as pure functions.
 */

#include <string>

namespace eve::grid {

/** @brief Cell topology: rectangle, hex, isometric, or staggered variants. */
enum class GridLayout { Rectangle, Hexagon, Isometric, Staggered, IsometricZAsY };
/** @brief Axis used for odd/even row or column offset in staggered layouts. */
enum class StaggerAxis { X, Y };
/** @brief Whether odd or even indices along the stagger axis are offset. */
enum class StaggerIndex { Odd, Even };

/**
 * @brief Placement plane for the second grid axis.
 * XY maps the second axis to world Y (2D tilemaps / top-down).
 * XZ maps the second axis to world Z with world Y as height (3D ground grids).
 */
enum class GridPlane { XY, XZ };

/**
 * @brief Grid shape, cell metrics, origin, and placement plane.
 * Pure data; use GridProjection helpers for world/cell conversion.
 */
struct EVENGINE_API_FOUNDATION GridConfig {
    GridLayout layout = GridLayout::Rectangle;
    GridPlane plane = GridPlane::XY;
    float cellW = 32.f;
    float cellH = 32.f;
    float cellGapX = 0.f;
    float cellGapY = 0.f;
    float originX = 0.f;
    float originY = 0.f;
    StaggerAxis staggerAxis = StaggerAxis::Y;
    StaggerIndex staggerIndex = StaggerIndex::Odd;
    float hexSideLength = 0.f;

    /** @brief Stable string name for a layout enum value. */
    static const char *layoutName(GridLayout l);
    /** @brief Parse a layout name; unknown names fall back to Rectangle. */
    static GridLayout layoutFromName(const std::string &name);
    /** @brief Stable string name for a plane enum value. */
    static const char *planeName(GridPlane p);
    /** @brief Parse a plane name; unknown names fall back to XY. */
    static GridPlane planeFromName(const std::string &name);
};

/** @brief Effective cell pitch on X including gap. */
inline float cellPitchX(const GridConfig &cfg) { return cfg.cellW + cfg.cellGapX; }
/** @brief Effective cell pitch on Y including gap. */
inline float cellPitchY(const GridConfig &cfg) { return cfg.cellH + cfg.cellGapY; }

}  // namespace eve::grid
