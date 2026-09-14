#include "procgen/algorithms/FoliageCluster.h"

#include "procgen/PointSet.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

V3 norm(V3 a) {
    const float n = std::sqrt(std::max(1e-12f, dot(a, a)));
    return mul(a, 1.f / n);
}

float randomRange(std::mt19937 &rng, float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

/** @brief Orthonormal frame with `axis` as the third basis vector. */
void basisFor(V3 axis, V3 &right, V3 &forward) {
    axis            = norm(axis);
    const V3 helper = std::fabs(axis.y) < 0.92f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
    right           = norm(cross(helper, axis));
    forward         = norm(cross(axis, right));
}

struct V2 {
    float x = 0.f, y = 0.f;
};

/**
 * @brief Blue-noise leaf centres inside one cluster plane's disk.
 *
 * Reuses the module's Bridson sampler (`procgen.poisson.*` covers its
 * determinism, minimum spacing and capping) rather than growing a second
 * implementation: sample the plane's bounding square, then keep the inscribed
 * disk. Filtering a blue-noise set preserves the minimum distance, and the
 * spacer below is solved so the disk - not the square - lands on the leaf cap.
 *
 * @param seed Per-plane RNG seed; the same seed reproduces the same centres.
 * @param radius Disk radius in world units.
 * @param minDistance Blue-noise spacing in world units.
 * @param maxLeaves Leaf cap for this plane.
 * @return Centres relative to the disk centre, in plane coordinates.
 */
std::vector<V2> samplePlaneCentres(std::uint32_t seed, float radius, float minDistance, int maxLeaves) {
    std::vector<V2> centres;
    if (radius <= 0.f || minDistance <= 0.f || maxLeaves <= 0) return centres;

    // Hexagonal packing fits about 2A / (sqrt(3) d^2) centres in area A. Solving
    // for the disk makes the *disk* land on the leaf cap; the enclosing square
    // then saturates above it, so the budget below is only a safety cap and the
    // fill is never truncated (a truncated Bridson fill is spatially biased).
    const float capSpacing   = std::sqrt(2.f * kPi * radius * radius / (std::sqrt(3.f) * float(maxLeaves)));
    const float spacing      = std::max(minDistance, capSpacing);
    const int   squareSide   = std::max(2, int(std::ceil(2.f * radius)));
    const float squareArea   = float(squareSide) * float(squareSide);
    const int   squareBudget = int(1.3f * float(maxLeaves) * squareArea / std::max(kPi * radius * radius, 1e-6f)) + 8;

    const PointSet square = poissonDiskPoints(squareSide, squareSide, spacing, seed, squareBudget);
    const float    half   = 0.5f * float(squareSide);
    // `poissonDiskPoints` samples the XZ plane, so the second area axis is z.
    for (const ProcgenPoint &point : square.points()) {
        const float dx = point.x - half;
        const float dz = point.z - half;
        if (dx * dx + dz * dz > radius * radius) continue;
        centres.push_back({dx, dz});
        if (int(centres.size()) >= maxLeaves) break;
    }
    return centres;
}
/**
 * @brief Append one leaf as a rounded six-point petal.
 *
 * Petals are fan-triangulated from the base vertex, which lies on the convex
 * hull, so no centre vertex is needed. `normalAt` maps a world position to its
 * shading normal, which is how the cluster's spherical normal is transferred
 * onto an otherwise flat card.
 */
template <typename NormalAt>
void addPetal(MeshBuild &out, V3 center, V3 right, V3 forward, float size, float roll, float u0, float u1,
              bool doubleSided, const NormalAt &normalAt) {
    const float c = std::cos(roll), s = std::sin(roll);
    const V3    axisX = add(mul(right, c), mul(forward, s));
    const V3    axisY = add(mul(right, -s), mul(forward, c));

    // Lanceolate outline: pointed base, widest just above the middle, single
    // tip. Six points read as a leaf only when the shoulders sit low and the
    // taper towards the tip stays long; a symmetric hexagon reads as a pebble.
    constexpr float kOutlineX[6] = {0.f, -0.42f, -0.50f, 0.f, 0.50f, 0.42f};
    constexpr float kOutlineY[6] = {-0.50f, -0.24f, 0.06f, 0.50f, 0.06f, -0.24f};

    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i < 6; ++i) {
        const V3    p = add(center, add(mul(axisX, kOutlineX[i] * size), mul(axisY, kOutlineY[i] * size)));
        const V3    n = normalAt(p);
        const float u = u0 + (0.5f + kOutlineX[i]) * (u1 - u0);
        const float v = 0.5f + kOutlineY[i];
        out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, u, v);
    }
    for (int i = 1; i < 5; ++i) {
        out.addTriangle(base, base + uint32_t(i), base + uint32_t(i + 1));
    }
    if (doubleSided) {
        for (int i = 1; i < 5; ++i) {
            out.addTriangle(base, base + uint32_t(i + 1), base + uint32_t(i));
        }
    }
}

}  // namespace

int addFoliageCluster(MeshBuild &out, const FoliageClusterDesc &desc) {
    const float radius      = std::max(1e-3f, desc.radius);
    const float leafSize    = std::max(1e-3f, desc.leafSize);
    const float leafSpacing = std::max(0.05f, desc.leafSpacing);
    const int   planes      = std::clamp(desc.planes, 1, 24);
    const int   capPlanes   = std::clamp(desc.capPlanes, 0, 8);
    const float tiltMax     = std::clamp(desc.tiltDegrees, 0.f, 80.f) * kPi / 180.f;
    const float planeOffset = std::clamp(desc.planeOffset, 0.f, 0.6f) * radius;
    const float scaleVar    = std::clamp(desc.planeScaleVariation, 0.f, 0.6f);
    const float leafJitter  = std::clamp(desc.leafJitter, 0.f, 0.5f) * radius;
    const float sizeVar     = std::clamp(desc.leafScaleVariation, 0.f, 0.6f);
    const int   maxLeaves   = std::clamp(desc.maxLeavesPerPlane, 1, 256);
    const float rounding    = std::clamp(desc.normalRounding, 0.f, 1.f);
    const float uvMin       = std::min(desc.uvMin, desc.uvMax);
    const float uvMax       = std::max(desc.uvMin, desc.uvMax);

    const V3     center{desc.centerX, desc.centerY, desc.centerZ};
    std::mt19937 rng(desc.seed);

    // Requested blue-noise spacing. `samplePlaneCentres` widens it further when
    // it would otherwise overshoot the per-plane leaf cap.
    const float minDistance = leafSize * leafSpacing;

    // Vertex normals come from the cluster sphere so a bundle of flat cards
    // still shades as one rounded mass. Near the centre the sphere direction is
    // ill-conditioned, so the blend falls back to the plane normal there.
    const float roundingDistance = std::max(1e-4f, radius * 0.35f);
    const auto  normalAt         = [&](V3 p, V3 planeNormal) {
        const V3    radial = sub(p, center);
        const float length = std::sqrt(dot(radial, radial));
        const float weight = rounding * std::clamp(length / roundingDistance, 0.f, 1.f);
        if (length < 1e-5f || weight <= 0.f) return planeNormal;
        const V3 blended = add(mul(mul(radial, 1.f / length), weight), mul(planeNormal, 1.f - weight));
        if (dot(blended, blended) < 1e-6f) return planeNormal;
        return norm(blended);
    };

    const int   planeCount = planes + capPlanes;
    const float yawPhase   = randomRange(rng, 0.f, 2.f * kPi);
    const float yawStep    = 2.f * kPi / float(planeCount);
    int         leaves     = 0;
    for (int index = 0; index < planeCount; ++index) {
        const bool cap = index >= planes;

        // Ring planes take an even share of the circle plus a jitter, so the
        // bundle never reads as a regular polygon from any angle.
        const float yaw =
            cap ? randomRange(rng, 0.f, 2.f * kPi) : yawPhase + (float(index) + randomRange(rng, 0.2f, 0.8f)) * yawStep;
        // Ring planes lean a little off vertical; the caps lean near-horizontal
        // so the cluster's poles are not left hollow.
        const float tilt = cap ? ((index & 1) ? 1.f : -1.f) * randomRange(rng, 62.f, 86.f) * kPi / 180.f
                               : randomRange(rng, -tiltMax, tiltMax);
        const V3    planeNormal{std::sin(yaw) * std::cos(tilt), std::sin(tilt), std::cos(yaw) * std::cos(tilt)};

        V3 right, forward;
        basisFor(planeNormal, right, forward);

        const float radialScale = 1.f - scaleVar * randomRange(rng, 0.f, 1.f) - (cap ? 0.35f : 0.f);
        const float planeRadius = radius * std::max(0.2f, radialScale);
        const float offsetAngle = randomRange(rng, 0.f, 2.f * kPi);
        const float offsetR     = planeOffset * std::sqrt(randomRange(rng, 0.f, 1.f));
        const V3    origin      = add(
            center, add(mul(right, std::cos(offsetAngle) * offsetR), mul(forward, std::sin(offsetAngle) * offsetR)));

        const std::vector<V2> centers = samplePlaneCentres(desc.seed ^ (std::uint32_t(index + 1) * 2654435761u),
                                                           planeRadius, minDistance, maxLeaves);
        for (const V2 &point : centers) {
            const float jitter = randomRange(rng, -1.f, 1.f) * leafJitter;
            const V3    position =
                add(origin, add(add(mul(right, point.x), mul(forward, point.y)), mul(planeNormal, jitter)));

            // Point the petals outward from the cluster centre so the bundle
            // reads as growth radiating from the branch rather than confetti.
            float    roll    = randomRange(rng, 0.f, 2.f * kPi);
            const V3 radial  = sub(position, center);
            const V3 tangent = sub(radial, mul(planeNormal, dot(radial, planeNormal)));
            if (dot(tangent, tangent) > 1e-8f) {
                const V3 outward = norm(tangent);
                roll             = std::atan2(dot(outward, forward), dot(outward, right)) - 0.5f * kPi +
                       randomRange(rng, -0.85f, 0.85f);
            }

            const float size         = leafSize * (1.f + randomRange(rng, -sizeVar, sizeVar));
            const float u0           = uvMin + randomRange(rng, 0.f, 0.35f) * (uvMax - uvMin);
            const float u1           = std::min(uvMax, u0 + 0.65f * (uvMax - uvMin));
            const auto  shadedNormal = [&](V3 p) { return normalAt(p, planeNormal); };
            addPetal(out, position, right, forward, size, roll, u0, u1, desc.doubleSided, shadedNormal);
            ++leaves;
        }
    }
    return leaves;
}

}  // namespace eve::procgen
