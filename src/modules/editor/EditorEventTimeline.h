#pragma once

#include "editor/EditorProtocol.h"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Immutable normalized runtime event copied into the editor timeline. */
struct EditorTimelineEvent {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::string domain;
    std::string type;
    std::string source;
    std::string correlation;
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    EditorValue payload;
};

/** @brief Deterministic event filter used by headless and visual presenters. */
struct EditorTimelineQuery {
    std::vector<std::string> domains;
    std::vector<std::string> types;
    std::string source;
    std::string correlation;
    std::optional<DiagnosticSeverity> minimumSeverity;
    std::uint64_t firstTick = 0;
    std::uint64_t lastTick = 0;
};

/** @brief Generation-qualified timeline query page. */
struct EditorTimelinePage {
    std::vector<EditorTimelineEvent> values;
    std::size_t nextOffset = 0;
    bool hasMore = false;
    std::uint64_t generation = 0;
    std::uint64_t droppedEvents = 0;
};

/** @brief Bounded copied event timeline with stale-page detection and correlation filtering. */
class EditorEventTimeline {
public:
    /** @brief Set retained event capacity; shrinking drops oldest events. */
    EditorResult<void> setCapacity(std::size_t capacity);
    /** @brief Copy one event, assigning a monotonic sequence when it is zero. */
    EditorResult<std::uint64_t> append(EditorTimelineEvent event,
                                       std::size_t maximumPayloadNodes = 10000);
    /** @brief Query a deterministic page; stale generations return Conflict. */
    EditorResult<EditorTimelinePage> query(const EditorTimelineQuery& filter,
                                           std::size_t offset, std::size_t limit,
                                           std::optional<std::uint64_t> generation = std::nullopt) const;
    /** @brief Clear retained events while preserving the next sequence number. */
    void clear();
    std::uint64_t generation() const { return generation_; }
    std::uint64_t droppedEvents() const { return dropped_; }
private:
    std::deque<EditorTimelineEvent> events_;
    std::size_t capacity_ = 10000;
    std::uint64_t nextSequence_ = 1;
    std::uint64_t generation_ = 0;
    std::uint64_t dropped_ = 0;
};

}  // namespace eve::editor
