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

/** @brief Domain classification of a saved participant's Protobuf message type. */
enum class GameSaveMessageTypeMatch { Unsupported, Current, Legacy };

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
    /** @brief Full Protobuf message name owned by this domain, without the type URL prefix. */
    virtual std::string_view messageType() const noexcept = 0;
    /** @brief Classify the current message type or a domain-owned read-only legacy type. */
    virtual GameSaveMessageTypeMatch matchMessageType(std::string_view type) const noexcept {
        return type == messageType() ? GameSaveMessageTypeMatch::Current : GameSaveMessageTypeMatch::Unsupported;
    }
    /** @brief Capture an owning, versioned Protobuf message as wire bytes. */
    [[nodiscard]] virtual Result<std::string> captureBinary() const = 0;
    /** @brief Validate wire bytes and prepare a no-fail commit without changing live state. */
    [[nodiscard]] virtual Result<std::unique_ptr<IPreparedGameRestore>> prepareBinary(std::string_view wire) = 0;
    /** @brief Compatibility-only legacy JSON prepare; new saves use prepareBinary. */
    [[nodiscard]] virtual Result<std::unique_ptr<IPreparedGameRestore>> prepare(const Value& state) = 0;
};

/** @brief Owning typed wire record used by content-version migrations. */
struct GameSaveParticipantMessage {
    std::string typeUrl;
    std::string wireBytes;
};

/** @brief One owning, keyed collection of participant Protobuf messages. */
using GameSaveParticipantMessages = std::map<std::string, GameSaveParticipantMessage>;

/**
 * @brief Seal and restore all registered domain owners as one game-save transaction.
 * @remarks Schema `game:save-session` version 2 uses Protobuf; version-1 JSON is read-only compatibility.
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
     * @brief Register a compatibility-only migration for a legacy version-1 JSON save.
     * @param fromVersion Nonempty prior content identifier.
     * @param migrate Pure transformation of the owning participants object; no live mutation.
     * @return Conflict for a duplicate route or InvalidArgument for an empty route.
     * @reentrancy The callback is invoked without locks before any participant preparation.
     * @cost One full participants payload copy and migration per load.
     */
    [[nodiscard]] Result<void> addContentMigration(std::string                                fromVersion,
                                                   std::function<Result<Value>(const Value&)> migrate);

    /**
     * @brief Register a pure old-to-current migration over typed participant wire records.
     * @param fromVersion Exact old content identifier.
     * @param migrate Domain-aware transformation performed before any participant prepare.
     * @return Applied, or a structured duplicate/invalid-route failure.
     * @reentrancy Invoked without locks or live mutation.
     * @cost One owning copy of all participant wire records per migrated load.
     */
    [[nodiscard]] Result<void> addBinaryContentMigration(
        std::string                                                                            fromVersion,
        std::function<Result<GameSaveParticipantMessages>(const GameSaveParticipantMessages&)> migrate);

    /**
     * @brief Capture and integrity-seal every participant in deterministic key order.
     * @param hashProvider Required schema-owned digest implementation.
     * @return Protobuf JSON mapping of the version-2 envelope; any participant failure aborts capture.
     * @cost O(total serialized game state), once per save.
     */
    [[nodiscard]] Result<std::string> snapshotJson(const SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Capture a versioned Protobuf envelope with typed domain messages.
     * @param hashProvider Required digest implementation over the exact payload wire bytes.
     * @return Owning binary snapshot or a structured failure.
     * @cost Linear in all participants; use only at save points.
     */
    [[nodiscard]] Result<std::string> snapshotBinary(const SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Verify, migrate and prepare all participants before any publication.
     * @param json Borrowed version-2 Protobuf JSON or legacy version-1 JSON.
     * @param hashProvider The same digest contract used at capture.
     * @return Applied only after every no-fail participant commit has completed.
     * @remarks Missing or extra participants, bad hashes and failed preparations leave live state unchanged.
     * @cost O(total serialized game state), once per load.
     */
    [[nodiscard]] Result<void> restoreJson(std::string_view json, const SnapshotHashProvider& hashProvider);

    /**
     * @brief Verify and prepare every Protobuf participant before any publication.
     * @param wire Borrowed Protobuf envelope bytes needed only during this call.
     * @param hashProvider Digest implementation used when captured.
     * @return Applied after every no-fail participant commit, or a structured failure.
     * @cost Linear in all participants; use only at load points.
     */
    [[nodiscard]] Result<void> restoreBinary(std::string_view wire, const SnapshotHashProvider& hashProvider);

private:
    std::string                                                       contentVersion_;
    std::map<std::string, IGameSaveParticipant*>                      participants_;
    std::map<std::string, std::function<Result<Value>(const Value&)>> migrations_;
    std::map<std::string, std::function<Result<GameSaveParticipantMessages>(const GameSaveParticipantMessages&)>>
        binaryMigrations_;
};

}  // namespace eve
