#include <algorithm>
#include <cmath>
#include "common/Assert.h"
#include "procgen/road/RoadBakeInternal.h"

namespace eve::procgen::road::detail {
namespace {
double size(V3 value) { return std::hypot(static_cast<double>(value.x), value.y, value.z); }
double turn(V3 a, V3 b) {
    const double denominator = size(a) * size(b);
    if (denominator <= 1e-12) return 0.;
    return std::acos(std::clamp(
               (static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z) /
                   denominator,
               -1., 1.)) *
           57.29577951308232;
}
}  // namespace
Result<std::vector<SplineFrameSample>> sampleRoadParametricFrames(const SplinePath& path, const RoadEdge& edge,
                                                                  const RoadBakeOptions& options,
                                                                  std::size_t&           samplingWork) {
    const auto& points   = edge.controlPoints;
    const int   segments = path.segmentCount();
    EV_ASSERT((!path.isClosed() && static_cast<std::size_t>(segments) + 1 == points.size()),
              "road parameter scan must consume its owning open edge path");
    std::vector<float> parameters{0.f};
    V3                 anchor{points.front().x, points.front().y, points.front().z};
    V3                 previous{points[1].x - points[0].x, 0.f, points[1].z - points[0].z};
    const double       spacing = static_cast<double>(edge.style.samplingSpacingMeters) * options.samplingScale;
    const double       threshold =
        std::min(180., static_cast<double>(edge.style.samplingAngleDegrees) * options.samplingScale);
    auto append = [&](float parameter) -> Result<void> {
        if (parameter < parameters.back())
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "parameter scan lost monotonic precision",
                                  "sampling");
        if (parameter == parameters.back()) return Result<void>::success();
        if (parameters.size() == 4097u)
            return bakeFail<void>(DiagnosticCode::PreconditionViolation, "parameter scan exceeds the 4097 frame budget",
                                  "sampling");
        parameters.push_back(parameter);
        return Result<void>::success();
    };
    for (int segment = 0; segment < segments; ++segment) {
        const auto&  a = points[static_cast<std::size_t>(segment)];
        const auto&  b = points[static_cast<std::size_t>(segment + 1)];
        const double chord =
            std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y, static_cast<double>(b.z) - a.z);
        if (chord <= 1e-7)
            return bakeFail<std::vector<SplineFrameSample>>(
                DiagnosticCode::InvalidArgument, "parameter scan requires separated marker positions", "sampling");
        const float step = static_cast<float>(1. / (chord * 5.));
        if (!std::isfinite(step) || step <= 0.f)
            return bakeFail<std::vector<SplineFrameSample>>(DiagnosticCode::InvalidArgument,
                                                            "parameter scan step is not representable", "sampling");
        for (float u = step; u < 1.f;) {
            if (samplingWork >= options.maximumSamplingEvaluations)
                return bakeFail<std::vector<SplineFrameSample>>(
                    DiagnosticCode::PreconditionViolation, "parameter scan exceeds the shared sampling work budget",
                    "samplingWorkBudget");
            ++samplingWork;
            const float parameter = (static_cast<float>(segment) + u) / static_cast<float>(segments);
            auto        sample    = path.evaluateResult(parameter);
            if (!sample.ok()) return Result<std::vector<SplineFrameSample>>::failure(sample.status());
            V3           current{sample.value().x, sample.value().y, sample.value().z};
            const V3     direction = current - anchor;
            const double distance  = size(direction);
            if (distance >= spacing || (distance >= 1. && turn(previous, direction) >= threshold)) {
                auto added = append(parameter);
                if (!added.ok()) return Result<std::vector<SplineFrameSample>>::failure(added.status());
                previous = direction;
                anchor   = current;
            }
            const float next = u + step;
            if (next <= u)
                return bakeFail<std::vector<SplineFrameSample>>(DiagnosticCode::PreconditionViolation,
                                                                "parameter scan cannot advance at float precision",
                                                                "sampling");
            u = next;
        }
        auto added = append(static_cast<float>(segment + 1) / static_cast<float>(segments));
        if (!added.ok()) return Result<std::vector<SplineFrameSample>>::failure(added.status());
        const V3 endpoint{b.x, b.y, b.z};
        const V3 incoming = endpoint - anchor;
        if (size(incoming) > 1e-7) previous = incoming;
        anchor = endpoint;
    }
    return path.sampleFramesAtParametersResult(parameters, 24);
}
}  // namespace eve::procgen::road::detail
