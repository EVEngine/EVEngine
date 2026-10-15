#pragma once
#include "common/Export.h"


/** @file TacticsPersistence.h @brief Versioned battle snapshots with transactional restore. */

#include "common/Snapshot.h"
#include "tactics/TacticsReplay.h"
#include "tactics/TacticsTypes.h"

#include <memory>
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
    /** @brief Fully validated battle candidate awaiting a no-fail publication. */
    class EVENGINE_API_DOMAINS PreparedRestore {
    public:
        ~PreparedRestore();
        PreparedRestore(PreparedRestore&&) noexcept;
        PreparedRestore& operator=(PreparedRestore&&) noexcept;
        PreparedRestore(const PreparedRestore&)            = delete;
        PreparedRestore& operator=(const PreparedRestore&) = delete;

    private:
        friend class TacticsPersistence;
        struct Impl;
        explicit PreparedRestore(std::unique_ptr<Impl> state);
        std::unique_ptr<Impl> state_;
    };

    /** @brief Capture a version-one, integrity-sealed battle snapshot. */
    [[nodiscard]] static Result<SnapshotEnvelope> snapshot(Battle& battle,
                                                           const SnapshotHashProvider& hashProvider);
    /**
     * @brief Capture a version-8 typed Protobuf battle snapshot in caller-owned bytes.
     * @param battle Battle observed on its owning simulation thread.
     * @param hashProvider Required digest provider for the exact payload wire bytes.
     * @return Serialized envelope or a structured stale-handle, hash, or codec failure.
     * @cost Linear in the full battle state per capture.
     * @thread Owning simulation thread; no concurrent mutation.
     * @reentrancy Does not invoke scripts or callbacks; the injected hash provider is invoked once.
     */
    [[nodiscard]] static Result<std::string> snapshotBinary(Battle& battle, const SnapshotHashProvider& hashProvider);
    /**
     * @brief Validate a version-8 Protobuf envelope and resolve a battle candidate without publishing it.
     * @param battle Target battle on its owning simulation thread; keep it stable until commit.
     * @param wire Caller-owned envelope bytes, consumed during this call only.
     * @param hashProvider Required digest provider for the exact payload wire bytes.
     * @return Prepared candidate or a structured parse, integrity, identity, or invariant failure.
     * @cost Linear in the full battle state per restore.
     * @thread Owning simulation thread; no concurrent mutation.
     * @reentrancy Does not invoke scripts or callbacks; the injected hash provider is invoked once.
     */
    [[nodiscard]] static Result<PreparedRestore> prepareRestoreBinary(Battle& battle, std::string_view wire,
                                                                      const SnapshotHashProvider& hashProvider);
    /**
     * @brief Verify and resolve a battle snapshot without changing live state.
     * @thread Owning simulation thread; battle and registries must remain stable until commit.
     * @reentrancy No callbacks or scripts are invoked.
     * @cost Comparable to a full battle restore; once per load, not per frame.
     */
    [[nodiscard]] static Result<PreparedRestore> prepareRestore(Battle& battle, const SnapshotEnvelope& snapshot,
                                                                const SnapshotHashProvider& hashProvider);
    /** @brief Publish a previously prepared battle state without further fallible work. */
    static void commitPrepared(PreparedRestore prepared) noexcept;
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
     * @brief Serialize accepted commands after @p fromRevision as a versioned Protobuf JSON document.
     * @return Canonical Protobuf JSON or a structured codec failure.
     * @cost Linear in the selected command history per export.
     * @thread Owning simulation thread; no concurrent mutation.
     */
    [[nodiscard]] static Result<std::string> commandLogJson(Battle& battle, Revision fromRevision);

    /**
     * @brief Parse a versioned Protobuf JSON command log, retaining legacy JSON logs for read-only migration.
     * @param json Caller-owned document text used only during this call.
     * @param snapshotRevision Revision at which replay must begin.
     * @return Validated commands anchored to the target revision, or a structured refusal.
     * @cost Linear in the full command log per replay.
     */
    [[nodiscard]] static Result<Battle::Commands> parseCommandLogJson(std::string_view json, Revision snapshotRevision);

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
