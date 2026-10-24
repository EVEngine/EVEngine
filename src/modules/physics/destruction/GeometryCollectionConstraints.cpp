#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "physics/destruction/GeometryCollectionInstance.h"
namespace eve::physics {
eve::Result<std::vector<std::unique_ptr<Joint3D>>> GeometryCollectionInstance::stageConstraints(
    const std::vector<EdgeRuntime>& edges) {
    using Candidate = std::vector<std::unique_ptr<Joint3D>>;
    auto* world     = resolveWorld();
    if (!world)
        return eve::Result<Candidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "constraint world is gone", "world"));
    Candidate candidate(edges.size());
    try {
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto& e = edges[i];
            if (e.broken) continue;
            auto       a = resolveBoneBody(e.boneA), b = resolveBoneBody(e.boneB);
            const bool aOk = a.ok(), bOk = b.ok();
            if (!aOk || !bOk) {
                if (!aOk) return eve::Result<Candidate>::failure(a.status());
                return eve::Result<Candidate>::failure(b.status());
            }
            candidate[i].reset(world->newWeldJoint(a.value(), b.value(), (a.value()->getX() + b.value()->getX()) * .5f,
                                                   (a.value()->getY() + b.value()->getY()) * .5f,
                                                   (a.value()->getZ() + b.value()->getZ()) * .5f, false));
            if (!candidate[i] || !candidate[i]->isValid())
                return eve::Result<Candidate>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed, "fragment weld creation failed", "joint"));
        }
    } catch (const std::exception& error) {
        return eve::Result<Candidate>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "joint"));
    }
    return eve::Result<Candidate>::success(std::move(candidate));
}
void GeometryCollectionInstance::releaseConstraints() {
    ownedJoints_.clear();
    for (auto& edge : edges_) edge.joint = PhysicsJointHandle::invalid();
}
}  // namespace eve::physics
