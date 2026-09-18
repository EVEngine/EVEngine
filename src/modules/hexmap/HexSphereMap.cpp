#include "hexmap/HexSphereMap.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace eve::hexmap {
namespace {

/** @brief Builds the module's standard "bad argument" void failure. */
[[nodiscard]] Result<void> invalidArgument(std::string message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap.sphere"));
}

[[nodiscard]] constexpr std::int32_t clampInt(std::int32_t value, std::int32_t low, std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

/** @brief Maps a brush feature index onto its packed layer; anything but 0/1 is plants. */
[[nodiscard]] constexpr std::int32_t layerIndex(std::int32_t feature) noexcept {
    return feature == 0 ? 0 : (feature == 1 ? 1 : 2);
}

[[nodiscard]] float dot3(HexVec3 a, HexVec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

[[nodiscard]] float length3(HexVec3 a) noexcept { return std::sqrt(dot3(a, a)); }

/**
 * @brief Intersection span of a ray with a sphere centred on the origin.
 *
 * @return False when the ray misses the sphere entirely or the whole sphere lies
 *         behind the origin; otherwise the entry and exit parameters, which may
 *         be negative when the origin is inside the sphere.
 */
[[nodiscard]] bool raySphereSpan(HexVec3 origin, HexVec3 direction, float radius, float& enter,
                                 float& exit) noexcept {
    const float b            = dot3(origin, direction);
    const float c            = dot3(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.f) return false;
    const float root = std::sqrt(discriminant);
    enter            = -b - root;
    exit             = -b + root;
    return exit > 0.f;
}

}  // namespace

// --- construction -----------------------------------------------------------

Result<void> HexSphereMap::reset(std::int32_t subdivision, float radius, std::uint32_t seed) {
    if (subdivision < 0 || subdivision > kMaxHexSphereSubdivision)
        return invalidArgument("subdivision level is out of range");
    if (!std::isfinite(radius) || radius <= 0.f) return invalidArgument("sphere radius must be finite and positive");

    auto built = HexSphereTopology::build(subdivision);
    if (!built.ok()) return Result<void>::failure(built.status());

    topology_      = std::move(built).takeValue();
    sphereRadius_  = radius;
    elevationStep_ = radius * kDefaultElevationRatio;
    noise_.reset(seed);
    cells_.assign(static_cast<std::size_t>(topology_.cellCount()), HexCellData{});
    cellDirty_.assign(cells_.size(), 0u);
    dirtyQueue_.clear();
    dirtyHead_ = 0;
    revision_  = 0;
    markAllDirty();
    return Result<void>::success();
}

void HexSphereMap::setElevationStep(float step) noexcept {
    elevationStep_ = step;
    ++revision_;
}

// --- geometry ---------------------------------------------------------------

float HexSphereMap::surfaceRadius(HexSphereCell cell) const noexcept {
    if (!contains(cell)) return sphereRadius_;
    return sphereRadius_ + static_cast<float>(elevation(cell)) * elevationStep_;
}

float HexSphereMap::cellSpacing() const noexcept {
    const std::int32_t count = cellCount();
    if (count < 2) return 0.f;
    // Mean area per cell is `4 * pi / count` of the sphere, so the mean spacing of a
    // hexagonal packing of that area is the square root of it.
    return sphereRadius_ * std::sqrt(4.f * 3.14159265358979f / static_cast<float>(count));
}

HexVec3 HexSphereMap::cellPosition(HexSphereCell cell) const noexcept {
    return topology_.direction(cell) * surfaceRadius(cell);
}

HexVec3 HexSphereMap::cellGroundPosition(HexSphereCell cell) const noexcept {
    return topology_.direction(cell) * sphereRadius_;
}

HexEdgeType HexSphereMap::edgeTypeTo(HexSphereCell a, HexSphereCell b) const noexcept {
    if (!contains(a) || !contains(b)) return HexEdgeType::Flat;
    if (topology_.directionOf(a, b) < 0) return HexEdgeType::Flat;
    return edgeType(elevation(a), elevation(b));
}

// --- cell access ------------------------------------------------------------

const HexCellData* HexSphereMap::cellAt(HexSphereCell cell) const noexcept {
    if (!contains(cell)) return nullptr;
    return &cells_[static_cast<std::size_t>(cell)];
}

HexCellData* HexSphereMap::mutableCell(HexSphereCell cell) noexcept {
    if (!contains(cell)) return nullptr;
    return &cells_[static_cast<std::size_t>(cell)];
}

std::int32_t HexSphereMap::elevation(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.elevation() : 0;
}
std::int32_t HexSphereMap::waterLevel(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.waterLevel() : 0;
}
std::int32_t HexSphereMap::terrainType(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.terrainType() : 0;
}
std::int32_t HexSphereMap::urbanLevel(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.urbanLevel() : 0;
}
std::int32_t HexSphereMap::farmLevel(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.farmLevel() : 0;
}
std::int32_t HexSphereMap::plantLevel(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.plantLevel() : 0;
}
std::int32_t HexSphereMap::specialIndex(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values.specialIndex() : 0;
}
bool HexSphereMap::isUnderwater(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->values.isUnderwater();
}
bool HexSphereMap::hasRiver(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->flags.hasRiver();
}
bool HexSphereMap::hasRoad(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->flags.hasRoad();
}
bool HexSphereMap::hasRiverThrough(HexSphereCell c, std::int32_t direction) const noexcept {
    const HexCellData* data = cellAt(c);
    if (!data || direction < 0 || direction >= kHexDirectionCount) return false;
    return data->flags.hasRiverThrough(static_cast<HexDirection>(direction));
}
bool HexSphereMap::isWalled(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->flags.isWalled();
}
bool HexSphereMap::isExplored(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->flags.isExplored();
}
bool HexSphereMap::isExplorable(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data && data->flags.isExplorable();
}
HexValues HexSphereMap::values(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->values : HexValues{};
}
HexFlags HexSphereMap::flags(HexSphereCell c) const noexcept {
    const HexCellData* data = cellAt(c);
    return data ? data->flags : HexFlags{};
}

// --- picking ----------------------------------------------------------------

Result<HexSphereCell> HexSphereMap::pickCell(HexVec3 rayOrigin, HexVec3 rayDirection) const {
    if (empty())
        return Result<HexSphereCell>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "hex sphere map is empty", "hexmap.sphere.pick"));
    const float directionLength = length3(rayDirection);
    if (!(directionLength > 1e-6f))
        return Result<HexSphereCell>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "ray direction must be non-zero",
                                                                "hexmap.sphere.pick"));
    const HexVec3 direction = rayDirection * (1.f / directionLength);

    // The surface is a step function of direction, so the ray is marched against
    // the bounding sphere and then bisected. Intersecting a single sphere and
    // refining would return the far-side cell whenever the camera sits between
    // the surface and that bounding sphere, which is exactly the low-orbit case
    // this map exists for.
    const float boundingRadius =
        sphereRadius_ + static_cast<float>(HexMetrics::kMaxElevation) * elevationStep_;
    float enter = 0.f;
    float exit  = 0.f;
    if (!raySphereSpan(rayOrigin, direction, boundingRadius, enter, exit))
        return Result<HexSphereCell>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "ray misses the hex planet", "hexmap.sphere.pick"));

    const float marchStep = std::max(boundingRadius * 0.01f, 1e-4f);
    float       tInside   = 0.f;
    HexSphereCell hit     = kNoHexSphereCell;
    for (float t = std::max(0.f, enter); t <= exit + marchStep; t += marchStep) {
        const HexVec3        point = rayOrigin + direction * t;
        const float          span  = length3(point);
        if (span <= 1e-6f) continue;
        const HexSphereCell  cell  = topology_.cellAt(point);
        if (cell == kNoHexSphereCell) continue;
        if (span <= surfaceRadius(cell)) {
            hit     = cell;
            tInside = t;
            break;
        }
    }
    if (hit == kNoHexSphereCell)
        return Result<HexSphereCell>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "ray never reached the hex planet surface", "hexmap.sphere.pick"));

    // Bisect between the last sample above the surface and the first one below it.
    float tOutside = std::max(std::max(0.f, enter), tInside - marchStep);
    for (std::int32_t iteration = 0; iteration < 24; ++iteration) {
        const float         middle = 0.5f * (tOutside + tInside);
        const HexVec3       point  = rayOrigin + direction * middle;
        const float         span   = length3(point);
        if (span <= 1e-6f) break;
        const HexSphereCell cell = topology_.cellAt(point);
        if (cell == kNoHexSphereCell) break;
        if (span <= surfaceRadius(cell)) {
            hit      = cell;
            tInside  = middle;
        } else {
            tOutside = middle;
        }
    }
    return Result<HexSphereCell>::success(hit);
}

// --- neighbourhood queries --------------------------------------------------

std::int32_t HexSphereMap::distance(HexSphereCell a, HexSphereCell b) const {
    if (!contains(a) || !contains(b)) return kNoHexSphereCell;
    if (a == b) return 0;

    std::vector<std::uint8_t>  visited(cells_.size(), 0u);
    std::vector<HexSphereCell> frontier;
    std::vector<HexSphereCell> next;
    frontier.push_back(a);
    visited[static_cast<std::size_t>(a)] = 1u;

    std::int32_t depth = 0;
    while (!frontier.empty()) {
        ++depth;
        next.clear();
        for (const HexSphereCell cell : frontier) {
            const std::int32_t count = topology_.neighborCount(cell);
            for (std::int32_t d = 0; d < count; ++d) {
                const HexSphereCell neighbour = topology_.neighbor(cell, d);
                if (neighbour == kNoHexSphereCell) continue;
                if (visited[static_cast<std::size_t>(neighbour)]) continue;
                if (neighbour == b) return depth;
                visited[static_cast<std::size_t>(neighbour)] = 1u;
                next.push_back(neighbour);
            }
        }
        frontier.swap(next);
    }
    // The neighbour graph of a sphere is connected, so this is unreachable.
    return kNoHexSphereCell;
}

void HexSphereMap::collectBrush(HexSphereCell center, std::int32_t radius,
                                std::vector<HexSphereCell>& out) const {
    out.clear();
    if (!contains(center) || radius < 0) return;

    std::vector<std::uint8_t> visited(cells_.size(), 0u);
    out.push_back(center);
    visited[static_cast<std::size_t>(center)] = 1u;

    std::size_t head = 0;
    for (std::int32_t step = 0; step < radius; ++step) {
        const std::size_t levelEnd = out.size();
        for (; head < levelEnd; ++head) {
            const HexSphereCell cell  = out[head];
            const std::int32_t  count = topology_.neighborCount(cell);
            for (std::int32_t d = 0; d < count; ++d) {
                const HexSphereCell neighbour = topology_.neighbor(cell, d);
                if (neighbour == kNoHexSphereCell) continue;
                if (visited[static_cast<std::size_t>(neighbour)]) continue;
                visited[static_cast<std::size_t>(neighbour)] = 1u;
                out.push_back(neighbour);
            }
        }
    }
}

// --- dirty tracking ---------------------------------------------------------

void HexSphereMap::markCellDirty(HexSphereCell cell) noexcept {
    if (!contains(cell)) return;
    if (cellDirty_[static_cast<std::size_t>(cell)]) return;
    cellDirty_[static_cast<std::size_t>(cell)] = 1u;
    dirtyQueue_.push_back(cell);
}

void HexSphereMap::markCellDirtyAndNeighbors(HexSphereCell cell) noexcept {
    if (!contains(cell)) return;
    markCellDirty(cell);
    const std::int32_t count = topology_.neighborCount(cell);
    for (std::int32_t d = 0; d < count; ++d) markCellDirty(topology_.neighbor(cell, d));
}

void HexSphereMap::markAllDirty() noexcept {
    dirtyQueue_.clear();
    dirtyHead_ = 0;
    cellDirty_.assign(cells_.size(), 0u);
    dirtyQueue_.reserve(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        cellDirty_[i] = 1u;
        dirtyQueue_.push_back(static_cast<HexSphereCell>(i));
    }
}

std::int32_t HexSphereMap::takeDirtyCell() noexcept {
    while (dirtyHead_ < dirtyQueue_.size()) {
        const HexSphereCell cell = dirtyQueue_[dirtyHead_++];
        if (!contains(cell)) continue;
        if (!cellDirty_[static_cast<std::size_t>(cell)]) continue;
        cellDirty_[static_cast<std::size_t>(cell)] = 0u;
        if (dirtyHead_ >= 4096u && dirtyHead_ * 2u >= dirtyQueue_.size()) {
            dirtyQueue_.erase(dirtyQueue_.begin(),
                              dirtyQueue_.begin() + static_cast<std::ptrdiff_t>(dirtyHead_));
            dirtyHead_ = 0u;
        }
        return cell;
    }
    dirtyQueue_.clear();
    dirtyHead_ = 0u;
    return kNoHexSphereCell;
}

// --- cell authoring ---------------------------------------------------------

void HexSphereMap::refreshCellDependents(HexSphereCell cell) noexcept {
    ++revision_;
    markCellDirtyAndNeighbors(cell);
}

void HexSphereMap::validateRivers(HexSphereCell cell) noexcept {
    const HexCellData* data = cellAt(cell);
    if (!data || !data->flags.hasRiver()) return;
    // A river may only leave a cell that is at least as high as its neighbour,
    // unless the cell itself is a lake at exactly its own elevation.
    const std::int32_t fromElevation = elevation(cell);
    const std::int32_t fromWater     = waterLevel(cell);
    const std::int32_t count         = neighborCount(cell);
    for (std::int32_t d = 0; d < count; ++d) {
        if (!data->flags.hasRiverOut(static_cast<HexDirection>(d))) continue;
        const HexSphereCell neighbour = topology_.neighbor(cell, d);
        if (neighbour == kNoHexSphereCell) continue;
        const bool canFlow = fromElevation >= elevation(neighbour) || fromWater == fromElevation;
        if (!canFlow) {
            removeRiver(cell).ignore("river validation must keep the map consistent");
            return;
        }
    }
}

Result<void> HexSphereMap::setElevation(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    const std::int32_t clamped = clampInt(value, HexMetrics::kMinElevation, HexMetrics::kMaxElevation);
    if (data->values.elevation() == clamped) return Result<void>::success();
    data->values = data->values.withElevation(clamped);

    // Roads cannot span a difference of more than one elevation step.
    bool               hasInvalidRoad = false;
    const std::int32_t count          = neighborCount(c);
    for (std::int32_t d = 0; d < count && !hasInvalidRoad; ++d) {
        if (!data->flags.hasRoad(static_cast<HexDirection>(d))) continue;
        const HexSphereCell neighbour = topology_.neighbor(c, d);
        if (neighbour == kNoHexSphereCell) continue;
        const std::int32_t delta = elevation(neighbour) - clamped;
        if (delta > 1 || delta < -1) hasInvalidRoad = true;
    }
    if (hasInvalidRoad) removeRoads(c).ignore("elevation change must drop invalid roads");
    refreshCellDependents(c);
    validateRivers(c);
    for (std::int32_t d = 0; d < count; ++d) {
        const HexSphereCell neighbour = topology_.neighbor(c, d);
        if (neighbour != kNoHexSphereCell) validateRivers(neighbour);
    }
    return Result<void>::success();
}

Result<void> HexSphereMap::setWaterLevel(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    const std::int32_t clamped = clampInt(value, 0, HexMetrics::kMaxElevation);
    if (data->values.waterLevel() == clamped) return Result<void>::success();
    data->values = data->values.withWaterLevel(clamped);
    refreshCellDependents(c);
    validateRivers(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setTerrainType(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->values = data->values.withTerrainType(clampTerrainType(value));
    ++revision_;
    return Result<void>::success();
}

Result<void> HexSphereMap::setUrbanLevel(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->values = data->values.withUrbanLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setFarmLevel(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->values = data->values.withFarmLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setPlantLevel(HexSphereCell c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->values = data->values.withPlantLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setSpecialIndex(HexSphereCell c, std::int32_t index) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    if (data->flags.hasRiver()) return Result<void>::success();
    data->values = data->values.withSpecialIndex(clampInt(index, 0, 3));
    removeRoads(c).ignore("special features replace roads");
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setWalled(HexSphereCell c, bool walled) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->flags = data->flags.withWalled(walled);
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setExplored(HexSphereCell c, bool explored) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->flags = data->flags.withExplored(explored);
    // Fog state changes do not alter geometry, so the cells stay clean: only the
    // revision is bumped so a renderer can tell that the visible set changed.
    ++revision_;
    return Result<void>::success();
}

Result<void> HexSphereMap::setExplorable(HexSphereCell c, bool explorable) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->flags = data->flags.withExplorable(explorable);
    ++revision_;
    return Result<void>::success();
}

Result<void> HexSphereMap::setCellState(HexSphereCell c, HexValues values, HexFlags flags) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    data->values = values;
    data->flags  = flags;
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::setOutgoingRiver(HexSphereCell c, std::int32_t direction) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    if (direction < 0 || direction >= neighborCount(c))
        return invalidArgument("river direction is not an edge of the cell");
    const HexSphereCell neighbour = topology_.neighbor(c, direction);
    if (neighbour == kNoHexSphereCell) return invalidArgument("river direction has no neighbour");
    const std::int32_t  back = topology_.oppositeDirection(c, direction);
    if (back < 0) return invalidArgument("river direction has no opposite edge");

    const auto out = static_cast<HexDirection>(direction);
    const auto in  = static_cast<HexDirection>(back);
    if (data->flags.hasAnyRiverOut() && data->flags.hasRiverOut(out)) return Result<void>::success();
    if (elevation(c) < elevation(neighbour) && waterLevel(c) != elevation(c))
        return invalidArgument("a river cannot flow uphill");

    // Drop only the links this call replaces, mirroring the reference cell's
    // `RemoveOutgoingRiver` + same-edge `RemoveIncomingRiver`. Calling the full
    // `removeRiver` here also erased the *incoming* link from the upstream cell,
    // so carving a channel step by step wiped the step behind it and only the
    // last edge survived.
    const std::int32_t count = neighborCount(c);
    for (std::int32_t d = 0; d < count; ++d) {
        const auto previous = static_cast<HexDirection>(d);
        if (!data->flags.hasRiverOut(previous)) continue;
        data->flags                        = data->flags.withoutRiverOut(previous);
        const HexSphereCell previousCell   = topology_.neighbor(c, d);
        const std::int32_t  previousBack   = topology_.oppositeDirection(c, d);
        if (previousCell != kNoHexSphereCell && previousBack >= 0) {
            HexCellData* previousData = mutableCell(previousCell);
            if (previousData) previousData->flags = previousData->flags.withoutRiverIn(static_cast<HexDirection>(previousBack));
            refreshCellDependents(previousCell);
        }
    }

    data = mutableCell(c);
    // A river may not leave and enter through the same edge.
    if (data->flags.hasRiverIn(out)) {
        data->flags = data->flags.withoutRiverIn(out);
        HexCellData* other = mutableCell(neighbour);
        if (other) other->flags = other->flags.withoutRiverOut(in);
    }

    data               = mutableCell(c);
    data->flags        = data->flags.withRiverOut(out).withoutRoad(out);
    data->values       = data->values.withSpecialIndex(0);
    HexCellData* other = mutableCell(neighbour);
    other->flags       = other->flags.withRiverIn(in).withoutRoad(in);
    other->values      = other->values.withSpecialIndex(0);
    refreshCellDependents(c);
    refreshCellDependents(neighbour);
    return Result<void>::success();
}

Result<void> HexSphereMap::removeRiver(HexSphereCell c) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    if (!data->flags.hasRiver()) return Result<void>::success();
    for (std::int32_t d = 0; d < kHexDirectionCount; ++d) {
        const auto direction = static_cast<HexDirection>(d);
        data->flags          = data->flags.withoutRiverIn(direction).withoutRiverOut(direction);
    }
    const std::int32_t count = neighborCount(c);
    for (std::int32_t d = 0; d < count; ++d) {
        const HexSphereCell neighbour = topology_.neighbor(c, d);
        const std::int32_t  back      = topology_.oppositeDirection(c, d);
        if (neighbour == kNoHexSphereCell || back < 0) continue;
        const auto   in    = static_cast<HexDirection>(back);
        HexCellData* other = mutableCell(neighbour);
        if (other) other->flags = other->flags.withoutRiverIn(in).withoutRiverOut(in);
        refreshCellDependents(neighbour);
    }
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexSphereMap::addRoad(HexSphereCell c, std::int32_t direction) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    if (direction < 0 || direction >= neighborCount(c))
        return invalidArgument("road direction is not an edge of the cell");
    const HexSphereCell neighbour = topology_.neighbor(c, direction);
    if (neighbour == kNoHexSphereCell) return invalidArgument("road direction has no neighbour");
    const std::int32_t back = topology_.oppositeDirection(c, direction);
    if (back < 0) return invalidArgument("road direction has no opposite edge");

    const auto out = static_cast<HexDirection>(direction);
    const auto in  = static_cast<HexDirection>(back);
    if (data->flags.hasRoad(out)) return Result<void>::success();
    if (data->flags.hasRiverThrough(out)) return Result<void>::success();
    if (data->flags.hasRiver() && !data->flags.hasRiverBeginOrEnd()) {
        // A road may only join a river at a begin or end cell.
        return Result<void>::success();
    }
    HexCellData* other = mutableCell(neighbour);
    if (other->values.specialIndex() != 0 || data->values.specialIndex() != 0) return Result<void>::success();
    if (other->flags.hasRiverThrough(in)) return Result<void>::success();
    const std::int32_t delta = elevation(neighbour) - elevation(c);
    if (delta > 1 || delta < -1) return Result<void>::success();

    data->flags  = data->flags.withRoad(out);
    other->flags = other->flags.withRoad(in);
    refreshCellDependents(c);
    refreshCellDependents(neighbour);
    return Result<void>::success();
}

Result<void> HexSphereMap::removeRoads(HexSphereCell c) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex sphere map");
    if (!data->flags.hasRoad()) return Result<void>::success();
    data->flags              = data->flags.without(HexFlags::roadMask());
    const std::int32_t count = neighborCount(c);
    for (std::int32_t d = 0; d < count; ++d) {
        const HexSphereCell neighbour = topology_.neighbor(c, d);
        const std::int32_t  back      = topology_.oppositeDirection(c, d);
        if (neighbour == kNoHexSphereCell || back < 0) continue;
        HexCellData* other = mutableCell(neighbour);
        if (other) other->flags = other->flags.withoutRoad(static_cast<HexDirection>(back));
        refreshCellDependents(neighbour);
    }
    refreshCellDependents(c);
    return Result<void>::success();
}

// --- brush authoring --------------------------------------------------------

Result<void> HexSphereMap::editElevation(HexSphereCell center, std::int32_t radius, std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex sphere map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<HexSphereCell> cells;
    collectBrush(center, radius, cells);
    for (const HexSphereCell cell : cells) {
        const std::int32_t next = elevation(cell) + delta;
        setElevation(cell, next).ignore("brush elevation edit clamps per cell");
    }
    return Result<void>::success();
}

Result<void> HexSphereMap::editWaterLevel(HexSphereCell center, std::int32_t radius, std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex sphere map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<HexSphereCell> cells;
    collectBrush(center, radius, cells);
    for (const HexSphereCell cell : cells)
        setWaterLevel(cell, waterLevel(cell) + delta).ignore("brush water edit clamps per cell");
    return Result<void>::success();
}

Result<void> HexSphereMap::editTerrainType(HexSphereCell center, std::int32_t radius, std::int32_t terrainType) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex sphere map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<HexSphereCell> cells;
    collectBrush(center, radius, cells);
    for (const HexSphereCell cell : cells)
        setTerrainType(cell, terrainType).ignore("brush terrain edit clamps per cell");
    return Result<void>::success();
}

Result<void> HexSphereMap::editFeatureLevel(HexSphereCell center, std::int32_t radius, std::int32_t feature,
                                            std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex sphere map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<HexSphereCell> cells;
    collectBrush(center, radius, cells);
    const std::int32_t layer = layerIndex(feature);
    for (const HexSphereCell cell : cells) {
        const HexCellData* data = cellAt(cell);
        if (!data || data->flags.hasRiver()) continue;
        if (data->values.specialIndex() != 0) continue;
        const std::int32_t current = layer == 0   ? data->values.urbanLevel()
                                     : layer == 1 ? data->values.farmLevel()
                                                  : data->values.plantLevel();
        const std::int32_t next    = clampInt(current + delta, 0, 3);
        if (layer == 0)
            setUrbanLevel(cell, next).ignore("brush urban edit clamps per cell");
        else if (layer == 1)
            setFarmLevel(cell, next).ignore("brush farm edit clamps per cell");
        else
            setPlantLevel(cell, next).ignore("brush plant edit clamps per cell");
    }
    return Result<void>::success();
}

}  // namespace eve::hexmap
