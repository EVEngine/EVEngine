#pragma once
/** @file RPGSpawnSession.h
 * @brief rpg_spawn module-interface: owning core/domain/provider session and two-phase bundle restoration.
 * @remarks Uses canonical joint admission, actual actor factory and event handoff. No map/graphics, new ECS root,
 * capability registry, script/tool protocol or persistent schema is introduced.
 */
#include <memory>
#include <span>
#include "common/Snapshot.h"
#include "spawn/SpawnWorld.h"
namespace ecs {
class Table;
}
namespace eve::rpg {
struct VitalsEvent;
class SkillConditionBindings;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
class RPGSpawnJointRestore;
class PreparedRPGSpawnSession;
class RPGSpawnFactory;
struct ActorSnapshot;
/** @brief Owning native RPG Spawn composition; a retired session cannot advance or save.
 * @ownership Shares its actual Table with gameplay consumers and owns core/factory/providers/pending facts.
 * Destruction revokes sources and releases this Table owner; external shared Table owners may retain old actors.
 * @thread Simulation/domain owner thread outside Views; callbacks must not move/destroy the session.
 */
class EVENGINE_API_PLATFORM RPGSpawnSession {
public:
    /** @brief Release local owners; no implicit domain cleanup or gameplay events. */
    ~RPGSpawnSession();
    RPGSpawnSession(const RPGSpawnSession&)            = delete;
    RPGSpawnSession& operator=(const RPGSpawnSession&) = delete;
    /** @brief Share the actual domain Table for canonical RPG gameplay, never a default-table wrapper. */
    [[nodiscard]] std::shared_ptr<ecs::Table> world() const noexcept;
    /** @brief Borrow read-only core diagnostics until session destruction, including retired stale-handle queries. */
    [[nodiscard]] const spawn::SpawnWorld& spawning() const noexcept;
    /** @brief Borrow source handles until destruction; retirement makes all of them stale. */
    [[nodiscard]] std::span<const spawn::SpawnerHandle> sources() const noexcept;
    /** @brief Inspect a source-owned actor through the actual factory; retained origin-free actors stay in the Table.
     */
    [[nodiscard]] Result<ActorSnapshot> inspect(SubjectRef subject) const;
    /** @brief Translate each borrowed RPG event batch once and retain it across rejected frames/exceptions.
     * @return Owning report; an empty event batch retries pending facts. Does not poll or steal RPG event queues.
     * @remarks After successful translation, do not resubmit that batch even if subsequent frame work fails.
     * @thread Owner thread; non-reentrant. Observers may inspect/enqueue, not advance, capture or replace this owner.
     * @cost Tracked actor/event/pending traversal plus configured bounded scheduler work per frame.
     */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(const SimulationStep&             step,
                                                       std::span<const rpg::VitalsEvent> events     = {},
                                                       spawn::ActivationFrame            activation = {});
    /** @brief Queue a canonical control on an active session, including from observers. */
    [[nodiscard]] Result<void> enqueue(spawn::ControlCommand command);
    /** @brief Register a new runtime observer; subscriptions are not restored or transferred on replacement. */
    [[nodiscard]] Result<Subscription> subscribe(Observer<spawn::SpawnEvent>::Callback callback);
    /** @brief Freeze and seal the supported core/domain/pending bundle at one idle boundary.
     * @return Owning artifact or failure retaining pending inputs and live state. Hash callbacks may not mutate owners.
     * @thread Owner thread; same-session mutations/replacement reject during callbacks, without locks.
     * @cost Full participant capture, canonical joint admission and bundle hashing per save artifact.
     * @throws std::bad_alloc Local allocation failed without consuming pending inputs.
     */
    [[nodiscard]] Result<SnapshotEnvelope> capture(PersistentId saveEpoch, Revision revision,
                                                   const SnapshotHashProvider& hashProvider);

private:
    friend class PreparedRPGSpawnSession;
    struct Impl;
    explicit RPGSpawnSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief CPU-only isolated session candidate; saved actors materialize during commit, not preparation.
 * @ownership Owns exact admitted facts, fresh Table/core/factory/providers and remapped pending input. Cancellation
 * releases only unpublished local data. No actor capability registration occurs during preparation.
 * @thread Simulation/content owner thread; callbacks must not move/destroy an active operation's owners.
 */
class EVENGINE_API_PLATFORM PreparedRPGSpawnSession {
public:
    /** @brief Cancel the unpublished candidate. */
    ~PreparedRPGSpawnSession();
    /** @brief Transfer candidate ownership. */
    PreparedRPGSpawnSession(PreparedRPGSpawnSession&&) noexcept;
    /** @brief Cancel previous candidate and transfer ownership. */
    PreparedRPGSpawnSession& operator=(PreparedRPGSpawnSession&&) noexcept;
    PreparedRPGSpawnSession(const PreparedRPGSpawnSession&)            = delete;
    PreparedRPGSpawnSession& operator=(const PreparedRPGSpawnSession&) = delete;
    /** @brief Verify a complete bundle and bind an isolated session without creating saved actors.
     * @param bundle Whole artifact copied before the first hash callback; pending cannot be overridden separately.
     * @param placement Actual owning spatial provider, validated before source binding; no map fallback is invented.
     * @param conditions Actual immutable policy owner, shared into admitted candidates; no policy is executed.
     * @return Owning prepared session or failure without changing an existing runtime session or input artifact.
     * @remarks Supports the bundle's explicit safe RPG checkpoint scope, not map assets or full active RPG
     * status/casts.
     * @cost Whole bundle admission and source/provider/index preparation per restoration attempt.
     * @throws std::bad_alloc Local preparation failed without actor publication.
     */
    [[nodiscard]] static Result<PreparedRPGSpawnSession> prepare(
        const SnapshotEnvelope& bundle, PersistentId saveEpoch, const SnapshotHashProvider& hashProvider,
        std::shared_ptr<spawn::ISpawnPlacement>            placement,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Materialize saved actors atomically, revoke the idle old core, then exchange the session owner once.
     * @param target Active session owner to replace, or null for initial publication; never changed on failure.
     * @return Retired previous session owner (possibly null), or failure retaining this candidate for retry.
     * @ownership Caller receives old ownership for explicit disposal; its Table may still have external owners.
     * Old leases are revoked before success returns. Observers stay with the retired owner and are never replayed.
     * @thread Owner thread outside Views; target/candidate reentry rejects. No gameplay callbacks/events or locks.
     * @cost Full saved actor materialization per commit attempt; final owner exchange and revocation allocate nothing.
     * @throws std::bad_alloc Materialization staging failed, with target and candidate retained for retry.
     */
    [[nodiscard]] Result<std::unique_ptr<RPGSpawnSession>> commit(std::unique_ptr<RPGSpawnSession>& target);

private:
    PreparedRPGSpawnSession(std::unique_ptr<RPGSpawnSession> session, std::unique_ptr<RPGSpawnJointRestore> admitted);
    std::unique_ptr<RPGSpawnSession>      session_;
    std::unique_ptr<RPGSpawnJointRestore> admitted_;
    bool                                  committing_   = false;
    bool                                  materialized_ = false;
};
}  // namespace eve::rpg_spawn
