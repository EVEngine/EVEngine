#pragma once

/**
 * @brief Voxelized signed distance field (SDF) of a solid surface.
 *
 * The mesh is baked into a uniform 3D grid once at setup; particles then query
 * distance + gradient per frame in the GPU kernels (trilinear interpolation,
 * central differences) to stay glued to the surface while gravity pushes them
 * tangentially along it. Positive distance = outside the solid.
 */

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace eve::fluids {

/** @brief Uniform signed-distance voxel field over a box. */
class MeshSdf {
public:
    /** @brief World-space position of voxel (0,0,0). */
    glm::vec3 origin{0.f};
    /** @brief World-space size of one voxel. */
    float cellSize = 1.f;
    /** @brief Voxel resolution along each axis. */
    glm::ivec3 dims{0};
    /** @brief Signed distances, dims.x * dims.y * dims.z floats. */
    std::vector<float> distances;

    /** @return dims.x * dims.y * dims.z. */
    int voxelCount() const;

    /** @return flat index for voxel (x,y,z); asserts bounds. */
    int index(int x, int y, int z) const;

    /** @return true when voxel coordinate is inside the field. */
    bool inBounds(const glm::ivec3& c) const;

    /**
     * @brief Trilinearly interpolated signed distance at p.
     * @param p world position.
     * @return distance, clamped at the field boundary.
     */
    float sample(const glm::vec3& p) const;

    /**
     * @brief Central-difference gradient (outward normal) at p.
     * @param p world position.
     * @return gradient vector (not normalized).
     */
    glm::vec3 gradient(const glm::vec3& p) const;

    /**
     * @brief Bake an analytic sphere into the field.
     * @param center sphere center.
     * @param radius sphere radius.
     * @param dims voxel resolution.
     * @return field spanning dims * cellSize = 2*(radius + margin) cube.
     */
    static MeshSdf makeSphere(const glm::vec3& center, float radius, const glm::ivec3& dims);

    /**
     * @brief Bake a horizontal plane y = planeY into the field.
     * @param planeY plane height.
     * @param dims voxel resolution.
     * @param halfExtent half side length of the square field.
     * @return field with signed distance = p.y - planeY.
     */
    static MeshSdf makePlane(float planeY, const glm::ivec3& dims, float halfExtent);

    /**
     * @brief Voxelize a closed triangle mesh.
     *
     * Unsigned distance is swept triangle-by-triangle over each triangle's
     * expanded AABB; the sign comes from an even-odd raycast along +X.
     * @param positions triangle vertices, 3 floats each.
     * @param indices triangle indices, 3 ints per triangle.
     * @param triangleCount number of triangles.
     * @param dims voxel resolution.
     * @return signed field over the mesh's bounding box (padded one voxel).
     */
    static MeshSdf makeFromTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                     const glm::ivec3& dims);
};

}  // namespace eve::fluids
