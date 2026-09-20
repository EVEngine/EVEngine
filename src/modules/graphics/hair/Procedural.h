#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstdint>

namespace eve::graphics::hair {

/**
 * @brief Parameters for growing strands on a triangle mesh (scalp / body).
 *
 * Inspired by UE groom import density + guide generation, but CPU-only and
 * deterministic from `seed` for tests.
 */
struct ProceduralParams {
    int strandCount     = 512;
    int pointsPerStrand = 8;
    float length        = 0.18f;
    float lengthJitter  = 0.04f;
    float rootRadius    = 0.0012f;
    float tipRadius     = 0.0004f;
    float curlStrength  = 0.02f;
    float minSlopeDot   = 0.25f;
    uint32_t seed       = 1;
};

/**
 * @brief Grow strands from area-weighted samples on an indexed triangle mesh.
 *
 * @param posXYZ Vertex positions (xyz packed). Borrowed only for this call.
 * @param nrmXYZ Vertex normals (xyz packed); may be null → geometric normals.
 * @ownership Returned `StrandsDatas` is uniquely owned by the caller.
 */
[[nodiscard]] Result<StrandsDatas> generateOnMesh(const float *posXYZ, const float *nrmXYZ,
                                                  int vertexCount, const uint32_t *indices,
                                                  int indexCount, const ProceduralParams &params);

/**
 * @brief Grow strands on a Y-up XZ plane centered at the origin.
 * @param sizeX / sizeZ Full plane extents.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<StrandsDatas> generateOnPlane(float sizeX, float sizeZ,
                                                   const ProceduralParams &params);

}  // namespace eve::graphics::hair
