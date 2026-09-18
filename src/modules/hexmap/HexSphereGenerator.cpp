#include "hexmap/HexSphereGenerator.h"

#include "common/Diagnostic.h"
#include "hexmap/HexCell.h"
#include "hexmap/HexMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::hexmap {
namespace {

// --- 3D value noise ---------------------------------------------------------
//
// `HexNoise` is a planar field sampled in XZ. Sampling it with a cell's direction
// would degenerate at the poles - the direction's XZ projection goes to zero there -
// and would seam where the longitude wraps, so the spherical generator carries its
// own lattice. It is the spherical analogue of the planar field, not a second
// source of truth for it: nothing else in the module samples it.

/** @brief Integer hash of a lattice point; the whole noise field derives from it. */
[[nodiscard]] std::uint32_t hashLattice(int x, int y, int z, std::uint32_t seed) noexcept {
    std::uint32_t h = seed ^ 0x9e3779b9u;
    h ^= static_cast<std::uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
    h ^= static_cast<std::uint32_t>(z) * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

/** @brief Lattice value in `[-1, 1)`. */
[[nodiscard]] float latticeValue(int x, int y, int z, std::uint32_t seed) noexcept {
    return static_cast<float>(hashLattice(x, y, z, seed) & 0xffffffu) / 8388608.0f - 1.f;
}

/** @brief Hermite fade curve of the trilinear blend. */
[[nodiscard]] float fade(float t) noexcept { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

[[nodiscard]] float valueNoise3(HexVec3 point, std::uint32_t seed) noexcept {
    const float fx = std::floor(point.x);
    const float fy = std::floor(point.y);
    const float fz = std::floor(point.z);
    const int   x0 = static_cast<int>(fx);
    const int   y0 = static_cast<int>(fy);
    const int   z0 = static_cast<int>(fz);
    const float tx = fade(point.x - fx);
    const float ty = fade(point.y - fy);
    const float tz = fade(point.z - fz);

    const float c000 = latticeValue(x0, y0, z0, seed);
    const float c100 = latticeValue(x0 + 1, y0, z0, seed);
    const float c010 = latticeValue(x0, y0 + 1, z0, seed);
    const float c110 = latticeValue(x0 + 1, y0 + 1, z0, seed);
    const float c001 = latticeValue(x0, y0, z0 + 1, seed);
    const float c101 = latticeValue(x0 + 1, y0, z0 + 1, seed);
    const float c011 = latticeValue(x0, y0 + 1, z0 + 1, seed);
    const float c111 = latticeValue(x0 + 1, y0 + 1, z0 + 1, seed);

    const auto blend = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = blend(c000, c100, tx);
    const float x10 = blend(c010, c110, tx);
    const float x01 = blend(c001, c101, tx);
    const float x11 = blend(c011, c111, tx);
    return blend(blend(x00, x10, ty), blend(x01, x11, ty), tz);
}

/** @brief Fractal Brownian motion of the 3D field, remapped into `[0, 1]`. */
[[nodiscard]] float fbm3(HexVec3 point, std::int32_t octaves, std::uint32_t seed) noexcept {
    float       total     = 0.f;
    float       amplitude = 1.f;
    float       sum       = 0.f;
    HexVec3     cursor    = point;
    for (std::int32_t octave = 0; octave < octaves; ++octave) {
        total += valueNoise3(cursor, seed + static_cast<std::uint32_t>(octave) * 7919u) * amplitude;
        sum += amplitude;
        amplitude *= 0.5f;
        cursor = HexVec3{cursor.x * 2.03f + 13.1f, cursor.y * 2.03f - 7.7f, cursor.z * 2.03f + 3.3f};
    }
    if (!(sum > 0.f)) return 0.5f;
    return std::clamp(total / sum * 0.5f + 0.5f, 0.f, 1.f);
}

/** @brief Value at a `fraction` quantile of an already sorted vector. */
[[nodiscard]] float quantile(const std::vector<float>& sorted, float fraction) noexcept {
    if (sorted.empty()) return 0.f;
    const float position = std::clamp(fraction, 0.f, 1.f) * static_cast<float>(sorted.size() - 1);
    return sorted[static_cast<std::size_t>(std::lround(position))];
}

/**
 * @brief Terrain palette entry chosen from elevation, moisture and latitude.
 *
 * Latitude is `direction.y`, so `|latitude|` is the sine of the latitude and the
 * bands below are *area* fractions of the sphere, not angular ones: `|latitude| > 0.86`
 * is the 14% of the surface inside 59 degrees, and `> 0.62` would already be 38%.
 * That is why the snow caps are placed on the highest band only - the planar
 * generator has no such coupling between a coordinate and a share of the map.
 */
[[nodiscard]] std::int32_t terrainFor(std::int32_t elevation, float moisture, float latitude,
                                      std::int32_t waterLevel) noexcept {
    const float absLatitude = std::fabs(latitude);
    if (absLatitude > 0.80f) return static_cast<std::int32_t>(HexTerrainType::Snow);
    if (elevation >= 7) return static_cast<std::int32_t>(HexTerrainType::Snow);
    if (elevation >= 5) return static_cast<std::int32_t>(HexTerrainType::Stone);
    if (absLatitude > 0.72f && elevation >= 2) return static_cast<std::int32_t>(HexTerrainType::Snow);
    // Flooded floor: dark, and mostly hidden by the ocean surface.
    if (elevation <= waterLevel) return static_cast<std::int32_t>(HexTerrainType::Mud);
    if (elevation == waterLevel + 1) return static_cast<std::int32_t>(HexTerrainType::Sand);
    if (elevation >= 4) return static_cast<std::int32_t>(HexTerrainType::Stone);
    if (moisture < 0.36f && absLatitude < 0.45f) return static_cast<std::int32_t>(HexTerrainType::Sand);
    if (moisture > 0.62f) return static_cast<std::int32_t>(HexTerrainType::Mud);
    return static_cast<std::int32_t>(HexTerrainType::Grass);
}

}  // namespace

Result<void> generateSphereMap(HexSphereMap& map, const HexSphereGeneratorSettings& settings) {
    if (map.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "cannot generate into an empty hex sphere map",
                                                       "hexmap.sphere.generate"));
    }

    const std::int32_t cellCount = map.cellCount();
    const float        frequency = std::max(0.05f, settings.frequency);
    const std::int32_t octaves   = std::clamp(settings.octaves, 1, 8);

    std::vector<float> continents(static_cast<std::size_t>(cellCount));
    std::vector<float> moisture(static_cast<std::size_t>(cellCount));
    for (HexSphereCell cell = 0; cell < cellCount; ++cell) {
        const HexVec3 direction = map.direction(cell);
        const HexVec3 coarse{direction.x * frequency, direction.y * frequency, direction.z * frequency};
        const HexVec3 damp{direction.x * frequency * 2.7f + 41.3f, direction.y * frequency * 2.7f - 17.9f,
                           direction.z * frequency * 2.7f + 5.1f};
        continents[static_cast<std::size_t>(cell)] = fbm3(coarse, octaves, settings.seed);
        moisture[static_cast<std::size_t>(cell)]   = fbm3(damp, 3, settings.seed ^ 0x5bf03635u);
    }

    // The sea level is chosen from the field itself rather than assumed, so
    // `landPercentage` is what decides how much of the planet is dry - the noise's own
    // distribution stays whatever the octave count made it.
    std::vector<float> sorted = continents;
    std::sort(sorted.begin(), sorted.end());
    const float landFraction = std::clamp(static_cast<float>(settings.landPercentage) / 100.f, 0.f, 1.f);
    const float seaLevel     = quantile(sorted, 1.f - landFraction);

    float highest = 0.f;
    float lowest  = 0.f;
    for (const float value : continents) {
        highest = std::max(highest, value - seaLevel);
        lowest  = std::max(lowest, seaLevel - value);
    }
    highest = std::max(highest, 1e-4f);
    lowest  = std::max(lowest, 1e-4f);

    const std::int32_t waterLevel = std::clamp(settings.waterLevel, HexMetrics::kMinElevation, HexMetrics::kMaxElevation);
    const float        landSpan   = static_cast<float>(HexMetrics::kMaxElevation - waterLevel - 1);
    const float        oceanSpan  = static_cast<float>(waterLevel - HexMetrics::kMinElevation);

    for (HexSphereCell cell = 0; cell < cellCount; ++cell) {
        const float above = continents[static_cast<std::size_t>(cell)] - seaLevel;
        const float unit  = above / (above >= 0.f ? highest : lowest);

        std::int32_t elevation = 0;
        if (above >= 0.f) {
            elevation = waterLevel + 1 + static_cast<std::int32_t>(std::lround(unit * landSpan));
        } else {
            elevation = waterLevel + static_cast<std::int32_t>(std::lround(unit * oceanSpan));
        }
        elevation = std::clamp(elevation, HexMetrics::kMinElevation, HexMetrics::kMaxElevation);

        const float latitude = map.direction(cell).y;
        map.setElevation(cell, elevation).ignore("generator clamps every elevation itself");
        map.setTerrainType(cell, terrainFor(elevation, moisture[static_cast<std::size_t>(cell)], latitude, waterLevel))
            .ignore("generator sets every terrain type itself");
        map.setWaterLevel(cell, elevation <= waterLevel ? waterLevel : 0)
            .ignore("generator sets every water level itself");
    }

    map.markAllDirty();
    return Result<void>::success();
}

}  // namespace eve::hexmap
