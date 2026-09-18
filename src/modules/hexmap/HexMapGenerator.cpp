/**
 * @file HexMapGenerator.cpp
 * @brief Deterministic port of the reference hex-map project's procedural generator.
 */

#include "hexmap/HexMapGenerator.h"

#include "common/Diagnostic.h"
#include "hexmap/HexSearch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace eve::hexmap {
namespace {

// ---------------------------------------------------------------------------
// Deterministic randomness
// ---------------------------------------------------------------------------

/**
 * @brief Deterministic xorshift random source, replacing Unity's `Random`.
 *
 * The generator must be reproducible from `settings.seed` and the grid alone, so
 * no time source and no `std::random_device` participates; an all-zero state is
 * remapped to a fixed non-zero constant because xorshift cannot leave it.
 */
class HexRandom {
public:
    /** @brief Seeds the sequence; the same seed always replays the same draws. */
    explicit HexRandom(std::uint32_t seed) noexcept : state_(seed == 0u ? 0x9E3779B9u : seed) {}

    /**
     * @brief Draws a value in `[0, 1)`.
     * @return The next uniform value.
     */
    [[nodiscard]] float unit() noexcept {
        state_ ^= state_ << 13u;
        state_ ^= state_ >> 17u;
        state_ ^= state_ << 5u;
        // 24 significant bits keep the conversion exact in single precision.
        return static_cast<float>(state_ >> 8) * (1.f / 16777216.f);
    }

    /**
     * @brief Draws an integer in `[0, count)`.
     * @param count Exclusive upper bound; must be positive.
     * @return The drawn index, or 0 when `count` is not positive.
     */
    [[nodiscard]] std::int32_t index(std::int32_t count) noexcept {
        if (count <= 0) return 0;
        const auto span = static_cast<std::uint32_t>(count);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(unit() * static_cast<float>(span))) % count;
    }

    /**
     * @brief Draws an integer in `[minimum, maximumExclusive)`.
     * @param minimum Inclusive lower bound.
     * @param maximumExclusive Exclusive upper bound.
     * @return The drawn value, clamped into the requested interval.
     */
    [[nodiscard]] std::int32_t range(std::int32_t minimum, std::int32_t maximumExclusive) noexcept {
        if (maximumExclusive <= minimum) return minimum;
        return minimum + index(maximumExclusive - minimum);
    }

    /**
     * @brief Answers whether a draw is below `probability`.
     * @param probability Threshold in `[0, 1]`.
     * @return True when the draw is strictly below the threshold.
     */
    [[nodiscard]] bool chance(float probability) noexcept { return unit() < probability; }

private:
    std::uint32_t state_;
};

/**
 * @brief Integer hash with good avalanche on the low bits (xxhash-style mix).
 *
 * Mirrors the mixing function used by the hex noise field so the generator's own
 * lattice noise is deterministic and independent of that field's seed.
 */
[[nodiscard]] std::uint32_t mix(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    std::uint32_t h = x * 0x9E3779B1u;
    h ^= y * 0x85EBCA77u;
    h = (h ^ (h >> 15)) * 0xC2B2AE3Du;
    h ^= z * 0x27D4EB2Fu;
    h = (h ^ (h >> 13)) * 0x165667B1u;
    return h ^ (h >> 16);
}

/** @brief Unit value of one noise lattice corner. */
[[nodiscard]] float lattice(std::int32_t ix, std::int32_t iz, std::uint32_t channel) noexcept {
    const std::uint32_t hashed = mix(static_cast<std::uint32_t>(ix), static_cast<std::uint32_t>(iz), channel);
    return static_cast<float>(hashed >> 8) * (1.f / 16777216.f);
}

/**
 * @brief Smooth value noise over the cell lattice.
 *
 * The reference samples its `HexMetrics` noise texture at a tenth of the cell
 * position; the port instead interpolates a hashed lattice on the same scale, so
 * the jitter stays spatially coherent and a cell always yields the same value.
 *
 * @param x Cell position along X.
 * @param z Cell position along Z.
 * @param channel Lattice channel, so each call site can draw an independent field.
 * @return The sampled value in `[0, 1]`.
 */
[[nodiscard]] float cellNoise(std::int32_t x, std::int32_t z, std::uint32_t channel) noexcept {
    constexpr float kScale = 0.1f;
    const float     fx     = static_cast<float>(x) * kScale;
    const float     fz     = static_cast<float>(z) * kScale;
    const float     bx     = std::floor(fx);
    const float     bz     = std::floor(fz);
    const auto      ix     = static_cast<std::int32_t>(bx);
    const auto      iz     = static_cast<std::int32_t>(bz);
    const float     tx     = fx - bx;
    const float     tz     = fz - bz;
    const float     sx     = tx * tx * (3.f - 2.f * tx);
    const float     sz     = tz * tz * (3.f - 2.f * tz);

    const float c00    = lattice(ix, iz, channel);
    const float c10    = lattice(ix + 1, iz, channel);
    const float c01    = lattice(ix, iz + 1, channel);
    const float c11    = lattice(ix + 1, iz + 1, channel);
    const float top    = c00 + (c10 - c00) * sx;
    const float bottom = c01 + (c11 - c01) * sx;
    return top + (bottom - top) * sz;
}

/** @brief Failure helper mirroring the shape used by `HexMap`. */
[[nodiscard]] Result<void> invalidArgument(std::string message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap"));
}

/** @brief Rounds a float to the nearest integer, matching `Mathf.RoundToInt`. */
[[nodiscard]] std::int32_t roundToInt(float value) noexcept {
    return static_cast<std::int32_t>(std::floor(value + 0.5f));
}

/** @brief Clamps an integer into the inclusive interval. */
[[nodiscard]] std::int32_t clampInt(std::int32_t value, std::int32_t low, std::int32_t high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

/**
 * @brief Substitutes a value when the setting is out of its supported range.
 *
 * The reference relies on its inspector ranges; the port keeps the map on a
 * defined path instead of trusting the caller, which avoids degenerate random
 * ranges and inverted region rectangles.
 */
[[nodiscard]] std::int32_t clampSetting(std::int32_t value, std::int32_t low, std::int32_t high,
                                        std::int32_t fallback) noexcept {
    if (value < low || value > high) return fallback;
    return value;
}

// ---------------------------------------------------------------------------
// Settings normalisation
// ---------------------------------------------------------------------------

/** @brief Generator tunables after clamping, with no unreachable values left. */
struct NormalizedSettings {
    float        highRiseProbability  = 0.25f;
    float        sinkProbability      = 0.2f;
    float        jitterProbability    = 0.25f;
    std::int32_t chunkSizeMin         = 30;
    std::int32_t chunkSizeMax         = 100;
    std::int32_t landPercentage       = 50;
    std::int32_t waterLevel           = 3;
    std::int32_t elevationMinimum     = -2;
    std::int32_t elevationMaximum     = 8;
    std::int32_t mapBorderX           = 5;
    std::int32_t mapBorderZ           = 5;
    std::int32_t regionBorder         = 5;
    std::int32_t regionCount          = 1;
    std::int32_t erosionPercentage    = 50;
    float        startingMoisture     = 0.1f;
    float        evaporationFactor    = 0.5f;
    float        precipitationFactor  = 0.25f;
    float        runoffFactor         = 0.25f;
    float        seepageFactor        = 0.25f;
    HexDirection windDirection        = HexDirection::NW;
    float        windStrength         = 4.f;
    std::int32_t riverPercentage      = 10;
    float        extraLakeProbability = 0.25f;
    float        lowTemperature       = 0.f;
    float        highTemperature      = 1.f;
    float        temperatureJitter    = 0.1f;
};

/**
 * @brief Clamps the caller's settings into the ranges the pipeline supports.
 *
 * `elevationMaximum` is additionally capped by `HexMetrics::kMaxElevation`: the
 * cell record cannot express an elevation above 8, so a larger request is
 * unreachable and is reported here instead of silently changing nothing. The
 * region split only has a defined shape for one to four regions, so any other
 * count falls back to the single-region case, exactly like the reference's
 * `switch` default arm.
 *
 * @param settings Caller-supplied tunables.
 * @return The normalised settings.
 */
[[nodiscard]] NormalizedSettings normalize(const HexMapGeneratorSettings& settings) noexcept {
    NormalizedSettings out;
    out.highRiseProbability  = std::clamp(settings.highRiseProbability, 0.f, 1.f);
    out.sinkProbability      = std::clamp(settings.sinkProbability, 0.f, 1.f);
    out.jitterProbability    = std::clamp(settings.jitterProbability, 0.f, 1.f);
    out.chunkSizeMin         = clampSetting(settings.chunkSizeMin, 1, 100000, out.chunkSizeMin);
    out.chunkSizeMax         = clampSetting(settings.chunkSizeMax, 2, 100000, out.chunkSizeMax);
    out.landPercentage       = clampSetting(settings.landPercentage, 0, 100, out.landPercentage);
    out.waterLevel           = clampInt(settings.waterLevel, 0, HexMetrics::kMaxElevation);
    out.elevationMinimum     = clampInt(settings.elevationMinimum, HexMetrics::kMinElevation, 0);
    out.elevationMaximum     = clampInt(settings.elevationMaximum, 1, HexMetrics::kMaxElevation);
    out.mapBorderX           = clampSetting(settings.mapBorderX, 0, 1000, out.mapBorderX);
    out.mapBorderZ           = clampSetting(settings.mapBorderZ, 0, 1000, out.mapBorderZ);
    out.regionBorder         = clampSetting(settings.regionBorder, 0, 1000, out.regionBorder);
    out.regionCount          = clampSetting(settings.regionCount, 1, 4, out.regionCount);
    out.erosionPercentage    = clampSetting(settings.erosionPercentage, 0, 100, out.erosionPercentage);
    out.startingMoisture     = std::clamp(settings.startingMoisture, 0.f, 1.f);
    out.evaporationFactor    = std::clamp(settings.evaporationFactor, 0.f, 1.f);
    out.precipitationFactor  = std::clamp(settings.precipitationFactor, 0.f, 1.f);
    out.runoffFactor         = std::clamp(settings.runoffFactor, 0.f, 1.f);
    out.seepageFactor        = std::clamp(settings.seepageFactor, 0.f, 1.f);
    out.windDirection        = settings.windDirection;
    out.windStrength         = std::clamp(settings.windStrength, 0.f, 1000.f);
    out.riverPercentage      = clampSetting(settings.riverPercentage, 0, 100, out.riverPercentage);
    out.extraLakeProbability = std::clamp(settings.extraLakeProbability, 0.f, 1.f);
    out.lowTemperature       = std::clamp(settings.lowTemperature, -10.f, 10.f);
    out.highTemperature      = std::clamp(settings.highTemperature, -10.f, 10.f);
    out.temperatureJitter    = std::clamp(settings.temperatureJitter, 0.f, 10.f);
    return out;
}

/** @brief Rows and columns of a rectangle in odd-row offset space. */
struct MapRegion {
    std::int32_t xMin = 0;
    std::int32_t xMax = 0;
    std::int32_t zMin = 0;
    std::int32_t zMax = 0;

    /** @brief Whether the rectangle holds at least one row and one column. */
    [[nodiscard]] bool valid() const noexcept { return xMax > xMin && zMax > zMin; }
};

/**
 * @brief Splits the map into the requested number of land regions.
 *
 * A direct port of the reference `CreateRegions` for the non-wrapping case.
 *
 * @param count Number of regions, one to four.
 * @param cellCountX Map width in columns.
 * @param cellCountZ Map height in rows.
 * @param settings Normalised tunables.
 * @param out Receives the regions, cleared first.
 */
void createRegions(std::int32_t count, std::int32_t cellCountX, std::int32_t cellCountZ,
                   const NormalizedSettings& settings, std::vector<MapRegion>& out) {
    out.clear();
    const std::int32_t borderX = settings.mapBorderX;
    const std::int32_t borderZ = settings.mapBorderZ;
    const std::int32_t midX    = cellCountX / 2;
    const std::int32_t midZ    = cellCountZ / 2;
    const std::int32_t thirdX  = cellCountX / 3;
    const std::int32_t border  = settings.regionBorder;

    switch (count) {
        case 2: {
            MapRegion first;
            MapRegion second;
            first.zMin  = borderZ;
            first.zMax  = cellCountZ - borderZ;
            second.zMin = borderZ;
            second.zMax = cellCountZ - borderZ;
            first.xMin  = borderX;
            first.xMax  = midX - border;
            second.xMin = midX + border;
            second.xMax = cellCountX - borderX;
            out.push_back(first);
            out.push_back(second);
            break;
        }
        case 3: {
            MapRegion first;
            MapRegion second;
            MapRegion third;
            first.zMin  = borderZ;
            first.zMax  = cellCountZ - borderZ;
            second.zMin = borderZ;
            second.zMax = cellCountZ - borderZ;
            third.zMin  = borderZ;
            third.zMax  = cellCountZ - borderZ;
            first.xMin  = borderX;
            first.xMax  = thirdX - border;
            second.xMin = thirdX + border;
            second.xMax = cellCountX * 2 / 3 - border;
            third.xMin  = cellCountX * 2 / 3 + border;
            third.xMax  = cellCountX - borderX;
            out.push_back(first);
            out.push_back(second);
            out.push_back(third);
            break;
        }
        case 4: {
            MapRegion first;
            MapRegion second;
            MapRegion third;
            MapRegion fourth;
            first.xMin  = borderX;
            first.zMin  = borderZ;
            first.xMax  = midX - border;
            first.zMax  = midZ - border;
            second.xMin = midX + border;
            second.zMin = borderZ;
            second.xMax = cellCountX - borderX;
            second.zMax = midZ - border;
            third.xMin  = midX + border;
            third.zMin  = midZ + border;
            third.xMax  = cellCountX - borderX;
            third.zMax  = cellCountZ - borderZ;
            fourth.xMin = borderX;
            fourth.zMin = midZ + border;
            fourth.xMax = midX - border;
            fourth.zMax = cellCountZ - borderZ;
            out.push_back(first);
            out.push_back(second);
            out.push_back(third);
            out.push_back(fourth);
            break;
        }
        default: {
            MapRegion only;
            only.xMin = borderX;
            only.xMax = cellCountX - borderX;
            only.zMin = borderZ;
            only.zMax = cellCountZ - borderZ;
            out.push_back(only);
            break;
        }
    }
}

/**
 * @brief Whether every requested region rectangle is non-empty.
 * @param regions Regions produced by `createRegions`.
 * @param regionCount Requested region count.
 * @return True when each requested region holds at least one cell.
 */
[[nodiscard]] bool regionsUsable(const std::vector<MapRegion>& regions, std::int32_t regionCount) {
    if (static_cast<std::int32_t>(regions.size()) != regionCount) return false;
    for (const MapRegion& region : regions) {
        if (!region.valid()) return false;
    }
    return true;
}

/** @brief One cell's moisture and cloud cover during the climate simulation. */
struct ClimateData {
    float clouds   = 0.f;
    float moisture = 0.f;
};

/** @brief Terrain and plant pairing of one temperature/moisture band. */
struct Biome {
    std::int32_t terrain = 0;
    std::int32_t plant   = 0;
};

/** @brief Temperature band boundaries, matching the reference table. */
constexpr float kTemperatureBands[3] = {0.1f, 0.3f, 0.6f};
/** @brief Moisture band boundaries, matching the reference table. */
constexpr float kMoistureBands[3] = {0.12f, 0.28f, 0.85f};
/** @brief Terrain/plant pairing of each temperature and moisture band. */
constexpr Biome kBiomes[16] = {{0, 0}, {4, 0}, {4, 0}, {4, 0}, {0, 0}, {2, 0}, {2, 1}, {2, 2},
                               {0, 0}, {1, 0}, {1, 1}, {1, 2}, {0, 0}, {1, 1}, {1, 2}, {1, 3}};

/** @brief Scratch state shared by the pipeline steps; never stored on the map. */
struct GeneratorScratch {
    /** @brief Frontier reused by the land-growth and erosion searches. */
    HexSearchContext search;
    /** @brief Cells still eligible for erosion. */
    std::vector<std::int32_t> erodible;
    /** @brief Membership flags parallel to the erodible list. */
    std::vector<std::uint8_t> erodibleFlag;
    /** @brief Candidate directions considered by one river step. */
    std::vector<HexDirection> flowDirections;
    /** @brief Weighted river-origin candidates. */
    std::vector<std::int32_t> riverOrigins;
    /** @brief Current moisture and cloud state per cell. */
    std::vector<ClimateData> climate;
    /** @brief Per-cell accumulation target of the next climate cycle. */
    std::vector<ClimateData> nextClimate;
    /** @brief Number of cells that count as land, driving the river budget. */
    std::int32_t landCells = 0;
};

// ---------------------------------------------------------------------------
// Elevation helpers
// ---------------------------------------------------------------------------

/**
 * @brief Writes one cell's elevation through the map's authoring API.
 *
 * The value is clamped to the editable range, so `elevationMaximum` above
 * `HexMetrics::kMaxElevation` is unreachable by construction.
 *
 * @param map Target grid.
 * @param coordinates Cell to write.
 * @param elevation Requested elevation.
 */
void writeElevation(HexMap& map, HexCoordinates coordinates, std::int32_t elevation) {
    map.setElevation(coordinates, elevation).ignore("generator writes are clamped per cell");
}

/**
 * @brief Reads a cell's water level.
 * @param map Source grid.
 * @param coordinates Cell to read.
 * @return The stored water level.
 */
[[nodiscard]] std::int32_t waterLevelOf(const HexMap& map, HexCoordinates coordinates) noexcept {
    return map.waterLevel(coordinates);
}

/** @brief Writes one cell's water level through the map's authoring API. */
void writeWaterLevel(HexMap& map, HexCoordinates coordinates, std::int32_t waterLevel) {
    map.setWaterLevel(coordinates, waterLevel).ignore("generator writes are clamped per cell");
}

// ---------------------------------------------------------------------------
// Land growth
// ---------------------------------------------------------------------------

/**
 * @brief Selects a uniformly random cell of a region.
 *
 * @param map Target grid.
 * @param region Region rectangle.
 * @param random Deterministic random source.
 * @return Linear index of the chosen cell.
 */
[[nodiscard]] std::int32_t randomCellIndex(const HexMap& map, const MapRegion& region, HexRandom& random) {
    const std::int32_t   offsetX = random.range(region.xMin, region.xMax);
    const std::int32_t   offsetZ = random.range(region.zMin, region.zMax);
    const HexCoordinates cell    = HexCoordinates::fromOffset(offsetX, offsetZ);
    const std::int32_t   index   = map.indexOf(cell);
    return index < 0 ? 0 : index;
}

/**
 * @brief Grows one land chunk upwards from a seeded cell.
 *
 * A direct port of the reference `RaiseTerrain`: the frontier is ordered by hex
 * distance from the chunk centre plus a random jitter, so a chunk is roundish
 * with a ragged edge. Growth stops when the chunk reaches `chunkSize` cells or
 * when the land budget is consumed.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 * @param region Region the chunk grows inside.
 * @param chunkSize Target number of cells to raise.
 * @param budget Remaining land budget; decremented per newly raised land cell.
 * @return The remaining land budget.
 */
std::int32_t raiseTerrain(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings,
                          const MapRegion& region, std::int32_t chunkSize, std::int32_t budget) {
    const std::int32_t phase               = scratch.search.beginPhase();
    const std::int32_t first               = randomCellIndex(map, region, random);
    scratch.search.data(first).searchPhase = phase;
    scratch.search.enqueue(first);
    const HexCoordinates center = map.coordinatesAt(first);

    const std::int32_t rise = random.chance(settings.highRiseProbability) ? 2 : 1;
    std::int32_t       size = 0;
    while (size < chunkSize) {
        std::int32_t index = -1;
        if (scratch.search.dequeue(index) != HexSearchPop::Cell) break;

        const std::int32_t originalElevation = map.elevation(map.coordinatesAt(index));
        const std::int32_t newElevation      = originalElevation + rise;
        if (newElevation > settings.elevationMaximum) continue;

        const HexCoordinates coordinates = map.coordinatesAt(index);
        writeElevation(map, coordinates, newElevation);
        if (originalElevation < settings.waterLevel && newElevation >= settings.waterLevel) {
            --budget;
            if (budget == 0) break;
        }
        ++size;

        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            HexCoordinates neighbour{};
            if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
            const std::int32_t neighbourIndex = map.indexOf(neighbour);
            if (neighbourIndex < 0) continue;
            if (scratch.search.data(neighbourIndex).searchPhase >= phase) continue;
            HexSearchData& record = scratch.search.data(neighbourIndex);
            record.searchPhase    = phase;
            record.distance       = neighbour.distanceTo(center);
            record.heuristic      = random.chance(settings.jitterProbability) ? 1 : 0;
            scratch.search.enqueue(neighbourIndex);
        }
    }
    return budget;
}

/**
 * @brief Sinks one land chunk downwards from a seeded cell.
 *
 * A direct port of the reference `SinkTerrain`: the mirror image of
 * `raiseTerrain`, except that flooding a cell that was dry returns one unit to
 * the land budget.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 * @param region Region the chunk sinks inside.
 * @param chunkSize Target number of cells to sink.
 * @param budget Remaining land budget; incremented per newly flooded cell.
 * @return The remaining land budget.
 */
std::int32_t sinkTerrain(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings,
                         const MapRegion& region, std::int32_t chunkSize, std::int32_t budget) {
    const std::int32_t phase               = scratch.search.beginPhase();
    const std::int32_t first               = randomCellIndex(map, region, random);
    scratch.search.data(first).searchPhase = phase;
    scratch.search.enqueue(first);
    const HexCoordinates center = map.coordinatesAt(first);

    const std::int32_t sink = random.chance(settings.highRiseProbability) ? 2 : 1;
    std::int32_t       size = 0;
    while (size < chunkSize) {
        std::int32_t index = -1;
        if (scratch.search.dequeue(index) != HexSearchPop::Cell) break;

        const HexCoordinates coordinates       = map.coordinatesAt(index);
        const std::int32_t   originalElevation = map.elevation(coordinates);
        const std::int32_t   newElevation      = originalElevation - sink;
        if (newElevation < settings.elevationMinimum) continue;

        writeElevation(map, coordinates, newElevation);
        if (originalElevation >= settings.waterLevel && newElevation < settings.waterLevel) ++budget;
        ++size;

        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            HexCoordinates neighbour{};
            if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
            const std::int32_t neighbourIndex = map.indexOf(neighbour);
            if (neighbourIndex < 0) continue;
            if (scratch.search.data(neighbourIndex).searchPhase >= phase) continue;
            HexSearchData& record = scratch.search.data(neighbourIndex);
            record.searchPhase    = phase;
            record.distance       = neighbour.distanceTo(center);
            record.heuristic      = random.chance(settings.jitterProbability) ? 1 : 0;
            scratch.search.enqueue(neighbourIndex);
        }
    }
    return budget;
}

/**
 * @brief Grows and sinks land chunks until the land budget is used up.
 *
 * A direct port of the reference `CreateLand`, including the guard against a
 * budget that can never be spent: the reference gives up after 10000 rounds and
 * reports the shortfall, which the port mirrors by simply leaving the surplus
 * unspent.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 * @param regions Region rectangles to grow inside.
 */
void createLand(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings,
                const std::vector<MapRegion>& regions) {
    const std::int32_t cellCount  = map.cellCount();
    const float        cellShare  = static_cast<float>(cellCount) * 0.01f;
    std::int32_t       landBudget = roundToInt(cellShare * static_cast<float>(settings.landPercentage));
    scratch.landCells             = landBudget;

    for (std::int32_t guard = 0; guard < 10000; ++guard) {
        const bool sink = random.chance(settings.sinkProbability);
        for (const MapRegion& region : regions) {
            const std::int32_t chunkSize = random.range(settings.chunkSizeMin, settings.chunkSizeMax - 1);
            if (sink) {
                landBudget = sinkTerrain(map, scratch, random, settings, region, chunkSize, landBudget);
            } else {
                landBudget = raiseTerrain(map, scratch, random, settings, region, chunkSize, landBudget);
                if (landBudget == 0) return;
            }
        }
    }
    if (landBudget > 0) {
        // The reference logs a warning here; the port records the shortfall in the
        // land count instead, which is what the river budget is derived from.
        scratch.landCells -= landBudget;
    }
}

// ---------------------------------------------------------------------------
// Erosion
// ---------------------------------------------------------------------------

/**
 * @brief Whether a cell sits at least two elevation steps above a neighbour.
 * @param map Source grid.
 * @param coordinates Cell to test.
 * @return True when the cell can give one elevation step to a neighbour.
 */
[[nodiscard]] bool isErodible(const HexMap& map, HexCoordinates coordinates) {
    const std::int32_t threshold = map.elevation(coordinates) - 2;
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
        if (map.elevation(neighbour) <= threshold) return true;
    }
    return false;
}

/**
 * @brief Picks the neighbour that receives the eroded elevation step.
 *
 * The reference selects uniformly among every neighbour at or below the erosion
 * threshold; the port takes the first such neighbour in direction order so the
 * choice is a function of the grid alone. The set of candidates is identical.
 *
 * @param map Source grid.
 * @param coordinates Cell being eroded.
 * @return Linear index of the target cell, or -1 when there is none.
 */
[[nodiscard]] std::int32_t erosionTarget(const HexMap& map, HexCoordinates coordinates) {
    const std::int32_t threshold = map.elevation(coordinates) - 2;
    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
        if (map.elevation(neighbour) <= threshold) return map.indexOf(neighbour);
    }
    return -1;
}

/**
 * @brief Adds a cell to the erosion set and the erosion frontier.
 *
 * The caller passes the current elevation because it has just written it; the
 * frontier priority is that elevation, so the lowest cell is always eroded first
 * and the coastline wears down evenly. Elevations live in `[-4, 8]`, so the pack
 * offset keeps every priority non-negative and every bucket index valid.
 *
 * @param scratch Reusable scratch buffers.
 * @param index Cell to add.
 * @param elevation Current elevation of `index`.
 */
void addErodible(GeneratorScratch& scratch, std::int32_t index, std::int32_t elevation) {
    if (index < 0 || static_cast<std::size_t>(index) >= scratch.erodibleFlag.size()) return;
    if (scratch.erodibleFlag[static_cast<std::size_t>(index)] != 0u) return;
    scratch.erodibleFlag[static_cast<std::size_t>(index)] = 1u;
    scratch.erodible.push_back(index);
    HexSearchData& record = scratch.search.data(index);
    record.distance       = elevation + 15;
    record.heuristic      = 0;
    scratch.search.enqueue(index);
}

/**
 * @brief Removes a cell from the erosion set and the erosion frontier.
 * @param scratch Reusable scratch buffers.
 * @param index Cell to remove.
 */
void removeErodible(GeneratorScratch& scratch, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= scratch.erodibleFlag.size()) return;
    if (scratch.erodibleFlag[static_cast<std::size_t>(index)] == 0u) return;
    scratch.erodibleFlag[static_cast<std::size_t>(index)] = 0u;
    if (!scratch.erodible.empty() && scratch.erodible.back() == index) {
        scratch.erodible.pop_back();
        return;
    }
    const auto position = std::find(scratch.erodible.begin(), scratch.erodible.end(), index);
    if (position == scratch.erodible.end()) return;
    *position = scratch.erodible.back();
    scratch.erodible.pop_back();
}

/**
 * @brief Re-queues a cell whose elevation, and therefore priority, was updated.
 *
 * The frontier must be told the priority the cell was queued with, so the caller
 * supplies the elevation it read before the write.
 *
 * @param scratch Reusable scratch buffers.
 * @param index Cell whose priority changed.
 * @param previousElevation Elevation the cell was queued with.
 */
void requeueErodible(GeneratorScratch& scratch, std::int32_t index, std::int32_t previousElevation) {
    if (index < 0 || static_cast<std::size_t>(index) >= scratch.erodibleFlag.size()) return;
    if (scratch.erodibleFlag[static_cast<std::size_t>(index)] == 0u) return;
    scratch.search.change(index, previousElevation + 15);
}

/**
 * @brief Moves one elevation step from a high cell to a neighbour below.
 *
 * A direct port of the reference `Erode`: the eroded cell loses one step, the
 * chosen neighbour gains one, and the erodible set is repaired around both.
 * Erosion stops when the remaining erodible count reaches the share of the map
 * that `erosionPercentage` asks to keep.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param cellIndex Cell to lower.
 */
void erodeOnce(HexMap& map, GeneratorScratch& scratch, std::int32_t cellIndex) {
    if (cellIndex < 0 || cellIndex >= map.cellCount()) return;
    const HexCoordinates cellCoordinates = map.coordinatesAt(cellIndex);
    const std::int32_t   cellElevation   = map.elevation(cellCoordinates);
    const std::int32_t   targetIndex     = erosionTarget(map, cellCoordinates);
    if (targetIndex < 0) {
        removeErodible(scratch, cellIndex);
        return;
    }
    const HexCoordinates targetCoordinates = map.coordinatesAt(targetIndex);
    const std::int32_t   targetElevation   = map.elevation(targetCoordinates);

    // Erosion moves land around rather than removing it, so the cell itself stays
    // land; the running count only has to follow the erosion it performs.
    --scratch.landCells;
    writeElevation(map, cellCoordinates, cellElevation - 1);
    writeElevation(map, targetCoordinates, targetElevation + 1);
    requeueErodible(scratch, cellIndex, cellElevation);
    requeueErodible(scratch, targetIndex, targetElevation);

    if (!isErodible(map, cellCoordinates)) removeErodible(scratch, cellIndex);

    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (!map.getNeighbor(cellCoordinates, static_cast<HexDirection>(i), neighbour)) continue;
        const std::int32_t neighbourIndex = map.indexOf(neighbour);
        if (neighbourIndex < 0) continue;
        const std::int32_t neighbourElevation = map.elevation(neighbour);
        if (neighbourElevation != cellElevation + 2) continue;
        addErodible(scratch, neighbourIndex, neighbourElevation);
    }

    if (isErodible(map, targetCoordinates)) addErodible(scratch, targetIndex, map.elevation(targetCoordinates));

    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        HexCoordinates neighbour{};
        if (!map.getNeighbor(targetCoordinates, static_cast<HexDirection>(i), neighbour)) continue;
        const std::int32_t neighbourIndex = map.indexOf(neighbour);
        if (neighbourIndex < 0 || neighbourIndex == cellIndex) continue;
        if (map.elevation(neighbour) != targetElevation + 1) continue;
        if (!isErodible(map, neighbour)) removeErodible(scratch, neighbourIndex);
    }
}

/**
 * @brief Erodes the coastline until only the requested share stays erodible.
 *
 * The reference picks a uniformly random erodible cell on every step; the port
 * always takes the lowest cell from the shared bucket-list frontier, which makes
 * the result a pure function of the grid and seed while preserving the
 * reference's effect of wearing down the highest, thinnest land first.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param settings Normalised tunables.
 */
void erodeLand(HexMap& map, GeneratorScratch& scratch, const NormalizedSettings& settings) {
    const std::int32_t cellCount = map.cellCount();
    scratch.erodible.clear();
    scratch.erodibleFlag.assign(static_cast<std::size_t>(cellCount), 0u);
    // The erosion frontier is the only user of the context here, and it is keyed
    // by elevation rather than by a visited phase, so the new phase is unused.
    (void)scratch.search.beginPhase();

    for (std::int32_t index = 0; index < cellCount; ++index) {
        const HexCoordinates coordinates = map.coordinatesAt(index);
        if (isErodible(map, coordinates)) addErodible(scratch, index, map.elevation(coordinates));
    }

    const auto target = static_cast<std::size_t>(static_cast<float>(scratch.erodible.size()) *
                                                 (100.f - static_cast<float>(settings.erosionPercentage)) * 0.01f);
    while (scratch.erodible.size() > target) {
        std::int32_t index = -1;
        if (scratch.search.dequeue(index) != HexSearchPop::Cell) break;
        if (scratch.erodibleFlag[static_cast<std::size_t>(index)] == 0u) continue;
        erodeOnce(map, scratch, index);
    }
}

// ---------------------------------------------------------------------------
// Climate
// ---------------------------------------------------------------------------

/**
 * @brief Temperature of one cell from its row, elevation and a small jitter.
 *
 * The reference derives latitude from the row and additionally supports
 * northern/southern hemisphere modes, which `HexMapGeneratorSettings` does not
 * expose; the port therefore uses the reference's `North` mapping, where the
 * first row is the pole and the last row is the equator. The reference's noise
 * channel choice is reproduced by shifting the lattice channel with the value
 * `SetTerrainType` drew.
 *
 * @param map Source grid.
 * @param coordinates Cell to evaluate.
 * @param settings Normalised tunables.
 * @param jitterChannel Lattice channel used for the jitter, `[0, 3]`.
 * @return The temperature, not clamped to any range.
 */
[[nodiscard]] float determineTemperature(const HexMap& map, HexCoordinates coordinates,
                                         const NormalizedSettings& settings, std::int32_t jitterChannel) noexcept {
    const float latitude    = static_cast<float>(coordinates.z) / static_cast<float>(map.cellCountZ());
    float       temperature = settings.lowTemperature + (settings.highTemperature - settings.lowTemperature) * latitude;

    const std::int32_t viewElevation = map.values(coordinates).viewElevation();
    const float        span          = static_cast<float>(settings.elevationMaximum - settings.waterLevel + 1);
    const float        elevationFactor =
        1.f - static_cast<float>(viewElevation - settings.waterLevel) / (span == 0.f ? 1.f : span);
    temperature *= elevationFactor;

    const HexVec3 position = map.cellGroundPosition(coordinates);
    const auto    channel  = static_cast<std::uint32_t>(clampInt(jitterChannel, 0, 3)) * 0x9E3779B9u;
    const float   jitter =
        cellNoise(static_cast<std::int32_t>(position.x), static_cast<std::int32_t>(position.z), channel);
    return temperature + (jitter * 2.f - 1.f) * settings.temperatureJitter;
}

/**
 * @brief Advances one cell's moisture and clouds by one step.
 *
 * A direct port of the reference `EvolveClimate`: underwater cells replenish
 * moisture, clouds evaporate and rain out, extra cloud is pushed back as rain,
 * and the remainder disperses downwind, downhill and sideways.
 *
 * @param map Source grid.
 * @param scratch Reusable scratch buffers.
 * @param settings Normalised tunables.
 * @param cellIndex Cell to advance.
 */
void evolveClimate(const HexMap& map, GeneratorScratch& scratch, const NormalizedSettings& settings,
                   std::int32_t cellIndex) {
    const HexCoordinates coordinates = map.coordinatesAt(cellIndex);
    const HexValues      values      = map.values(coordinates);
    ClimateData          cellClimate = scratch.climate[static_cast<std::size_t>(cellIndex)];

    if (values.isUnderwater()) {
        cellClimate.moisture = 1.f;
        cellClimate.clouds += settings.evaporationFactor;
    } else {
        const float evaporation = cellClimate.moisture * settings.evaporationFactor;
        cellClimate.moisture -= evaporation;
        cellClimate.clouds += evaporation;
    }

    const float precipitation = cellClimate.clouds * settings.precipitationFactor;
    cellClimate.clouds -= precipitation;
    cellClimate.moisture += precipitation;

    const float maximumElevation = static_cast<float>(settings.elevationMaximum);
    const float cloudMaximum     = 1.f - static_cast<float>(values.viewElevation()) / (maximumElevation + 1.f);
    if (cellClimate.clouds > cloudMaximum) {
        cellClimate.moisture += cellClimate.clouds - cloudMaximum;
        cellClimate.clouds = cloudMaximum;
    }

    const HexDirection mainDispersalDirection = opposite(settings.windDirection);
    const float        cloudDispersal         = cellClimate.clouds * (1.f / (5.f + settings.windStrength));
    const float        runoff                 = cellClimate.moisture * settings.runoffFactor * (1.f / 6.f);
    const float        seepage                = cellClimate.moisture * settings.seepageFactor * (1.f / 6.f);

    for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
        const auto     direction = static_cast<HexDirection>(i);
        HexCoordinates neighbour{};
        if (!map.getNeighbor(coordinates, direction, neighbour)) continue;
        const std::int32_t neighbourIndex = map.indexOf(neighbour);
        if (neighbourIndex < 0) continue;

        ClimateData neighbourClimate = scratch.nextClimate[static_cast<std::size_t>(neighbourIndex)];
        if (direction == mainDispersalDirection) {
            neighbourClimate.clouds += cloudDispersal * settings.windStrength;
        } else {
            neighbourClimate.clouds += cloudDispersal;
        }

        const std::int32_t elevationDelta = map.values(neighbour).viewElevation() - values.viewElevation();
        if (elevationDelta < 0) {
            cellClimate.moisture -= runoff;
            neighbourClimate.moisture += runoff;
        } else if (elevationDelta == 0) {
            cellClimate.moisture -= seepage;
            neighbourClimate.moisture += seepage;
        }
        scratch.nextClimate[static_cast<std::size_t>(neighbourIndex)] = neighbourClimate;
    }

    ClimateData nextCell = scratch.nextClimate[static_cast<std::size_t>(cellIndex)];
    nextCell.moisture += cellClimate.moisture;
    if (nextCell.moisture > 1.f) nextCell.moisture = 1.f;
    scratch.nextClimate[static_cast<std::size_t>(cellIndex)] = nextCell;
    scratch.climate[static_cast<std::size_t>(cellIndex)]     = ClimateData{};
}

/**
 * @brief Simulates moisture transport for a fixed number of cycles.
 *
 * A direct port of the reference `CreateClimate`: 40 passes of downwind moisture
 * transport, with the accumulation and evaporation buffers swapped after each.
 *
 * @param map Source grid.
 * @param scratch Reusable scratch buffers.
 * @param settings Normalised tunables.
 */
void createClimate(const HexMap& map, GeneratorScratch& scratch, const NormalizedSettings& settings) {
    const auto  cellCount = static_cast<std::size_t>(map.cellCount());
    ClimateData initial;
    initial.moisture = settings.startingMoisture;
    scratch.climate.assign(cellCount, initial);
    scratch.nextClimate.assign(cellCount, ClimateData{});

    for (std::int32_t cycle = 0; cycle < 40; ++cycle) {
        for (std::int32_t index = 0; index < map.cellCount(); ++index) evolveClimate(map, scratch, settings, index);
        std::swap(scratch.climate, scratch.nextClimate);
    }
}

// ---------------------------------------------------------------------------
// Rivers
// ---------------------------------------------------------------------------

/**
 * @brief Builds the weighted list of cells a river may start from.
 *
 * A direct port of the reference weighting: a dry-land cell with more moisture
 * and more elevation above the water level is listed up to three times, so it is
 * proportionally more likely to be drawn as an origin.
 *
 * @param map Source grid.
 * @param scratch Reusable scratch buffers.
 * @param settings Normalised tunables.
 */
void collectRiverOrigins(const HexMap& map, GeneratorScratch& scratch, const NormalizedSettings& settings) {
    scratch.riverOrigins.clear();
    const float span = static_cast<float>(settings.elevationMaximum - settings.waterLevel);
    for (std::int32_t index = 0; index < map.cellCount(); ++index) {
        const HexCoordinates coordinates = map.coordinatesAt(index);
        const HexValues      values      = map.values(coordinates);
        if (values.isUnderwater()) continue;
        const float weight = scratch.climate[static_cast<std::size_t>(index)].moisture *
                             static_cast<float>(values.elevation() - settings.waterLevel) / (span == 0.f ? 1.f : span);
        if (weight > 0.75f) {
            scratch.riverOrigins.push_back(index);
            scratch.riverOrigins.push_back(index);
        }
        if (weight > 0.5f) scratch.riverOrigins.push_back(index);
        if (weight > 0.25f) scratch.riverOrigins.push_back(index);
    }
}

/**
 * @brief Carves one river from `originIndex` downhill until it floods or stops.
 *
 * A direct port of the reference `CreateRiver`: the frontier collects downhill
 * neighbours (triple weighted), reversed directions are discouraged while the
 * river keeps going, and joining an existing river ends the flow. A dead end
 * either abandons a length-one river or raises the cell's water level into a
 * lake, exactly as the reference does.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 * @param originIndex Cell the river starts from.
 * @return Number of cells the river traversed.
 */
std::int32_t createRiver(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings,
                         std::int32_t originIndex) {
    std::int32_t   length          = 1;
    HexCoordinates cellCoordinates = map.coordinatesAt(originIndex);
    HexValues      cellValues      = map.values(cellCoordinates);
    bool           cellUnderwater  = cellValues.isUnderwater();
    // The direction taken on the previous step; it is only read once the river is
    // longer than one cell, which is exactly when the reference starts using it.
    HexDirection direction = HexDirection::NE;

    while (!cellUnderwater) {
        std::int32_t minNeighborElevation = std::numeric_limits<std::int32_t>::max();
        scratch.flowDirections.clear();
        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            const auto     candidate = static_cast<HexDirection>(i);
            HexCoordinates neighbour{};
            if (!map.getNeighbor(cellCoordinates, candidate, neighbour)) continue;
            const std::int32_t neighbourIndex = map.indexOf(neighbour);
            if (neighbourIndex < 0) continue;
            const HexCellData* neighbourCell = map.cellAt(neighbourIndex);
            if (neighbourCell == nullptr) continue;

            const std::int32_t neighbourElevation = neighbourCell->values.elevation();
            if (neighbourElevation < minNeighborElevation) minNeighborElevation = neighbourElevation;

            if (neighbourIndex == originIndex || neighbourCell->flags.hasAnyRiverIn()) continue;

            const std::int32_t delta = neighbourElevation - cellValues.elevation();
            if (delta > 0) continue;

            if (neighbourCell->flags.hasAnyRiverOut()) {
                map.setOutgoingRiver(cellCoordinates, candidate).ignore("river join is validated by the map");
                return length;
            }

            if (delta < 0) {
                scratch.flowDirections.push_back(candidate);
                scratch.flowDirections.push_back(candidate);
                scratch.flowDirections.push_back(candidate);
            }
            // A reversal is discouraged once the river has a direction; on the first
            // step there is none, so every candidate keeps its weight.
            if (length == 1 || (candidate != next2(direction) && candidate != previous2(direction))) {
                scratch.flowDirections.push_back(candidate);
            }
            scratch.flowDirections.push_back(candidate);
        }

        if (scratch.flowDirections.empty()) {
            if (length == 1) return 0;
            if (minNeighborElevation >= cellValues.elevation()) {
                writeWaterLevel(map, cellCoordinates, minNeighborElevation);
                if (minNeighborElevation == cellValues.elevation()) {
                    writeElevation(map, cellCoordinates, minNeighborElevation - 1);
                }
            }
            break;
        }

        const auto         chosen        = scratch.flowDirections[static_cast<std::size_t>(
            random.index(static_cast<std::int32_t>(scratch.flowDirections.size())))];
        const std::int32_t outgoingIndex = map.indexOf(cellCoordinates.step(chosen));
        if (outgoingIndex < 0) break;

        map.setOutgoingRiver(cellCoordinates, chosen).ignore("flow direction was selected downhill");
        direction = chosen;
        ++length;

        if (minNeighborElevation >= cellValues.elevation() && random.chance(settings.extraLakeProbability)) {
            writeWaterLevel(map, cellCoordinates, cellValues.elevation());
            writeElevation(map, cellCoordinates, cellValues.elevation() - 1);
        }

        cellCoordinates = map.coordinatesAt(outgoingIndex);
        cellValues      = map.values(cellCoordinates);
        cellUnderwater  = cellValues.isUnderwater();
    }
    return length;
}

/**
 * @brief Spends the river budget on downhill rivers from weighted origins.
 *
 * A direct port of the reference `CreateRivers`: an origin is rejected when any
 * neighbour already carries a river or is flooded, and a river that cannot leave
 * its origin costs nothing.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 */
void createRivers(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings) {
    collectRiverOrigins(map, scratch, settings);
    std::int32_t riverBudget =
        roundToInt(static_cast<float>(scratch.landCells) * static_cast<float>(settings.riverPercentage) * 0.01f);
    while (riverBudget > 0 && !scratch.riverOrigins.empty()) {
        const auto position =
            static_cast<std::size_t>(random.index(static_cast<std::int32_t>(scratch.riverOrigins.size())));
        const std::int32_t originIndex = scratch.riverOrigins[position];
        scratch.riverOrigins[position] = scratch.riverOrigins.back();
        scratch.riverOrigins.pop_back();

        const HexCellData* origin = map.cellAt(originIndex);
        if (origin == nullptr || origin->flags.hasRiver()) continue;
        bool validOrigin = true;
        for (std::int32_t i = 0; i < kHexDirectionCount && validOrigin; ++i) {
            HexCoordinates neighbour{};
            if (!map.getNeighbor(map.coordinatesAt(originIndex), static_cast<HexDirection>(i), neighbour)) continue;
            const HexCellData* neighbourCell = map.cell(neighbour);
            if (neighbourCell == nullptr) continue;
            if (neighbourCell->flags.hasRiver() || neighbourCell->values.isUnderwater()) validOrigin = false;
        }
        if (!validOrigin) continue;
        riverBudget -= createRiver(map, scratch, random, settings, originIndex);
    }
}

// ---------------------------------------------------------------------------
// Terrain types
// ---------------------------------------------------------------------------

/**
 * @brief Chooses the terrain of a flooded cell from its neighbours.
 *
 * A direct port of the reference's underwater branch: water next to a lot of
 * relief turns to grass, water next to cliffs or slopes takes sand, stone or
 * grass, deep water becomes stone and shallow water becomes mud.
 *
 * @param map Source grid.
 * @param coordinates Flooded cell to classify.
 * @param settings Normalised tunables.
 * @return The terrain palette index.
 */
[[nodiscard]] std::int32_t underwaterTerrain(const HexMap& map, HexCoordinates coordinates,
                                             const NormalizedSettings& settings) {
    const std::int32_t elevation = map.elevation(coordinates);
    if (elevation == settings.waterLevel - 1) {
        const std::int32_t water  = waterLevelOf(map, coordinates);
        std::int32_t       cliffs = 0;
        std::int32_t       slopes = 0;
        for (std::int32_t i = 0; i < kHexDirectionCount; ++i) {
            HexCoordinates neighbour{};
            if (!map.getNeighbor(coordinates, static_cast<HexDirection>(i), neighbour)) continue;
            const std::int32_t delta = map.elevation(neighbour) - water;
            if (delta == 0) {
                ++slopes;
            } else if (delta > 0) {
                ++cliffs;
            }
        }
        if (cliffs + slopes > 3) return 1;
        if (cliffs > 0) return 3;
        if (slopes > 0) return 0;
        return 1;
    }
    if (elevation >= settings.waterLevel) return 1;
    if (elevation < 0) return 3;
    return 2;
}

/**
 * @brief Chooses the terrain and plant level of every cell.
 *
 * A direct port of the reference `SetTerrainType`: temperature and moisture pick
 * a biome, dry high ground becomes stone, the top elevation turns to snow, snow
 * carries no plants, a river adds one plant level, and the waterline carries a
 * sand band whose shape follows the surrounding relief.
 *
 * @param map Target grid.
 * @param scratch Reusable scratch buffers.
 * @param random Deterministic random source.
 * @param settings Normalised tunables.
 */
void setTerrainType(HexMap& map, GeneratorScratch& scratch, HexRandom& random, const NormalizedSettings& settings) {
    const std::int32_t jitterChannel = random.index(4);
    const std::int32_t rockDesertElevation =
        settings.elevationMaximum - (settings.elevationMaximum - settings.waterLevel) / 2;

    for (std::int32_t index = 0; index < map.cellCount(); ++index) {
        const HexCoordinates coordinates = map.coordinatesAt(index);
        HexValues            values      = map.values(coordinates);
        const float          temperature = determineTemperature(map, coordinates, settings, jitterChannel);
        const float          moisture    = scratch.climate[static_cast<std::size_t>(index)].moisture;

        if (!values.isUnderwater()) {
            std::int32_t temperatureBand = 0;
            for (; temperatureBand < 3; ++temperatureBand) {
                if (temperature < kTemperatureBands[temperatureBand]) break;
            }
            std::int32_t moistureBand = 0;
            for (; moistureBand < 3; ++moistureBand) {
                if (moisture < kMoistureBands[moistureBand]) break;
            }
            Biome biome = kBiomes[static_cast<std::size_t>(temperatureBand * 4 + moistureBand)];

            if (biome.terrain == 0) {
                if (values.elevation() >= rockDesertElevation) biome.terrain = 3;
            } else if (values.elevation() == settings.elevationMaximum) {
                biome.terrain = 4;
            }

            if (biome.terrain == 4) {
                biome.plant = 0;
            } else if (biome.plant < 3 && map.hasRiver(coordinates)) {
                ++biome.plant;
            }
            values = values.withTerrainType(biome.terrain).withPlantLevel(biome.plant);
        } else {
            std::int32_t terrain = underwaterTerrain(map, coordinates, settings);
            if (terrain == 1 && temperature < kTemperatureBands[0]) terrain = 2;
            values = values.withTerrainType(terrain);
        }
        map.setCellState(coordinates, values, map.flags(coordinates))
            .ignore("terrain classification writes the values it just read back");
    }
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

/**
 * @brief Leaves the grid dirty and ready for a fog-of-war reveal.
 *
 * `HexMap::reset` already leaves every cell *explorable but unexplored* and the
 * generator never latches the explored flag, so this only has to restate the
 * contract by marking every chunk dirty after the pipeline mutated the cells.
 *
 * @param map Target grid.
 */
void finalizeGrid(HexMap& map) { map.markAllChunksDirty(); }

}  // namespace

Result<void> generateHexMap(HexMap& map, const HexMapGeneratorSettings& settings) {
    if (map.empty()) return invalidArgument("cannot generate a hex map into an empty grid");

    const NormalizedSettings normalized = normalize(settings);

    std::vector<MapRegion> regions;
    createRegions(normalized.regionCount, map.cellCountX(), map.cellCountZ(), normalized, regions);
    if (!regionsUsable(regions, normalized.regionCount))
        return invalidArgument("generator regions do not fit the hex map; enlarge the map or shrink the borders");

    GeneratorScratch scratch;
    scratch.search.resize(map.cellCount());
    scratch.erodibleFlag.assign(static_cast<std::size_t>(map.cellCount()), 0u);
    scratch.climate.assign(static_cast<std::size_t>(map.cellCount()), ClimateData{});
    scratch.nextClimate.assign(static_cast<std::size_t>(map.cellCount()), ClimateData{});
    scratch.landCells = 0;

    HexRandom random(settings.seed);

    for (std::int32_t index = 0; index < map.cellCount(); ++index) {
        const HexCoordinates coordinates = map.coordinatesAt(index);
        writeElevation(map, coordinates, normalized.elevationMinimum);
        writeWaterLevel(map, coordinates, normalized.waterLevel);
    }

    createLand(map, scratch, random, normalized, regions);
    erodeLand(map, scratch, normalized);
    createClimate(map, scratch, normalized);
    createRivers(map, scratch, random, normalized);
    setTerrainType(map, scratch, random, normalized);
    finalizeGrid(map);

    return Result<void>::success();
}

}  // namespace eve::hexmap
