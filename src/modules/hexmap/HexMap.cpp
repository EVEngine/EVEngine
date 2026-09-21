#include "hexmap/HexMap.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::hexmap {
namespace {

[[nodiscard]] Result<void> invalidArgument(std::string message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap"));
}

[[nodiscard]] std::int32_t clampInt(std::int32_t value, std::int32_t low, std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

[[nodiscard]] std::int32_t layerIndex(std::int32_t feature) noexcept { return clampInt(feature, 0, 2); }

/**
 * @brief `value + delta` with saturation instead of signed overflow.
 *
 * `delta` reaches this file from script bindings, so `elevation(c) + delta` used to
 * overflow int32 for a large delta - undefined behaviour that, with the wrap both
 * compilers emit here, turned a "raise this cell" brush into a lowering one.
 */
[[nodiscard]] std::int32_t saturatingAdd(std::int32_t value, std::int32_t delta) noexcept {
    const std::int64_t sum = static_cast<std::int64_t>(value) + static_cast<std::int64_t>(delta);
    if (sum < std::numeric_limits<std::int32_t>::min()) return std::numeric_limits<std::int32_t>::min();
    if (sum > std::numeric_limits<std::int32_t>::max()) return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(sum);
}

}  // namespace

// --- construction -----------------------------------------------------------

Result<void> HexMap::reset(std::int32_t cellCountX, std::int32_t cellCountZ, std::uint32_t seed) {
    if (cellCountX <= 0 || cellCountZ <= 0) return invalidArgument("hex map size must be positive");
    if (cellCountX % HexMetrics::kChunkSizeX != 0 || cellCountZ % HexMetrics::kChunkSizeZ != 0)
        return invalidArgument("hex map size must be a multiple of the 5x5 chunk size");

    cellCountX_  = cellCountX;
    cellCountZ_  = cellCountZ;
    chunkCountX_ = cellCountX / HexMetrics::kChunkSizeX;
    chunkCountZ_ = cellCountZ / HexMetrics::kChunkSizeZ;
    // `HexValues` stores the elevation biased by +15, so a zeroed record would read
    // back as -15. Every cell therefore starts explicitly at elevation 0 and water 0.
    // Every cell also starts *explorable but unexplored*, which is the fog-of-war
    // precondition of the reference project: a viewer may reveal it, nothing has yet.
    HexCellData initial{};
    initial.values = initial.values.withElevation(0).withWaterLevel(0);
    initial.flags  = initial.flags.withExplorable(true).withExplored(false);
    cells_.assign(static_cast<std::size_t>(cellCountX_) * static_cast<std::size_t>(cellCountZ_), initial);
    chunkDirty_.assign(static_cast<std::size_t>(chunkCount()), 0u);
    dirtyQueue_.clear();
    noise_.reset(seed);
    revision_ = 0;
    markAllChunksDirty();
    return Result<void>::success();
}

// --- topology ---------------------------------------------------------------

std::int32_t HexMap::indexOf(HexCoordinates coordinates) const noexcept {
    if (!contains(coordinates)) return -1;
    return coordinates.offsetX() + coordinates.z * cellCountX_;
}

HexCoordinates HexMap::coordinatesAt(std::int32_t index) const noexcept {
    if (index < 0 || index >= cellCount()) return HexCoordinates{};
    return HexCoordinates::fromOffset(index % cellCountX_, index / cellCountX_);
}

const HexCellData* HexMap::cellAt(std::int32_t index) const noexcept {
    if (index < 0 || index >= cellCount()) return nullptr;
    return &cells_[static_cast<std::size_t>(index)];
}

const HexCellData* HexMap::cell(HexCoordinates coordinates) const noexcept { return cellAt(indexOf(coordinates)); }

HexCellData* HexMap::mutableCell(HexCoordinates coordinates) noexcept {
    const std::int32_t index = indexOf(coordinates);
    if (index < 0) return nullptr;
    return &cells_[static_cast<std::size_t>(index)];
}

bool HexMap::getNeighbor(HexCoordinates coordinates, HexDirection direction, HexCoordinates& out) const noexcept {
    const HexCoordinates candidate = coordinates.step(direction);
    if (!contains(candidate)) return false;
    out = candidate;
    return true;
}

HexEdgeType HexMap::edgeTypeTo(HexCoordinates a, HexCoordinates b) const noexcept {
    if (!contains(a) || !contains(b)) return HexEdgeType::Flat;
    if (a.distanceTo(b) != 1) return HexEdgeType::Flat;
    return edgeType(elevation(a), elevation(b));
}

// --- chunking ---------------------------------------------------------------

std::int32_t HexMap::chunkIndexOf(HexCoordinates coordinates) const noexcept {
    if (!contains(coordinates)) return -1;
    return coordinates.chunkColumn() + coordinates.chunkRow() * chunkCountX_;
}

std::int32_t HexMap::chunkColumnOf(std::int32_t chunkIndex) const noexcept {
    return chunkCountX_ > 0 ? chunkIndex % chunkCountX_ : 0;
}

std::int32_t HexMap::chunkRowOf(std::int32_t chunkIndex) const noexcept {
    return chunkCountX_ > 0 ? chunkIndex / chunkCountX_ : 0;
}

HexCoordinates HexMap::chunkCell(std::int32_t chunkIndex, std::int32_t column, std::int32_t row) const noexcept {
    const std::int32_t offsetX = chunkColumnOf(chunkIndex) * HexMetrics::kChunkSizeX + column;
    const std::int32_t offsetZ = chunkRowOf(chunkIndex) * HexMetrics::kChunkSizeZ + row;
    return HexCoordinates::fromOffset(offsetX, offsetZ);
}

HexVec3 HexMap::chunkCenter(std::int32_t chunkIndex) const noexcept {
    if (chunkIndex < 0 || chunkIndex >= chunkCount()) return HexVec3{};
    const HexVec3 a = cellPosition(chunkCell(chunkIndex, 0, 0));
    const HexVec3 b = cellPosition(chunkCell(chunkIndex, HexMetrics::kChunkSizeX - 1, HexMetrics::kChunkSizeZ - 1));
    return HexVec3{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
}

void HexMap::markChunkDirty(std::int32_t chunkIndex) noexcept {
    if (chunkIndex < 0 || chunkIndex >= chunkCount()) return;
    auto& flag = chunkDirty_[static_cast<std::size_t>(chunkIndex)];
    if (flag != 0u) return;
    flag = 1u;
    dirtyQueue_.push_back(chunkIndex);
}

void HexMap::markChunkDirtyAndNeighbors(std::int32_t chunkIndex) noexcept {
    if (chunkIndex < 0 || chunkIndex >= chunkCount()) return;
    markChunkDirty(chunkIndex);
    const std::int32_t column = chunkColumnOf(chunkIndex);
    const std::int32_t row    = chunkRowOf(chunkIndex);
    for (std::int32_t dz = -1; dz <= 1; ++dz) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const std::int32_t cx = column + dx;
            const std::int32_t cz = row + dz;
            if (cx < 0 || cx >= chunkCountX_ || cz < 0 || cz >= chunkCountZ_) continue;
            markChunkDirty(cx + cz * chunkCountX_);
        }
    }
}

void HexMap::markAllChunksDirty() noexcept {
    dirtyQueue_.clear();
    for (std::int32_t i = 0; i < chunkCount(); ++i) {
        chunkDirty_[static_cast<std::size_t>(i)] = 1u;
        dirtyQueue_.push_back(i);
    }
}

std::int32_t HexMap::takeDirtyChunk() noexcept {
    if (dirtyQueue_.empty()) return -1;
    const std::int32_t chunkIndex = dirtyQueue_.back();
    dirtyQueue_.pop_back();
    chunkDirty_[static_cast<std::size_t>(chunkIndex)] = 0u;
    return chunkIndex;
}

// --- geometry ---------------------------------------------------------------

HexVec3 HexMap::cellPosition(HexCoordinates coordinates) const noexcept {
    HexVec3            position = HexCoordinates::toWorldPosition(coordinates);
    const std::int32_t level    = elevation(coordinates);
    position.y                  = HexMetrics::elevationY(level) + noise_.elevationPerturb(position.x, position.z);
    return position;
}

Result<HexCoordinates> HexMap::pickCell(HexVec3 rayOrigin, HexVec3 rayDirection) const noexcept {
    if (empty())
        return Result<HexCoordinates>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "hex map is empty", "hexmap.pick"));

    const float dirLength =
        std::sqrt(rayDirection.x * rayDirection.x + rayDirection.y * rayDirection.y + rayDirection.z * rayDirection.z);
    if (dirLength <= 1e-6f)
        return Result<HexCoordinates>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "ray direction must be non-zero", "hexmap.pick"));
    rayDirection.x /= dirLength;
    rayDirection.y /= dirLength;
    rayDirection.z /= dirLength;

    // Bounding box of the whole map, padded by one cell and one elevation step.
    HexVec3     low  = cellGroundPosition(HexCoordinates::fromOffset(0, 0));
    HexVec3     high = cellGroundPosition(HexCoordinates::fromOffset(cellCountX_ - 1, cellCountZ_ - 1));
    const float pad  = HexMetrics::kOuterRadius;
    low.x -= pad;
    low.z -= pad;
    low.y = HexMetrics::elevationY(HexMetrics::kMinElevation) - HexMetrics::kElevationStep;
    high.x += pad;
    high.z += pad;
    high.y = HexMetrics::elevationY(HexMetrics::kMaxElevation) + HexMetrics::kElevationStep;

    float       tEnter       = 0.f;
    float       tExit        = std::numeric_limits<float>::max();
    const float origin[3]    = {rayOrigin.x, rayOrigin.y, rayOrigin.z};
    const float direction[3] = {rayDirection.x, rayDirection.y, rayDirection.z};
    const float boxLow[3]    = {low.x, low.y, low.z};
    const float boxHigh[3]   = {high.x, high.y, high.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) < 1e-8f) {
            if (origin[axis] < boxLow[axis] || origin[axis] > boxHigh[axis]) {
                return Result<HexCoordinates>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "ray misses the hex map bounds", "hexmap.pick"));
            }
            continue;
        }
        const float inv = 1.f / direction[axis];
        float       t0  = (boxLow[axis] - origin[axis]) * inv;
        float       t1  = (boxHigh[axis] - origin[axis]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit  = std::min(tExit, t1);
    }
    if (tExit < tEnter)
        return Result<HexCoordinates>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "ray misses the hex map bounds", "hexmap.pick"));

    // The march only has to cover the span in which the ray can still meet a surface:
    // the box entry/exit clipped by the vertical band every cell lies in. A fixed step
    // budget (this used to be 512 steps of 1.5 world units, i.e. 768 units past the box
    // entry) silently reported NotFound for hits further along the ray - reachable on a
    // large grid or with a long grazing ray. Deriving the count from the clipped span
    // keeps the worst case bounded by the box itself, because a normalised direction
    // always has at least one component large enough to leave the box in finite time.
    float marchStart = tEnter;
    float marchEnd   = tExit;
    if (std::fabs(direction[1]) >= 1e-8f) {
        const float invY = 1.f / direction[1];
        float       t0   = (boxLow[1] - origin[1]) * invY;
        float       t1   = (boxHigh[1] - origin[1]) * invY;
        if (t0 > t1) std::swap(t0, t1);
        marchStart = std::max(marchStart, t0);
        marchEnd   = std::min(marchEnd, t1);
    }
    if (marchEnd < marchStart)
        return Result<HexCoordinates>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "ray never reached the hex surface", "hexmap.pick"));

    const float    step         = HexMetrics::kElevationStep * 0.5f;
    const float    maxRayLength = marchEnd;
    const int      maxSteps     = static_cast<int>((marchEnd - marchStart) / step) + 2;
    float          previousT    = marchStart;
    HexCoordinates previousCoordinates{};
    bool           havePrevious = false;

    for (int i = 0; i <= maxSteps; ++i) {
        const float          t = std::min(marchStart + static_cast<float>(i) * step, maxRayLength);
        const HexVec3        point{rayOrigin.x + rayDirection.x * t, rayOrigin.y + rayDirection.y * t,
                                   rayOrigin.z + rayDirection.z * t};
        const HexCoordinates coordinates = HexCoordinates::fromWorldPosition(point);
        if (contains(coordinates)) {
            const float surfaceY = cellPosition(coordinates).y;
            if (point.y <= surfaceY) {
                // Bisect between the last sample above the surface and this one.
                float lo = havePrevious ? previousT : marchStart;
                float hi = t;
                // Seed with the sample that actually hit: the previous in-grid sample
                // when the ray entered from above, otherwise this one. Seeding from
                // `previousCoordinates` unconditionally would fall back to its default
                // (0, 0), which `contains` accepts on every non-empty map, so a ray
                // that first meets the surface already inside the grid returned (0, 0).
                HexCoordinates best = havePrevious ? previousCoordinates : coordinates;
                for (int iteration = 0; iteration < 8; ++iteration) {
                    const float          mid = (lo + hi) * 0.5f;
                    const HexVec3        sample{rayOrigin.x + rayDirection.x * mid, rayOrigin.y + rayDirection.y * mid,
                                                rayOrigin.z + rayDirection.z * mid};
                    const HexCoordinates candidate = HexCoordinates::fromWorldPosition(sample);
                    if (!contains(candidate)) break;
                    best = candidate;
                    if (sample.y <= cellPosition(candidate).y)
                        hi = mid;
                    else
                        lo = mid;
                }
                if (contains(best)) return Result<HexCoordinates>::success(best);
                return Result<HexCoordinates>::success(coordinates);
            }
            previousCoordinates = coordinates;
            havePrevious        = true;
        }
        previousT = t;
        if (t >= maxRayLength) break;
    }

    return Result<HexCoordinates>::failure(
        Diagnostic::error(DiagnosticCode::NotFound, "ray never reached the hex surface", "hexmap.pick"));
}

// --- cell queries -----------------------------------------------------------

std::int32_t HexMap::elevation(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.elevation() : 0;
}
std::int32_t HexMap::waterLevel(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.waterLevel() : 0;
}
std::int32_t HexMap::terrainType(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.terrainType() : 0;
}
std::int32_t HexMap::urbanLevel(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.urbanLevel() : 0;
}
std::int32_t HexMap::farmLevel(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.farmLevel() : 0;
}
std::int32_t HexMap::plantLevel(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.plantLevel() : 0;
}
std::int32_t HexMap::specialIndex(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.specialIndex() : 0;
}
bool HexMap::isUnderwater(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values.isUnderwater() : false;
}
bool HexMap::hasRiver(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.hasRiver() : false;
}
bool HexMap::hasRoad(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.hasRoad() : false;
}
bool HexMap::hasRiverThrough(HexCoordinates c, HexDirection d) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.hasRiverThrough(d) : false;
}
bool HexMap::isWalled(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.isWalled() : false;
}
bool HexMap::isExplored(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.isExplored() : false;
}
bool HexMap::isExplorable(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags.isExplorable() : false;
}
HexValues HexMap::values(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->values : HexValues{};
}
HexFlags HexMap::flags(HexCoordinates c) const noexcept {
    const HexCellData* data = cell(c);
    return data ? data->flags : HexFlags{};
}

// --- cell authoring ---------------------------------------------------------

void HexMap::refreshCellDependents(HexCoordinates coordinates) noexcept {
    ++revision_;
    markChunkDirtyAndNeighbors(chunkIndexOf(coordinates));
}

void HexMap::validateRivers(HexCoordinates coordinates) noexcept {
    const HexCellData* data = cell(coordinates);
    if (!data || !data->flags.hasRiver()) return;
    // A river may only leave a cell that is at least as high as its neighbour,
    // unless the cell itself is a lake at exactly its own elevation.
    const std::int32_t fromElevation = elevation(coordinates);
    const std::int32_t fromWater     = waterLevel(coordinates);
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const HexDirection direction = static_cast<HexDirection>(i);
        if (!data->flags.hasRiverOut(direction)) continue;
        HexCoordinates neighbour{};
        const bool     canFlow = getNeighbor(coordinates, direction, neighbour) &&
                                 (fromElevation >= elevation(neighbour) || fromWater == fromElevation);
        if (!canFlow) {
            removeRiver(coordinates).ignore("river validation must keep the map consistent");
            return;
        }
    }
}

Result<void> HexMap::setElevation(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    const std::int32_t clamped = clampInt(value, HexMetrics::kMinElevation, HexMetrics::kMaxElevation);
    if (data->values.elevation() == clamped) return Result<void>::success();
    data->values = data->values.withElevation(clamped);

    // Roads cannot span a difference of more than one elevation step.
    bool hasInvalidRoad = false;
    for (std::int32_t i = 0; i < kHexDirectionCount && !hasInvalidRoad; ++i) {
        const HexDirection direction = static_cast<HexDirection>(i);
        if (!data->flags.hasRoad(direction)) continue;
        HexCoordinates neighbour{};
        if (!getNeighbor(c, direction, neighbour)) continue;
        const std::int32_t delta = elevation(neighbour) - clamped;
        if (delta > 1 || delta < -1) hasInvalidRoad = true;
    }
    if (hasInvalidRoad) removeRoads(c).ignore("elevation change must drop invalid roads");
    refreshCellDependents(c);
    validateRivers(c);
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (getNeighbor(c, static_cast<HexDirection>(i), neighbour)) validateRivers(neighbour);
    }
    return Result<void>::success();
}

Result<void> HexMap::setWaterLevel(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    const std::int32_t clamped = clampInt(value, 0, HexMetrics::kMaxElevation);
    if (data->values.waterLevel() == clamped) return Result<void>::success();
    data->values = data->values.withWaterLevel(clamped);
    refreshCellDependents(c);
    validateRivers(c);
    return Result<void>::success();
}

Result<void> HexMap::setTerrainType(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->values = data->values.withTerrainType(clampTerrainType(value));
    // Terrain type is baked into the terrain mesh's vertex encoding, so the chunk
    // must be rebuilt just like any other authoring write.
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setUrbanLevel(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->values = data->values.withUrbanLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setFarmLevel(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->values = data->values.withFarmLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setPlantLevel(HexCoordinates c, std::int32_t value) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->values = data->values.withPlantLevel(clampInt(value, 0, 3));
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setSpecialIndex(HexCoordinates c, std::int32_t index) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    if (data->flags.hasRiver()) return Result<void>::success();
    data->values = data->values.withSpecialIndex(clampInt(index, 0, 3));
    removeRoads(c).ignore("special features replace roads");
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setWalled(HexCoordinates c, bool walled) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->flags = data->flags.withWalled(walled);
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setExplored(HexCoordinates c, bool explored) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->flags = data->flags.withExplored(explored);
    // The fog overlay *is* geometry derived from this latch: `buildFogMesh` emits a
    // column shaded 1 while the cell is unexplored, 0 once it is explored but unseen,
    // and nothing at all while it is visible. Flipping the latch therefore changes the
    // chunk mesh, which is why `HexVisibility::increase` dirties the chunk itself.
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setExplorable(HexCoordinates c, bool explorable) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->flags = data->flags.withExplorable(explorable);
    // Not rendered on its own, but it gates every later visibility sweep, so keep the
    // documented "every mutation dirties its chunk" invariant unconditional rather
    // than leave a fact that a future fog rule could read without a rebuild.
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setCellState(HexCoordinates c, HexValues values, HexFlags flags) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    data->values = values;
    data->flags  = flags;
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::setOutgoingRiver(HexCoordinates c, HexDirection direction) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    HexCoordinates neighbour{};
    if (!getNeighbor(c, direction, neighbour)) return invalidArgument("river direction leaves the hex map");
    if (data->flags.hasAnyRiverOut() && data->flags.hasRiverOut(direction)) return Result<void>::success();
    if (elevation(c) < elevation(neighbour) && waterLevel(c) != elevation(c))
        return invalidArgument("a river cannot flow uphill");

    // Drop only the links this call replaces, mirroring the reference cell's
    // `RemoveOutgoingRiver` + same-edge `RemoveIncomingRiver`. Calling the full
    // `removeRiver` here also erased the *incoming* link from the upstream cell, so
    // carving a channel step by step wiped the step behind it and only the last
    // edge survived; that is why a 40-cell walk produced a 2-cell river.
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const HexDirection previous = static_cast<HexDirection>(i);
        if (!data->flags.hasRiverOut(previous)) continue;
        data->flags = data->flags.withoutRiverOut(previous);
        HexCoordinates previousNeighbour{};
        if (getNeighbor(c, previous, previousNeighbour)) {
            HexCellData* previousData = mutableCell(previousNeighbour);
            previousData->flags       = previousData->flags.withoutRiverIn(opposite(previous));
            refreshCellDependents(previousNeighbour);
        }
    }
    data = mutableCell(c);
    // A river may not leave and enter through the same edge.
    if (data->flags.hasRiverIn(direction)) {
        data->flags             = data->flags.withoutRiverIn(direction);
        HexCellData* otherData  = mutableCell(neighbour);
        otherData->flags        = otherData->flags.withoutRiverOut(opposite(direction));
    }

    data               = mutableCell(c);
    data->flags        = data->flags.withRiverOut(direction).withoutRoad(direction);
    data->values       = data->values.withSpecialIndex(0);
    HexCellData* other = mutableCell(neighbour);
    other->flags       = other->flags.withRiverIn(opposite(direction)).withoutRoad(opposite(direction));
    other->values      = other->values.withSpecialIndex(0);
    refreshCellDependents(c);
    refreshCellDependents(neighbour);
    return Result<void>::success();
}

Result<void> HexMap::removeRiver(HexCoordinates c) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    if (!data->flags.hasRiver()) return Result<void>::success();
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const HexDirection direction = static_cast<HexDirection>(i);
        data->flags                  = data->flags.withoutRiverIn(direction).withoutRiverOut(direction);
    }
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const HexDirection direction = static_cast<HexDirection>(i);
        HexCoordinates     neighbour{};
        if (!getNeighbor(c, direction, neighbour)) continue;
        HexCellData* other = mutableCell(neighbour);
        other->flags       = other->flags.withoutRiverIn(opposite(direction)).withoutRiverOut(opposite(direction));
        refreshCellDependents(neighbour);
    }
    refreshCellDependents(c);
    return Result<void>::success();
}

Result<void> HexMap::addRoad(HexCoordinates c, HexDirection direction) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    HexCoordinates neighbour{};
    if (!getNeighbor(c, direction, neighbour)) return invalidArgument("road direction leaves the hex map");
    if (data->flags.hasRoad(direction)) return Result<void>::success();
    if (data->flags.hasRiverThrough(direction)) return Result<void>::success();
    if (data->flags.hasRiver() && !data->flags.hasRiverBeginOrEnd()) {
        // A road may only join a river at a begin or end cell.
        return Result<void>::success();
    }
    HexCellData* other = mutableCell(neighbour);
    if (other->values.specialIndex() != 0 || data->values.specialIndex() != 0) return Result<void>::success();
    if (other->flags.hasRiverThrough(opposite(direction))) return Result<void>::success();
    const std::int32_t delta = elevation(neighbour) - elevation(c);
    if (delta > 1 || delta < -1) return Result<void>::success();

    data->flags  = data->flags.withRoad(direction);
    other->flags = other->flags.withRoad(opposite(direction));
    refreshCellDependents(c);
    refreshCellDependents(neighbour);
    return Result<void>::success();
}

Result<void> HexMap::removeRoads(HexCoordinates c) {
    HexCellData* data = mutableCell(c);
    if (!data) return invalidArgument("cell is outside the hex map");
    if (!data->flags.hasRoad()) return Result<void>::success();
    data->flags = data->flags.without(HexFlags::roadMask());
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const HexDirection direction = static_cast<HexDirection>(i);
        HexCoordinates     neighbour{};
        if (!getNeighbor(c, direction, neighbour)) continue;
        HexCellData* other = mutableCell(neighbour);
        other->flags       = other->flags.withoutRoad(opposite(direction));
        refreshCellDependents(neighbour);
    }
    refreshCellDependents(c);
    return Result<void>::success();
}

// --- brush authoring --------------------------------------------------------

void HexMap::collectBrush(HexCoordinates center, std::int32_t radius, std::vector<std::int32_t>& out) const {
    out.clear();
    if (!contains(center) || radius < 0) return;
    // The brush reaches `radius` steps, so its offset-space footprint is at most
    // `2 * radius` columns wide and `radius` rows tall. A radius past the grid
    // diameter already covers everything, and clamping it here keeps the `2 * radius`
    // footprint (and the loop trip count) inside int32 for script-supplied radii.
    const std::int64_t diameter     = static_cast<std::int64_t>(cellCountX_) + static_cast<std::int64_t>(cellCountZ_);
    const std::int64_t reach        = std::min<std::int64_t>(static_cast<std::int64_t>(radius), diameter);
    const std::int64_t spread       = reach * 2;
    const std::int32_t centerColumn = center.offsetX();
    const std::int32_t minColumn =
        static_cast<std::int32_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(centerColumn) - spread));
    const std::int32_t maxColumn = static_cast<std::int32_t>(
        std::min<std::int64_t>(cellCountX_ - 1, static_cast<std::int64_t>(centerColumn) + spread));
    const std::int32_t minZ =
        static_cast<std::int32_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(center.z) - reach));
    const std::int32_t maxZ =
        static_cast<std::int32_t>(std::min<std::int64_t>(cellCountZ_ - 1, static_cast<std::int64_t>(center.z) + reach));
    for (std::int32_t z = minZ; z <= maxZ; ++z) {
        for (std::int32_t column = minColumn; column <= maxColumn; ++column) {
            const HexCoordinates candidate = HexCoordinates::fromOffset(column, z);
            if (center.distanceTo(candidate) <= radius) out.push_back(column + z * cellCountX_);
        }
    }
}

Result<void> HexMap::editElevation(HexCoordinates center, std::int32_t radius, std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<std::int32_t> cells;
    collectBrush(center, radius, cells);
    for (std::int32_t index : cells) {
        const HexCoordinates coordinates = coordinatesAt(index);
        const std::int32_t   next        = saturatingAdd(elevation(coordinates), delta);
        setElevation(coordinates, next).ignore("brush elevation edit clamps per cell");
    }
    return Result<void>::success();
}

Result<void> HexMap::editWaterLevel(HexCoordinates center, std::int32_t radius, std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<std::int32_t> cells;
    collectBrush(center, radius, cells);
    for (std::int32_t index : cells) {
        const HexCoordinates coordinates = coordinatesAt(index);
        setWaterLevel(coordinates, saturatingAdd(waterLevel(coordinates), delta))
            .ignore("brush water edit clamps per cell");
    }
    return Result<void>::success();
}

Result<void> HexMap::editTerrainType(HexCoordinates center, std::int32_t radius, std::int32_t terrainType) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<std::int32_t> cells;
    collectBrush(center, radius, cells);
    for (std::int32_t index : cells) {
        const HexCoordinates coordinates = coordinatesAt(index);
        setTerrainType(coordinates, terrainType).ignore("brush terrain edit clamps per cell");
    }
    return Result<void>::success();
}

Result<void> HexMap::editFeatureLevel(HexCoordinates center, std::int32_t radius, std::int32_t feature,
                                      std::int32_t delta) {
    if (!contains(center)) return invalidArgument("brush centre is outside the hex map");
    if (radius < 0) return invalidArgument("brush radius must be non-negative");
    std::vector<std::int32_t> cells;
    collectBrush(center, radius, cells);
    const std::int32_t layer = layerIndex(feature);
    for (std::int32_t index : cells) {
        const HexCoordinates coordinates = coordinatesAt(index);
        const HexCellData*   data        = cellAt(index);
        if (!data || data->flags.hasRiver()) continue;
        if (data->values.specialIndex() != 0) continue;
        const std::int32_t current = layer == 0   ? data->values.urbanLevel()
                                     : layer == 1 ? data->values.farmLevel()
                                                  : data->values.plantLevel();
        const std::int32_t next    = clampInt(saturatingAdd(current, delta), 0, 3);
        if (layer == 0)
            setUrbanLevel(coordinates, next).ignore("brush urban edit clamps per cell");
        else if (layer == 1)
            setFarmLevel(coordinates, next).ignore("brush farm edit clamps per cell");
        else
            setPlantLevel(coordinates, next).ignore("brush plant edit clamps per cell");
    }
    return Result<void>::success();
}

}  // namespace eve::hexmap
