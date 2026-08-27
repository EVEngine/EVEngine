#include "game_event/GameEvent.h"

#include "common/SquirrelBinding.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace eve::game_event {
namespace {

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else
                    out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}

bool parseU64(const eve::json::Value& value, uint64_t& out) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        out         = std::stoull(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parseI64(const eve::json::Value& value, int64_t& out) {
    if (!value.isString()) return false;
    try {
        const std::string text = value.asString();
        size_t            used = 0;
        out                    = std::stoll(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

std::string canonicalJson(const eve::json::Value& value) {
    if (value.isNull()) return "null";
    if (value.isBool()) return value.asBool() ? "true" : "false";
    if (value.isNumber()) return value.asString();
    if (value.isString()) return quote(value.asString());
    if (value.isArray()) {
        std::string out = "[";
        for (size_t i = 0; i < value.size(); ++i) {
            if (i) out += ',';
            out += canonicalJson(value.at(i));
        }
        return out + ']';
    }
    if (value.isObject()) {
        auto keys = value.keys();
        std::sort(keys.begin(), keys.end());
        std::string out = "{";
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) out += ',';
            out += quote(keys[i]) + ':' + canonicalJson(value.get(keys[i].c_str()));
        }
        return out + '}';
    }
    return {};
}

const char* correlationKindName(CorrelationId::Kind kind) {
    switch (kind) {
    case CorrelationId::Kind::None: return "none";
    case CorrelationId::Kind::Id: return "id";
    case CorrelationId::Kind::Legacy: return "legacy";
    }
    return "none";
}

const char* causationKindName(CausationRef::Kind kind) {
    switch (kind) {
    case CausationRef::Kind::None: return "none";
    case CausationRef::Kind::Event: return "event";
    case CausationRef::Kind::Command: return "command";
    case CausationRef::Kind::Legacy: return "legacy";
    }
    return "none";
}

CorrelationId correlationFromSnapshot(const std::string& kind, const std::string& text) {
    if (kind == "none") return {};
    if (kind == "id") {
        if (const auto parsed = EventId::parse(text)) return CorrelationId::fromEventId(*parsed);
        return {};
    }
    return CorrelationId::fromLegacy(text);
}

CausationRef causationFromSnapshot(const std::string& kind, const std::string& text) {
    if (kind == "none") return {};
    if (kind == "event") {
        if (const auto parsed = EventId::parse(text)) return CausationRef::fromEventId(*parsed);
        return {};
    }
    if (kind == "command") {
        if (const auto parsed = CommandId::parse(text)) return CausationRef::fromCommandId(*parsed);
        return {};
    }
    return CausationRef::fromLegacy(text);
}

eve::LogicalId eventStreamSchema() {
    const auto schema = eve::LogicalId::parse("game_event:stream");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& eventStreamMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto registration = result.add(
            eventStreamSchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
            [](const eve::Value& payload) -> eve::Result<eve::Value> {
                const auto* object = payload.getIf<eve::Value::Object>();
                if (!object)
                    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "event stream payload must be an object"));
                return eve::Result<eve::Value>::success(payload);
            });
        if (!registration.ok()) std::terminate();
        return result;
    }();
    return chain;
}

template <class T>
eve::Result<T> snapshotFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

EventConsumer::EventConsumer(GameEventLog* stream, EventSequence sequence)
    : stream_(stream), nextSequence_(sequence.value() == 0 ? EventSequence(1) : sequence) {}

int EventConsumer::read(int maxCount) {
    batch_.clear();
    if (!stream_ || maxCount <= 0) return 0;
    if (nextSequence_.value() < stream_->oldestSequence().value())
        nextSequence_ = stream_->oldestSequence();
    for (const auto& event : stream_->events_) {
        if (event.sequence.value() < nextSequence_.value()) continue;
        if (static_cast<int>(batch_.size()) >= maxCount) break;
        batch_.push_back(&event);
        nextSequence_ = EventSequence(event.sequence.value() + 1);
    }
    return static_cast<int>(batch_.size());
}

int EventConsumer::batchCount() const { return static_cast<int>(batch_.size()); }

const GameEvent* EventConsumer::batchAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < batch_.size() ? batch_[static_cast<size_t>(index)] : nullptr;
}

EventSequence EventConsumer::position() const { return nextSequence_; }

void EventConsumer::seek(EventSequence sequence) {
    nextSequence_ = sequence.value() == 0 ? EventSequence(1) : sequence;
    batch_.clear();
}

void EventConsumer::seek(uint64_t sequence) { seek(EventSequence(sequence)); }

GameEventLog::GameEventLog(eve::UuidEntropySource entropy, eve::UuidClock clock)
    : eventIdGenerator_(std::in_place, std::move(entropy), std::move(clock)) {}

eve::Result<EventSequence> GameEventLog::append(GameEvent envelope) {
    const auto failure = [](eve::DiagnosticCode code, std::string message) {
        return eve::Result<EventSequence>::failure(eve::Diagnostic::error(code, std::move(message)));
    };
    if (envelope.type.empty()) return failure(eve::DiagnosticCode::InvalidArgument, "event type must not be empty");
    if (!envelope.schemaId.isValid())
        return failure(eve::DiagnosticCode::InvalidArgument, "event schemaId must be a valid logical ID");
    if (envelope.schemaVersion.isZero())
        return failure(eve::DiagnosticCode::InvalidArgument, "event schemaVersion must be non-zero");
    auto payloadDocument = eve::json::Document::parse(envelope.payload);
    if (!payloadDocument.valid()) return failure(eve::DiagnosticCode::ParseError, "payload must be valid JSON");
    const auto next = nextSequence_.incremented();
    if (!next) return failure(eve::DiagnosticCode::Failed, "event sequence exhausted");
    if (!revision_.incremented()) return failure(eve::DiagnosticCode::Failed, "event stream revision exhausted");

    if (envelope.eventId.isNil()) {
        if (eventIdGenerator_) {
            const auto generated = eventIdGenerator_->generate();
            if (!generated) return failure(eve::DiagnosticCode::Failed, "event ID generation failed");
            envelope.eventId = EventId::fromUuid(*generated);
        } else {
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "eventId is required when no UUIDv7 generator is configured");
        }
    }
    if (!envelope.eventId.isNil()) {
        for (const auto& existing : events_) {
            if (existing.eventId == envelope.eventId)
                return failure(eve::DiagnosticCode::Conflict, "eventId already exists in this stream");
        }
    }

    envelope.sequence = nextSequence_;
    envelope.payload  = canonicalJson(payloadDocument.root());
    events_.push_back(std::move(envelope));
    nextSequence_ = *next;
    revision_ = *revision_.incremented();
    if (events_.back().tick > snapshotTick_) snapshotTick_ = events_.back().tick;
    return eve::Result<EventSequence>::success(events_.back().sequence);
}

const GameEvent* GameEventLog::find(EventSequence sequence) const {
    const auto it = std::lower_bound(events_.begin(), events_.end(), sequence.value(),
                                     [](const GameEvent& event, uint64_t value) {
                                         return event.sequence.value() < value;
                                     });
    return it != events_.end() && it->sequence.value() == sequence.value() ? &*it : nullptr;
}

const GameEvent* GameEventLog::find(uint64_t sequence) const { return find(EventSequence(sequence)); }

int GameEventLog::queryType(const std::string& type) {
    query_.clear();
    for (const auto& event : events_)
        if (event.type == type) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int GameEventLog::querySource(const std::string& source) {
    query_.clear();
    for (const auto& event : events_)
        if (event.source == source) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int GameEventLog::querySubject(const std::string& subject) {
    query_.clear();
    for (const auto& event : events_)
        if (event.subject == subject) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int GameEventLog::querySequence(EventSequence firstSequence) {
    query_.clear();
    for (const auto& event : events_)
        if (event.sequence.value() >= firstSequence.value()) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int GameEventLog::querySequence(uint64_t firstSequence) { return querySequence(EventSequence(firstSequence)); }

const GameEvent* GameEventLog::queryAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < query_.size() ? query_[static_cast<size_t>(index)] : nullptr;
}

int GameEventLog::size() const { return static_cast<int>(events_.size()); }

const GameEvent* GameEventLog::at(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

EventConsumer* GameEventLog::newConsumer(EventSequence firstSequence) {
    consumers_.push_back(std::unique_ptr<EventConsumer>(new EventConsumer(this, firstSequence)));
    return consumers_.back().get();
}

EventConsumer* GameEventLog::newConsumer(uint64_t firstSequence) { return newConsumer(EventSequence(firstSequence)); }

void GameEventLog::clearBefore(EventSequence firstSequence) {
    while (!events_.empty() && events_.front().sequence.value() < firstSequence.value()) events_.pop_front();
    query_.clear();
    for (auto& consumer : consumers_) consumer->batch_.clear();
}

void GameEventLog::clearBefore(uint64_t firstSequence) { clearBefore(EventSequence(firstSequence)); }

void GameEventLog::clear() {
    events_.clear();
    query_.clear();
    for (auto& consumer : consumers_) consumer->batch_.clear();
}

void GameEventLog::reset() {
    clear();
    nextSequence_ = EventSequence(1);
    revision_ = eve::Revision::zero();
    snapshotTick_ = eve::SimulationTick::zero();
    for (auto& consumer : consumers_) consumer->seek(1);
}

std::string GameEventLog::snapshotJson() const {
    std::ostringstream out;
    out << "{\"version\":2,\"revision\":" << quote(std::to_string(revision_.value()))
        << ",\"nextSequence\":" << quote(std::to_string(nextSequence_.value())) << ",\"events\":[";
    bool first = true;
    for (const auto& event : events_) {
        if (!first) out << ',';
        first = false;
        out << "{\"eventId\":" << quote(event.eventId.format())
            << ",\"sequence\":" << quote(std::to_string(event.sequence.value())) << ",\"type\":" << quote(event.type)
            << ",\"source\":" << quote(event.source) << ",\"subject\":" << quote(event.subject)
            << ",\"causationKind\":" << quote(causationKindName(event.causation.kind()))
            << ",\"causation\":" << quote(event.causation.format())
            << ",\"correlationKind\":" << quote(correlationKindName(event.correlation.kind()))
            << ",\"correlation\":" << quote(event.correlation.format())
            << ",\"schemaId\":" << quote(event.schemaId.format())
            << ",\"schemaVersion\":" << quote(std::to_string(event.schemaVersion.value()))
            << ",\"tick\":" << quote(std::to_string(event.tick.value())) << ",\"flags\":" << event.flags
            << ",\"payload\":" << event.payload << '}';
    }
    return out.str() + "]}";
}

eve::Result<void> GameEventLog::restore(std::string_view json) {
    std::string error;
    auto        document = eve::json::Document::parse(std::string(json), &error);
    const auto  root     = document.root();
    const auto invalid = [&](eve::DiagnosticCode code, std::string message) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message)));
    };
    const int snapshotVersion = root.getInt("version");
    if (!document.valid() || !root.isObject() || (snapshotVersion != 1 && snapshotVersion != 2)) {
        return invalid(eve::DiagnosticCode::ParseError,
                       error.empty() ? "invalid event stream snapshot" : error);
    }
    uint64_t restoredNext = 0;
    if (!parseU64(root.get("nextSequence"), restoredNext) || restoredNext == 0) {
        return invalid(eve::DiagnosticCode::ParseError, "invalid nextSequence");
    }
    uint64_t restoredRevision = restoredNext - 1;
    if (!root.get("revision").isNull() && !parseU64(root.get("revision"), restoredRevision))
        return invalid(eve::DiagnosticCode::ParseError, "invalid revision");
    const auto values = root.get("events");
    if (!values.isArray()) {
        return invalid(eve::DiagnosticCode::ParseError, "events must be an array");
    }
    std::deque<GameEvent> restored;
    uint64_t                  previous = 0;
    std::vector<std::string>  restoredEventIds;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto    value = values.at(i);
        GameEvent event;
        int64_t       restoredTick = 0;
        uint64_t restoredSequence = 0;
        if (!value.isObject() || !parseU64(value.get("sequence"), restoredSequence) || restoredSequence <= previous ||
            !parseI64(value.get("tick"), restoredTick) || !value.get("flags").isNumber() || !value.get("payload")) {
            return invalid(eve::DiagnosticCode::ParseError, "invalid event at index " + std::to_string(i));
        }
        event.sequence = EventSequence(restoredSequence);
        if (restoredTick < 0) {
            return invalid(eve::DiagnosticCode::ParseError, "invalid tick at index " + std::to_string(i));
        }
        event.tick = SimulationTick(static_cast<uint64_t>(restoredTick));
        event.type = value.getString("type");
        if (event.type.empty()) {
            return invalid(eve::DiagnosticCode::ParseError, "event type must not be empty");
        }
        const std::string eventIdText = value.getString("eventId");
        if (eventIdText.empty()) {
            event.eventId = EventId::nil();
        } else {
            const auto parsedId = EventId::parse(eventIdText);
            if (!parsedId) return invalid(eve::DiagnosticCode::ParseError, "invalid eventId at index " + std::to_string(i));
            event.eventId = *parsedId;
            if (!event.eventId.isNil()) {
                const auto formattedId = event.eventId.format();
                if (std::find(restoredEventIds.begin(), restoredEventIds.end(), formattedId) != restoredEventIds.end())
                    return invalid(eve::DiagnosticCode::Conflict,
                                   "duplicate non-nil eventId at index " + std::to_string(i));
                restoredEventIds.push_back(formattedId);
            }
        }
        event.source       = value.getString("source");
        event.subject      = value.getString("subject");
        const std::string causationText = value.getString("causation");
        const std::string correlationText = value.getString("correlation");
        const std::string causationKind = snapshotVersion == 1 ?
            (causationText.empty() ? "none" : "legacy") : value.getString("causationKind");
        const std::string correlationKind = snapshotVersion == 1 ?
            (correlationText.empty() ? "none" : "legacy") : value.getString("correlationKind");
        if (causationKind != "none" && causationKind != "event" && causationKind != "command" &&
            causationKind != "legacy")
            return invalid(eve::DiagnosticCode::ParseError, "invalid causation kind at index " + std::to_string(i));
        if (correlationKind != "none" && correlationKind != "id" && correlationKind != "legacy")
            return invalid(eve::DiagnosticCode::ParseError, "invalid correlation kind at index " + std::to_string(i));
        if ((causationKind == "none" && !causationText.empty()) ||
            (causationKind != "none" && causationText.empty()) ||
            (correlationKind == "none" && !correlationText.empty()) ||
            (correlationKind != "none" && correlationText.empty()))
            return invalid(eve::DiagnosticCode::ParseError, "causal reference value/kind mismatch at index " + std::to_string(i));
        event.causation   = causationFromSnapshot(causationKind, causationText);
        event.correlation = correlationFromSnapshot(correlationKind, correlationText);
        if ((causationKind == "event" || causationKind == "command") && event.causation.kind() == CausationRef::Kind::None)
            return invalid(eve::DiagnosticCode::ParseError, "invalid causation ID at index " + std::to_string(i));
        if (correlationKind == "id" && event.correlation.kind() == CorrelationId::Kind::None)
            return invalid(eve::DiagnosticCode::ParseError, "invalid correlation ID at index " + std::to_string(i));
        const std::string schemaIdText = value.getString("schemaId");
        if (schemaIdText.empty()) {
            event.schemaId = eve::LogicalId::fromParts("game_event", event.type).value_or(eve::LogicalId());
        } else {
            const auto parsedSchema = eve::LogicalId::parse(schemaIdText);
            if (!parsedSchema) return invalid(eve::DiagnosticCode::ParseError, "invalid schemaId at index " + std::to_string(i));
            event.schemaId = *parsedSchema;
        }
        uint64_t restoredSchemaVersion = snapshotVersion == 1 ? 1 : 0;
        if (snapshotVersion == 2 &&
            (!parseU64(value.get("schemaVersion"), restoredSchemaVersion) || restoredSchemaVersion == 0)) {
            return invalid(eve::DiagnosticCode::ParseError, "invalid schemaVersion at index " + std::to_string(i));
        }
        event.schemaVersion = SchemaVersion(restoredSchemaVersion);
        const double flags = value.getDouble("flags", -1.0);
        if (flags < 0 || flags > std::numeric_limits<uint32_t>::max() || std::floor(flags) != flags) {
            return invalid(eve::DiagnosticCode::ParseError, "invalid flags at index " + std::to_string(i));
        }
        event.flags   = static_cast<uint32_t>(flags);
        event.payload = canonicalJson(value.get("payload"));
        previous      = event.sequence.value();
        restored.push_back(std::move(event));
    }
    if (!restored.empty() && restored.back().sequence.value() >= restoredNext) {
        return invalid(eve::DiagnosticCode::ParseError, "nextSequence must exceed retained events");
    }
    events_       = std::move(restored);
    nextSequence_ = EventSequence(restoredNext);
    revision_     = eve::Revision(restoredRevision);
    snapshotTick_ = eve::SimulationTick::zero();
    for (const auto& event : events_)
        if (event.tick > snapshotTick_) snapshotTick_ = event.tick;
    query_.clear();
    consumers_.clear();
    return eve::Result<void>::success();
}

eve::Result<eve::SnapshotEnvelope> GameEventLog::snapshot(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("game_event.stream", eventStreamSchema(), eve::SchemaVersion(1), instanceId_,
                                     revision_, snapshotTick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> GameEventLog::restoreSnapshot(
    const eve::SnapshotEnvelope& source, const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "game_event.stream" || source.schema != eventStreamSchema())
        return snapshotFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to game_event::GameEventLog");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "snapshot instanceId does not match game_event::GameEventLog");
    auto migrated = eventStreamMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    auto metadata = eve::validateSnapshotPayloadMetadata(migrated.value().payload,
                                                         migrated.value().revision,
                                                         migrated.value().tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto payload = migrated.value().payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());

    GameEventLog candidate(instanceId_);
    candidate.eventIdGenerator_ = eventIdGenerator_;
    auto restored = candidate.restore(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    if (candidate.snapshotTick_ != migrated.value().tick)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "event stream payload tick disagrees with snapshot envelope");
    candidate.instanceId_   = migrated.value().instanceId;
    candidate.revision_     = migrated.value().revision;
    candidate.snapshotTick_ = migrated.value().tick;
    // The UUID generator is a runtime capability, not persisted state. Keep
    // it across the atomic payload replacement and invalidate old cursors.
    eventIdGenerator_ = std::move(candidate.eventIdGenerator_);
    instanceId_       = candidate.instanceId_;
    revision_         = candidate.revision_;
    snapshotTick_     = candidate.snapshotTick_;
    nextSequence_     = candidate.nextSequence_;
    events_           = std::move(candidate.events_);
    query_.clear();
    consumers_.clear();
    return eve::Result<void>::success();
}

eve::Result<std::string> GameEventLog::snapshotEnvelopeJson(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> GameEventLog::restoreSnapshotJson(
    std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

EventSequence GameEventLog::oldestSequence() const { return events_.empty() ? nextSequence_ : events_.front().sequence; }

GameEventLog* GameEventModule::newLog() {
    auto* module = GameEventModule::create();
    module->streams_.push_back(std::make_unique<GameEventLog>());
    return module->streams_.back().get();
}

ModuleRegister GameEventModule_register(
    "GameEvent", (ModuleManager::creator_t)(GameEventModule::create), GameEventModule::expose);
GameEventModule* GameEventModule::create() {
    auto* existing = ModuleManager::find(name);
    if (existing) return static_cast<GameEventModule*>(existing);
    auto* module = new GameEventModule();
    ModuleManager::insert(name, module);
    return module;
}
const char* GameEventModule::name = "GameEvent";

void GameEventModule::expose(ssq::Table& table) {
    auto envelope = table.addClass<GameEvent>(
        "GameEventRecord", std::function<GameEvent*()>([]() -> GameEvent* { return nullptr; }), false);
    envelope.addFunc("getEventId", [](GameEvent* e) { return e ? e->eventId.format() : std::string{}; });
    envelope.addFunc("getSequence",
                     [](GameEvent* e) {
                         return e ? static_cast<int64_t>(e->sequence.value()) : int64_t{0};
                     });
    envelope.addFunc("getType", [](GameEvent* e) { return e ? e->type : std::string{}; });
    envelope.addFunc("getSource", [](GameEvent* e) { return e ? e->source : std::string{}; });
    envelope.addFunc("getSubject", [](GameEvent* e) { return e ? e->subject : std::string{}; });
    envelope.addFunc("getCausation", [](GameEvent* e) {
        return e ? e->causation.format() : std::string{};
    });
    envelope.addFunc("getCausationKind", [](GameEvent* e) {
        return e ? std::string(causationKindName(e->causation.kind())) : std::string{};
    });
    envelope.addFunc("getCorrelation", [](GameEvent* e) {
        return e ? e->correlation.format() : std::string{};
    });
    envelope.addFunc("getCorrelationKind", [](GameEvent* e) {
        return e ? std::string(correlationKindName(e->correlation.kind())) : std::string{};
    });
    envelope.addFunc("getSchemaId", [](GameEvent* e) {
        return e ? e->schemaId.format() : std::string{};
    });
    envelope.addFunc("getSchemaVersion", [](GameEvent* e) {
        return e ? static_cast<int64_t>(e->schemaVersion.value()) : int64_t{0};
    });
    envelope.addFunc("getTick", [](GameEvent* e) {
        return e ? static_cast<int64_t>(e->tick.value()) : int64_t{0};
    });
    envelope.addFunc("getFlags", [](GameEvent* e) { return e ? static_cast<int64_t>(e->flags) : int64_t{0}; });
    envelope.addFunc("getPayload", [](GameEvent* e) { return e ? e->payload : std::string{}; });

    auto consumer = table.addClass<EventConsumer>(
        "EventConsumer", std::function<EventConsumer*()>([]() -> EventConsumer* { return nullptr; }), false);
    consumer.addFunc("read", &EventConsumer::read);
    consumer.addFunc("batchCount", &EventConsumer::batchCount);
    consumer.addFunc("batchAt", [](EventConsumer* c, int index) -> GameEvent* {
        return c ? const_cast<GameEvent*>(c->batchAt(index)) : nullptr;
    });
    consumer.addFunc("position", [](EventConsumer* c) {
        return c ? static_cast<int64_t>(c->position().value()) : int64_t{0};
    });
    consumer.addFunc("seek", [](EventConsumer* c, int64_t sequence) {
        if (c) c->seek(sequence > 0 ? uint64_t(sequence) : 1);
    });

    auto stream =
        table.addClass<GameEventLog>("GameEventLog", std::function<GameEventLog*()>([]() -> GameEventLog* { return nullptr; }), false);
    const HSQUIRRELVM vm = table.getHandle();
    stream.addFunc("append", [vm](GameEventLog* s, const std::string& eventId,
                                   const std::string& type, const std::string& source,
                                   const std::string& subject, const std::string& causation,
                                   const std::string& correlation, int64_t tick, int64_t flags,
                                   const std::string& payload) {
        if (!s) {
            return eve::script::projectResult(vm, eve::Result<EventSequence>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "event stream must not be null", "stream")),
                [](EventSequence) { return eve::Value::null(); });
        }
        if (tick < 0 || flags < 0 || static_cast<std::uint64_t>(flags) > std::numeric_limits<std::uint32_t>::max()) {
            return eve::script::projectResult(vm, eve::Result<EventSequence>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "event tick and flags must be non-negative and in range", "envelope")),
                [](EventSequence) { return eve::Value::null(); });
        }
        GameEvent envelope;
        if (!eventId.empty()) {
            const auto parsed = EventId::parse(eventId);
            if (!parsed) {
                return eve::script::projectResult(vm, eve::Result<EventSequence>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                           "eventId must be a UUID", "eventId")),
                    [](EventSequence) { return eve::Value::null(); });
            }
            envelope.eventId = *parsed;
        }
        envelope.type = type;
        envelope.source = source;
        envelope.subject = subject;
        if (!causation.empty()) {
            const auto parsed = EventId::parse(causation);
            envelope.causation = parsed ? CausationRef::fromEventId(*parsed)
                                        : CausationRef::fromLegacy(causation);
        }
        if (!correlation.empty()) {
            const auto parsed = EventId::parse(correlation);
            envelope.correlation = parsed ? CorrelationId::fromEventId(*parsed)
                                          : CorrelationId::fromLegacy(correlation);
        }
        const auto schema = eve::LogicalId::fromParts("game_event", type);
        if (schema) envelope.schemaId = *schema;
        envelope.schemaVersion = SchemaVersion(1);
        envelope.tick = SimulationTick(static_cast<std::uint64_t>(tick));
        envelope.flags = static_cast<std::uint32_t>(flags);
        envelope.payload = payload;
        return eve::script::projectResult(vm, s->append(std::move(envelope)),
                                          [](EventSequence sequence) {
                                              return eve::Value::integer(static_cast<std::int64_t>(sequence.value()));
                                          });
    });
    stream.addFunc("find", [](GameEventLog* s, int64_t sequence) -> GameEvent* {
        return s && sequence > 0 ? const_cast<GameEvent*>(s->find(uint64_t(sequence))) : nullptr;
    });
    stream.addFunc("queryType", &GameEventLog::queryType);
    stream.addFunc("querySource", &GameEventLog::querySource);
    stream.addFunc("querySubject", &GameEventLog::querySubject);
    stream.addFunc("querySequence", [](GameEventLog* s, int64_t sequence) {
        return s ? s->querySequence(sequence > 0 ? uint64_t(sequence) : 1) : 0;
    });
    stream.addFunc("queryAt", [](GameEventLog* s, int index) -> GameEvent* {
        return s ? const_cast<GameEvent*>(s->queryAt(index)) : nullptr;
    });
    stream.addFunc("size", &GameEventLog::size);
    stream.addFunc("at", [](GameEventLog* s, int index) -> GameEvent* {
        return s ? const_cast<GameEvent*>(s->at(index)) : nullptr;
    });
    stream.addFunc("newConsumer", [](GameEventLog* s, int64_t sequence) {
        return s ? s->newConsumer(sequence > 0 ? uint64_t(sequence) : 1) : nullptr;
    });
    stream.addFunc("clearBefore", [](GameEventLog* s, int64_t sequence) {
        if (s) s->clearBefore(sequence > 0 ? uint64_t(sequence) : 1);
    });
    stream.addFunc("clear", &GameEventLog::clear);
    stream.addFunc("reset", &GameEventLog::reset);
    stream.addFunc("snapshotJson", &GameEventLog::snapshotJson);
    stream.addFunc("restore", [vm = table.getHandle()](GameEventLog* stream,
                                                        const std::string& json) {
        if (!stream)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "game event log must not be null")));
        return eve::script::projectResult(vm, stream->restore(json));
    });

    auto cls = table.addClass(name, GameEventModule::create, false);
    expose(cls);
}

void GameEventModule::expose(ssq::Class& cls) {
    cls.addFunc("getName", &GameEventModule::getName);
    cls.addFunc("newLog", [](GameEventModule*) { return GameEventModule::newLog(); });
}

}  // namespace eve::game_event
