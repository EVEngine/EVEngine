#pragma once

#include "common/Snapshot.h"
#include "procgen/recursive/RecursiveInstance.h"

namespace eve::procgen::recursive {

/** @brief Restored owner plus the persistent identity and explicit simulation tick supplied by its host. */
struct RestoredRecursiveModel {
    RecursiveInstance instance;
    PersistentId      instanceId;
    SimulationTick    tick;
};

/**
 * @brief Capture and seal schema eve:recursive-model version 1 using the common snapshot format.
 * @param instance Borrowed on its owner thread until capture completes; no reference escapes.
 * @param instanceId Host-owned stable non-nil identity; runtime owner credentials are never serialized.
 * @param tick Explicit simulation observation tick, not wall time.
 * @param hasher Borrowed synchronous pure digest provider; invoked only after owning state capture.
 * @return Owning envelope, or capture/validation/hash/allocation failure. Use common serialization for JSON.
 * @cost O(E) capture/encoding plus provider cost over canonical payload bytes per save.
 * @thread Owner-thread capture; no locks. Hash callbacks may reenter after capture, not during it.
 * @throws Exceptions thrown by the injected provider propagate; no live state is changed.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<SnapshotEnvelope> saveRecursiveModel(const RecursiveInstance& instance,
                                                                                  PersistentId             instanceId,
                                                                                  SimulationTick           tick,
                                                                                  const SnapshotHashProvider& hasher);

/**
 * @brief Strictly validate version/fields/hash and rebuild an independent recursive owner.
 * @param snapshot Borrowed immutable envelope until return; unknown envelope/payload fields follow common strict
 * parsing.
 * @param budget Resource admission for restored edits and ancestor summaries.
 * @param hasher Borrowed synchronous pure digest provider used for integrity verification, under no lock.
 * @return Fully restored owner/identity/tick, or failure without mutating any existing instance.
 * @cost O(E) decoding plus hashing and sparse-ledger reconstruction per restore; no cached summaries are trusted.
 * @remarks Version 1 is the first schema; no predecessor migration exists. Reject unknown/newer versions.
 * To publish, move the returned owner into the host only after success; old candidates then fail owner checks.
 * @thread Caller synchronizes input; no shared mutable state. Provider exceptions propagate unchanged.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<RestoredRecursiveModel> restoreRecursiveModel(
    const SnapshotEnvelope& snapshot, EditBudget budget, const SnapshotHashProvider& hasher);
}  // namespace eve::procgen::recursive
