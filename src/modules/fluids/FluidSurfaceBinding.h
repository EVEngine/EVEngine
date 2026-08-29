#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace eve::fluids {

/** @brief Stable material-space address of a point on a triangle surface. */
struct SurfaceLocation {
    uint32_t  triangle = 0;
    glm::vec3 barycentric{1.f, 0.f, 0.f};
};

/** @brief Evaluated world-space frame and motion at a surface location. */
struct SurfaceSample {
    SurfaceLocation location;
    glm::vec3       position{0.f};
    glm::vec3       previousPosition{0.f};
    glm::vec3       normal{0.f, 1.f, 0.f};
    glm::vec3       tangent{1.f, 0.f, 0.f};
    glm::vec3       bitangent{0.f, 0.f, 1.f};
    glm::vec3       velocity{0.f};
    glm::vec2       uv{0.f};
};

/** @brief Result of walking a material point across triangle adjacency. */
struct SurfaceWalkResult {
    SurfaceLocation location;
    glm::vec3       remainingDisplacement{0.f};
    bool            reachedBoundary = false;
    bool            valid           = false;
};

/**
 * @brief Dynamic triangle surface used to bind films and droplets to deforming meshes.
 *
 * Locations are stored as triangle id plus barycentric coordinates, so they remain
 * stable while the supplied vertices move. Call setTransform for rigid motion or
 * setDeformedPositions for skinning, morphing and procedural deformation.
 */
class FluidSurfaceBinding {
public:
    /**
     * @brief Build topology and initialize the current and previous poses.
     * @param positions model-space vertex positions.
     * @param indices triangle indices; the size must be a multiple of three.
     * @param uvs optional per-vertex simulation UVs.
     * @return true when the mesh is a valid, non-empty triangle surface.
     */
    bool build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
               const std::vector<glm::vec2>& uvs = {});

    /** @brief Apply a new rigid pose and preserve the old pose for velocity queries. */
    void setTransform(const glm::mat4& transform);

    /**
     * @brief Supply a new deformed world-space pose and preserve the previous pose.
     * @return false when the vertex count differs from the topology.
     */
    bool setDeformedPositions(const std::vector<glm::vec3>& worldPositions);

    /** @brief Make the current pose the previous pose, yielding zero surface velocity. */
    void commitPose();

    /** @return true when a valid topology and pose are available. */
    bool isValid() const;

    /** @return number of triangles in the bound topology. */
    int triangleCount() const;

    /** @return number of vertices in the bound topology. */
    int vertexCount() const { return int(currentPositions_.size()); }

    /** @return indexed triangle topology for wet-film fields and render integration. */
    std::vector<glm::uvec3> triangles() const;

    /**
     * @brief Evaluate a surface address in the current and previous poses.
     * @param location triangle id and barycentric coordinates.
     * @param dt seconds between the two poses; non-positive values yield zero velocity.
     */
    SurfaceSample evaluate(const SurfaceLocation& location, float dt) const;

    /**
     * @brief Find the closest material point on the current surface.
     * @param worldPosition point to project.
     * @param maxDistance maximum accepted distance; negative means unlimited.
     * @param outLocation receives the closest address.
     * @return true when a point within maxDistance was found.
     */
    bool project(const glm::vec3& worldPosition, float maxDistance, SurfaceLocation& outLocation) const;

    /**
     * @brief Move a location by a world-space displacement, crossing triangle edges.
     * @param start initial material-space address.
     * @param worldDisplacement desired displacement; its normal component is discarded.
     * @param maxCrossings safety bound for topology traversal.
     * @return final address, boundary state and unconsumed displacement.
     */
    SurfaceWalkResult walkAcrossSurface(const SurfaceLocation& start, const glm::vec3& worldDisplacement,
                                        int maxCrossings = 16) const;

    /** @return neighboring triangle across the edge opposite local vertex, or -1 at a boundary. */
    int adjacentTriangle(uint32_t triangle, int oppositeVertex) const;

private:
    glm::vec3 barycentric(uint32_t triangle, const glm::vec3& point) const;
    glm::vec3 triangleNormal(uint32_t triangle) const;

    std::vector<glm::vec3> restPositions_;
    std::vector<glm::vec3> currentPositions_;
    std::vector<glm::vec3> previousPositions_;
    std::vector<glm::vec2> uvs_;
    std::vector<uint32_t>  indices_;
    std::vector<glm::ivec3> adjacency_;
};

}  // namespace eve::fluids
