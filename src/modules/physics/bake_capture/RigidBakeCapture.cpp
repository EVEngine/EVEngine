#include "physics/bake_capture/RigidBakeCapture.h"
#include <cmath>
#include <set>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/backend/bake/RigidPoseTrack.h"

namespace eve::physics {
namespace {
Result<void> validateBindings(PhysicsWorldHandle owner, std::span<const RigidBakeBinding> bindings, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || bindings.empty() || bindings.size() > 16384)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "invalid capture time or binding count", "rigidBakeCapture"));
    std::set<PersistentId>  identities;
    std::set<std::uint64_t> bodies;
    for (const auto& binding : bindings) {
        if (binding.world != owner || !binding.body.isValid())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle,
                                                           "foreign world or invalid body link", "rigidBakeCapture"));
        if (binding.id.isNil() || !identities.insert(binding.id).second || !bodies.insert(binding.body.packed()).second)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "nil or duplicate track/body binding", "rigidBakeCapture"));
    }
    return Result<void>::success();
}
}  // namespace
Result<RigidTrackFrame> captureRigidFrame(const World& world, std::span<const RigidBakeBinding> bindings,
                                          float seconds) {
    auto valid = validateBindings(world.runtimeHandle(), bindings, seconds);
    if (!valid) return Result<RigidTrackFrame>::failure(valid.status());
    RigidTrackFrame frame{seconds, {}};
    frame.samples.reserve(bindings.size());
    for (const auto& binding : bindings) {
        // Borrowed only until this iteration ends; structural world mutation is excluded by the contract.
        const auto* body = world.findBody(binding.body);
        if (!body)
            return Result<RigidTrackFrame>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "recorded 2D body no longer exists", "rigidBakeCapture"));
        const double halfAngle = static_cast<double>(body->getAngle()) * 0.5;
        frame.samples.push_back({binding.id,
                                 {body->getX(), body->getY(), 0, 0, 0, static_cast<float>(std::sin(halfAngle)),
                                  static_cast<float>(std::cos(halfAngle))}});
    }
    return Result<RigidTrackFrame>::success(std::move(frame));
}
Result<RigidTrackFrame> captureRigidFrame(const World3D& world, std::span<const RigidBakeBinding> bindings,
                                          float seconds) {
    auto valid = validateBindings(world.runtimeHandle(), bindings, seconds);
    if (!valid) return Result<RigidTrackFrame>::failure(valid.status());
    RigidTrackFrame frame{seconds, {}};
    frame.samples.reserve(bindings.size());
    for (const auto& binding : bindings) {
        // Borrowed only until this iteration ends; never placed in the returned frame.
        const auto* body = world.findBody(binding.body);
        if (!body)
            return Result<RigidTrackFrame>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "recorded 3D body no longer exists", "rigidBakeCapture"));
        frame.samples.push_back({binding.id,
                                 {body->getX(), body->getY(), body->getZ(), body->getRotX(), body->getRotY(),
                                  body->getRotZ(), body->getRotW()}});
    }
    return Result<RigidTrackFrame>::success(std::move(frame));
}
}  // namespace eve::physics
