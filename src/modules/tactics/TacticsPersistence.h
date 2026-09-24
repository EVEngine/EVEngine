#pragma once
#include "common/Export.h"


/** @file TacticsPersistence.h @brief Versioned battle snapshots with transactional restore. */

#include "common/Snapshot.h"
#include "tactics/TacticsReplay.h"
#include "tactics/TacticsTypes.h"

#include <string_view>

namespace eve::tactics {

/**
 * @brief Stateless codec for the authoritative state of one battle.
 *
 * Runtime ECS handles never cross the persistence boundary. Restore resolves
 * stable SubjectRef values against the target battle, constructs and validates
 * all candidate values first, and only then replaces mutable components.
 * Methods are simulation-thread-affine and invoke no unknown callbacks.
 */
class EVENGINE_API_DOMAINS TacticsPersistence {
public:
    /** @brief Capture a version-one, integrity-sealed battle snapshot. */
    [[nodiscard]] static Result<SnapshotEnvelope> snapshot(Battle& battle,
                                                           const SnapshotHashProvider& hashProvider);
    /** @brief Restore a verified compatible snapshot without partial mutation on failure. */
    [[nodiscard]] static Result<void> restore(Battle& battle, const SnapshotEnvelope& snapshot,
                                              const SnapshotHashProvider& hashProvider);

    /**
     * @brief Serialize the accepted command log after @p fromRevision in its persisted shape.
     *
     * @return The same `{nextSequence, values: [...]}` object a snapshot payload carries, which
     *         is what makes it replayable by @ref parseCommandLog. Callers that only want to
     *         display a log use the projected script shape instead; this one exists so replay
     *         does not need a second, second-guessed parser.
     */
    [[nodiscard]] static Result<Value> commandLogValue(Battle& battle, Revision fromRevision);

    /**
     * @brief Parse a command log written by @ref commandLogValue.
     * @param value Owning command-log object.
     * @param snapshotRevision Revision the log must end at, so a log replayed onto the wrong
     *        battle state is refused instead of silently re-applied.
     * @return The commands, or a structured parse/ordering failure.
     * @remarks Uses the current schema's command shape, exactly like restore: one parser owns
     *          the persisted command format.
     */
    [[nodiscard]] static Result<Battle::Commands> parseCommandLog(const Value& value, Revision snapshotRevision);
};

}  // namespace eve::tactics
