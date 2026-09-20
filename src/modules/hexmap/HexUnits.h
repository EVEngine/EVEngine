#pragma once
#include "common/Export.h"


/** @file HexUnits.h @brief Units that occupy cells, travel along paths and carry vision. */

#include "common/Result.h"
#include "hexmap/HexSearch.h"
#include "hexmap/HexVisibility.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/** @brief Per-unit tuning shared by every unit of a registry. */
struct HexUnitTuning {
    /** @brief Movement points per turn, matching `HexMoveRules::speed`. */
    std::int32_t speed = 24;
    /** @brief Vision radius in cells, matching `HexMoveRules::visionRange`. */
    std::int32_t visionRange = 3;
    /** @brief Travel speed in path segments per second. */
    float travelSpeed = 4.f;
    /** @brief Turn rate in degrees per second while travelling. */
    float rotationSpeed = 180.f;

    /** @brief The pathfinding rules implied by this tuning. */
    [[nodiscard]] HexMoveRules moveRules() const noexcept { return HexMoveRules{speed, visionRange}; }
};

/** @brief Serializable state of one unit. */
struct HexUnitState {
    /** @brief Linear index of the occupied cell, or -1 when the unit is not on the grid. */
    std::int32_t locationIndex = -1;
    /** @brief Facing angle in degrees. */
    float orientation = 0.f;
};

/** @brief Snapshot of one unit used to drive rendering. */
struct HexUnitSample {
    /** @brief Stable unit id. */
    std::int32_t id = -1;
    /** @brief Cell the unit occupies; for a travelling unit this is its destination. */
    HexCoordinates location{};
    /** @brief Cell the unit is currently walking away from, or `(0, 0)` when idle. */
    HexCoordinates travelFrom{};
    /** @brief Interpolated world position. */
    HexVec3 position{};
    /** @brief Interpolated facing angle in degrees. */
    float orientation = 0.f;
    /** @brief Whether the unit is still moving along its path. */
    bool traveling = false;
};

/**
 * @brief Owns the units of one map and their fog-of-war contribution.
 *
 * A unit reserves its **destination** cell as soon as travel starts, exactly like
 * the reference project: the destination is what other actors path around, and
 * the travelled-through cells are never occupied. Vision moves with the unit as
 * it enters each cell of the path, so the revealed corridor follows the walk.
 *
 * Ownership and lifetime: owns one record per unit and one travel plan per
 * travelling unit. `occupancyQuery()` returns a `std::function` that borrows
 * `*this`; it stays valid only while this registry is alive and unmodified.
 *
 * Thread affinity: main thread only; no locking and no callbacks.
 */
class EVENGINE_API_WORLD HexUnitRegistry {
public:
    HexUnitRegistry() = default;

    /** @brief Current tuning. */
    [[nodiscard]] const HexUnitTuning& tuning() const noexcept { return tuning_; }
    /** @brief Replaces the tuning; ongoing travel keeps its plan but uses the new speed. */
    void setTuning(const HexUnitTuning& tuning) noexcept { tuning_ = tuning; }

    /** @brief Whether no unit is registered. */
    [[nodiscard]] bool empty() const noexcept { return units_.empty(); }
    /** @brief Number of registered units. */
    [[nodiscard]] std::int32_t unitCount() const noexcept { return static_cast<std::int32_t>(units_.size()); }

    /**
     * @brief Adds a unit on `location` and grants its vision.
     *
     * @param map Map the unit is placed on.
     * @param visibility Fog-of-war state to update.
     * @param scratch Search scratch, resized to `map.cellCount()`.
     * @param location Cell to occupy; must be inside the grid and free.
     * @param orientation Initial facing angle in degrees.
     * @return The new unit id, or InvalidArgument when the cell is outside the
     *         grid or already occupied.
     * @cost One visibility sweep over the unit's vision radius.
     */
    [[nodiscard]] Result<std::int32_t> addUnit(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                               HexCoordinates location, float orientation);

    /**
     * @brief Removes a unit and withdraws its vision.
     * @return Success, or NotFound for an unknown id.
     */
    [[nodiscard]] Result<void> removeUnit(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                          std::int32_t unitId);

    /**
     * @brief Removes every unit and clears the visibility counters of the map.
     *
     * @param map The annotated map, whose fog chunks are dirtied for every cell that
     *            loses its last viewer.
     * @param visibility Counters to clear.
     */
    void removeAll(HexMap& map, HexVisibility& visibility) noexcept;

    /** @brief Id of the unit occupying a cell, or -1 when the cell is free. */
    [[nodiscard]] std::int32_t unitIdAt(HexCoordinates coordinates) const noexcept;
    /** @brief Whether any unit occupies a cell. */
    [[nodiscard]] bool isOccupied(HexCoordinates coordinates) const noexcept {
        return unitIdAt(coordinates) >= 0;
    }

    /**
     * @brief Occupancy predicate bound to this registry.
     * @ownership The returned callable borrows `*this`; it must not outlive the registry.
     */
    [[nodiscard]] HexOccupancyQuery occupancyQuery() const;

    /**
     * @brief Starts travelling along `path`.
     *
     * `path` holds linear cell indices from the unit's current cell to the
     * destination, inclusive, as produced by `findPath`. The destination is
     * reserved immediately; visibility moves to the first step and then follows
     * the walk, so the fog is revealed along the corridor the unit walks.
     *
     * @param map Map the unit lives on.
     * @param visibility Fog-of-war state to update.
     * @param scratch Search scratch.
     * @param unitId Unit to move.
     * @param path Cell indices to walk.
     * @return Success, or InvalidArgument for an unknown id, a path that does not
     *         start on the unit's cell, a non-adjacent step, or an out-of-range index.
     * @cost One visibility sweep when the unit leaves its cell and one when it enters.
     */
    [[nodiscard]] Result<void> beginTravel(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                           std::int32_t unitId, const std::vector<std::int32_t>& path);

    /**
     * @brief Advances one unit's travel by `dt` seconds and reports where it now is.
     *
     * This is also the single way to read a unit's current pose: `dt <= 0` performs
     * no motion and no turning, so a caller that only wants to draw an idle unit
     * calls this with a zero delta and uses the returned sample. The sample's
     * position is elevation- and perturbation-aware and interpolated along the
     * travel curve, which is what a renderable needs.
     *
     * @param map Map the unit lives on.
     * @param visibility Fog-of-war state to update as the unit crosses cells.
     * @param scratch Search scratch.
     * @param unitId Unit to advance.
     * @param dt Elapsed seconds; values `<= 0` leave both position and facing alone.
     * @return The updated sample, or NotFound for an unknown id.
     * @cost One visibility sweep per cell crossed.
     */
    [[nodiscard]] Result<HexUnitSample> advance(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                                std::int32_t unitId, float dt);

    /** @brief Advances every travelling unit by `dt` seconds. */
    void advanceAll(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch, float dt);

    /**
     * @brief Re-derives the whole fog of war from the units' current cells.
     *
     * Use after loading a map, after the terrain changed, or after a bulk edit
     * invalidated the counters.
     *
     * @cost One visibility sweep per unit.
     */
    void refreshVisibility(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch);

    /** @brief Re-snaps every idle unit onto its cell's current position. */
    void refreshPositions(const HexMap& map) noexcept;

    /** @brief Serializable state of every unit, in id order. */
    [[nodiscard]] std::vector<HexUnitState> snapshot() const;

    /**
     * @brief Replaces every unit with `states`, granting their vision.
     *
     * The registry is cleared first; a failure leaves it empty rather than
     * partially populated.
     *
     * @return Success, or InvalidArgument for an out-of-range or duplicated cell.
     */
    [[nodiscard]] Result<void> restore(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                       const std::vector<HexUnitState>& states);

private:
    struct Unit {
        std::int32_t locationIndex = -1;
        float        orientation = 0.f;
        bool         traveling = false;
        /** @brief Destination-reserving path; `path[0]` is the cell the unit left. */
        std::vector<std::int32_t> path;
        /** @brief Index of the next path entry being walked towards. */
        std::int32_t segment = 1;
        /** @brief Progress inside the current segment in `[0, 1)`. */
        float t = 0.f;
        /** @brief Cell currently granting the unit's vision. */
        std::int32_t visionIndex = -1;
        /**
         * @brief Cell the unit occupies, cached as coordinates.
         *
         * `locationIndex` is the same cell in linear form; the coordinates are
         * cached because `unitIdAt` and `isOccupied` have no map to resolve an
         * index against. While a unit travels this is its reserved destination.
         */
        HexCoordinates location{};
    };

    /**
     * @brief Borrowed record of one unit.
     * @param unitId Unit id (registry position).
     * @return The record.
     * @ownership Borrowed; this registry owns the record.
     * @lifetime Valid until the next unit is added, removed or restored.
     * @nullable Yes, for an unknown id.
     */
    [[nodiscard]] Unit* find(std::int32_t unitId) noexcept;
    /**
     * @brief Borrowed const record of one unit.
     * @ownership Borrowed; this registry owns the record.
     * @lifetime Valid until the next unit is added, removed or restored.
     * @nullable Yes, for an unknown id.
     */
    [[nodiscard]] const Unit* find(std::int32_t unitId) const noexcept;
    [[nodiscard]] Result<HexUnitSample> makeSample(const HexMap& map, std::int32_t unitId, const Unit& unit) const;

    HexUnitTuning  tuning_{};
    std::vector<Unit> units_;
};

}  // namespace eve::hexmap
