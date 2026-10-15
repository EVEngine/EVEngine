#include "graphics/MeshInstanceRange.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>
#include <new>
#include "graphics/Mesh.h"
#include "graphics/MeshShaderRasterState.h"

// MeshInstanceRange.cpp
namespace eve::graphics {
Result<void> validateMeshInstanceRange(const MeshInstanceRange& range) {
    if (range.first > 1048576 || range.count > 1048576 - range.first ||
        !std::isfinite(range.maximumHorizontalDistance) || range.maximumHorizontalDistance < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid instance range or maximum horizontal distance",
                                                       "graphics.instances"));
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(range.minimum[i]) || !std::isfinite(range.maximum[i]) || range.minimum[i] > range.maximum[i])
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid instance bounds", "graphics.instances"));
    return Result<void>::success();
}
bool meshInstanceRangeVisible(const MeshInstanceRange& range, const glm::mat4& model, const glm::mat4& vp,
                              const glm::vec3& eye) {
    if (!range.count) return false;
    glm::vec3 localCenter, half;
    for (int i = 0; i < 3; ++i) {
        localCenter[i] = range.minimum[i] * .5f + range.maximum[i] * .5f;
        half[i]        = range.maximum[i] * .5f - range.minimum[i] * .5f;
    }
    const glm::vec3 center(model * glm::vec4(localCenter, 1));
    const glm::vec3 axes[]{glm::vec3(model[0]) * half.x, glm::vec3(model[1]) * half.y, glm::vec3(model[2]) * half.z};
    const auto      row = [&](int i) { return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]); };
    const glm::vec4 planes[]{row(3) + row(0), row(3) - row(0), row(3) + row(1),
                             row(3) - row(1), row(2),          row(3) - row(2)};
    for (const auto& p : planes) {
        const glm::vec3 n(p);
        const float     radius =
            std::abs(glm::dot(n, axes[0])) + std::abs(glm::dot(n, axes[1])) + std::abs(glm::dot(n, axes[2]));
        if (glm::dot(n, center) + p.w < -radius) return false;
    }
    if (range.maximumHorizontalDistance > 0) {
        const glm::vec3 extent = glm::abs(axes[0]) + glm::abs(axes[1]) + glm::abs(axes[2]);
        const double    limit  = double(range.maximumHorizontalDistance) + std::max(extent.x, extent.z);
        const double    dx = double(center.x) - eye.x, dz = double(center.z) - eye.z;
        if (dx * dx + dz * dz > limit * limit) return false;
    }
    return true;
}
}  // namespace eve::graphics

// MeshShaderRasterState.cpp
namespace eve::graphics {
Result<void> validateMeshShaderRasterState(const MeshShaderRasterState& state) {
    if ((state.depthCompare != MeshDepthCompare::Less && state.depthCompare != MeshDepthCompare::LessEqual &&
         state.depthCompare != MeshDepthCompare::Always) ||
        state.colorWriteMask > 15 || !std::isfinite(state.depthBiasConstant) || !std::isfinite(state.depthBiasSlope))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid custom mesh depth comparison, bias or color mask",
                                                       "shader.raster"));
    return Result<void>::success();
}
}  // namespace eve::graphics

// MeshTangent.cpp
namespace eve::graphics {
Result<void> Mesh::validateVegetationFactors(std::span<const float> values) const {
    if (!values.empty()) {
        if (gpuVertexCount <= 0 || values.size() != size_t(gpuVertexCount) * 5)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "vegetation factor stream must contain five values per mesh vertex"));
        for (size_t i = 0; i < values.size(); ++i)
            if (!std::isfinite(values[i]) || (i % 5 < 3 && (values[i] < 0 || values[i] > 1)))
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                      "vegetation factors must be unit values and detail UVs must be finite"));
    }
    return Result<void>::success();
}
Result<void> Mesh::adoptVegetationFactors(std::vector<float>&& values) {
    auto valid = validateVegetationFactors(values);
    if (!valid) return valid;
    vegetationFactors_.swap(values);
    return Result<void>::success();
}
Result<void> Mesh::validateVegetationDeformationFactors(std::span<const float> values) const {
    if (values.empty()) return Result<void>::success();
    if (gpuVertexCount <= 0 || values.size() != size_t(gpuVertexCount) * 9)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "vegetation deformation stream must contain nine values per mesh vertex"));
    for (size_t i = 0; i < values.size(); i += 9) {
        for (size_t component = 0; component < 9; ++component)
            if (!std::isfinite(values[i + component]))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "vegetation deformation factors must be finite"));
        for (size_t component = 3; component <= 6; ++component)
            if (values[i + component] < 0 || values[i + component] > 1)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "vegetation deformation masks must be unit values"));
        if (values[i + 7] < 0 || values[i + 8] < 0)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "vegetation deformation bounds must be nonnegative"));
    }
    return Result<void>::success();
}
Result<void> Mesh::adoptVegetationDeformationFactors(std::vector<float>&& values) {
    auto valid = validateVegetationDeformationFactors(values);
    if (!valid) return valid;
    vegetationDeformationFactors_.swap(values);
    return Result<void>::success();
}
Result<void> Mesh::validateMotionHighlights(std::span<const float> values) const {
    if (!values.empty()) {
        if (gpuVertexCount <= 0 || values.size() != size_t(gpuVertexCount))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "motion highlight stream must match mesh vertex count"));
        for (float value : values)
            if (!std::isfinite(value) || value < 0)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "motion highlights must be finite and nonnegative"));
    }
    return Result<void>::success();
}
Result<void> Mesh::adoptMotionHighlights(std::vector<float>&& values) {
    auto valid = validateMotionHighlights(values);
    if (!valid) return valid;
    motionHighlights_.swap(values);
    return Result<void>::success();
}
Result<void> Mesh::validateTangentFrame(std::span<const float> tangents, std::span<const float> bitangents) const {
    auto invalid = [] {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "tangent frame must contain finite independent XYZ vectors for every mesh vertex"));
    };
    if (tangents.empty() && bitangents.empty()) {
        return Result<void>::success();
    }
    if (gpuVertexCount <= 0 || tangents.size() != size_t(gpuVertexCount) * 3 || bitangents.size() != tangents.size())
        return invalid();
    for (size_t i = 0; i < tangents.size(); i += 3) {
        double tt = 0, bb = 0, tb = 0;
        for (unsigned c = 0; c < 3; ++c) {
            const double t = tangents[i + c], b = bitangents[i + c];
            if (!std::isfinite(t) || !std::isfinite(b)) return invalid();
            tt += t * t;
            bb += b * b;
            tb += t * b;
        }
        if (tt <= 1e-20 || bb <= 1e-20 || tt * bb - tb * tb <= tt * bb * 1e-12) return invalid();
    }
    return Result<void>::success();
}
Result<void> Mesh::adoptTangentFrame(std::vector<float>&& tangents, std::vector<float>&& bitangents) {
    if (&tangents == &bitangents)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "tangent frame buffers must be distinct"));
    auto valid = validateTangentFrame(tangents, bitangents);
    if (!valid) return valid;
    importedTangents_.swap(tangents);
    importedBitangents_.swap(bitangents);
    return Result<void>::success();
}
Result<void> Mesh::setTangentFrame(std::span<const float> tangents, std::span<const float> bitangents) {
    auto valid = validateTangentFrame(tangents, bitangents);
    if (!valid) return valid;
    try {
        std::vector<float> t(tangents.begin(), tangents.end()), b(bitangents.begin(), bitangents.end());
        importedTangents_.swap(t);
        importedBitangents_.swap(b);
        return Result<void>::success();
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "tangent frame allocation failed"));
    }
}
}  // namespace eve::graphics
