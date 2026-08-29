#pragma once

/** @file TacticsPersistence.h @brief Versioned battle snapshots with transactional restore. */

#include "common/Snapshot.h"
#include "tactics/TacticsTypes.h"

namespace eve::tactics {

/**
 * @brief Stateless codec for the authoritative state of one battle.
 *
 * Runtime ECS handles never cross the persistence boundary. Restore resolves
 * stable SubjectRef values against the target battle, constructs and validates
 * all candidate values first, and only then replaces mutable components.
 * Methods are simulation-thread-affine and invoke no unknown callbacks.
 */
class TacticsPersistence {
public:
    /** @brief Capture a version-one, integrity-sealed battle snapshot. */
    [[nodiscard]] static Result<SnapshotEnvelope> snapshot(Battle& battle,
                                                           const SnapshotHashProvider& hashProvider);
    /** @brief Restore a verified compatible snapshot without partial mutation on failure. */
    [[nodiscard]] static Result<void> restore(Battle& battle, const SnapshotEnvelope& snapshot,
                                              const SnapshotHashProvider& hashProvider);
};

}  // namespace eve::tactics
