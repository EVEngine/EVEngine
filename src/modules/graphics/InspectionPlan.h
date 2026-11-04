#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include "common/Result.h"

namespace eve::graphics {
/** @brief World-space bounds, supplied from one frozen pose or a union of sampled poses. */
struct InspectionBounds {
    glm::vec3 minimum{0.f}, maximum{0.f};
};
/** @brief Owning camera value snapshot; matrices use engine RH, zero-to-one, Y-down clip space. */
struct InspectionCamera {
    glm::mat4 view{1.f}, projection{1.f};
    uint32_t  width = 512, height = 512;
};
/** @brief Row-major front (+Z), right (+X), top (+Y), and unchanged game camera. */
struct InspectionViews {
    std::array<InspectionCamera, 4> cameras;
    glm::vec3                       center{0.f};
    float                           worldUnitsPerPixel = 0.f;
};
/**
 * @internal
 * @brief Plan three equally scaled orthographic views and copy a game camera without reframing it.
 * @param bounds Borrowed world bounds; all corners must fit the three diagnostic views.
 * @param camera Borrowed original camera matrices and native viewport; copied exactly into panel four.
 * @param cellWidth Diagnostic viewport width, 1..1024 pixels.
 * @param cellHeight Diagnostic viewport height, 1..1024 pixels.
 * @param padding Framing multiplier, 1..4; 1 means tight bounds.
 * @return Owning matrices or a diagnostic, including unrepresentable floating-point geometry.
 * @thread Any thread with immutable inputs; no callbacks, ECS access or retained references.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<InspectionViews> planInspectionViews(const InspectionBounds& bounds,
                                                                                const InspectionCamera& camera,
                                                                                uint32_t cellWidth, uint32_t cellHeight,
                                                                                float padding);

/** @brief Explicit clip-end behavior; loop wraps exact duration to time zero. */
enum class InspectionLoop { Once, Repeat };
/** @brief A requested timeline time and its independent clip evaluation time, in seconds. */
struct InspectionSample {
    double timelineTime = 0., clipTime = 0.;
};
/**
 * @internal
 * @brief Build at most 16 independent clip sample times using start + integer index / fps.
 * @param start Nonnegative timeline start in seconds.
 * @param end Inclusive timeline end in seconds, at most 1e6.
 * @param fps Sampling rate in (0, 240].
 * @param duration Positive clip duration in seconds, at most 1e6.
 * @param loop Once rejects ranges beyond duration; Repeat wraps independently of simulation state.
 * @return Owning samples or a diagnostic; never truncates an oversized request.
 * @note Endpoints within four double epsilon units scaled by time are snapped to end.
 * Loop boundaries use the same tolerance capped at one millionth of the clip duration.
 * @thread Any thread; no callbacks, animation events, root-motion application or retained references.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<std::vector<InspectionSample>> planInspectionSamples(double start,
                                                                                                double end, double fps,
                                                                                                double         duration,
                                                                                                InspectionLoop loop);
}  // namespace eve::graphics
