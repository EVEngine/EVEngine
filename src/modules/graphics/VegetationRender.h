#pragma once
#include "common/Export.h"
#include <array>
#include <cstdint>
#include <span>
#include "common/Result.h"
#include "graphics/PbrSurface.h"

namespace eve::graphics {
class Graphics;
class Mesh;
class Material;
class VegetationField;
struct VegetationVertex;
struct VegetationMotion;
struct VegetationSample;

/** @brief Owning surface inputs, supplied afresh to avoid accumulating tint/wetness across frames.
 * Color inputs are linear. Overlay, wetness, projection and occlusion controls are [0,1].
 * The fragment stage derives the final overlay mask from mesh variation and
 * occlusion, the mapped normal and textured luminance. It then applies overlay
 * color/normal/material response followed by wetness color/normal/smoothness.
 */
struct VegetationSurface {
    std::array<float, 4> albedo{1, 1, 1, 1};
    std::array<float, 3> overlayColor{1, 1, 1}, emission{0, 0, 0}, vertexOcclusionColor{1, 1, 1};
    float                roughness = 0.65f, overlaySmoothness = .5f;
    float                wetnessCoverage = 1.f, wetnessContrast = .5f, overlayCoverage = 1.f, overlayVariation = .5f;
    float                overlayProjection = .5f, vertexOcclusionAlpha = .5019608f, alphaCutoff = .5f;
    float                overlayNormalScale = .5f, wetnessNormalScale = .5f;
    float                overlaySubsurface = .5f;
    float                colorsCoverage = 1.f, colorsIntensity = 1.f, colorsMask = 1.f, colorsVariation = .5f;
    float                vertexOcclusionMinimum = 0.f, vertexOcclusionMaximum = 1.f;
    bool                 invertVertexOcclusion = false, invertVertexOcclusionColors = false;
    PbrVegetationBackfaceNormalMode backfaceNormalMode = PbrVegetationBackfaceNormalMode::Flip;
};

/** @brief Deform rest geometry then update an existing mesh through the active Graphics backend.
 * Render-thread only; graphics owns mesh and must outlive this call and subsequent draws.
 * All references/spans are immediate borrows; no callbacks or pointers are retained.
 * Rest geometry is object-space with an explicit motion.objectToWorld transform.
 * Render the resulting world-space mesh with the identity model transform.
 * Updated positions and bounds are shared by color, depth and shadow passes.
 * @return Validation failure before upload; Unsupported if backend cannot update geometry.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> updateVegetationMesh(Graphics& graphics, Mesh& mesh, const VegetationField& field,
                                                std::span<const VegetationVertex> vertices,
                                                const VegetationMotion& motion, std::span<const float> texcoords,
                                                std::span<const uint32_t> indices);

/** @brief Apply an independently sampled field to a masked double-sided PBR material.
 * Render-thread only, no callbacks; material is borrowed during this call only.
 * Existing texture bindings are retained by their existing material lifetime contract.
     * @return InvalidArgument before mutation; otherwise applies tint, wetness, overlay,
 * emission and alpha atomically with respect to validation. No texture creation occurs.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> applyVegetationSurface(Material& material, const VegetationSample& sample,
                                                  const VegetationSurface& surface);
}  // namespace eve::graphics
