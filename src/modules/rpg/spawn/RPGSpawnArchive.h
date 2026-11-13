#pragma once
/** @file RPGSpawnArchive.h
 * @brief rpg_spawn module-interface: owning verified archive structure before domain restore admission.
 * @remarks No ECS, capabilities, events, scripts, tools or live registry mutation. No live restore handles are issued.
 */
#include <memory>
#include <optional>
#include <span>
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
#include "spawn/SpawnMap.h"
#include "spawn/SpawnSavedState.h"
namespace eve::definitions {
struct Definition;
}
namespace eve::rpg {
struct EffectDefinition;
struct ImplementationIdentity;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
/** @brief Saved core source key, resolved by the joint restore before any live source can be issued.
 * @remarks Deliberately a different type from SpawnerHandle; contains no process incarnation or lease.
 */
using ArchivedSpawnOrigin = spawn::SavedSpawnerKey;
/** @brief Structurally parsed actor record; checkpoint/member semantics still require canonical domain admission. */
struct ArchivedSpawnActor {
    SubjectRef subject;
    /** @brief Durable source key for a later coordinated join, never a running source handle. */
    std::optional<ArchivedSpawnOrigin> origin;
    spawn::Point                       position;
    std::size_t                        unitRevision = 0;
    std::string                        checkpointJson;
};
/** @brief Verified eve.rpg-spawn-archive/v1-v4 structure, not permission to create or publish actors.
 * @ownership Owns copied records and actor JSON, without source world/registry references.
 * @thread Owner-thread parsing with no locks; only the explicit digest callback invokes external code.
 */
class EVENGINE_API_PLATFORM RPGSpawnArchive {
public:
    /** @brief Release parsed local facts. */
    ~RPGSpawnArchive();
    /** @brief Transfer parsed facts. */
    RPGSpawnArchive(RPGSpawnArchive&&) noexcept;
    /** @brief Release previous facts and transfer ownership. */
    RPGSpawnArchive& operator=(RPGSpawnArchive&&) noexcept;
    RPGSpawnArchive(const RPGSpawnArchive&)            = delete;
    RPGSpawnArchive& operator=(const RPGSpawnArchive&) = delete;
    /** @brief Copy input, verify its digest/epoch and strictly parse the archive's structural contract.
     * @param input Synchronous envelope borrow, copied before invoking hashProvider.
     * @param saveEpoch Required joint-save identity, not a live world incarnation.
     * @param hashProvider Required digest implementation, not retained; exceptions become CallbackFailure.
     * @return Owning structural archive or failure without publication. Unknown archive fields/versions are rejected.
     * @remarks V4 stores explicit future unit indices. V1/v2/v3 migrate to empty future bindings; core restore must
     * reject missing future references instead of choosing an actor's generation. V3 stores implementation revisions.
     * V1/v2 migrate to an empty identity set: missing custom identity remains an admission error, never replaced by
     * live metadata. V2 stores direct effect fields. V1 migrates to an empty effect set; domain admission rejects
     * missing references instead of using live effects. Nested actor v1/v2/v3 JSON and unit/skill records retain their
     * canonical domain field policies; this stage checks headers, keys, ranges and indexes, not actor/member semantics
     * or content closure. A later domain admission must validate every record/reference before materialization; no
     * latest-content fallback.
     * @cost Full envelope copy/hash verification and bounded structural parsing per imported participant.
     * @throws std::bad_alloc Owning allocation failed without changing any live owner.
     */
    [[nodiscard]] static Result<RPGSpawnArchive> parse(const SnapshotEnvelope& input, PersistentId saveEpoch,
                                                       const SnapshotHashProvider& hashProvider);
    /** @brief Borrow validated effect fields until this archive is moved or destroyed; v1 supplies an empty set. */
    [[nodiscard]] std::span<const rpg::EffectDefinition> effects() const noexcept;
    /** @brief Borrow saved implementation revisions until move/destruction; v1/v2 supply an empty set. */
    [[nodiscard]] std::span<const rpg::ImplementationIdentity> implementations() const noexcept;
    /** @brief Saved joint identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Verified envelope revision; zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Verified envelope simulation boundary; zero after move. */
    [[nodiscard]] SimulationTick tick() const noexcept;
    /** @brief Borrow parsed actors until this archive is moved or destroyed. */
    [[nodiscard]] std::span<const ArchivedSpawnActor> actors() const noexcept;
    /** @brief Borrow exact unit source revisions until this archive is moved or destroyed. */
    [[nodiscard]] std::span<const definitions::Definition> units() const noexcept;
    /** @brief Borrow validated unit-record indices for exact future bindings until move/destruction.
     * @remarks At most one index per logical unit reference. V1/v2/v3 migrate to an empty set, never inferred from
     * actors.
     */
    [[nodiscard]] std::span<const std::size_t> futureUnitRevisions() const noexcept;
    /** @brief Borrow exact skill source records until this archive is moved or destroyed. */
    [[nodiscard]] std::span<const definitions::Definition> skills() const noexcept;

private:
    struct Impl;
    explicit RPGSpawnArchive(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rpg_spawn
