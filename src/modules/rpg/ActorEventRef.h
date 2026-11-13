#pragma once
/** @file ActorEventRef.h
 * @brief rpg module-interface: non-owning event actor reference with world and entity retirement checks.
 * @remarks No identity registry, capability, script or tool endpoint. This process-local reference is not persistent.
 */
#include <ECS.hpp>
#include <optional>
#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "common/SubjectRef.h"

namespace eve::rpg {
class RPGActor;
class SimulationEventArchive;
/** @brief Presence of the originally captured actor, independent of slot/address reuse. */
enum class ActorEventPresence { None, Live, Retired };
/**
 * @brief Copyable event reference; never retains an actor or its Table.
 * @ownership Holds an inert generation handle and a weak Table incarnation witness.
 * @thread Actor/Table owner thread only; not synchronization against concurrent destruction.
 * @reentrancy No callbacks. Resolve again after any callback or structural mutation.
 */
class EVENGINE_API_PLATFORM ActorEventRef {
public:
    /** @brief Construct an absent reference, for example a default-self cast target. */
    ActorEventRef() = default;
    /** @brief Capture a currently borrowed actor; its Table must still be alive. No allocation. */
    explicit ActorEventRef(RPGActor& actor) noexcept;
    /** @brief Borrow the original live actor for the immediate operation, or return empty if retired/absent. */
    [[nodiscard]] OptionalRef<RPGActor> resolve() const noexcept;
    /** @brief Distinguish absent from retired references without accessing an expired world. */
    [[nodiscard]] ActorEventPresence presence() const noexcept;
    /** @brief Identity observed at reference construction, including after retirement; never backfilled by later
     * registration.
     * @ownership Returns an immutable historical value, not the actor's current registration or a resolver. */
    [[nodiscard]] std::optional<SubjectRef> capturedSubject() const noexcept { return subject_; }
    /** @brief Inert process-local identity; resolving this handle alone requires an independently live Table. */
    [[nodiscard]] ecs::EntityHandle handle() const noexcept { return handle_; }
    /** @brief Whether this reference belongs to the given simulation Table incarnation, including after retirement.
     * @remarks Restored retired history has destination affiliation but deliberately no native entity handle. */
    [[nodiscard]] bool isFromWorld(const ecs::TableLifetime& world) const noexcept;

private:
    friend class SimulationEventArchive;
    bool                      specified_ = false;
    ecs::EntityHandle         handle_;
    ecs::TableLifetime        world_;
    std::optional<SubjectRef> subject_;
};
}  // namespace eve::rpg
