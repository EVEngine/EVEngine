#include <algorithm>
#include <cmath>
#include "procgen/road/RoadBakeInternal.h"

namespace eve::procgen::road::detail {
namespace {
double frameTurn(const SplineFrameSample& a, const SplineFrameSample& b) {
    auto angle = [](double ax, double ay, double az, double bx, double by, double bz) {
        const double norm = std::hypot(ax, ay, az) * std::hypot(bx, by, bz);
        return std::acos(std::clamp((ax * bx + ay * by + az * bz) / norm, -1., 1.)) * 57.29577951308232;
    };
    return std::max({angle(a.forwardX, a.forwardY, a.forwardZ, b.forwardX, b.forwardY, b.forwardZ),
                     angle(a.sideX, a.sideY, a.sideZ, b.sideX, b.sideY, b.sideZ),
                     angle(a.upX, a.upY, a.upZ, b.upX, b.upY, b.upZ)});
}
}  // namespace
Result<std::vector<SplineFrameSample>> sampleRoadFrames(const SplinePath& path, const RoadEdge& edge,
                                                        const RoadBakeOptions& options, float length,
                                                        std::size_t& samplingWork) {
    const auto& style = edge.style;
    if (style.samplingSpacingMeters == 0.f)
        return path.sampleFramesResult(std::max(4, options.pathSegmentsPerEdge), true, 0.f, 24);
    if (!std::isfinite(length) || length <= 1e-7f)
        return bakeFail<std::vector<SplineFrameSample>>(DiagnosticCode::InvalidArgument,
                                                        "metric road length must be finite and positive", "path");
    if (style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn)
        return sampleRoadParametricFrames(path, edge, options, samplingWork);
    constexpr int maximum  = 4096;
    const double  spacing  = static_cast<double>(style.samplingSpacingMeters) * options.samplingScale;
    const double  required = std::ceil(static_cast<double>(length) / spacing);
    if (required > maximum || path.segmentCount() > maximum / 4)
        return bakeFail<std::vector<SplineFrameSample>>(DiagnosticCode::PreconditionViolation,
                                                        "metric road sampling exceeds the 4096 interval budget",
                                                        "style.samplingSpacingMeters");
    int          count  = std::max({4, static_cast<int>(required), path.segmentCount() * 4});
    const double target = std::min(180., static_cast<double>(style.samplingAngleDegrees) * options.samplingScale);
    auto         frames = path.sampleFramesResult(count, true, 0.f, 24, true);
    if (!frames.ok()) return frames;
    if (target == 0.) return frames;
    for (;;) {
        double largest = 0.;
        for (std::size_t i = 1; i < frames.value().size(); ++i)
            largest = std::max(largest, frameTurn(frames.value()[i - 1], frames.value()[i]));
        if (largest <= target * .5) break;
        if (count == maximum)
            return bakeFail<std::vector<SplineFrameSample>>(
                DiagnosticCode::PreconditionViolation,
                "metric road angle refinement exceeds the 4096 interval budget; remaining turn=" +
                    std::to_string(largest),
                "style.samplingAngleDegrees");
        count  = std::min(maximum, count * 2);
        frames = path.sampleFramesResult(count, true, 0.f, 24, true);
        if (!frames.ok()) return frames;
    }
    std::vector<SplineFrameSample> selected;
    selected.reserve(frames.value().size());
    selected.push_back(frames.value().front());
    std::size_t anchor = 0;
    double      turn   = 0.;
    for (std::size_t i = 1; i < frames.value().size(); ++i) {
        const double step = frameTurn(frames.value()[i - 1], frames.value()[i]);
        const double distance =
            (frames.value()[i].sample.normalizedDistance - frames.value()[anchor].sample.normalizedDistance) * length;
        if (i > anchor + 1 && (distance > spacing * (1. + 1e-5) || turn + step > target)) {
            selected.push_back(frames.value()[i - 1]);
            anchor = i - 1;
            turn   = 0.;
        }
        if (frames.value()[i].authoredMarker) {
            selected.push_back(frames.value()[i]);
            anchor = i;
            turn   = 0.;
        } else
            turn += step;
    }
    if (selected.back().sample.normalizedDistance != frames.value().back().sample.normalizedDistance)
        selected.push_back(frames.value().back());
    return Result<std::vector<SplineFrameSample>>::success(std::move(selected));
}
ResultRef<const std::vector<SplineFrameSample>> prepareRoadFrames(RoadFrameCache& cache, const SplinePath& path,
                                                                  const RoadEdge& edge, const RoadBakeOptions& options,
                                                                  float length) {
    if (const auto found = cache.frames.find(edge.id); found != cache.frames.end())
        return ResultRef<const std::vector<SplineFrameSample>>::success(std::cref(found->second));
    auto prepared = sampleRoadFrames(path, edge, options, length, cache.samplingWork);
    if (!prepared.ok()) return ResultRef<const std::vector<SplineFrameSample>>::failure(prepared.status());
    auto inserted = cache.frames.emplace(edge.id, std::move(prepared).takeValue());
    return ResultRef<const std::vector<SplineFrameSample>>::success(std::cref(inserted.first->second));
}
}  // namespace eve::procgen::road::detail
