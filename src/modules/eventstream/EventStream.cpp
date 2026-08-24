#include "eventstream/EventStream.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::eventstream {
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

}  // namespace

EventConsumer::EventConsumer(Stream* stream, uint64_t sequence)
    : stream_(stream), nextSequence_(std::max<uint64_t>(1, sequence)) {}

int EventConsumer::read(int maxCount) {
    batch_.clear();
    if (!stream_ || maxCount <= 0) return 0;
    nextSequence_ = std::max(nextSequence_, stream_->oldestSequence());
    for (const auto& event : stream_->events_) {
        if (event.sequence < nextSequence_) continue;
        if (static_cast<int>(batch_.size()) >= maxCount) break;
        batch_.push_back(&event);
        nextSequence_ = event.sequence + 1;
    }
    return static_cast<int>(batch_.size());
}

int EventConsumer::batchCount() const { return static_cast<int>(batch_.size()); }

const EventEnvelope* EventConsumer::batchAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < batch_.size() ? batch_[static_cast<size_t>(index)] : nullptr;
}

uint64_t EventConsumer::position() const { return nextSequence_; }

void EventConsumer::seek(uint64_t sequence) {
    nextSequence_ = std::max<uint64_t>(1, sequence);
    batch_.clear();
}

uint64_t Stream::emit(const std::string& type, const std::string& source, const std::string& subject,
                      const std::string& causation, const std::string& correlation, int64_t tick, uint32_t flags,
                      const std::string& jsonPayload) {
    lastError_.clear();
    if (type.empty()) {
        lastError_ = "event type must not be empty";
        return 0;
    }
    auto payloadDocument = eve::json::Document::parse(jsonPayload);
    if (!payloadDocument.valid()) {
        lastError_ = "payload must be valid JSON";
        return 0;
    }
    if (nextSequence_ == std::numeric_limits<uint64_t>::max()) {
        lastError_ = "event sequence exhausted";
        return 0;
    }
    const uint64_t sequence = nextSequence_++;
    events_.push_back(
        {sequence, type, source, subject, causation, correlation, tick, flags, canonicalJson(payloadDocument.root())});
    return sequence;
}

const EventEnvelope* Stream::find(uint64_t sequence) const {
    const auto it = std::lower_bound(events_.begin(), events_.end(), sequence,
                                     [](const EventEnvelope& event, uint64_t value) { return event.sequence < value; });
    return it != events_.end() && it->sequence == sequence ? &*it : nullptr;
}

int Stream::queryType(const std::string& type) {
    query_.clear();
    for (const auto& event : events_)
        if (event.type == type) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int Stream::querySource(const std::string& source) {
    query_.clear();
    for (const auto& event : events_)
        if (event.source == source) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int Stream::querySubject(const std::string& subject) {
    query_.clear();
    for (const auto& event : events_)
        if (event.subject == subject) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

int Stream::querySequence(uint64_t firstSequence) {
    query_.clear();
    for (const auto& event : events_)
        if (event.sequence >= firstSequence) query_.push_back(&event);
    return static_cast<int>(query_.size());
}

const EventEnvelope* Stream::queryAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < query_.size() ? query_[static_cast<size_t>(index)] : nullptr;
}

int Stream::size() const { return static_cast<int>(events_.size()); }

const EventEnvelope* Stream::at(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

EventConsumer* Stream::newConsumer(uint64_t firstSequence) {
    consumers_.push_back(std::unique_ptr<EventConsumer>(new EventConsumer(this, firstSequence)));
    return consumers_.back().get();
}

void Stream::clearBefore(uint64_t firstSequence) {
    while (!events_.empty() && events_.front().sequence < firstSequence) events_.pop_front();
    query_.clear();
    for (auto& consumer : consumers_) consumer->batch_.clear();
}

void Stream::clear() {
    events_.clear();
    query_.clear();
    for (auto& consumer : consumers_) consumer->batch_.clear();
}

void Stream::reset() {
    clear();
    nextSequence_ = 1;
    for (auto& consumer : consumers_) consumer->seek(1);
    lastError_.clear();
}

std::string Stream::snapshotJson() const {
    std::ostringstream out;
    out << "{\"version\":1,\"nextSequence\":" << quote(std::to_string(nextSequence_)) << ",\"events\":[";
    bool first = true;
    for (const auto& event : events_) {
        if (!first) out << ',';
        first = false;
        out << "{\"sequence\":" << quote(std::to_string(event.sequence)) << ",\"type\":" << quote(event.type)
            << ",\"source\":" << quote(event.source) << ",\"subject\":" << quote(event.subject)
            << ",\"causation\":" << quote(event.causation) << ",\"correlation\":" << quote(event.correlation)
            << ",\"tick\":" << quote(std::to_string(event.tick)) << ",\"flags\":" << event.flags
            << ",\"payload\":" << event.payload << '}';
    }
    return out.str() + "]}";
}

bool Stream::restoreJson(const std::string& json) {
    lastError_.clear();
    std::string error;
    auto        document = eve::json::Document::parse(json, &error);
    const auto  root     = document.root();
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1) {
        lastError_ = error.empty() ? "invalid event stream snapshot" : error;
        return false;
    }
    uint64_t restoredNext = 0;
    if (!parseU64(root.get("nextSequence"), restoredNext) || restoredNext == 0) {
        lastError_ = "invalid nextSequence";
        return false;
    }
    const auto values = root.get("events");
    if (!values.isArray()) {
        lastError_ = "events must be an array";
        return false;
    }
    std::deque<EventEnvelope> restored;
    uint64_t                  previous = 0;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto    value = values.at(i);
        EventEnvelope event;
        int64_t       restoredTick = 0;
        if (!value.isObject() || !parseU64(value.get("sequence"), event.sequence) || event.sequence <= previous ||
            !parseI64(value.get("tick"), restoredTick) || !value.get("flags").isNumber() || !value.get("payload")) {
            lastError_ = "invalid event at index " + std::to_string(i);
            return false;
        }
        event.type = value.getString("type");
        if (event.type.empty()) {
            lastError_ = "event type must not be empty";
            return false;
        }
        event.source       = value.getString("source");
        event.subject      = value.getString("subject");
        event.causation    = value.getString("causation");
        event.correlation  = value.getString("correlation");
        event.tick         = restoredTick;
        const double flags = value.getDouble("flags", -1.0);
        if (flags < 0 || flags > std::numeric_limits<uint32_t>::max() || std::floor(flags) != flags) {
            lastError_ = "invalid flags at index " + std::to_string(i);
            return false;
        }
        event.flags   = static_cast<uint32_t>(flags);
        event.payload = canonicalJson(value.get("payload"));
        previous      = event.sequence;
        restored.push_back(std::move(event));
    }
    if (!restored.empty() && restored.back().sequence >= restoredNext) {
        lastError_ = "nextSequence must exceed retained events";
        return false;
    }
    events_       = std::move(restored);
    nextSequence_ = restoredNext;
    query_.clear();
    consumers_.clear();
    return true;
}

const std::string& Stream::lastError() const { return lastError_; }

uint64_t Stream::oldestSequence() const { return events_.empty() ? nextSequence_ : events_.front().sequence; }

Stream* EventStream::newStream() {
    auto* module = EventStream::create();
    module->streams_.push_back(std::make_unique<Stream>());
    return module->streams_.back().get();
}

Module_IMPL(EventStream, new EventStream());

void EventStream::expose(ssq::Table& table) {
    auto envelope = table.addClass<EventEnvelope>(
        "EventEnvelope", std::function<EventEnvelope*()>([]() -> EventEnvelope* { return nullptr; }), false);
    envelope.addFunc("getSequence",
                     [](EventEnvelope* e) { return e ? static_cast<int64_t>(e->sequence) : int64_t{0}; });
    envelope.addFunc("getType", [](EventEnvelope* e) { return e ? e->type : std::string{}; });
    envelope.addFunc("getSource", [](EventEnvelope* e) { return e ? e->source : std::string{}; });
    envelope.addFunc("getSubject", [](EventEnvelope* e) { return e ? e->subject : std::string{}; });
    envelope.addFunc("getCausation", [](EventEnvelope* e) { return e ? e->causation : std::string{}; });
    envelope.addFunc("getCorrelation", [](EventEnvelope* e) { return e ? e->correlation : std::string{}; });
    envelope.addFunc("getTick", [](EventEnvelope* e) { return e ? e->tick : int64_t{0}; });
    envelope.addFunc("getFlags", [](EventEnvelope* e) { return e ? static_cast<int64_t>(e->flags) : int64_t{0}; });
    envelope.addFunc("getPayload", [](EventEnvelope* e) { return e ? e->payload : std::string{}; });

    auto consumer = table.addClass<EventConsumer>(
        "EventConsumer", std::function<EventConsumer*()>([]() -> EventConsumer* { return nullptr; }), false);
    consumer.addFunc("read", &EventConsumer::read);
    consumer.addFunc("batchCount", &EventConsumer::batchCount);
    consumer.addFunc("batchAt", [](EventConsumer* c, int index) -> EventEnvelope* {
        return c ? const_cast<EventEnvelope*>(c->batchAt(index)) : nullptr;
    });
    consumer.addFunc("position", [](EventConsumer* c) { return c ? static_cast<int64_t>(c->position()) : int64_t{0}; });
    consumer.addFunc("seek", [](EventConsumer* c, int64_t sequence) {
        if (c) c->seek(sequence > 0 ? uint64_t(sequence) : 1);
    });

    auto stream =
        table.addClass<Stream>("Stream", std::function<Stream*()>([]() -> Stream* { return nullptr; }), false);
    stream.addFunc("emit", [](Stream* s, const std::string& type, const std::string& source, const std::string& subject,
                              const std::string& causation, const std::string& correlation, int64_t tick, int64_t flags,
                              const std::string& payload) {
        return s && flags >= 0 && static_cast<uint64_t>(flags) <= std::numeric_limits<uint32_t>::max()
                   ? static_cast<int64_t>(s->emit(type, source, subject, causation, correlation, tick,
                                                  static_cast<uint32_t>(flags), payload))
                   : int64_t{0};
    });
    stream.addFunc("find", [](Stream* s, int64_t sequence) -> EventEnvelope* {
        return s && sequence > 0 ? const_cast<EventEnvelope*>(s->find(uint64_t(sequence))) : nullptr;
    });
    stream.addFunc("queryType", &Stream::queryType);
    stream.addFunc("querySource", &Stream::querySource);
    stream.addFunc("querySubject", &Stream::querySubject);
    stream.addFunc("querySequence", [](Stream* s, int64_t sequence) {
        return s ? s->querySequence(sequence > 0 ? uint64_t(sequence) : 1) : 0;
    });
    stream.addFunc("queryAt", [](Stream* s, int index) -> EventEnvelope* {
        return s ? const_cast<EventEnvelope*>(s->queryAt(index)) : nullptr;
    });
    stream.addFunc("size", &Stream::size);
    stream.addFunc("at", [](Stream* s, int index) -> EventEnvelope* {
        return s ? const_cast<EventEnvelope*>(s->at(index)) : nullptr;
    });
    stream.addFunc("newConsumer", [](Stream* s, int64_t sequence) {
        return s ? s->newConsumer(sequence > 0 ? uint64_t(sequence) : 1) : nullptr;
    });
    stream.addFunc("clearBefore", [](Stream* s, int64_t sequence) {
        if (s) s->clearBefore(sequence > 0 ? uint64_t(sequence) : 1);
    });
    stream.addFunc("clear", &Stream::clear);
    stream.addFunc("reset", &Stream::reset);
    stream.addFunc("snapshotJson", &Stream::snapshotJson);
    stream.addFunc("restoreJson", &Stream::restoreJson);
    stream.addFunc("lastError", &Stream::lastError);

    auto cls = table.addClass(name, EventStream::create, false);
    expose(cls);
}

void EventStream::expose(ssq::Class& cls) {
    cls.addFunc("getName", &EventStream::getName);
    cls.addFunc("newStream", [](EventStream*) { return EventStream::newStream(); });
}

}  // namespace eve::eventstream
