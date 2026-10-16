#include <algorithm>
#include <cmath>
#include <limits>
#include "procgen/road/RoadBakeInternal.h"

namespace eve::procgen::road::detail {
Result<RoadSurfacePolyline> prepareSurfacePolyline(std::span<const SplineFrameSample> frames) {
    if (frames.size() < 2 || frames.size() > 4099)
        return bakeFail<RoadSurfacePolyline>(DiagnosticCode::InvalidArgument, "surface path needs 2..4099 rows",
                                             "sidePlacement.pathSampling");
    RoadSurfacePolyline result;
    result.frames      = frames;
    result.lastVisible = frames.size() - 1;
    result.distances.reserve(frames.size());
    result.distances.push_back(0.);
    for (std::size_t i = 1; i < frames.size(); ++i) {
        const auto&  a = frames[i - 1].sample;
        const auto&  b = frames[i].sample;
        const double step =
            std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y, static_cast<double>(b.z) - a.z);
        if (!std::isfinite(step) || step <= 1e-7)
            return bakeFail<RoadSurfacePolyline>(DiagnosticCode::InvalidArgument,
                                                 "surface path contains a degenerate segment",
                                                 "sidePlacement.pathSampling");
        result.distances.push_back(result.distances.back() + step);
    }
    if (result.distances.back() > std::numeric_limits<float>::max())
        return bakeFail<RoadSurfacePolyline>(DiagnosticCode::InvalidArgument, "surface length is not representable",
                                             "sidePlacement.pathSampling");
    result.length = static_cast<float>(result.distances.back());
    return Result<RoadSurfacePolyline>::success(std::move(result));
}

Result<SplineFrameSample> sampleSurfacePolyline(const RoadSurfacePolyline& path, float distance) {
    if (!std::isfinite(distance) || distance < 0 || path.frames.size() < 2 ||
        path.distances.size() != path.frames.size())
        return bakeFail<SplineFrameSample>(DiagnosticCode::InvalidArgument, "invalid surface sampling request",
                                           "sidePlacement.pathSampling");
    auto        end = std::lower_bound(path.distances.begin() + 1, path.distances.end(), static_cast<double>(distance));
    const auto  index = end == path.distances.end() ? path.frames.size() - 2
                                                    : static_cast<std::size_t>(end - path.distances.begin() - 1);
    const auto& a     = path.frames[index];
    const auto& b     = path.frames[index + 1];
    const float t =
        static_cast<float>((distance - path.distances[index]) / (path.distances[index + 1] - path.distances[index]));
    const float attributeT           = std::clamp(t, 0.f, 1.f);
    auto        result               = a;
    result.sample.x                  = std::lerp(a.sample.x, b.sample.x, t);
    result.sample.y                  = std::lerp(a.sample.y, b.sample.y, t);
    result.sample.z                  = std::lerp(a.sample.z, b.sample.z, t);
    result.sample.scaleX             = std::lerp(a.sample.scaleX, b.sample.scaleX, attributeT);
    result.sample.scaleY             = std::lerp(a.sample.scaleY, b.sample.scaleY, attributeT);
    result.sample.normalizedDistance = distance / path.length;
    const V3 forward = normalize(V3{b.sample.x - a.sample.x, b.sample.y - a.sample.y, b.sample.z - a.sample.z});
    V3       up{std::lerp(a.upX, b.upX, attributeT), std::lerp(a.upY, b.upY, attributeT),
          std::lerp(a.upZ, b.upZ, attributeT)};
    up = up - forward * dot(up, forward);
    if (length(up) < 1e-6f) {
        up = V3{a.upX, a.upY, a.upZ} - forward * dot(V3{a.upX, a.upY, a.upZ}, forward);
        if (length(up) < 1e-6f) {
            const V3 axis = std::fabs(forward.y) < .9f ? V3{0, 1, 0} : V3{1, 0, 0};
            up            = axis - forward * dot(axis, forward);
        }
    }
    const V3 side          = normalize(cross(up, forward));
    up                     = normalize(cross(forward, side));
    result.forwardX        = forward.x;
    result.forwardY        = forward.y;
    result.forwardZ        = forward.z;
    result.sideX           = side.x;
    result.sideY           = side.y;
    result.sideZ           = side.z;
    result.upX             = up.x;
    result.upY             = up.y;
    result.upZ             = up.z;
    result.sample.tangentX = forward.x;
    result.sample.tangentY = forward.y;
    result.sample.tangentZ = forward.z;
    return Result<SplineFrameSample>::success(result);
}

Result<V3> sampleSurfaceProfilePoint(const RoadSurfacePolyline& path, float distance, float side, float asphaltHalf) {
    if (!std::isfinite(distance) || distance < 0 || !std::isfinite(side) || !std::isfinite(asphaltHalf) ||
        path.frames.size() < 2 || path.distances.size() != path.frames.size())
        return bakeFail<V3>(DiagnosticCode::InvalidArgument, "invalid surface profile request",
                            "sidePlacement.pathSampling");
    auto        end = std::lower_bound(path.distances.begin() + 1, path.distances.end(), static_cast<double>(distance));
    const auto  index = end == path.distances.end() ? path.frames.size() - 2
                                                    : static_cast<std::size_t>(end - path.distances.begin() - 1);
    const auto& a     = path.frames[index];
    const auto& b     = path.frames[index + 1];
    const float t =
        static_cast<float>((distance - path.distances[index]) / (path.distances[index + 1] - path.distances[index]));
    const float attributeT = std::clamp(t, 0.f, 1.f);
    const V3    offsetA    = V3{a.sideX, a.sideY, a.sideZ} * scaledProfileSide(side, asphaltHalf, a.sample.scaleX);
    const V3    offsetB    = V3{b.sideX, b.sideY, b.sideZ} * scaledProfileSide(side, asphaltHalf, b.sample.scaleX);
    return Result<V3>::success({std::lerp(a.sample.x, b.sample.x, t) + std::lerp(offsetA.x, offsetB.x, attributeT),
                                std::lerp(a.sample.y, b.sample.y, t) + std::lerp(offsetA.y, offsetB.y, attributeT),
                                std::lerp(a.sample.z, b.sample.z, t) + std::lerp(offsetA.z, offsetB.z, attributeT)});
}

Result<SplineSample> closestSurfacePolyline(const RoadSurfacePolyline& path, float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || path.frames.size() < 2)
        return bakeFail<SplineSample>(DiagnosticCode::InvalidArgument, "invalid surface closest-point request",
                                      "sidePlacement.pathSampling");
    const V3 target{x, y, z};
    float    best = std::numeric_limits<float>::max(), along = 0;
    for (std::size_t i = path.firstVisible + 1; i <= path.lastVisible; ++i) {
        const auto& a = path.frames[i - 1].sample;
        const auto& b = path.frames[i].sample;
        const V3    start{a.x, a.y, a.z}, delta{b.x - a.x, b.y - a.y, b.z - a.z};
        const float t          = std::clamp(dot(target - start, delta) / dot(delta, delta), 0.f, 1.f);
        const V3    difference = target - (start + delta * t);
        const float squared    = dot(difference, difference);
        if (squared < best) {
            best  = squared;
            along = static_cast<float>(std::lerp(path.distances[i - 1], path.distances[i], static_cast<double>(t)));
        }
    }
    auto sampled = sampleSurfacePolyline(path, along);
    if (!sampled.ok()) return Result<SplineSample>::failure(sampled.status());
    return Result<SplineSample>::success(sampled.value().sample);
}
}  // namespace eve::procgen::road::detail
