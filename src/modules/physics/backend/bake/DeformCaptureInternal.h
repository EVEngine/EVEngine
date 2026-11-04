#pragma once
#include <cmath>
#include "physics/backend/bake/DeformTrack.h"
namespace eve::physics::bake_detail {
template <class Source, class Connectivity>
Result<DeformFrame> captureDeformSource(const Source& source, float seconds, DeformPrimitive primitive,
                                        Connectivity connectivity) {
    const int count = source.getParticleCount();
    if (!std::isfinite(seconds) || seconds < 0 || count <= 0 || count > 1048576)
        return Result<DeformFrame>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "invalid deformation capture time or particle count",
                                                              "deformBakeCapture"));
    auto indices = connectivity();
    if (!indices) return Result<DeformFrame>::failure(indices.status());
    auto topology =
        DeformTopology::prepare(primitive, static_cast<std::size_t>(count), std::move(indices).takeValue(), {});
    if (!topology) return Result<DeformFrame>::failure(topology.status());
    std::vector<DeformPoint> positions;
    positions.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        DeformPoint point{source.getParticleX(i), source.getParticleY(i), source.getParticleZ(i)};
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            return Result<DeformFrame>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "nonfinite deformation capture position", "deformBakeCapture"));
        positions.push_back(point);
    }
    return Result<DeformFrame>::success({seconds, std::move(topology).takeValue(), std::move(positions)});
}
}  // namespace eve::physics::bake_detail
