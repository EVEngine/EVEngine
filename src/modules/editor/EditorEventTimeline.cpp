#include "editor/EditorEventTimeline.h"

#include <algorithm>
#include <utility>

namespace eve::editor {
namespace {

std::size_t nodeCount(const EditorValue& value, std::size_t limit) {
    std::size_t result = 1;
    if (const auto* array = value.getIf<EditorValue::Array>()) {
        for (const auto& child : *array) {
            if (result > limit) break;
            result += nodeCount(child, limit - std::min(result, limit));
        }
    } else if (const auto* object = value.getIf<EditorValue::Object>()) {
        for (const auto& [key, child] : *object) {
            (void)key;
            if (result > limit) break;
            result += nodeCount(child, limit - std::min(result, limit));
        }
    }
    return result;
}

bool includes(const std::vector<std::string>& values, const std::string& value) {
    return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
}

int severityRank(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Info: return 0;
        case DiagnosticSeverity::Warning: return 1;
        case DiagnosticSeverity::Error: return 2;
        case DiagnosticSeverity::Fatal: return 3;
    }
    return 0;
}

}  // namespace

EditorResult<void> EditorEventTimeline::setCapacity(std::size_t capacity) {
    if (capacity == 0 || capacity > 1000000)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.timeline.capacity"),
                                          "Timeline capacity must be between 1 and 1,000,000");
    capacity_ = capacity;
    while (events_.size() > capacity_) { events_.pop_front(); ++dropped_; }
    ++generation_;
    return eve::editing::applied<void>();
}

EditorResult<std::uint64_t> EditorEventTimeline::append(EditorTimelineEvent event,
                                                        std::size_t maximumPayloadNodes) {
    if (event.domain.empty() || event.type.empty() || maximumPayloadNodes == 0 ||
        nodeCount(event.payload, maximumPayloadNodes) > maximumPayloadNodes)
        return eve::editing::failed<std::uint64_t>(EditorStatus::Rejected,
            RuleId("editor.timeline.invalid-event"), "Timeline event identity or payload budget is invalid");
    if (event.sequence == 0) event.sequence = nextSequence_++;
    else {
        if (event.sequence < nextSequence_)
            return eve::editing::failed<std::uint64_t>(EditorStatus::Conflict,
                RuleId("editor.timeline.sequence"), "Timeline event sequence is stale or duplicated");
        nextSequence_ = event.sequence + 1;
    }
    const std::uint64_t sequence = event.sequence;
    if (events_.size() == capacity_) { events_.pop_front(); ++dropped_; }
    events_.push_back(std::move(event)); ++generation_;
    return eve::editing::applied<std::uint64_t>(sequence);
}

EditorResult<EditorTimelinePage> EditorEventTimeline::query(
    const EditorTimelineQuery& filter, std::size_t offset, std::size_t limit,
    std::optional<std::uint64_t> generation) const {
    if (limit == 0 || limit > 10000)
        return eve::editing::failed<EditorTimelinePage>(EditorStatus::Rejected,
            RuleId("editor.timeline.page-size"), "Timeline page size must be between 1 and 10,000");
    if (generation && *generation != generation_)
        return eve::editing::failed<EditorTimelinePage>(EditorStatus::Conflict,
            RuleId("editor.timeline.stale-generation"), "Timeline changed while paging");
    std::vector<EditorTimelineEvent> matched;
    for (const auto& event : events_) {
        if (!includes(filter.domains, event.domain) || !includes(filter.types, event.type) ||
            (!filter.source.empty() && event.source != filter.source) ||
            (!filter.correlation.empty() && event.correlation != filter.correlation) ||
            (filter.minimumSeverity && severityRank(event.severity) < severityRank(*filter.minimumSeverity)) ||
            event.tick < filter.firstTick || (filter.lastTick > 0 && event.tick > filter.lastTick)) continue;
        matched.push_back(event);
    }
    EditorTimelinePage result; result.generation = generation_; result.droppedEvents = dropped_;
    const std::size_t begin = std::min(offset, matched.size());
    const std::size_t end = std::min(begin + limit, matched.size());
    result.values.assign(matched.begin() + static_cast<std::ptrdiff_t>(begin),
                         matched.begin() + static_cast<std::ptrdiff_t>(end));
    result.nextOffset = end; result.hasMore = end < matched.size();
    return eve::editing::applied<EditorTimelinePage>(std::move(result));
}

void EditorEventTimeline::clear() {
    events_.clear(); ++generation_;
}

}  // namespace eve::editor
