#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eve::spatial {

/** @brief Axis-aligned 2D bounding box (min/max inclusive). */
struct AABB2 {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;

    /** @brief Extent along X (max - min). */
    float width() const { return maxX - minX; }
    /** @brief Extent along Y (max - min). */
    float height() const { return maxY - minY; }
    /** @brief Center X of the box. */
    float centerX() const { return (minX + maxX) * 0.5f; }
    /** @brief Center Y of the box. */
    float centerY() const { return (minY + maxY) * 0.5f; }

    /** @brief True when min <= max on every axis. */
    bool valid() const { return minX <= maxX && minY <= maxY; }

    /** @brief True if the point lies inside or on the boundary. */
    bool containsPoint(float x, float y) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }

    /** @brief True if the other box is fully inside this one. */
    bool containsAABB(const AABB2 &o) const {
        return o.minX >= minX && o.maxX <= maxX && o.minY >= minY && o.maxY <= maxY;
    }

    /** @brief True if the boxes overlap (inclusive edges). */
    bool intersectsAABB(const AABB2 &o) const {
        return minX <= o.maxX && maxX >= o.minX && minY <= o.maxY && maxY >= o.minY;
    }

    /** @brief True if the circle overlaps this 2D box. */
    bool intersectsCircle(float cx, float cy, float radius) const {
        const float nearestX = std::clamp(cx, minX, maxX);
        const float nearestY = std::clamp(cy, minY, maxY);
        const float dx       = cx - nearestX;
        const float dy       = cy - nearestY;
        return dx * dx + dy * dy <= radius * radius;
    }
};

/** @brief Axis-aligned 3D bounding box (min/max inclusive). */
struct AABB3 {
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;

    /** @brief Extent along X (max - min). */
    float width() const { return maxX - minX; }
    /** @brief Extent along Y (max - min). */
    float height() const { return maxY - minY; }
    /** @brief Extent along Z (max - min). */
    float depth() const { return maxZ - minZ; }
    /** @brief Center X of the box. */
    float centerX() const { return (minX + maxX) * 0.5f; }
    /** @brief Center Y of the box. */
    float centerY() const { return (minY + maxY) * 0.5f; }
    /** @brief Center Z of the box. */
    float centerZ() const { return (minZ + maxZ) * 0.5f; }

    /** @brief True when min <= max on every axis. */
    bool valid() const { return minX <= maxX && minY <= maxY && minZ <= maxZ; }

    /** @brief True if the point lies inside or on the boundary. */
    bool containsPoint(float x, float y, float z) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
    }

    /** @brief True if the other box is fully inside this one. */
    bool containsAABB(const AABB3 &o) const {
        return o.minX >= minX && o.maxX <= maxX && o.minY >= minY && o.maxY <= maxY &&
               o.minZ >= minZ && o.maxZ <= maxZ;
    }

    /** @brief True if the boxes overlap (inclusive edges). */
    bool intersectsAABB(const AABB3 &o) const {
        return minX <= o.maxX && maxX >= o.minX && minY <= o.maxY && maxY >= o.minY &&
               minZ <= o.maxZ && maxZ >= o.minZ;
    }

    /** @brief True if the sphere overlaps this 3D box. */
    bool intersectsSphere(float cx, float cy, float cz, float radius) const {
        const float nearestX = std::clamp(cx, minX, maxX);
        const float nearestY = std::clamp(cy, minY, maxY);
        const float nearestZ = std::clamp(cz, minZ, maxZ);
        const float dx       = cx - nearestX;
        const float dy       = cy - nearestY;
        const float dz       = cz - nearestZ;
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
};

/** @brief Builds a normalized 2D AABB (swaps inverted mins/maxes). */
inline AABB2 makeAABB2(float minX, float minY, float maxX, float maxY) {
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);
    return AABB2{minX, minY, maxX, maxY};
}

/** @brief Builds a normalized 3D AABB (swaps inverted mins/maxes). */
inline AABB3 makeAABB3(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);
    if (minZ > maxZ) std::swap(minZ, maxZ);
    return AABB3{minX, minY, minZ, maxX, maxY, maxZ};
}

/** @brief Integer cell key for spatial hashing (stable across platforms for reasonable ranges). */
inline uint64_t cellKey2(int cx, int cy) {
    return (uint64_t(uint32_t(cx)) << 32) | uint64_t(uint32_t(cy));
}

/** @brief Packs 3D cell indices into a 63-bit hash key (21 bits/axis). */
inline uint64_t cellKey3(int cx, int cy, int cz) {
    // 21 bits per axis packed into 63 bits (signed via uint cast).
    const uint64_t x = uint64_t(uint32_t(cx) & 0x1fffffu);
    const uint64_t y = uint64_t(uint32_t(cy) & 0x1fffffu);
    const uint64_t z = uint64_t(uint32_t(cz) & 0x1fffffu);
    return (x << 42) | (y << 21) | z;
}

}  // namespace eve::spatial
