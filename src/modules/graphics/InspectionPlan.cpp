#include "graphics/InspectionPlan.h"
#include "graphics/ClipSpace.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::graphics {
namespace {
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
bool finite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}
}  // namespace

Result<InspectionViews> planInspectionViews(const InspectionBounds& bounds, const InspectionCamera& camera,
                                            uint32_t cellWidth, uint32_t cellHeight, float padding) {
    if (!cellWidth || !cellHeight || cellWidth > 1024 || cellHeight > 1024 || !camera.width || !camera.height ||
        !std::isfinite(padding) || padding < 1.f || padding > 4.f || !finite(camera.view) || !finite(camera.projection))
        return invalid<InspectionViews>("Invalid inspection viewport, camera matrix or padding");
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis] || std::abs(bounds.minimum[axis]) > 1e8f ||
            std::abs(bounds.maximum[axis]) > 1e8f)
            return invalid<InspectionViews>("Inspection bounds must be finite, ordered and within world limits");
    const glm::vec3 extent  = bounds.maximum - bounds.minimum;
    const float     largest = std::max({extent.x, extent.y, extent.z});
    if (largest < 1e-4f) return invalid<InspectionViews>("Inspection bounds are empty or too small");
    InspectionViews output;
    output.center                             = bounds.minimum + extent * .5f;
    output.worldUnitsPerPixel                 = padding * std::max(std::max(extent.x, extent.z) / float(cellWidth),
                                                                   std::max(extent.y, extent.z) / float(cellHeight));
    const float                    halfWidth  = output.worldUnitsPerPixel * float(cellWidth) * .5f;
    const float                    halfHeight = output.worldUnitsPerPixel * float(cellHeight) * .5f;
    const float                    nearZ      = std::max(.001f, largest * .01f);
    const float                    distance   = largest * 2.f + nearZ;
    const float                    farZ       = largest * 4.f + nearZ * 2.f;
    const std::array<glm::vec3, 3> directions{{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
    const std::array<glm::vec3, 3> ups{{{0, 1, 0}, {0, 1, 0}, {0, 0, -1}}};
    for (size_t index = 0; index < 3; ++index) {
        auto& view      = output.cameras[index];
        view.width      = cellWidth;
        view.height     = cellHeight;
        view.view       = glm::lookAtRH(output.center + directions[index] * distance, output.center, ups[index]);
        view.projection = orthoVulkanRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearZ, farZ);
        if (!finite(view.view) || !finite(view.projection))
            return invalid<InspectionViews>("Inspection camera cannot represent bounds at this world position");
        // Verify the actual float matrices, not just the ideal framing formula.
        for (unsigned corner = 0; corner < 8; ++corner) {
            glm::vec3 position;
            for (int axis = 0; axis < 3; ++axis)
                position[axis] = (corner & (1u << axis)) ? bounds.maximum[axis] : bounds.minimum[axis];
            const auto clip = view.projection * view.view * glm::vec4(position, 1.f);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) ||
                std::abs(clip.x) > 1.001f || std::abs(clip.y) > 1.001f || clip.z < 0.f || clip.z > 1.f)
                return invalid<InspectionViews>("Inspection bounds lose precision or fall outside the planned view");
        }
    }
    output.cameras[3] = camera;
    return Result<InspectionViews>::success(std::move(output));
}

Result<std::vector<InspectionSample>> planInspectionSamples(double start, double end, double fps, double duration,
                                                            InspectionLoop loop) {
    using Samples = std::vector<InspectionSample>;
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(fps) || !std::isfinite(duration) || start < 0. ||
        end < start || end > 1e6 || fps <= 0. || fps > 240. || duration <= 0. || duration > 1e6 ||
        (loop != InspectionLoop::Once && loop != InspectionLoop::Repeat) ||
        (loop == InspectionLoop::Once && end > duration))
        return invalid<Samples>("Invalid inspection time range, frame rate, clip duration or loop mode");
    Samples samples;
    samples.reserve(16);
    const double tolerance = 4. * std::numeric_limits<double>::epsilon() * std::max(1., end);
    for (unsigned index = 0; index <= 16; ++index) {
        double time = start + double(index) / fps;
        if (time > end && time - end > tolerance) break;
        if (index == 16) return invalid<Samples>("Inspection request exceeds 16 samples; split the range explicitly");
        if (index != 0 && std::abs(time - end) <= tolerance) time = end;
        double clipTime = loop == InspectionLoop::Repeat ? std::fmod(time, duration) : time;
        if (loop == InspectionLoop::Repeat) {
            const double wrapTolerance = std::min(duration * 1e-6, tolerance);
            if (clipTime <= wrapTolerance || duration - clipTime <= wrapTolerance) clipTime = 0.;
        }
        samples.push_back({time, clipTime});
    }
    return Result<Samples>::success(std::move(samples));
}
}  // namespace eve::graphics
