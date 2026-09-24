#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "graphics/VegetationField.h"

namespace eve::graphics {
/** @brief Object mode rotates around pivots; batched mode uses height-scaled offsets. */
enum class VegetationBatchMode { Object, Batched };

/** @brief Owning rest vertex in object space, with unpacked authoring channels.
 * Bending/branch/flutter/variation/occlusion/detail are [0,1]. Bounds are meters.
 * A pivot and variation shared by one plant preserve coherent motion after mesh combining.
 */
struct VegetationVertex {
    glm::vec3 position{0.f}, normal{0.f, 1.f, 0.f}, pivot{0.f};
    float     bending = 0.f, branch = 0.f, flutter = 0.f;
    float     variation = 0.5f, occlusion = 1.f, detail = 0.f;
    float     boundsHeight = 1.f, boundsRadius = 1.f;
    glm::vec2 texcoord{0.f}, secondaryTexcoord{0.f}, detailCoord{0.f};
    /** @brief Optional authored tangent XYZ plus signed unit handedness; all vertices agree on presence. */
    std::optional<glm::vec4> tangent;
};

/** @brief Source-equivalent plant motion parameters with explicit time and texture inputs.
 * All data is owned. No wall clock/RNG/global shader state is read. Noise is linear
 * RGBA with bilinear repeat addressing. The built-in 2x2 pattern is a native default;
 * matching an authored appearance requires the same noise texture as the source.
 * Speed/scale/amplitude controls retain TVE 12.6 plant shader units. Object transforms
 * must be finite, affine and invertible. Output geometry is world space.
 */
struct VegetationMotion {
    double              time      = 0.;
    double              timeScale = 1., timeOffset = 0.;
    float               timeOverride = 0.f;
    glm::mat4           objectToWorld{1.f};
    glm::vec3           camera{0.f}, worldOrigin{0.f};
    VegetationBatchMode batchMode   = VegetationBatchMode::Object;
    bool                usePivots   = true;
    float               dynamicMode = 0.f, rigidity = 0.5f, facing = 0.5f;
    float               bending = 0.2f, bendingSpeed = 2.f, bendingScale = 1.f, bendingVariation = 0.f;
    float               branch = 0.2f, rolling = 0.2f, branchSpeed = 6.f, branchScale = 3.f, branchVariation = 0.f;
    float               flutter = 0.2f, flutterSpeed = 20.f, flutterScale = 10.f, flutterVariation = 0.f;
    float               globalBending = 1.f, globalBranch = 1.f, globalFlutter = 1.f, noiseTiling = 1.f;
    float               interaction = 1.f, interactionMask = 1.f, fadeDistance = 100.f;
    float               globalSize = 1.f, sizeFadeStart = 1000000.f, sizeFadeEnd = 1000001.f, distanceFadeBias = 1.f;
    float               perspectivePush = 0.f, perspectiveNoise = 0.f, perspectiveAngle = 1.f;
    uint8_t             motionLayer = 0, vertexLayer = 0;
    VegetationMask      noise{2,
                         2,
                              {glm::vec4(0.2f, 0.7f, 0.85f, 1.f), glm::vec4(0.8f, 0.4f, 0.55f, 1.f),
                               glm::vec4(0.4f, 0.2f, 0.65f, 1.f), glm::vec4(0.7f, 0.8f, 1.f, 1.f)}};
};

/** @brief Owning geometry projection, packed world XYZ positions and unit normals. */
struct VegetationGeometry {
    std::vector<float> positions, normals;
    /** @brief Interleaved variation, occlusion, detail mask and detail UV, five floats per vertex. */
    std::vector<float> vegetationFactors;
    /** @brief Owning per-rest-vertex motion highlight scalar, parallel to positions/3.
     * Evaluated before raster
     * interpolation using noise alpha, wind power, fade and authored height.
     * Immutable worker output; no GPU
     * state or retained borrows. Not a persistent format.
     */
    std::vector<float> motionHighlights;
    /** @brief Owning deformed world XYZ frame streams; both empty when source tangents are absent. */
    std::vector<float> tangents, bitangents;
};

/** @brief Evaluate plant bending, squash, rolling, flutter, interaction and perspective.
 * Worker-safe with immutable field/inputs; no retained borrows, callbacks or GPU calls.
 * Normals use a local deformation Jacobian with masks held constant, followed by the
 * inverse-transpose transform. Evaluating derivatives before world translation avoids
 * large-coordinate cancellation. Geometry is tolerance-bounded across platforms;
 * no bit-exact transcendental or GPU half-precision equivalence is claimed.
 * @return Owning world-space geometry or InvalidArgument/Failed, never partial output.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationGeometry> deformVegetation(
    const VegetationField& field, std::span<const VegetationVertex> vertices, const VegetationMotion& motion);
}  // namespace eve::graphics
