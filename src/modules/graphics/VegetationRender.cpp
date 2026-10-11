#include "graphics/VegetationRender.h"
#include <algorithm>
#include <cmath>
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationMotion.h"

namespace eve::graphics {
namespace {
Diagnostic invalid() {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid vegetation render data", {}, {},
                             "graphics.vegetation");
}
bool unit(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool finite(glm::vec4 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}
}  // namespace

Result<void> updateVegetationMesh(Graphics& graphics, Mesh& mesh, const VegetationField& field,
                                  std::span<const VegetationVertex> vertices, const VegetationMotion& motion,
                                  std::span<const float> texcoords, std::span<const uint32_t> indices) {
    if (vertices.empty() || vertices.size() > 4 * 1024 * 1024 || texcoords.size() != vertices.size() * 2 ||
        indices.empty() || indices.size() % 3 != 0 || indices.size() > 12 * 1024 * 1024)
        return Result<void>::failure(invalid());
    for (auto i : indices)
        if (i >= vertices.size()) return Result<void>::failure(invalid());
    for (auto uv : texcoords)
        if (!std::isfinite(uv)) return Result<void>::failure(invalid());
    auto geometry = deformVegetation(field, vertices, motion);
    if (!geometry.ok()) return Result<void>::failure(geometry.status());
    auto& data       = geometry.value();
    auto  frameValid = mesh.validateTangentFrame(data.tangents, data.bitangents);
    if (!frameValid) return frameValid;
    auto highlightsValid = mesh.validateMotionHighlights(data.motionHighlights);
    if (!highlightsValid) return highlightsValid;
    auto factorsValid = mesh.validateVegetationFactors(data.vegetationFactors);
    if (!factorsValid) return factorsValid;
    if (!graphics.updateMeshVertices(&mesh, data.positions.data(), data.normals.data(), texcoords.data(),
                                     int(vertices.size()), indices.data(), int(indices.size())))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "backend rejected vegetation mesh update", {}, {}, "graphics.vegetation"));
    mesh.computeBounds(data.positions.data(), int(vertices.size()));
    // The vertex count and owning frame were validated before upload. This swap cannot allocate.
    auto adopted = mesh.adoptTangentFrame(std::move(data.tangents), std::move(data.bitangents));
    if (!adopted) return adopted;
    auto highlightsAdopted = mesh.adoptMotionHighlights(std::move(data.motionHighlights));
    if (!highlightsAdopted) return highlightsAdopted;
    auto factorsAdopted = mesh.adoptVegetationFactors(std::move(data.vegetationFactors));
    if (!factorsAdopted) return factorsAdopted;
    return Result<void>::success();
}

Result<void> applyVegetationSurface(Material& material, const VegetationSample& sample,
                                    const VegetationSurface& surface) {
    if (!finite(sample.color) || !finite(sample.extras) || !unit(surface.roughness) ||
        !unit(surface.overlaySmoothness) || !unit(surface.wetnessCoverage) || !unit(surface.wetnessContrast) ||
        !unit(surface.overlayCoverage) ||
        !unit(surface.overlayVariation) || !unit(surface.overlayProjection) ||
        !unit(surface.vertexOcclusionAlpha) || !unit(surface.overlayNormalScale) ||
        !unit(surface.wetnessNormalScale) || !unit(surface.overlaySubsurface) || !unit(surface.alphaCutoff) ||
        !unit(surface.colorsCoverage) || !std::isfinite(surface.colorsIntensity) || surface.colorsIntensity < 0 ||
        surface.colorsIntensity > 2 || !unit(surface.colorsMask) || !unit(surface.colorsVariation) ||
        !unit(surface.vertexOcclusionMinimum) || !unit(surface.vertexOcclusionMaximum) ||
        surface.vertexOcclusionMaximum - surface.vertexOcclusionMinimum + .0001f == 0.f)
        return Result<void>::failure(invalid());
    for (auto value : surface.albedo)
        if (!std::isfinite(value) || value < 0) return Result<void>::failure(invalid());
    for (auto value : surface.overlayColor)
        if (!std::isfinite(value) || value < 0) return Result<void>::failure(invalid());
    for (auto value : surface.emission)
        if (!std::isfinite(value) || value < 0) return Result<void>::failure(invalid());
    for (auto value : surface.vertexOcclusionColor)
        if (!std::isfinite(value) || value < 0) return Result<void>::failure(invalid());
    const float          wetness = std::clamp(sample.extras.y, 0.f, 1.f);
    const float          overlay = std::clamp(sample.extras.z * surface.overlayCoverage, 0.f, 1.f);
    const float          alpha   = std::clamp(surface.albedo[3] * sample.extras.w, 0.f, 1.f);
    auto                 pbr = material.pbrSurface();
    pbr.vegetationColor.fieldColor = {std::max(sample.color.r, 0.f), std::max(sample.color.g, 0.f),
                                      std::max(sample.color.b, 0.f), std::clamp(sample.color.a, 0.f, 1.f)};
    pbr.vegetationColor.overlayColor = surface.overlayColor;
    pbr.vegetationColor.overlay      = overlay;
    pbr.vegetationColor.wetness      = std::clamp(wetness * surface.wetnessCoverage, 0.f, 1.f);
    pbr.vegetationColor.overlayVariation      = surface.overlayVariation;
    pbr.vegetationColor.overlayProjection     = surface.overlayProjection;
    pbr.vegetationColor.vertexOcclusionAlpha  = surface.vertexOcclusionAlpha;
    pbr.vegetationColor.vertexOcclusionColor  = surface.vertexOcclusionColor;
    pbr.vegetationColor.invertVertexOcclusion = surface.invertVertexOcclusion;
    pbr.vegetationColor.overlayNormalScale    = surface.overlayNormalScale;
    pbr.vegetationColor.wetnessNormalScale    = surface.wetnessNormalScale;
    pbr.vegetationColor.overlaySmoothness     = surface.overlaySmoothness;
    pbr.vegetationColor.wetnessContrast       = surface.wetnessContrast;
    pbr.vegetationColor.overlaySubsurface     = surface.overlaySubsurface;
    pbr.vegetationColor.colorsCoverage        = surface.colorsCoverage;
    pbr.vegetationColor.colorsIntensity       = surface.colorsIntensity;
    pbr.vegetationColor.colorsMask            = surface.colorsMask;
    pbr.vegetationColor.colorsVariation       = surface.colorsVariation;
    pbr.vegetationColor.vertexOcclusionMinimum = surface.vertexOcclusionMinimum;
    pbr.vegetationColor.vertexOcclusionMaximum = surface.vertexOcclusionMaximum;
    pbr.vegetationColor.invertVertexOcclusionColors = surface.invertVertexOcclusionColors;
    pbr.vegetationColor.backfaceNormalMode = surface.backfaceNormalMode;
    for (size_t c = 0; c < 3; ++c) {
        pbr.emissive[c] = surface.emission[c] * std::max(sample.extras.x, 0.f);
        if (!std::isfinite(pbr.emissive[c])) return Result<void>::failure(invalid());
    }
    auto accepted = material.setPbrSurface(pbr);
    if (!accepted.ok()) return accepted;
    material.setTint(surface.albedo[0], surface.albedo[1], surface.albedo[2], alpha);
    material.setRoughness(surface.roughness);
    material.setSurfaceMode("masked");
    material.setAlphaCutoff(surface.alphaCutoff);
    material.setDoubleSided(true);
    return Result<void>::success();
}
}  // namespace eve::graphics
