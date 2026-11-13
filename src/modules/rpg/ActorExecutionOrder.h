#pragma once
/** @file ActorExecutionOrder.h
 * @brief rpg module-interface: persistent simulation ordering values, never actor lookup identities.
 * @remarks ECS/data face: ActorExecutionOrder is owned by RPG actors and encoded only by actor v5.
 * No new capability, event, script or tool endpoint; phase time remains caller supplied.
 */
#include <optional>
#include "common/Identity.h"
namespace eve::rpg {
/** @brief Type domain for ordering values; not a subject identity. */
struct ActorExecutionTag;
/** @brief Strong ordering key. Domain coordinators may seed it from a stable subject's UUID bytes.
 * @remarks This re-tag expresses an execution order value, not identity ownership or a resolvable Link.
 */
using ActorExecutionKey = StrongUuid<ActorExecutionTag>;
/** @brief Hot authoritative persistent RPG phase order; no entity, world or service reference.
 * @remarks Absent/equal keys retain legacy View order. Replay coordinators must admit non-nil unique keys
 * for their complete participant. Keys are sampled once per system phase; callbacks affect later phases.
 * @thread Actor simulation owner thread; no implicit callbacks or allocation when assigning a key.
 */
struct ActorExecutionOrder {
    /** @brief Local simulation order, persisted by actor v5; null selects the legacy unkeyed group. */
    std::optional<ActorExecutionKey> key;
};
}  // namespace eve::rpg
