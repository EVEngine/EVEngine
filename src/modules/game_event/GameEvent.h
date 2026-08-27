#pragma once

#include "common/Identity.h"
#include "common/Module.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/EventSequence.h"
#include "common/Revision.h"
#include "common/SchemaVersion.h"
#include "common/Time.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace eve::game_event {

using EventSequence  = eve::EventSequence;
using SimulationTick = eve::SimulationTick;
using SchemaId       = eve::LogicalId;
using SchemaVersion  = eve::SchemaVersion;

/**
 * @brief Event and command identities are aliases of the common UUID type.
 *
 * The aliases preserve the game_event namespace as a source-compatible
 * facade while keeping parsing, formatting, hashing and domain separation in
 * one implementation (`common/Identity.h`).
 */
using EventId   = eve::EventId;
using CommandId = eve::CommandId;

/**
 * @brief Strong business-workflow correlation identity.
 *
 * `Legacy` is retained only when restoring old persisted envelopes whose
 * causal metadata was not UUID-typed. New producers must use typed IDs.
 */
class CorrelationId {
public:
    enum class Kind : uint8_t { None, Id, Legacy };

    /** @brief Constructs an absent correlation reference. */
    CorrelationId() = default;

    /** @brief Creates a canonical correlation ID from an EventId. */
    [[nodiscard]] static CorrelationId fromEventId(EventId value) {
        CorrelationId result;
        result.kind_ = value.isNil() ? Kind::None : Kind::Id;
        result.id_   = value;
        return result;
    }

    /** @brief Creates a compatibility-only arbitrary string correlation. */
    [[nodiscard]] static CorrelationId fromLegacy(std::string value) {
        CorrelationId result;
        result.kind_ = value.empty() ? Kind::None : Kind::Legacy;
        result.legacy_ = std::move(value);
        return result;
    }

    /** @brief Returns the tagged correlation kind. */
    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    /** @brief Returns whether this reference uses the canonical ID form. */
    [[nodiscard]] bool isCanonical() const noexcept { return kind_ != Kind::Legacy; }

    /** @brief Returns the stable ID or compatibility text projection. */
    [[nodiscard]] std::string format() const {
        return kind_ == Kind::Id ? id_.format() : legacy_;
    }

    friend bool operator==(const CorrelationId&, const CorrelationId&) noexcept = default;

private:
    Kind              kind_ = Kind::None;
    EventId           id_;
    std::string       legacy_;
};

/**
 * @brief Tagged direct causation reference to an event or command.
 *
 * A legacy string can be carried by compatibility callers. New producers
 * should use `fromEventId` or `fromCommandId`; append preserves legacy data.
 */
class CausationRef {
public:
    enum class Kind : uint8_t { None, Event, Command, Legacy };

    /** @brief Constructs an absent causation reference. */
    CausationRef() = default;

    /** @brief Creates a causation reference to an event. */
    [[nodiscard]] static CausationRef fromEventId(EventId value) {
        CausationRef result;
        if (!value.isNil()) {
            result.kind_ = Kind::Event;
            result.value_ = std::move(value);
        }
        return result;
    }

    /** @brief Creates a causation reference to a command. */
    [[nodiscard]] static CausationRef fromCommandId(CommandId value) {
        CausationRef result;
        if (!value.isNil()) {
            result.kind_ = Kind::Command;
            result.value_ = std::move(value);
        }
        return result;
    }

    /** @brief Creates a compatibility-only arbitrary string causation. */
    [[nodiscard]] static CausationRef fromLegacy(std::string value) {
        CausationRef result;
        result.kind_ = value.empty() ? Kind::None : Kind::Legacy;
        result.value_ = std::move(value);
        return result;
    }

    /** @brief Returns the tagged causation kind. */
    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    /** @brief Returns whether this reference uses the canonical ID form. */
    [[nodiscard]] bool isCanonical() const noexcept { return kind_ != Kind::Legacy; }

    /** @brief Returns the stable UUID or compatibility text projection. */
    [[nodiscard]] std::string format() const {
        if (kind_ == Kind::Event) return std::get<EventId>(value_).format();
        if (kind_ == Kind::Command) return std::get<CommandId>(value_).format();
        if (kind_ == Kind::Legacy) return std::get<std::string>(value_);
        return {};
    }

    friend bool operator==(const CausationRef&, const CausationRef&) noexcept = default;

private:
    Kind kind_ = Kind::None;
    std::variant<std::monostate, EventId, CommandId, std::string> value_;
};

/** @brief Writes a correlation representation tag for diagnostics. */
inline std::ostream& operator<<(std::ostream& stream, CorrelationId::Kind kind) {
    switch (kind) {
    case CorrelationId::Kind::None: return stream << "none";
    case CorrelationId::Kind::Id: return stream << "id";
    case CorrelationId::Kind::Legacy: return stream << "legacy";
    }
    return stream << "unknown";
}

/** @brief Writes a causation representation tag for diagnostics. */
inline std::ostream& operator<<(std::ostream& stream, CausationRef::Kind kind) {
    switch (kind) {
    case CausationRef::Kind::None: return stream << "none";
    case CausationRef::Kind::Event: return stream << "event";
    case CausationRef::Kind::Command: return stream << "command";
    case CausationRef::Kind::Legacy: return stream << "legacy";
    }
    return stream << "unknown";
}

/**
 * @brief Envelope metadata shared by event domains.
 *
 * The envelope carries identity, ordering and causal metadata only. The
 * `payload` member is an opaque, canonical event-stream representation at the
 * serialization boundary; a domain must keep its payload strongly typed and
 * use `GameEventLog::appendTyped` to encode it. The stream never interprets that
 * representation as a universal domain object.
 */
struct GameEvent {
    /** @brief Stable event identity; nil is allowed only for legacy transient events. */
    EventId eventId = EventId::nil();
    /** @brief Sequence assigned by the owning stream; it is never global identity. */
    EventSequence sequence = EventSequence::zero();
    /** @brief Compatibility event name and diagnostic topic. */
    std::string type;
    /** @brief Stable source reference, owned by this envelope. */
    std::string source;
    /** @brief Stable subject reference, owned by this envelope. */
    std::string subject;
    /** @brief Direct causation reference, normally an event or command ID. */
    CausationRef causation;
    /** @brief Business workflow correlation reference. */
    CorrelationId correlation;
    /** @brief Logical schema identifier for the typed payload codec. */
    SchemaId schemaId;
    /** @brief Version of the payload schema, not a stream or runtime generation. */
    SchemaVersion schemaVersion = SchemaVersion::zero();
    /** @brief Deterministic simulation time at which the event was emitted. */
    SimulationTick tick = SimulationTick::zero();
    /** @brief Domain-defined bit flags; their meanings belong to the schema. */
    uint32_t flags = 0;
    /** @brief Canonical serialized payload at the stream persistence boundary. */
    std::string payload = "null";
};

/**
 * @brief Strongly typed event value used before serialization into a stream.
 * @tparam Payload Domain-owned payload type; it is never replaced by a JSON map.
 */
template <class Payload>
struct TypedEventEnvelope {
    GameEvent metadata;
    Payload       payload;
};

class GameEventLog;

/** @brief Independent sequence cursor used to consume an GameEventLog in batches. */
class EventConsumer {
public:
    /** @brief Reads at most maxCount events and advances past the returned batch. */
    int read(int maxCount);
    /** @brief Returns the number of events in the most recent batch. */
    int batchCount() const;
    /**
     * @brief Returns an event from the most recent batch, or nullptr.
     * @ownership Borrowed from the source GameEventLog; never delete it.
     * @lifetime Valid until the consumer reads again or the source log mutates.
     * @thread GameEventLog owner thread only.
     * @reentrancy No callbacks are invoked.
     */
    const GameEvent* batchAt(int index) const;
    /** @brief Returns the next stream-local sequence this consumer will inspect. */
    [[nodiscard]] EventSequence position() const;
    /** @brief Moves the cursor to a sequence without reading events. */
    void seek(EventSequence sequence);
    /** @brief Compatibility facade accepting the old untyped sequence value. */
    void seek(uint64_t sequence);

private:
    friend class GameEventLog;
    EventConsumer(GameEventLog* stream, EventSequence sequence);

    GameEventLog*                           stream_       = nullptr;
    EventSequence                     nextSequence_ = EventSequence(1);
    std::vector<const GameEvent*> batch_;
};

/**
 * @brief Deterministic in-memory stream of topic-neutral event envelopes.
 *
 * Borrowed event views remain valid until the pointed event is removed by a
 * clear, clear-before, reset, or restore operation. Query results remain
 * available until the next query on the same stream.
 */
class GameEventLog {
public:
    /** @brief Creates a stream without an implicit entropy source. */
    GameEventLog() = default;

    /** @brief Creates a stream with a persistent snapshot identity. */
    explicit GameEventLog(eve::PersistentId instanceId) : instanceId_(instanceId) {}

    /**
     * @brief Creates a stream with an injected UUIDv7 event identity source.
     * @param entropy Entropy callback used only when an appended envelope has a nil ID.
     * @param clock Optional clock for UUIDv7 timestamps.
     */
    explicit GameEventLog(eve::UuidEntropySource entropy, eve::UuidClock clock = {});

    /**
     * @brief Appends a typed-metadata envelope and assigns stream-local order.
     * @param envelope Envelope metadata and canonical serialized payload.
     * @return The assigned stream-local sequence, or structured validation failure.
     * @remarks A non-nil EventId and valid schema id/version are required. If
     *          EventId is nil and this stream has an injected generator, it is
     *          generated before the event becomes observable.
     */
    [[nodiscard]] eve::Result<EventSequence> append(GameEvent envelope);

    /**
     * @brief Encodes a strongly typed domain payload and appends its envelope.
     * @tparam Payload Domain payload type owned by the caller/domain module.
     * @param envelope Envelope metadata; its payload field is replaced by the codec output.
     * @param payload Strongly typed domain payload; the stream does not inspect its fields.
     * @param encode Caller-owned canonical wire encoder returning valid JSON for stream snapshots.
     * @return The assigned stream-local sequence, or the encoder/append failure.
     */
    template <class Payload>
    [[nodiscard]] eve::Result<EventSequence> appendTyped(
        GameEvent envelope, const Payload& payload,
        const std::function<eve::Result<std::string>(const Payload&)>& encode) {
        if (!encode) {
            return eve::Result<EventSequence>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "event payload encoder must not be empty"));
        }
        auto encoded = encode(payload);
        if (!encoded) return eve::Result<EventSequence>::failure(encoded.status());
        envelope.payload = std::move(encoded).takeValue();
        return append(std::move(envelope));
    }

    /**
     * @brief Returns an event by exact sequence, or nullptr.
     * @ownership Borrowed from this stream; callers must not delete or retain it.
     * @nullable Null when the sequence is not retained.
     * @lifetime Until stream clear/reset/restore removes the event; a query does
     *            not invalidate this exact-sequence view.
     * @thread GameEventLog-owner thread unless externally synchronized.
     * @reentrancy No callbacks are made.
     */
    [[nodiscard]] const GameEvent* find(EventSequence sequence) const;
    /**
     * @brief Compatibility facade accepting the old untyped sequence value.
     * @ownership Borrowed view with the same lifetime as the typed `find` overload.
     * @nullable Null when the sequence is not retained.
     * @lifetime Until stream clear/reset/restore removes the event.
     * @thread GameEventLog-owner thread; no callback or re-entry is performed.
     */
    [[nodiscard]] const GameEvent* find(uint64_t sequence) const;
    /** @brief Queries all retained events of a type in sequence order. */
    int queryType(const std::string& type);
    /** @brief Queries all retained events from a source in sequence order. */
    int querySource(const std::string& source);
    /** @brief Queries all retained events concerning a subject in sequence order. */
    int querySubject(const std::string& subject);
    /** @brief Queries retained events whose sequence is at least firstSequence. */
    int querySequence(EventSequence firstSequence);
    /** @brief Compatibility facade accepting the old untyped sequence value. */
    int querySequence(uint64_t firstSequence);
    /** @brief Returns one result from the latest query, or nullptr. */
    const GameEvent* queryAt(int index) const;
    /** @brief Returns the number of retained events. */
    int size() const;
    /** @brief Returns a retained event by stream order, or nullptr. */
    const GameEvent* at(int index) const;
    /** @brief Creates an independently positioned consumer cursor. */
    [[nodiscard]] EventConsumer* newConsumer(EventSequence firstSequence = EventSequence(1));
    /** @brief Compatibility facade accepting the old untyped sequence value. */
    [[nodiscard]] EventConsumer* newConsumer(uint64_t firstSequence);
    /** @brief Removes retained events with sequence lower than firstSequence. */
    void clearBefore(EventSequence firstSequence);
    /** @brief Compatibility facade accepting the old untyped sequence value. */
    void clearBefore(uint64_t firstSequence);
    /** @brief Removes retained events while preserving the next sequence number. */
    void clear();
    /** @brief Removes all state and restarts sequence allocation at one. */
    void reset();
    /** @brief Exports stream state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /**
     * @brief Transactionally restores stream state from a snapshot.
     * @param json Snapshot produced by snapshotJson().
     * @return Success, or structured parse/validation failure; failure leaves
     *         all retained events, consumers and sequence allocation unchanged.
     */
    [[nodiscard]] eve::Result<void> restore(std::string_view json);

    /** @brief Captures the stream payload in the common snapshot envelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Restores a verified or migrated stream envelope atomically.
     * @param snapshot Source envelope with schema `game_event:stream`.
     * @param hashProvider Explicit content-digest provider.
     * @return Success, or a failure leaving retained events and cursors intact.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);
    /** @brief Serializes the common event-stream snapshot envelope. */
    [[nodiscard]] eve::Result<std::string> snapshotEnvelopeJson(
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Parses and transactionally restores a common event-stream envelope. */
    [[nodiscard]] eve::Result<void> restoreSnapshotJson(
        std::string_view json, const eve::SnapshotHashProvider& hashProvider);

private:
    friend class EventConsumer;
    [[nodiscard]] EventSequence oldestSequence() const;

    std::optional<eve::UuidV7Generator> eventIdGenerator_;
    eve::PersistentId                         instanceId_;
    eve::Revision                             revision_ = eve::Revision::zero();
    eve::SimulationTick                       snapshotTick_ = eve::SimulationTick::zero();
    EventSequence                       nextSequence_ = EventSequence(1);
    std::deque<GameEvent>                   events_;
    std::vector<const GameEvent*>           query_;
    std::vector<std::unique_ptr<EventConsumer>> consumers_;
};

/** @brief Script module factory for generic GameEventLog objects. */
class GameEventModule : public Module {
public:
    Module_REG(GameEventModule);
    GameEventModule() = default;
    ~GameEventModule() override = default;

    /**
     * @brief Allocates a module-owned event stream.
     * @ownership Borrowed from the manager-owned GameEvent module; the caller
     *            must not delete it and must retain no ownership claim.
     * @nullable Null only when module allocation fails.
     * @lifetime Until module shutdown or the module's owning stream registry releases it.
     * @thread Module construction/owner thread; no callback is invoked.
     * @reentrancy Not reentrant during module registration.
     */
    [[nodiscard("event stream ownership must be retained or explicitly handled")]] static GameEventLog* newLog();

private:
    std::vector<std::unique_ptr<GameEventLog>> streams_;
};

}  // namespace eve::game_event
