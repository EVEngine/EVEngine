#pragma once

/** @file GameSaveSession.h @brief Single-transaction composition of domain save participants. */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/Value.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace eve {

/** @brief Owning prepared state whose publication cannot fail or invoke callbacks. */
class EVENGINE_API_FOUNDATION_INLINE IPreparedGameRestore {
public:
    virtual ~IPreparedGameRestore() = default;
    /** @brief Publish the prepared domain state exactly once on its owner thread. */
    virtual void commit() noexcept = 0;
};

/**
 * @brief One authoritative domain participating in a game save.
 * @lifetime The participant and its live domain owner must outlive the GameSaveSession.
 * @thread All methods run on the owning simulation thread at a paused save/load boundary.
 * @reentrancy No scripts or unknown callbacks during capture or prepare.
 */
class EVENGINE_API_FOUNDATION_INLINE IGameSaveParticipant {
public:
    virtual ~IGameSaveParticipant() = default;
    /** @brief Stable unique participant key within the game-save schema. */
    virtual std::string_view saveKey() const noexcept = 0;
    /** @brief Capture an independent owning snapshot of this domain. */
    [[nodiscard]] virtual Result<Value> capture() const = 0;
    /** @brief Validate and prepare a no-fail commit without changing live state. */
    [[nodiscard]] virtual Result<std::unique_ptr<IPreparedGameRestore>> prepare(const Value& state) = 0;
};

/**
 * @brief Seal and restore all registered domain owners as one game-save transaction.
 * @remarks Schema `game:save-session` version 1 rejects unknown fields and unknown participants.
 * Content migrations are explicit pure functions over an owning participants payload.
 */
class EVENGINE_API_FOUNDATION GameSaveSession {
public:
    /** @brief Configure the exact current content contract; empty identifiers are rejected by operations. */
    explicit GameSaveSession(std::string contentVersion);

    /**
     * @brief Borrow one domain participant for this session's lifetime.
     * @return Conflict for a duplicate key, InvalidArgument for an empty key.
     * @thread Owning simulation thread only; do not change registration during capture or restore.
     */
    [[nodiscard]] Result<void> add(IGameSaveParticipant& participant);

    /**
     * @brief Register a direct old-to-current content migration before loading.
     * @param fromVersion Nonempty prior content identifier.
     * @param migrate Pure transformation of the owning participants object; no live mutation.
     * @return Conflict for a duplicate route or InvalidArgument for an empty route.
     * @reentrancy The callback is invoked without locks before any participant preparation.
     * @cost One full participants payload copy and migration per load.
     */
    [[nodiscard]] Result<void> addContentMigration(std::string                                fromVersion,
                                                   std::function<Result<Value>(const Value&)> migrate);

    /**
     * @brief Capture and integrity-seal every participant in deterministic key order.
     * @param hashProvider Required schema-owned digest implementation.
     * @return Owning versioned JSON; any participant failure aborts capture.
     * @cost O(total serialized game state), once per save.
     */
    [[nodiscard]] Result<std::string> snapshotJson(const SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Verify, migrate and prepare all participants before any publication.
     * @param json Borrowed serialized save bytes needed only during this call.
     * @param hashProvider The same digest contract used at capture.
     * @return Applied only after every no-fail participant commit has completed.
     * @remarks Missing or extra participants, bad hashes and failed preparations leave live state unchanged.
     * @cost O(total serialized game state), once per load.
     */
    [[nodiscard]] Result<void> restoreJson(std::string_view json, const SnapshotHashProvider& hashProvider);

private:
    std::string                                                       contentVersion_;
    std::map<std::string, IGameSaveParticipant*>                      participants_;
    std::map<std::string, std::function<Result<Value>(const Value&)>> migrations_;
};

}  // namespace eve
