#pragma once
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "common/Result.h"

namespace eve::graphics {
/** @brief Channel layout of an already filtered, linear vegetation normal sample. */
enum class VegetationNormalEncoding {
    /** @brief ASTC normal encoding: source red and green. */
    RedGreen,
    /** @brief Default Unity encoding: red times alpha, then green. */
    RedAlphaGreen,
    /** @brief UNITY_NO_DXT5nm source branch: alpha and green. */
    AlphaGreen
};

/** @brief Evaluate TVE 12.6 primary/detail tangent normal reconstruction.
 * @param sample Already sampled linear RGBA in [0,1], after texture filtering.
 * @param encoding Explicit source shader channel selection, not source-file encoding.
 * @param strength Signed authored strength in [-8,8]. Zero yields (0,0,1).
 * @return Owning, unnormalized (decodedXY * strength, 1) or checked invalid-input diagnostic.
 * @remarks No square-root Z reconstruction or normalization is performed: source world-space
 * detail masks consume the unnormalized vector. Decode after filtering; interpolating decoded
 * red-times-alpha texels is not equivalent. Source TIFF/importer processing happens before this API.
 * Thread-safe, reentrant, allocation-free on success, no callbacks or retained references.
 * CPU/GPU parity uses floating-point tolerance, not bitwise equality across backends.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<glm::vec3> decodeVegetationNormal(glm::vec4 sample, VegetationNormalEncoding encoding,
                                                       float strength);
}  // namespace eve::graphics
