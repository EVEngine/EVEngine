#include "fluids/OceanMesh.h"
#include "fluids/OceanWaterView.h"

#include <cmath>

namespace eve::fluids {
Result<OceanMeshFrame> OceanMeshFrameBuilder::build() const {
    auto valid = [](double value) { return std::isfinite(value) && std::abs(value) <= 1e9; };
    if (!valid(center.x) || !valid(center.y) || !valid(origin.x) || !valid(origin.y) || !valid(origin.z) ||
        mesh_.parameterOffsets.empty() || mesh_.parameterOffsets.size() > 524289)
        return Result<OceanMeshFrame>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid ocean mesh frame coordinates or size", "fluids.ocean.mesh"));
    OceanMeshFrame frame;
    frame.origin = origin;
    frame.time   = water_.time();
    frame.positions.reserve(mesh_.parameterOffsets.size() * 3);
    frame.normals.reserve(mesh_.parameterOffsets.size() * 3);
    frame.surface.reserve(mesh_.parameterOffsets.size() * 2);
    for (const auto offset : mesh_.parameterOffsets) {
        auto surface = water_.geometry(center + offset);
        if (!surface) return Result<OceanMeshFrame>::failure(surface.status());
        const auto  local    = surface.value().position - origin;
        const auto  normal   = surface.value().normal();
        const auto& geometry = surface.value();
        frame.surface.push_back(static_cast<float>(geometry.foam));
        frame.surface.push_back(
            static_cast<float>(geometry.tangentX.x * geometry.tangentZ.z - geometry.tangentZ.x * geometry.tangentX.z));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
            return Result<OceanMeshFrame>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "Degenerate ocean surface tangent", "fluids.ocean.mesh"));
        for (unsigned axis = 0; axis < 3; ++axis) {
            frame.positions.push_back(static_cast<float>(local[axis]));
            frame.normals.push_back(static_cast<float>(normal[axis]));
        }
    }
    return Result<OceanMeshFrame>::success(std::move(frame));
}
}  // namespace eve::fluids
