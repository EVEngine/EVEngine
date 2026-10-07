#include "physics/softbody/SoftBodyModel.h"

#include <cmath>

namespace eve::physics {
  // namespace

eve::Result<void> SoftBodyModel::validate() const {
    if (sourceMesh.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("sourceMesh must not be empty"), std::move("sourceMesh")));
    if (particles.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("model must contain particles"), std::move("particles")));
    for (std::size_t index = 0; index < particles.size(); ++index) {
        const auto& particle = particles[index];
        if (!std::isfinite(particle.x) || !std::isfinite(particle.y) || !std::isfinite(particle.z))
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("particle position must be finite"), std::move("particles[" + std::to_string(index) + "]")));
    }
    if (surfaceIndices.empty() || surfaceIndices.size() % 3 != 0)
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("surface indices must contain complete triangles"), std::move("surfaceIndices")));
    for (const auto index : surfaceIndices)
        if (index >= surfaceBindings.size()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("surface index is out of range"), std::move("surfaceIndices")));
    if (surfaceBindings.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("surface bindings must not be empty"), std::move("surfaceBindings")));
    for (const auto& binding : surfaceBindings)
        if (binding.particleIndex >= particles.size())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("surface binding particle is out of range"), std::move("surfaceBindings")));
    if (clusters.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("model must contain shape clusters"), std::move("clusters")));
    for (const auto& cluster : clusters) {
        if (cluster.particleIndices.size() < 3)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("shape cluster must contain at least three particles"), std::move("clusters")));
        for (const auto index : cluster.particleIndices)
            if (index >= particles.size()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move("cluster particle is out of range"), std::move("clusters")));
    }
    return eve::Result<void>::success();
}

}  // namespace eve::physics
