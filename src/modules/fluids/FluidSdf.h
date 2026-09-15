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

/** @brief One trilinear SDF sample and its local-space analytic gradient. */
struct MeshSdfSample {
    float     distance = 0.f;
    glm::vec3 gradient{0.f, 1.f, 0.f};
};

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
     * @return distance, extended by Euclidean distance beyond the field boundary.
     */
    float sample(const glm::vec3& p) const;

    /**
     * @brief Samples distance and analytic trilinear gradient from the same eight voxels.
     * @param p world
     * position.
     * @return distance and outward gradient at p.
     */
    MeshSdfSample sampleWithGradient(const glm::vec3& p) const;

    /**
     * @brief Analytic trilinear gradient (outward normal) at p.
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
     * expanded AABB, with exact evaluation for untouched cells; the sign comes
     * from a deterministic
     * skew-direction even-odd raycast.
     * @param positions triangle vertices, 3 floats each.
     * @param indices triangle indices, 3 ints per triangle.
     * @param dims voxel resolution.
     * @return signed field over the mesh's bounding box (padded one voxel).
     */
    static MeshSdf makeFromTriangles(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
                                     const glm::ivec3& dims);
};

}  // namespace eve::fluids
