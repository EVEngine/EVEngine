// ECS scope: RPGActor View; reads SpawnedActorState; no writes, structural changes or events.
// Services: observed domain table; phase: spatial plan/final admission, before actor publication.
// eve-hot-path
// read canonical actor footprints at explicit spatial batch boundaries.
#include "rpg/spawn/RPGSpawnOccupancy.h"
#include <cmath>
#include "rpg/RPGActor.h"
#include "rpg/spawn/RPGSpawnFactory.h"
namespace eve::rpg_spawn {
RPGSpawnOccupancy::RPGSpawnOccupancy(std::shared_ptr<ecs::Table> world, double bodyRadius)
    : world_(std::move(world)), bodyRadius_(bodyRadius) {}
Result<std::vector<spawn::OccupiedDisk>> RPGSpawnOccupancy::capture() const {
    auto world = world_.lock();
    if (!world)
        return Result<std::vector<spawn::OccupiedDisk>>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RPG occupancy world destroyed", "rpg.spawn.occupancy"));
    if (!std::isfinite(bodyRadius_) || bodyRadius_ < 0)
        return Result<std::vector<spawn::OccupiedDisk>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG body radius", "rpg.spawn.occupancy"));
    std::vector<spawn::OccupiedDisk> result;
    auto                             view = ecs::View<rpg::RPGActor>(*world);
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto* actor  = it.entity();
        auto* buffer = actor->getComponentManager().getComponentBuffer<SpawnedActorState>();
        if (!buffer || !buffer->has(actor->id))
            return Result<std::vector<spawn::OccupiedDisk>>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "Actor lacks canonical world position projection", "rpg.spawn.occupancy"));
        const auto& state      = *buffer->ptr(actor->id);
        const auto  registered = rpg::actorSubject(*actor);
        if (!registered || !std::isfinite(state.position.x) || !std::isfinite(state.position.y))
            return Result<std::vector<spawn::OccupiedDisk>>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "Invalid actor spatial projection", "rpg.spawn.occupancy"));
        result.push_back({*registered, state.position, bodyRadius_});
    }
    return Result<std::vector<spawn::OccupiedDisk>>::success(std::move(result));
}
}  // namespace eve::rpg_spawn
