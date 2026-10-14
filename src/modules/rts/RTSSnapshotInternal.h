#pragma once
#include "rts/RTSTypes.h"
namespace eve::rts::snapshot_internal {
inline SubjectRef subjectOf(ecs::Entity* entity) {
    if (auto* value = dynamic_cast<Unit*>(entity)) return value->identity()->subject;
    if (auto* value = dynamic_cast<Building*>(entity)) return value->identity()->subject;
    if (auto* value = dynamic_cast<ResourceNode*>(entity)) return value->identity()->subject;
    if (auto* value = dynamic_cast<Player*>(entity)) return value->identity()->subject;
    if (auto* value = dynamic_cast<Faction*>(entity)) return value->identity()->subject;
    if (auto* value = dynamic_cast<Match*>(entity)) return value->identity()->subject;
    return {};
}

inline SubjectRef subjectOf(ecs::EntityHandle handle) {
    if (handle.table != ecs::current()) return {};
    return subjectOf(ecs::try_get(handle));
}

}  // namespace eve::rts::snapshot_internal
