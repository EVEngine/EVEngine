#pragma once
#include "rts/RTSComponentAccessInternal.h"
#include "rts/RTSTypes.h"
namespace eve::rts::snapshot_internal {
/** @brief Observe an existing identity without materializing missing ECS state.
 * @ownership Borrows entity/component only during the query; returns an independent identity value.
 * @lifetime Entity and its Table must remain alive throughout this synchronous owner-thread call.
 */
template <class T>
SubjectRef existingSubject(T& entity) noexcept {
    const auto* identity = detail::existingComponent<typename T::Identity>(entity);
    return identity ? identity->subject : SubjectRef{};
}
/** @brief Read a stable RTS identity without creating missing identity state.
 * @param entity Nullable ECS entity borrowed only for this synchronous query.
 * @ownership No entity/component pointer is retained; the returned identity is an independent value.
 * @lifetime The entity and its Table must remain alive during this owner-thread call.
 * @return Empty for null, unsupported entity families or a missing identity component; no callbacks.
 */
inline SubjectRef subjectOf(ecs::Entity* entity) {
    if (auto* value = dynamic_cast<Unit*>(entity)) return existingSubject(*value);
    if (auto* value = dynamic_cast<Building*>(entity)) return existingSubject(*value);
    if (auto* value = dynamic_cast<ResourceNode*>(entity)) return existingSubject(*value);
    if (auto* value = dynamic_cast<Player*>(entity)) return existingSubject(*value);
    if (auto* value = dynamic_cast<Faction*>(entity)) return existingSubject(*value);
    if (auto* value = dynamic_cast<Match*>(entity)) return existingSubject(*value);
    return {};
}

inline SubjectRef subjectOf(ecs::EntityHandle handle) {
    if (handle.table != ecs::current()) return {};
    return subjectOf(ecs::try_get(handle));
}

}  // namespace eve::rts::snapshot_internal
