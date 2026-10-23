#include "physics/WorldSphereSweep.h"
#include <charconv>
#include <cmath>
#include <limits>
#include "common/Diagnostic.h"
#include "common/WorldSphereSweep.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

namespace eve::physics {
namespace {
class Sweep final : public IWorldSphereSweep {
public:
    Sweep(PhysicsWorldHandle world, std::uint64_t epoch, PhysicsBodyHandle ignored, WorldSphereSweepSettings settings)
        : world_(world), epoch_(epoch), ignored_(ignored), settings_(settings) {}
    Result<std::vector<WorldSphereContact>> query(double x, double y, double z, double endX, double endY,
                                                  double endZ) const override {
        auto* world = World3D::findWorld(world_);
        if (!world || !world->isValid() || world->queryEpoch() != epoch_)
            return Result<std::vector<WorldSphereContact>>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Bound World3D was destroyed or restored; rebind the sweep", "world"));
        if (settings_.ignoredBodyId >= 0) {
            auto* body = world->findBodyById(settings_.ignoredBodyId);
            if (!body || body->runtimeHandle() != ignored_)
                return Result<std::vector<WorldSphereContact>>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound,
                                      "Ignored body was destroyed or replaced; rebind the sweep", "ignoredBody"));
        }
        return world->sweepSphereOwned(x, y, z, endX, endY, endZ, settings_);
    }

private:
    PhysicsWorldHandle       world_;
    std::uint64_t            epoch_;
    PhysicsBodyHandle        ignored_;
    WorldSphereSweepSettings settings_;
};
class Factory final : public IWorldSphereSweepFactory {
public:
    Result<std::unique_ptr<IWorldSphereSweep>> bind(std::string_view         token,
                                                    WorldSphereSweepSettings settings) const override {
        const auto    separator = token.find(':');
        std::uint64_t packed = 0, epoch = 0;
        if (separator == std::string_view::npos)
            return Result<std::unique_ptr<IWorldSphereSweep>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid World3D query token", "world"));
        const auto first = token.substr(0, separator), second = token.substr(separator + 1);
        auto       parse = [](std::string_view part, std::uint64_t& result) {
            const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result, 16);
            return !part.empty() && parsed.ec == std::errc{} && parsed.ptr == part.data() + part.size();
        };
        if (!parse(first, packed) || !parse(second, epoch) || !std::isfinite(settings.radius) || settings.radius <= 0 ||
            settings.ignoredBodyId < -1)
            return Result<std::unique_ptr<IWorldSphereSweep>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Invalid world token, radius or ignored body", "worldSweep"));
        const auto handle = PhysicsWorldHandle::fromPacked(packed);
        auto*      world  = World3D::findWorld(handle);
        if (!world || !world->isValid() || world->queryEpoch() != epoch)
            return Result<std::unique_ptr<IWorldSphereSweep>>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "World3D query token is stale", "world"));
        PhysicsBodyHandle ignored = PhysicsBodyHandle::invalid();
        if (settings.ignoredBodyId >= 0) {
            auto* body = world->findBodyById(settings.ignoredBodyId);
            if (!body)
                return Result<std::unique_ptr<IWorldSphereSweep>>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "Ignored body does not exist in this world", "ignoredBody"));
            ignored = body->runtimeHandle();
        }
        return Result<std::unique_ptr<IWorldSphereSweep>>::success(
            std::make_unique<Sweep>(handle, epoch, ignored, settings));
    }
};
}  // namespace
IWorldSphereSweepFactory& worldSphereSweepFactory() {
    static Factory factory;
    return factory;
}
std::string World3D::queryToken() const {
    char        buffer[17]{};
    const auto  encoded = std::to_chars(buffer, buffer + 16, runtimeHandle().packed(), 16);
    std::string token(buffer, encoded.ptr);
    const auto  epoch = std::to_chars(buffer, buffer + 16, queryEpoch(), 16);
    return token + ":" + std::string(buffer, epoch.ptr);
}
}  // namespace eve::physics
