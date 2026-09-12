#include "graphics/MeshInstanceRange.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

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
