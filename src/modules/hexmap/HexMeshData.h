#pragma once

/** @file HexMeshData.h @brief CPU vertex/index container shared by the hex surface builders. */

#include "hexmap/HexMetrics.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/**
 * @brief CPU-side triangle soup for one hex chunk surface.
 *
 * Vertices are intentionally not shared between triangles: the reference
 * hex-map mesh builder emits every triangle with its own three vertices, which
 * keeps the blend/terrace/cliff seams crisp and makes flat face normals exact.
 * `finalize()` therefore computes per-face normals and duplicates them across
 * the three vertices of each triangle.
 *
 * @note Owns plain host memory; upload is the caller's responsibility
 *       (`gfx.newMeshFromArrays` / `gfx.updateMeshVertices`).
 */
class HexMeshData {
public:
    /** @brief Removes every vertex and index, keeping the allocated capacity. */
    void clear() noexcept;

    /** @brief Whether the mesh has no triangles. */
    [[nodiscard]] bool empty() const noexcept { return indices_.empty(); }
    /** @brief Number of vertices. */
    [[nodiscard]] std::size_t vertexCount() const noexcept { return positions_.size() / 3u; }
    /** @brief Number of triangles. */
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices_.size() / 3u; }

    /**
     * @brief Appends one vertex.
     *
     * @param position Vertex position in world space. Callers apply noise
     *                 perturbation themselves so a vertex is never displaced twice.
     * @param u First texture coordinate (surface-specific encoding).
     * @param v Second texture coordinate (surface-specific encoding).
     * @return Index of the new vertex.
     */
    std::uint32_t addVertex(HexVec3 position, float u, float v);

    /** @brief Appends a triangle with counter-clockwise winding. */
    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c);
    /** @brief Appends a quad as two triangles (`a,c,b` and `b,c,d`). */
    void addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);

    /** @brief Computes flat per-face normals. Must be called before upload. */
    void finalize() noexcept;

    /** @brief Interleaved-free position stream (`xyz` per vertex). */
    [[nodiscard]] const std::vector<float>& positions() const noexcept { return positions_; }
    /** @brief Normal stream (`xyz` per vertex), valid after `finalize()`. */
    [[nodiscard]] const std::vector<float>& normals() const noexcept { return normals_; }
    /** @brief Texture coordinate stream (`uv` per vertex). */
    [[nodiscard]] const std::vector<float>& uvs() const noexcept { return uvs_; }
    /** @brief Triangle index stream. */
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept { return indices_; }

    /** @brief Whether `finalize()` produced a normal for every vertex. */
    [[nodiscard]] bool hasNormals() const noexcept { return normals_.size() == positions_.size(); }

private:
    std::vector<float>         positions_;
    std::vector<float>         normals_;
    std::vector<float>         uvs_;
    std::vector<std::uint32_t> indices_;
};

}  // namespace eve::hexmap
