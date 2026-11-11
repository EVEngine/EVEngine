// eve-hot-path
// ECS: Unit family, existing Identity/Motion/Crowd/Containment reads only; no writes or structural mutation.
// Phase: explicit placement/final-check capture outside Views. No events, clocks, RNG, locks or callbacks.
#include "rts/spawn/RTSSpawnOccupancy.h"
#include <cmath>
#include "common/LifetimeWitness.h"
#include "rts/RTS.h"
#include "rts/RTSComponentAccessInternal.h"
namespace eve::rts_spawn {
struct RTSSpawnOccupancy::Impl {
    std::weak_ptr<ecs::Table> world;
    LifetimeWitness           lifetime;
};
RTSSpawnOccupancy::RTSSpawnOccupancy(rts::RTS& domain)
    : impl_(std::make_unique<Impl>(Impl{domain.ownedWorld(), domain.lifetimeWitness()})) {}
RTSSpawnOccupancy::~RTSSpawnOccupancy() = default;
Result<std::vector<spawn::OccupiedDisk>> RTSSpawnOccupancy::capture() const {
    using R    = Result<std::vector<spawn::OccupiedDisk>>;
    auto world = impl_->world.lock();
    if (!world || !impl_->lifetime.isAlive())
        return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "RTS occupancy owner expired"));
    if (world->is_deferred())
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RTS occupancy requires a Table outside Views"));
    std::vector<spawn::OccupiedDisk> result;
    auto                             view = ecs::View<rts::Unit>(*world);
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto*       unit        = it.entity();
        const auto* identity    = rts::detail::existingComponent<rts::Unit::Identity>(*unit);
        const auto* motion      = rts::detail::existingComponent<rts::Unit::Motion>(*unit);
        const auto* settings    = rts::detail::existingComponent<rts::Unit::Crowd>(*unit);
        const auto* containment = rts::detail::existingComponent<rts::Unit::Containment>(*unit);
        if (!identity || !motion || !settings || !containment)
            return R::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "RTS unit lacks canonical spatial components"));
        if (containment->container.isBound()) continue;
        if (!identity->subject.isValid() || !std::isfinite(motion->x) || !std::isfinite(motion->y) ||
            !std::isfinite(settings->radius) || settings->radius < 0)
            return R::failure(Diagnostic::error(DiagnosticCode::InvariantViolation, "RTS unit footprint is invalid"));
        result.push_back({identity->subject, {motion->x, motion->y}, settings->radius});
    }
    return R::success(std::move(result));
}
}  // namespace eve::rts_spawn
