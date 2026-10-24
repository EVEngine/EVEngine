#include <algorithm>
#include <cmath>
#include <limits>
#include "common/ProcgenWorldQuery.h"
#include "procgen/road/RoadBakeInternal.h"
namespace eve::procgen::road::detail {
Result<V3> sideTerrainNormal(eve::IProcgenWorldQuery& query, float x, float z, const RoadBakeOptions& options,
                             std::size_t& work) {
    if (!std::isfinite(options.placementQueryMinY) || !std::isfinite(options.placementQueryMaxY) ||
        options.placementQueryMinY > options.placementQueryMaxY || options.maximumPlacementQueries < 1 ||
        options.maximumPlacementQueries > 4194304u)
        return bakeFail<V3>(DiagnosticCode::InvalidArgument, "invalid placement projection window or budget",
                            "sideTerrain");
    if (work >= options.maximumPlacementQueries)
        return bakeFail<V3>(DiagnosticCode::PreconditionViolation, "side-object terrain query budget exceeded",
                            "sideTerrain");
    ++work;
    auto projected =
        query.projectDown(x, z, options.placementQueryMaxY, options.placementQueryMinY, options.placementQueryMask);
    if (!projected.ok()) return Result<V3>::failure(projected.status());
    const auto& hit = projected.value();
    if (!hit.hit) return bakeFail<V3>(DiagnosticCode::NotFound, "side-object normal projection missed", "sideTerrain");
    const V3 normal{hit.normalX, hit.normalY, hit.normalZ};
    if (!std::isfinite(hit.y) || hit.y < options.placementQueryMinY || hit.y > options.placementQueryMaxY ||
        !std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
        !std::isfinite(length(normal)) || length(normal) < 1e-6f)
        return bakeFail<V3>(DiagnosticCode::PreconditionViolation,
                            "terrain provider returned an invalid normal or height", "sideTerrain");
    return Result<V3>::success(normalize(normal));
}
Result<float> sideTerrainHeight(const RoadSideTerrainRule& rule, eve::IProcgenWorldQuery& query, float x, float z,
                                const ProcgenPoint& pose, const std::array<float, 6>* assetBounds,
                                const RoadBakeOptions& options, std::size_t& work) {
    if (!std::isfinite(options.placementQueryMinY) || !std::isfinite(options.placementQueryMaxY) ||
        options.placementQueryMinY > options.placementQueryMaxY || options.maximumPlacementQueries < 1 ||
        options.maximumPlacementQueries > 4194304u)
        return bakeFail<float>(DiagnosticCode::InvalidArgument, "invalid placement projection window or budget",
                               "sideTerrain");
    const int       count   = rule.mode == RoadSideTerrainMode::Center ? 1 : rule.samplesPerAxis;
    constexpr float radians = .017453292519943295f;
    const float     c = std::cos(pose.yaw * radians), s = std::sin(pose.yaw * radians);
    const float     cp = std::cos(pose.pitch * radians), sp = std::sin(pose.pitch * radians);
    const float     cr = std::cos(pose.roll * radians), sr = std::sin(pose.roll * radians);
    const int       layers = count > 1 && assetBounds && (*assetBounds)[1] != (*assetBounds)[4] &&
                               (std::fabs(sp) > 1e-6f || std::fabs(sr) > 1e-6f)
                                 ? 2
                                 : 1;
    double          sum    = 0.;
    float           low = std::numeric_limits<float>::max(), high = -low;
    for (int layer = 0; layer < layers; ++layer)
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j) {
                if (work >= options.maximumPlacementQueries)
                    return bakeFail<float>(DiagnosticCode::PreconditionViolation,
                                           "side-object terrain query budget exceeded", "sideTerrain");
                const float side =
                    count == 1 ? 0.f : std::lerp(rule.minSide, rule.maxSide, static_cast<float>(i) / (count - 1));
                const float forward =
                    count == 1 ? 0.f : std::lerp(rule.minForward, rule.maxForward, static_cast<float>(j) / (count - 1));
                const float up = layers == 2 ? (*assetBounds)[layer == 0 ? 1 : 4] : 0.f;
                const float rx = cr * side - sr * up, ry = sr * side + cr * up;
                const float rz = sp * ry + cp * forward;
                const float px = x + c * rx + s * rz, pz = z - s * rx + c * rz;
                if (!std::isfinite(px) || !std::isfinite(pz))
                    return bakeFail<float>(DiagnosticCode::InvalidArgument, "placement footprint is not representable",
                                           "sideTerrain");
                ++work;
                auto projected = query.projectDown(px, pz, options.placementQueryMaxY, options.placementQueryMinY,
                                                   options.placementQueryMask);
                if (!projected.ok()) return Result<float>::failure(projected.status());
                const auto& hit = projected.value();
                if (!hit.hit)
                    return bakeFail<float>(DiagnosticCode::NotFound,
                                           "side-object terrain footprint has a projection miss", "sideTerrain");
                if (!std::isfinite(hit.y) || hit.y < options.placementQueryMinY || hit.y > options.placementQueryMaxY)
                    return bakeFail<float>(DiagnosticCode::PreconditionViolation,
                                           "terrain provider returned an invalid height", "sideTerrain");
                low  = std::min(low, hit.y);
                high = std::max(high, hit.y);
                sum += hit.y;
            }
    if (rule.mode == RoadSideTerrainMode::Lowest) return Result<float>::success(low);
    if (rule.mode == RoadSideTerrainMode::Highest) return Result<float>::success(high);
    return Result<float>::success(static_cast<float>(sum / (count * count * layers)));
}
}  // namespace eve::procgen::road::detail
