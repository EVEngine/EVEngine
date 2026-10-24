#include "physics/bake/deform_capture/DeformBakeCapture.h"
#include <cmath>
#include "physics/bake/DeformTrack.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/rope/Rope3D.h"
namespace eve::physics {
namespace {
Result<std::vector<std::uint32_t>> connectivity(const Rope3D& source) { return source.captureSegmentIndices(); }
Result<std::vector<std::uint32_t>> connectivity(const Cloth3D& source) { return source.captureTriangleIndices(); }
template <class Source>
Result<DeformFrame> capture(const Source& source, float seconds, DeformPrimitive primitive) {
    const int count = source.getParticleCount();
    if (!std::isfinite(seconds) || seconds < 0 || count <= 0 || count > 1048576)
        return Result<DeformFrame>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "invalid deformation capture time or particle count",
                                                              "deformBakeCapture"));
    auto indices = connectivity(source);
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
}  // namespace
Result<DeformFrame> captureDeformFrame(const Rope3D& source, float seconds) {
    return capture(source, seconds, DeformPrimitive::Segments);
}
Result<DeformFrame> captureDeformFrame(const Cloth3D& source, float seconds) {
    return capture(source, seconds, DeformPrimitive::Triangles);
}
}  // namespace eve::physics
