#pragma once
/** @file SpawnDiagnostics.h
 * @brief Owning published scheduler observations; not persistent data, source leases or domain actor snapshots.
 */
#include "spawn/SpawnWorld.h"
namespace eve::spawn {
/** @brief Actual shared budget ledger at one published simulation boundary; no in-flight reservation survives it. */
struct BudgetSnapshot {
    std::string   id;
    std::uint32_t alive = 0;
    std::uint32_t limit = 0;
};
/** @brief Actual instance observation with its pinned immutable content owner and budget binding. */
struct DiagnosticSource {
    SpawnerHandle                           source;
    SpawnerSnapshot                         state;
    std::string                             budget;
    std::uint32_t                           aliveLimit = 0;
    std::shared_ptr<const CompiledTemplate> templateRevision;
    /** @brief Owning activation predicate from the actual admitted instance; not inferred from phase. */
    Activation activation;
};
/** @brief Detached diagnostic builder output; mutable projections cannot modify authoritative scheduler facts.
 * @ownership Owns rows/subjects and shares actual immutable template revisions; retains no world/provider/callback.
 * Handles are observation identities, not leases. Safe to transfer as an owning value to the UI thread.
 */
struct WorldDiagnostics {
    PersistentId                  owner;
    std::uint64_t                 epoch = 0;
    WorldIncarnation              incarnation;
    SimulationTick                tick;
    EventSequence                 sequence;
    std::vector<DiagnosticSource> sources;
    std::vector<BudgetSnapshot>   budgets;
};
}  // namespace eve::spawn
