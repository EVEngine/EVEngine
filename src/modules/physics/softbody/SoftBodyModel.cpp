#include "physics/softbody/SoftBodyModel.h"

#include <cmath>

namespace eve::physics {
  // namespace

eve::Result<void> SoftBodyModel::validate() const {
    if (sourceMesh.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "sourceMesh must not be empty", "sourceMesh"));
    if (particles.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "model must contain particles", "particles"));
    for (std::size_t index = 0; index < particles.size(); ++index) {
        const auto& particle = particles[index];
        if (!std::isfinite(particle.x) || !std::isfinite(particle.y) || !std::isfinite(particle.z))
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "particle position must be finite", "particles[" + std::to_string(index) + "]"));
    }
    if (surfaceIndices.empty() || surfaceIndices.size() % 3 != 0)
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "surface indices must contain complete triangles", "surfaceIndices"));
    for (const auto index : surfaceIndices)
        if (index >= surfaceBindings.size()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "surface index is out of range", "surfaceIndices"));
    if (surfaceBindings.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "surface bindings must not be empty", "surfaceBindings"));
    for (const auto& binding : surfaceBindings)
        if (binding.particleIndex >= particles.size())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "surface binding particle is out of range", "surfaceBindings"));
    if (clusters.empty()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "model must contain shape clusters", "clusters"));
    for (const auto& cluster : clusters) {
        if (cluster.particleIndices.size() < 3)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "shape cluster must contain at least three particles", "clusters"));
        for (const auto index : cluster.particleIndices)
            if (index >= particles.size()) return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "cluster particle is out of range", "clusters"));
    }
    return eve::Result<void>::success();
}

}  // namespace eve::physics
