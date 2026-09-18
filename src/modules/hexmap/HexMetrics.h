#pragma once

/** @file HexMetrics.h @brief Pointy-top hex metrics, directions and vertex helpers. */

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Minimal 3-component float vector used by the hex mesh builders.
 *
 * The hex map needs only a handful of vector operations, so it carries its own
 * value type and keeps rendering-backend types out of its public headers; see the
 * module boundary rules in AGENTS.md.
 */
struct HexVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

[[nodiscard]] constexpr HexVec3 operator+(HexVec3 a, HexVec3 b) noexcept {
    return HexVec3{a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] constexpr HexVec3 operator-(HexVec3 a, HexVec3 b) noexcept {
    return HexVec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] constexpr HexVec3  operator*(HexVec3 a, float s) noexcept { return HexVec3{a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] constexpr HexVec3  operator*(float s, HexVec3 a) noexcept { return a * s; }
[[nodiscard]] constexpr HexVec3& operator+=(HexVec3& a, HexVec3 b) noexcept {
    a = a + b;
    return a;
}

/** @brief Four-component float vector, matching the reference four-channel noise sample. */
struct HexVec4 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;
};

/** @brief Linear interpolation between two positions. */
[[nodiscard]] constexpr HexVec3 lerp(HexVec3 a, HexVec3 b, float t) noexcept {
    return HexVec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/** @brief Blends the three terrain-layer weights of a hex vertex. */
[[nodiscard]] constexpr HexVec3 weightsLerp(HexVec3 a, HexVec3 b, float t) noexcept { return lerp(a, b, t); }

/** @brief Hex facing directions, counter-clockwise from north-east. */
enum class HexDirection : std::int32_t { NE = 0, E = 1, SE = 2, SW = 3, W = 4, NW = 5 };

/** @brief Number of hex edges / facing directions. */
inline constexpr std::int32_t kHexDirectionCount = 6;

/** @brief The direction opposite to `d`. */
[[nodiscard]] constexpr HexDirection opposite(HexDirection d) noexcept {
    const auto v = static_cast<std::int32_t>(d);
    return static_cast<HexDirection>(v < 3 ? v + 3 : v - 3);
}

/** @brief The next direction counter-clockwise (NE wraps to NW). */
[[nodiscard]] constexpr HexDirection previous(HexDirection d) noexcept {
    return d == HexDirection::NE ? HexDirection::NW : static_cast<HexDirection>(static_cast<int>(d) - 1);
}

/** @brief The next direction clockwise (NW wraps to NE). */
[[nodiscard]] constexpr HexDirection next(HexDirection d) noexcept {
    return d == HexDirection::NW ? HexDirection::NE : static_cast<HexDirection>(static_cast<int>(d) + 1);
}

/** @brief Two steps counter-clockwise. */
[[nodiscard]] constexpr HexDirection previous2(HexDirection d) noexcept {
    const auto v = static_cast<std::int32_t>(d) - 2;
    return static_cast<HexDirection>(v < 0 ? v + 6 : v);
}

/** @brief Two steps clockwise. */
[[nodiscard]] constexpr HexDirection next2(HexDirection d) noexcept {
    const auto v = static_cast<std::int32_t>(d) + 2;
    return static_cast<HexDirection>(v > 5 ? v - 6 : v);
}

/** @brief Relationship between two neighbouring cells of different elevation. */
enum class HexEdgeType : std::int32_t { Flat = 0, Slope = 1, Cliff = 2 };

/** @brief The relationship between two elevations (single-step changes are slopes). */
[[nodiscard]] constexpr HexEdgeType edgeType(int elevation1, int elevation2) noexcept {
    if (elevation1 == elevation2) return HexEdgeType::Flat;
    const int delta = elevation2 - elevation1;
    if (delta == 1 || delta == -1) return HexEdgeType::Slope;
    return HexEdgeType::Cliff;
}

/**
 * @brief Hexagon metrics shared by the map model and every mesh builder.
 *
 * The layout matches the Catlike Coding "Hex Map" reference project: pointy-top
 * hexes on the XZ plane, +X east, +Z north, odd rows shifted half a cell to the
 * right, Y up. All radii are expressed in world units; `outerRadius` is settable
 * so a caller can rescale the whole map without touching the derived ratios.
 */
class HexMetrics {
public:
    /** @brief Ratio of the outer radius to the inner radius. */
    static constexpr float kOuterToInner = 0.866025404f;
    /** @brief Ratio of the inner radius to the outer radius. */
    static constexpr float kInnerToOuter = 1.f / kOuterToInner;
    /** @brief Default outer (corner) radius of one hex cell. */
    static constexpr float kDefaultOuterRadius = 10.f;
    /** @brief Vertical distance between two elevation levels. */
    static constexpr float kElevationStep = 3.f;
    /** @brief Terraces generated per slope. */
    static constexpr int kTerracesPerSlope = 2;
    /** @brief Terrace interpolation steps per slope (`terracesPerSlope * 2 + 1`). */
    static constexpr int kTerraceSteps = kTerracesPerSlope * 2 + 1;
    /** @brief Horizontal fraction of one terrace interpolation step. */
    static constexpr float kHorizontalTerraceStepSize = 1.f / static_cast<float>(kTerraceSteps);
    /** @brief Vertical fraction of one terrace interpolation step. */
    static constexpr float kVerticalTerraceStepSize = 1.f / static_cast<float>(kTerracesPerSlope + 1);
    /** @brief Factor of the solid, uniform-colour hex inscribed in a cell. */
    static constexpr float kSolidFactor = 0.8f;
    /** @brief Factor of the blend region between two neighbouring cells. */
    static constexpr float kBlendFactor = 1.f - kSolidFactor;
    /** @brief Factor of the solid water surface inscribed in a cell. */
    static constexpr float kWaterFactor = 0.6f;
    /** @brief Factor of the water blend region between two cells. */
    static constexpr float kWaterBlendFactor = 1.f - kWaterFactor;
    /** @brief Strength of the XZ position perturbation. */
    static constexpr float kCellPerturbStrength = 4.f;
    /** @brief Strength of the vertical elevation perturbation. */
    static constexpr float kElevationPerturbStrength = 1.5f;
    /** @brief Elevation offset of a carved river bed relative to the cell. */
    static constexpr float kStreamBedElevationOffset = -1.75f;
    /** @brief Elevation offset of a water surface relative to the water level. */
    static constexpr float kWaterElevationOffset = -0.5f;
    /** @brief Width of the river channel. */
    static constexpr float kRiverSurfaceScale = 0.6f;
    /** @brief Height of a city/farm wall. */
    static constexpr float kWallHeight = 4.f;
    /** @brief Vertical wall offset; negative so walls do not float above the surface. */
    static constexpr float kWallYOffset = -1.f;
    /** @brief Wall thickness. */
    static constexpr float kWallThickness = 0.75f;
    /** @brief Wall elevation offset, matching the vertical terrace step size. */
    static constexpr float kWallElevationOffset = kVerticalTerraceStepSize;
    /** @brief Chunk size in the X dimension. */
    static constexpr int kChunkSizeX = 5;
    /** @brief Chunk size in the Z dimension. */
    static constexpr int kChunkSizeZ = 5;
    /** @brief Minimum editable elevation. */
    static constexpr int kMinElevation = -4;
    /** @brief Maximum editable elevation. */
    static constexpr int kMaxElevation = 8;

    /** @brief Outer (corner) radius of one hex cell in world units. */
    static constexpr float kOuterRadius = kDefaultOuterRadius;
    /** @brief Inner (edge) radius of one hex cell in world units. */
    static constexpr float kInnerRadius = kDefaultOuterRadius * kOuterToInner;
    /** @brief Width of one hex column. */
    static constexpr float kInnerDiameter = kInnerRadius * 2.f;
    /** @brief Row spacing between hex rows. */
    static constexpr float kRowSpacing = kDefaultOuterRadius * 1.5f;

    /** @brief Radius of a hex cell in world units. */
    [[nodiscard]] static constexpr float outerRadius() noexcept { return kOuterRadius; }
    /** @brief Inner radius of a hex cell. */
    [[nodiscard]] static constexpr float innerRadius() noexcept { return kInnerRadius; }
    /** @brief Width of one hex column. */
    [[nodiscard]] static constexpr float innerDiameter() noexcept { return kInnerDiameter; }
    /** @brief Row spacing between hex rows. */
    [[nodiscard]] static constexpr float rowSpacing() noexcept { return kRowSpacing; }

    /** @brief Outer corner `direction` of a unit hex, scaled by `scale`. */
    [[nodiscard]] static HexVec3 corner(HexDirection direction, float scale = 1.f) noexcept {
        const float r = kOuterRadius * scale;
        const float i = kInnerRadius * scale;
        switch (direction) {
            case HexDirection::NE: return HexVec3{0.f, 0.f, r};
            case HexDirection::E: return HexVec3{i, 0.f, 0.5f * r};
            case HexDirection::SE: return HexVec3{i, 0.f, -0.5f * r};
            case HexDirection::SW: return HexVec3{0.f, 0.f, -r};
            case HexDirection::W: return HexVec3{-i, 0.f, -0.5f * r};
            case HexDirection::NW: return HexVec3{-i, 0.f, 0.5f * r};
        }
        return HexVec3{0.f, 0.f, r};
    }

    /** @brief First (counter-clockwise) outer corner of `direction`. */
    [[nodiscard]] static HexVec3 firstCorner(HexDirection d) noexcept { return corner(d); }
    /** @brief Second (clockwise) outer corner of `direction`. */
    [[nodiscard]] static HexVec3 secondCorner(HexDirection d) noexcept { return corner(next(d)); }
    /** @brief First solid corner of `direction`. */
    [[nodiscard]] static HexVec3 firstSolidCorner(HexDirection d) noexcept { return corner(d, kSolidFactor); }
    /** @brief Second solid corner of `direction`. */
    [[nodiscard]] static HexVec3 secondSolidCorner(HexDirection d) noexcept { return corner(next(d), kSolidFactor); }
    /** @brief Midpoint of the solid edge of `direction`. */
    [[nodiscard]] static HexVec3 solidEdgeMiddle(HexDirection d) noexcept {
        return (corner(d) + corner(next(d))) * (0.5f * kSolidFactor);
    }
    /** @brief First water corner of `direction`. */
    [[nodiscard]] static HexVec3 firstWaterCorner(HexDirection d) noexcept { return corner(d, kWaterFactor); }
    /** @brief Second water corner of `direction`. */
    [[nodiscard]] static HexVec3 secondWaterCorner(HexDirection d) noexcept { return corner(next(d), kWaterFactor); }
    /** @brief Bridge vector from the solid edge of `direction` to the neighbour. */
    [[nodiscard]] static HexVec3 bridge(HexDirection d) noexcept {
        return (corner(d) + corner(next(d))) * kBlendFactor;
    }
    /** @brief Bridge vector from the water edge of `direction` to the neighbour. */
    [[nodiscard]] static HexVec3 waterBridge(HexDirection d) noexcept {
        return (corner(d) + corner(next(d))) * kWaterBlendFactor;
    }

    /**
     * @brief Interpolates a position along a terraced slope.
     *
     * @param a Start position (low side).
     * @param b End position (high side).
     * @param step Terrace step in `[0, kTerraceSteps]`.
     */
    [[nodiscard]] static HexVec3 terraceLerp(HexVec3 a, HexVec3 b, int step) noexcept {
        const float h = static_cast<float>(step) * kHorizontalTerraceStepSize;
        a.x += (b.x - a.x) * h;
        a.z += (b.z - a.z) * h;
        const float v = static_cast<float>((step + 1) / 2) * kVerticalTerraceStepSize;
        a.y += (b.y - a.y) * v;
        return a;
    }

    /** @brief Interpolates terrain weights along a terraced slope. */
    [[nodiscard]] static HexVec3 terraceLerpWeights(HexVec3 a, HexVec3 b, int step) noexcept {
        const float h = static_cast<float>(step) * kHorizontalTerraceStepSize;
        return lerp(a, b, h);
    }

    /** @brief Position of the midpoint of a wall between two elevations. */
    [[nodiscard]] static HexVec3 wallLerp(HexVec3 near, HexVec3 far) noexcept {
        near.x += (far.x - near.x) * 0.5f;
        near.z += (far.z - near.z) * 0.5f;
        const float v = near.y < far.y ? kWallElevationOffset : (1.f - kWallElevationOffset);
        near.y += (far.y - near.y) * v + kWallYOffset;
        return near;
    }

    /** @brief Half-thickness offset applied to a wall segment. */
    [[nodiscard]] static HexVec3 wallThicknessOffset(HexVec3 near, HexVec3 far) noexcept {
        float       dx  = far.x - near.x;
        float       dz  = far.z - near.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len <= 1e-6f) return HexVec3{0.f, 0.f, 0.f};
        const float s = (kWallThickness * 0.5f) / len;
        return HexVec3{dx * s, 0.f, dz * s};
    }

    /** @brief Y coordinate of a cell's elevation surface. */
    [[nodiscard]] static float elevationY(int elevation) noexcept {
        return static_cast<float>(elevation) * kElevationStep;
    }
    /** @brief Y coordinate of a river bed inside a cell. */
    [[nodiscard]] static float streamBedY(int elevation) noexcept {
        return static_cast<float>(elevation) * kElevationStep + kStreamBedElevationOffset;
    }
    /** @brief Y coordinate of a water surface at `waterLevel`. */
    [[nodiscard]] static float waterSurfaceY(int waterLevel) noexcept {
        return static_cast<float>(waterLevel) * kElevationStep + kWaterElevationOffset;
    }

    /** @brief Distance from a cell centre to its nearest edge midpoint. */
    [[nodiscard]] static float solidRadius() noexcept { return innerRadius() * kSolidFactor; }
};

/** @brief Five sample points along one hex edge, from v1 to v5. */
struct EdgeVertices {
    HexVec3 v1;
    HexVec3 v2;
    HexVec3 v3;
    HexVec3 v4;
    HexVec3 v5;

    EdgeVertices() = default;

    /** @brief Evenly samples the segment `c1 -> c2` at 0, 0.25, 0.5, 0.75 and 1. */
    EdgeVertices(HexVec3 c1, HexVec3 c2) noexcept {
        v1 = c1;
        v2 = lerp(c1, c2, 0.25f);
        v3 = lerp(c1, c2, 0.5f);
        v4 = lerp(c1, c2, 0.75f);
        v5 = c2;
    }

    /** @brief Samples the segment using an explicit outer step for v2/v4. */
    EdgeVertices(HexVec3 c1, HexVec3 c2, float outerStep) noexcept {
        v1 = c1;
        v2 = lerp(c1, c2, outerStep);
        v3 = lerp(c1, c2, 0.5f);
        v4 = lerp(c1, c2, 1.f - outerStep);
        v5 = c2;
    }

    /** @brief Terrace-interpolates every sample between two edges. */
    [[nodiscard]] static EdgeVertices terraceLerp(const EdgeVertices& a, const EdgeVertices& b, int step) noexcept {
        EdgeVertices result;
        result.v1 = HexMetrics::terraceLerp(a.v1, b.v1, step);
        result.v2 = HexMetrics::terraceLerp(a.v2, b.v2, step);
        result.v3 = HexMetrics::terraceLerp(a.v3, b.v3, step);
        result.v4 = HexMetrics::terraceLerp(a.v4, b.v4, step);
        result.v5 = HexMetrics::terraceLerp(a.v5, b.v5, step);
        return result;
    }
};

}  // namespace eve::hexmap
