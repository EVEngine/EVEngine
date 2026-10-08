#pragma once
#include "common/Export.h"

#include <glm/glm.hpp>

#include <vector>

namespace eve::gpuagents {

/** @brief One SDF sample with outward gradient. */
struct ObstacleSample {
    float     distance = 1e6f;
    glm::vec3 gradient{0.f, 1.f, 0.f};
};

/** @brief Dynamic spherical obstacle source (e.g. moving prop / wake body). */
struct DynamicObstacleSource {
    glm::vec3 center{0.f};
    float     radius = 1.f;
};

/**
 * @brief Unified static + dynamic obstacle distance field.
 *
 * Static samples live on a regular voxel grid (positive = free space).
 * Dynamic sources are analytic spheres combined with `min`.
 */
class EVENGINE_API_DOMAINS ObstacleField {
public:
    glm::vec3 origin{0.f};
    float     cellSize = 1.f;
    glm::ivec3 dims{0};
    std::vector<float> distances;
    std::vector<DynamicObstacleSource> dynamicSources;

    /** @brief True when a static grid is allocated. */
    bool hasStaticGrid() const { return dims.x > 0 && dims.y > 0 && dims.z > 0; }

    /** @brief Clear static grid and dynamic sources. */
    void clear();

    /**
     * @brief Bake an empty (all free) box field.
     * @param originMin World corner of voxel (0,0,0).
     * @param dims Voxel counts.
     * @param cellSize Voxel edge length.
     * @param fillDistance Initial free-space distance written to every voxel.
     */
    void bakeEmpty(const glm::vec3& originMin, const glm::ivec3& dims, float cellSize, float fillDistance = 10.f);

    /**
     * @brief Carve a solid sphere into the static grid (min with existing).
     * @param center Sphere center.
     * @param radius Sphere radius.
     */
    void carveSphere(const glm::vec3& center, float radius);

    /**
     * @brief Carve a solid axis-aligned box (min with existing).
     * @param minCorner Inclusive AABB min.
     * @param maxCorner Inclusive AABB max.
     */
    void carveBox(const glm::vec3& minCorner, const glm::vec3& maxCorner);

    /** @brief Replace the dynamic obstacle list. */
    void setDynamicSources(std::vector<DynamicObstacleSource> sources);

    /** @brief Sample combined static+dynamic distance and gradient at p. */
    ObstacleSample sample(const glm::vec3& p) const;

    /**
     * @brief Push an agent out of obstacles and keep feasible tangential velocity.
     * @param position In/out world position.
     * @param velocity In/out world velocity.
     * @param agentRadius Collision radius.
     * @param predictTime Velocity extrapolation horizon for predictive sample.
     */
    void resolve(glm::vec3& position, glm::vec3& velocity, float agentRadius, float predictTime) const;

private:
    float sampleStatic(const glm::vec3& p) const;
    glm::vec3 gradientStatic(const glm::vec3& p) const;
    ObstacleSample sampleDynamic(const glm::vec3& p) const;
};

}  // namespace eve::gpuagents
