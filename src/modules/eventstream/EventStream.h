#pragma once

#include "common/Module.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace eve::eventstream {

/** @brief Immutable metadata and JSON payload for one generic event. */
struct EventEnvelope {
    uint64_t    sequence = 0;
    std::string type;
    std::string source;
    std::string subject;
    std::string causation;
    std::string correlation;
    int64_t     tick    = 0;
    uint32_t    flags   = 0;
    std::string payload = "null";
};

class Stream;

/** @brief Independent sequence cursor used to consume an Stream in batches. */
class EventConsumer {
public:
    /** @brief Reads at most maxCount events and advances past the returned batch. */
    int read(int maxCount);
    /** @brief Returns the number of events in the most recent batch. */
    int batchCount() const;
    /** @brief Returns an event from the most recent batch, or nullptr. */
    const EventEnvelope* batchAt(int index) const;
    /** @brief Returns the next sequence this consumer will inspect. */
    uint64_t position() const;
    /** @brief Moves the cursor to a sequence without reading events. */
    void seek(uint64_t sequence);

private:
    friend class Stream;
    EventConsumer(Stream* stream, uint64_t sequence);

    Stream*                           stream_       = nullptr;
    uint64_t                          nextSequence_ = 1;
    std::vector<const EventEnvelope*> batch_;
};

/**
 * @brief Deterministic in-memory stream of topic-neutral event envelopes.
 *
 * Event pointers remain valid until the pointed event is removed by clear(),
 * clearBefore(), reset(), or restoreJson(). Query results remain available
 * until the next query on the same stream.
 */
class Stream {
public:
    /** @brief Emits an event and returns its stable monotonically increasing sequence. */
    uint64_t emit(const std::string& type, const std::string& source, const std::string& subject,
                  const std::string& causation, const std::string& correlation, int64_t tick, uint32_t flags,
                  const std::string& jsonPayload = "null");
    /** @brief Returns an event by exact sequence, or nullptr. */
    const EventEnvelope* find(uint64_t sequence) const;
    /** @brief Queries all retained events of a type in sequence order. */
    int queryType(const std::string& type);
    /** @brief Queries all retained events from a source in sequence order. */
    int querySource(const std::string& source);
    /** @brief Queries all retained events concerning a subject in sequence order. */
    int querySubject(const std::string& subject);
    /** @brief Queries retained events whose sequence is at least firstSequence. */
    int querySequence(uint64_t firstSequence);
    /** @brief Returns one result from the latest query, or nullptr. */
    const EventEnvelope* queryAt(int index) const;
    /** @brief Returns the number of retained events. */
    int size() const;
    /** @brief Returns a retained event by stream order, or nullptr. */
    const EventEnvelope* at(int index) const;
    /** @brief Creates an independently positioned consumer cursor. */
    EventConsumer* newConsumer(uint64_t firstSequence = 1);
    /** @brief Removes retained events with sequence lower than firstSequence. */
    void clearBefore(uint64_t firstSequence);
    /** @brief Removes retained events while preserving the next sequence number. */
    void clear();
    /** @brief Removes all state and restarts sequence allocation at one. */
    void reset();
    /** @brief Exports stream state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /** @brief Replaces stream state from a snapshot produced by snapshotJson(). */
    bool restoreJson(const std::string& json);
    /** @brief Returns the last restore or emit validation error. */
    const std::string& lastError() const;

private:
    friend class EventConsumer;
    uint64_t oldestSequence() const;

    uint64_t                                    nextSequence_ = 1;
    std::deque<EventEnvelope>                   events_;
    std::vector<const EventEnvelope*>           query_;
    std::vector<std::unique_ptr<EventConsumer>> consumers_;
    std::string                                 lastError_;
};

/** @brief Script module factory for generic Stream objects. */
class EventStream : public Module {
public:
    Module_REG(EventStream);
    EventStream()           = default;
    ~EventStream() override = default;

    /** @brief Allocates a module-owned event stream. */
    static Stream* newStream();

private:
    std::vector<std::unique_ptr<Stream>> streams_;
};

}  // namespace eve::eventstream
