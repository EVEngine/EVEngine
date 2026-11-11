#pragma once
/** @file ActorIdentity.h
 * @brief rpg module-interface: authoritative optional actor subject registration and nonmutating lookup.
 * @remarks No registry, entity superclass, capability, event, script or tool endpoint is introduced.
 */
#include <optional>
#include "common/Export.h"
#include "common/Result.h"
#include "common/SubjectRef.h"

namespace eve::rpg {
class RPGActor;
/**
 * @brief Bind the caller-supplied logical identity once to a live actor incarnation.
 * @ownership ActorIdentity owns the subject; the caller owns identity allocation/reservation across sessions.
 * This checks the current RPG-family roster, not other domains or retired identity reservations.
 * @return Applied/idempotent success, or a checked nil, stale, rename, duplicate or phase failure.
 * @thread Actor/Table owner thread, outside Views; no callbacks or locks.
 * @reentrancy Non-reentrant synchronous registration. Body checkpoint restore never changes this binding.
 * @cost Scan the current RPG roster once per explicit registration; constant component storage per actor.
 * @throws std::bad_alloc Before binding publication; no actor identity facts change on failure.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> bindActorIdentity(RPGActor& actor, SubjectRef subject);
/** @brief Read a live borrowed actor's registered subject without allocation, mutation or callbacks.
 * @ownership Returns a value snapshot, not an actor/world borrow. Unregistered actors return nullopt.
 * @thread Actor/Table owner thread; the caller keeps the borrowed actor alive for this immediate query. */
[[nodiscard]] EVENGINE_API_PLATFORM std::optional<SubjectRef> actorSubject(const RPGActor& actor) noexcept;
/** @brief Orthogonal optional primary identity; actor body prototypes/checkpoints deliberately do not copy it. */
class ActorIdentity {
public:
    /** @brief Start unregistered; registration is explicit and never invents an ID. */
    ActorIdentity() = default;

private:
    SubjectRef                       subject_;
    friend Result<void>              bindActorIdentity(RPGActor&, SubjectRef);
    friend std::optional<SubjectRef> actorSubject(const RPGActor&) noexcept;
};
}  // namespace eve::rpg
