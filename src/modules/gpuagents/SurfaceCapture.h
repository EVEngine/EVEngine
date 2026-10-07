#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "gpuagents/SurfaceField.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace eve::gpuagents {

/**
 * @brief Orthographic surface capture that rebuilds height + normals on a SurfaceField.
 *
 * P2 CPU path: project triangle meshes into an XZ heightfield (max height wins),
 * then rebuild normals from central differences. Graphics depth-texture capture is
 * a follow-up that writes the same SurfaceField layout.
 */
class EVENGINE_API_DOMAINS SurfaceCapture {
public:
    /**
     * @brief Bake triangle positions into `out` over the given XZ domain.
     * @param out Destination surface (reinitialized).
     * @param positions Vertex positions (world space).
     * @param indices Triangle indices (3 per face).
     * @param originMin World-space corner of texel (0,0).
     * @param worldSize Side length of the square domain on XZ.
     * @param resolution Texel resolution (>= 2).
     * @param baseHeight Height written where no triangle covers a texel.
     * @ownership Borrowed spans; not retained.
     * @lifetime Input spans valid only for the duration of the call.
     */
    [[nodiscard("check surface capture")]] static Result<void> captureFromTriangles(
        SurfaceField& out, std::span<const glm::vec3> positions, std::span<const std::uint32_t> indices,
        const glm::vec3& originMin, float worldSize, int resolution, float baseHeight = 0.f);

    /**
     * @brief Rebuild normals from the current height grid (central differences).
     * @param field Surface whose height samples are already populated.
     */
    static void rebuildNormals(SurfaceField& field);
};

}  // namespace eve::gpuagents
