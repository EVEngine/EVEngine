// eve-hot-path
#include "rpg/ActorEventRef.h"
#include "rpg/RPGActor.h"

namespace eve::rpg {
ActorEventRef::ActorEventRef(RPGActor& actor) noexcept
    : specified_(true),
      handle_(ecs::handle_of(&actor)),
      world_(actor.table->lifetime()),
      subject_(actorSubject(actor)) {}
OptionalRef<RPGActor> ActorEventRef::resolve() const noexcept {
    if (!world_.isAlive()) return {};
    auto* actor = dynamic_cast<RPGActor*>(ecs::try_get(handle_));
    return actor ? OptionalRef<RPGActor>{std::ref(*actor)} : OptionalRef<RPGActor>{};
}
ActorEventPresence ActorEventRef::presence() const noexcept {
    if (!specified_) return ActorEventPresence::None;
    return resolve() ? ActorEventPresence::Live : ActorEventPresence::Retired;
}
bool ActorEventRef::isFromWorld(const ecs::TableLifetime& world) const noexcept { return world_.sameOwner(world); }
}  // namespace eve::rpg
