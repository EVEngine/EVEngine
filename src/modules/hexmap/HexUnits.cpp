#include "hexmap/HexUnits.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace eve::hexmap {
namespace {

/**
 * @brief Diagnostic for a rejected argument.
 *
 * Returned as a `Diagnostic` rather than a `Result<void>` so one helper can build
 * the failure arm of every `Result<T>` shape this translation unit returns.
 */
[[nodiscard]] Diagnostic invalidArgument(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap");
}

/** @brief Diagnostic for an unknown unit id. */
[[nodiscard]] Diagnostic notFound(std::string message) {
    return Diagnostic::error(DiagnosticCode::NotFound, std::move(message), "hexmap");
}

/** @brief Wraps an angle in degrees into `[-180, 180)`. */
[[nodiscard]] float wrapDegrees(float degrees) noexcept {
    if (!std::isfinite(degrees)) return 0.f;
    float wrapped = std::fmod(degrees + 180.f, 360.f);
    if (wrapped < 0.f) wrapped += 360.f;
    return wrapped - 180.f;
}

/** @brief Shortest signed turn from `from` to `to`, in `[-180, 180)`. */
[[nodiscard]] float angularDifference(float from, float to) noexcept { return wrapDegrees(to - from); }

/** @brief Yaw of a horizontal direction, using the reference `LookRotation` convention. */
[[nodiscard]] float yawFromDirection(HexVec3 direction) noexcept {
    return std::atan2(direction.x, direction.z) * (180.f / std::numbers::pi_v<float>);
}

/** @brief Point on the quadratic Bézier of one travel segment. */
[[nodiscard]] HexVec3 bezierPoint(HexVec3 a, HexVec3 b, HexVec3 c, float t) noexcept {
    const float r = 1.f - t;
    return a * (r * r) + b * (2.f * r * t) + c * (t * t);
}

/** @brief Horizontal derivative of the quadratic Bézier of one travel segment; Y is dropped. */
[[nodiscard]] HexVec3 bezierDerivative(HexVec3 a, HexVec3 b, HexVec3 c, float t) noexcept {
    const HexVec3 slope = (b - a) * (1.f - t) + (c - b) * t;
    return HexVec3{2.f * slope.x, 0.f, 2.f * slope.z};
}

/** @brief Control points of the travel segment that enters `path[segment]`. */
struct TravelSegment {
    /** @brief Cell the unit walks away from on this segment. */
    HexCoordinates from{};
    /** @brief Cell the unit walks towards on this segment. */
    HexCoordinates to{};
    /** @brief Curve start: the previous segment's end point. */
    HexVec3 a{};
    /** @brief Curve middle: the cell the unit leaves. */
    HexVec3 b{};
    /** @brief Curve end: half way to the destination cell centre. */
    HexVec3 c{};
};

/**
 * @brief Resolves the quadratic Bézier the unit follows on its current segment.
 *
 * This mirrors the reference project: the curve ends at the midpoint between the
 * segment's start cell and its destination, and the unit snaps onto the
 * destination cell centre as soon as travel completes.
 *
 * @param map Map that owns the cells.
 * @param path Travel plan; `path[segment]` is the cell being entered.
 * @param segment Index of the travelled segment, `[1, path.size() - 1]`.
 * @return The control points and the two cells of the segment.
 */
[[nodiscard]] TravelSegment travelSegment(const HexMap& map, const std::vector<std::int32_t>& path,
                                          std::int32_t segment) noexcept {
    const std::int32_t last  = static_cast<std::int32_t>(path.size()) - 1;
    const std::int32_t index = segment < 1 ? 1 : (segment > last ? last : segment);

    TravelSegment result;
    result.from = map.coordinatesAt(path[static_cast<std::size_t>(index - 1)]);
    result.to   = map.coordinatesAt(path[static_cast<std::size_t>(index)]);
    result.b    = map.cellPosition(result.from);
    result.c    = (result.b + map.cellPosition(result.to)) * 0.5f;
    if (index <= 1) {
        result.a = result.b;
    } else {
        result.a = (map.cellPosition(map.coordinatesAt(path[static_cast<std::size_t>(index - 2)])) + result.b) * 0.5f;
    }
    return result;
}

}  // namespace

// --- lookup -----------------------------------------------------------------

HexUnitRegistry::Unit* HexUnitRegistry::find(std::int32_t unitId) noexcept {
    if (unitId < 0 || unitId >= static_cast<std::int32_t>(units_.size())) return nullptr;
    return &units_[static_cast<std::size_t>(unitId)];
}

const HexUnitRegistry::Unit* HexUnitRegistry::find(std::int32_t unitId) const noexcept {
    if (unitId < 0 || unitId >= static_cast<std::int32_t>(units_.size())) return nullptr;
    return &units_[static_cast<std::size_t>(unitId)];
}

std::int32_t HexUnitRegistry::unitIdAt(HexCoordinates coordinates) const noexcept {
    // Ids are positional, and `Unit::location` caches the occupied coordinates so
    // this lookup needs no map.
    for (std::size_t index = 0; index < units_.size(); ++index) {
        if (units_[index].location == coordinates) return static_cast<std::int32_t>(index);
    }
    return -1;
}

HexOccupancyQuery HexUnitRegistry::occupancyQuery() const {
    // The returned callable borrows `*this` and reads the cached occupied cells, so
    // it stays valid exactly as long as this registry does; see the ownership note
    // on `HexUnitRegistry::occupancyQuery`.
    return HexOccupancyQuery{[this](HexCoordinates coordinates) { return isOccupied(coordinates); }};
}

// --- adding and removing ----------------------------------------------------

Result<std::int32_t> HexUnitRegistry::addUnit(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                              HexCoordinates location, float orientation) {
    if (map.empty()) return Result<std::int32_t>::failure(invalidArgument("cannot add a unit to an empty map"));
    if (!map.contains(location))
        return Result<std::int32_t>::failure(invalidArgument("unit location is outside the hex map"));
    if (isOccupied(location))
        return Result<std::int32_t>::failure(invalidArgument("unit location is already occupied"));

    Unit unit;
    unit.locationIndex = map.indexOf(location);
    unit.location      = location;
    unit.orientation   = orientation;
    unit.visionIndex   = unit.locationIndex;
    units_.push_back(std::move(unit));

    // The id is the unit's position in the registry, so it stays valid until a
    // removal shifts the ids that follow it.
    const std::int32_t id      = static_cast<std::int32_t>(units_.size()) - 1;
    auto               granted = visibility.increase(map, scratch, location, tuning_.visionRange);
    if (!granted.ok()) {
        units_.pop_back();
        return Result<std::int32_t>::failure(granted.status());
    }
    return Result<std::int32_t>::success(id);
}

Result<void> HexUnitRegistry::removeUnit(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                         std::int32_t unitId) {
    const Unit* unit = find(unitId);
    if (unit == nullptr) return Result<void>::failure(notFound("unknown unit id"));

    // Vision is always granted from `visionIndex`; while a unit travels that is the
    // cell it walks through, not the destination cell `locationIndex` reserves.
    const HexCoordinates viewer    = map.coordinatesAt(unit->visionIndex);
    auto                 withdrawn = visibility.decrease(map, scratch, viewer, tuning_.visionRange);
    if (!withdrawn.ok()) return Result<void>::failure(withdrawn.status());

    // Ids are positional: every unit after `unitId` shifts down by one, so callers
    // must re-read `sample`/`snapshot` instead of reusing ids captured earlier.
    units_.erase(units_.begin() + static_cast<std::ptrdiff_t>(unitId));
    return Result<void>::success();
}

void HexUnitRegistry::removeAll(HexVisibility& visibility) noexcept {
    units_.clear();
    visibility.clear();
}

// --- snapshots ---------------------------------------------------------------

Result<HexUnitSample> HexUnitRegistry::makeSample(const HexMap& map, std::int32_t unitId, const Unit& unit) const {
    HexUnitSample sample;
    sample.id          = unitId;
    sample.location    = unit.location;
    sample.orientation = unit.orientation;
    sample.traveling   = unit.traveling;

    if (unit.locationIndex < 0 || unit.locationIndex >= map.cellCount()) {
        // The unit's cell is no longer part of the map; `refreshPositions` is what
        // takes such a unit off the grid. Until then its position stays at origin.
        return Result<HexUnitSample>::success(sample);
    }
    if (!unit.traveling || unit.path.size() < 2) {
        sample.position = map.cellPosition(unit.location);
        return Result<HexUnitSample>::success(sample);
    }

    const TravelSegment segment = travelSegment(map, unit.path, unit.segment);
    sample.travelFrom           = segment.from;
    sample.position             = bezierPoint(segment.a, segment.b, segment.c, unit.t);
    return Result<HexUnitSample>::success(sample);
}

// --- travel ------------------------------------------------------------------

Result<void> HexUnitRegistry::beginTravel(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                          std::int32_t unitId, const std::vector<std::int32_t>& path) {
    Unit* unit = find(unitId);
    if (unit == nullptr) return Result<void>::failure(invalidArgument("unknown unit id"));

    if (path.empty()) return Result<void>::failure(invalidArgument("travel path must not be empty"));
    if (path.front() != unit->locationIndex)
        return Result<void>::failure(invalidArgument("travel path must start on the unit's cell"));

    const std::int32_t cellCount = map.cellCount();
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] < 0 || path[index] >= cellCount)
            return Result<void>::failure(invalidArgument("travel path leaves the hex map"));
        if (index > 0 && map.coordinatesAt(path[index - 1]).distanceTo(map.coordinatesAt(path[index])) != 1)
            return Result<void>::failure(invalidArgument("travel path steps between non-adjacent cells"));
    }

    const std::int32_t   destinationIndex = path.back();
    const HexCoordinates destination      = map.coordinatesAt(destinationIndex);
    // The unit reserves the destination itself, so the reachability test must run
    // without this registry's own occupancy predicate.
    if (!isValidDestination(map, destination, {}))
        return Result<void>::failure(invalidArgument("travel destination is not a valid destination"));

    if (path.size() <= 1) return Result<void>::success();

    const HexCoordinates origin    = unit->location;
    const HexCoordinates firstStep = map.coordinatesAt(path[1]);

    // Visibility moves before the unit does, so a failed sweep leaves the unit
    // exactly where it was instead of half-way into a travel plan.
    auto withdrawn = visibility.decrease(map, scratch, origin, tuning_.visionRange);
    if (!withdrawn.ok()) return Result<void>::failure(withdrawn.status());
    auto granted = visibility.increase(map, scratch, firstStep, tuning_.visionRange);
    if (!granted.ok()) {
        visibility.increase(map, scratch, origin, tuning_.visionRange)
            .ignore("restore the origin viewer after a failed travel start");
        return Result<void>::failure(granted.status());
    }

    unit->path.assign(path.begin(), path.end());
    unit->segment       = 1;
    unit->t             = 0.f;
    unit->traveling     = true;
    unit->locationIndex = destinationIndex;
    unit->location      = destination;
    unit->visionIndex   = path[1];
    return Result<void>::success();
}

Result<HexUnitSample> HexUnitRegistry::advance(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                               std::int32_t unitId, float dt) {
    Unit* unit = find(unitId);
    if (unit == nullptr) return Result<HexUnitSample>::failure(notFound("unknown unit id"));
    if (!unit->traveling) return makeSample(map, unitId, *unit);

    const float step = dt > 0.f ? dt : 0.f;
    unit->t += step * tuning_.travelSpeed;

    const std::int32_t lastSegment = static_cast<std::int32_t>(unit->path.size()) - 1;
    while (unit->t >= 1.f && unit->segment < lastSegment) {
        unit->t -= 1.f;
        ++unit->segment;
        const HexCoordinates left      = map.coordinatesAt(unit->path[static_cast<std::size_t>(unit->segment - 1)]);
        const HexCoordinates right     = map.coordinatesAt(unit->path[static_cast<std::size_t>(unit->segment)]);
        auto                 withdrawn = visibility.decrease(map, scratch, left, tuning_.visionRange);
        if (!withdrawn.ok()) return Result<HexUnitSample>::failure(withdrawn.status());
        auto granted = visibility.increase(map, scratch, right, tuning_.visionRange);
        if (!granted.ok()) return Result<HexUnitSample>::failure(granted.status());
        unit->visionIndex = unit->path[static_cast<std::size_t>(unit->segment)];
    }
    if (unit->t >= 1.f) {
        // The final segment is consumed: the unit stands on its reserved destination.
        unit->traveling   = false;
        unit->t           = 0.f;
        unit->segment     = 1;
        unit->visionIndex = unit->locationIndex;
        unit->path.clear();
        return makeSample(map, unitId, *unit);
    }

    if (dt > 0.f) {
        const TravelSegment segment  = travelSegment(map, unit->path, unit->segment);
        const HexVec3       velocity = bezierDerivative(segment.a, segment.b, segment.c, unit->t);
        if (velocity.x * velocity.x + velocity.z * velocity.z > 1e-8f) {
            const float heading = yawFromDirection(velocity);
            const float turn    = tuning_.rotationSpeed * dt;
            const float delta   = angularDifference(unit->orientation, heading);
            const float applied = delta > turn ? turn : (delta < -turn ? -turn : delta);
            unit->orientation   = wrapDegrees(unit->orientation + applied);
        }
    }
    return makeSample(map, unitId, *unit);
}

void HexUnitRegistry::advanceAll(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch, float dt) {
    const std::int32_t count = unitCount();
    for (std::int32_t unitId = 0; unitId < count; ++unitId) {
        // Every cell a unit can step onto was validated by `beginTravel`, so the
        // per-unit vision sweeps cannot fail here; `advance` reports a failure
        // rather than continuing past it.
        advance(map, visibility, scratch, unitId, dt).ignore("travel cells are validated by beginTravel");
    }
}

// --- bulk state --------------------------------------------------------------

void HexUnitRegistry::refreshVisibility(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch) {
    visibility.clear();
    for (const Unit& unit : units_) {
        // The viewer cell is `visionIndex`, the cell whose fog of war this unit
        // currently holds; an idle unit's `visionIndex` is its occupied cell.
        visibility.increase(map, scratch, map.coordinatesAt(unit.visionIndex), tuning_.visionRange)
            .ignore("unit cells are validated when a unit is added or restored");
    }
}

void HexUnitRegistry::refreshPositions(const HexMap& map) noexcept {
    // Positions are derived from the map on demand, so an idle unit picks up a new
    // elevation or perturbation without any write here. What this pass does check
    // is that the cached cell still exists: a unit outside the grid is taken off
    // the grid so `unitIdAt` stops reporting it.
    const std::int32_t cellCount = map.cellCount();
    for (Unit& unit : units_) {
        if (unit.locationIndex >= 0 && unit.locationIndex < cellCount) continue;
        unit.locationIndex = -1;
        unit.location      = HexCoordinates{};
        unit.visionIndex   = -1;
        unit.traveling     = false;
        unit.segment       = 1;
        unit.t             = 0.f;
        unit.path.clear();
    }
}

std::vector<HexUnitState> HexUnitRegistry::snapshot() const {
    std::vector<HexUnitState> states;
    states.reserve(units_.size());
    for (const Unit& unit : units_) {
        HexUnitState state;
        state.locationIndex = unit.locationIndex;
        state.orientation   = unit.orientation;
        states.push_back(state);
    }
    return states;
}

Result<void> HexUnitRegistry::restore(HexMap& map, HexVisibility& visibility, HexSearchContext& scratch,
                                      const std::vector<HexUnitState>& states) {
    // The registry is cleared before validation, which is why a rejected payload
    // leaves it empty instead of partially populated: there is no half-restored
    // registry any caller could observe. The map's explored latches stay latched
    // because those are one-way by design.
    units_.clear();
    visibility.clear();

    const std::int32_t        cellCount = map.cellCount();
    std::vector<std::int32_t> claimed;
    claimed.reserve(states.size());
    for (const HexUnitState& state : states) {
        if (state.locationIndex < 0 || state.locationIndex >= cellCount)
            return Result<void>::failure(invalidArgument("restored unit cell is outside the hex map"));
        if (std::find(claimed.begin(), claimed.end(), state.locationIndex) != claimed.end())
            return Result<void>::failure(invalidArgument("restored units occupy the same cell"));
        if (!isValidDestination(map, map.coordinatesAt(state.locationIndex), {}))
            return Result<void>::failure(invalidArgument("restored unit cell is not a valid destination"));
        claimed.push_back(state.locationIndex);
    }

    units_.reserve(states.size());
    for (const HexUnitState& state : states) {
        Unit unit;
        unit.locationIndex = state.locationIndex;
        unit.location      = map.coordinatesAt(state.locationIndex);
        unit.orientation   = state.orientation;
        unit.visionIndex   = state.locationIndex;
        units_.push_back(std::move(unit));
    }
    refreshVisibility(map, visibility, scratch);
    return Result<void>::success();
}

}  // namespace eve::hexmap
