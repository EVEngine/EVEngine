#include "action/editor/ActionTimelineEditor.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace eve::editor {
namespace {

void sortTrack(action::ActionTrack& track) {
    std::stable_sort(track.notifies.begin(), track.notifies.end(), [](const auto& left, const auto& right) {
        return left.time == right.time ? left.id.format() < right.id.format() : left.time < right.time;
    });
    std::stable_sort(track.states.begin(), track.states.end(), [](const auto& left, const auto& right) {
        return left.start == right.start ? left.id.format() < right.id.format() : left.start < right.start;
    });
}

void sortAnimationSections(action::ActionTimeline& timeline) {
    std::stable_sort(
        timeline.animationSections.begin(), timeline.animationSections.end(), [](const auto& left, const auto& right) {
            return left.start == right.start ? left.id.format() < right.id.format() : left.start < right.start;
        });
}

}  // namespace

EditorResult<TimelineSelectionRange> ActionTimelineEditor::selectionRange() const {
    std::optional<TimelineSelectionRange> range;
    const auto include = [&](Duration start, Duration end) {
        if (!range) {
            range = TimelineSelectionRange{start, end};
            return;
        }
        range->start = std::min(range->start, start);
        range->end   = std::max(range->end, end);
    };
    for (const auto& section : target_.timeline().animationSections)
        if (selection_.contains(section.id.format())) include(section.start, section.end);
    for (const auto& track : target_.timeline().tracks) {
        for (const auto& notify : track.notifies)
            if (selection_.contains(notify.id.format())) include(notify.time, notify.time);
        for (const auto& state : track.states)
            if (selection_.contains(state.id.format())) include(state.start, state.end);
    }
    if (!range)
        return eve::editing::failed<TimelineSelectionRange>(EditorStatus::Rejected,
                                                             RuleId("editor.action.timeline.selection-empty"),
                                                             "No timeline items are selected");
    return eve::editing::applied<TimelineSelectionRange>(*range);
}

EditorResult<void> ActionTimelineEditor::moveSelection(Duration delta) {
    if (selection_.empty()) return rejected("editor.action.timeline.selection-empty", "No timeline items are selected");
    action::ActionTimeline candidate = target_.timeline();
    const auto             move      = [&](Duration value) { return value.tryAdd(delta); };
    for (auto& section : candidate.animationSections) {
        if (!selection_.contains(section.id.format())) continue;
        auto start = move(section.start);
        auto end   = move(section.end);
        if (!start || !end)
            return rejected("editor.action.timeline.time-overflow", "Selected animation-section time overflowed");
        section.start = start.value();
        section.end   = end.value();
    }
    for (auto& track : candidate.tracks) {
        const bool selectedNotify = std::any_of(track.notifies.begin(), track.notifies.end(),
                                                [&](const auto& item) { return selection_.contains(item.id.format()); });
        const bool selectedState = std::any_of(track.states.begin(), track.states.end(),
                                               [&](const auto& item) { return selection_.contains(item.id.format()); });
        if (track.locked && (selectedNotify || selectedState))
            return rejected("editor.action.timeline.track-locked", "Selection contains an item on a locked track");
        for (auto& notify : track.notifies) {
            if (!selection_.contains(notify.id.format())) continue;
            auto time = move(notify.time);
            if (!time) return rejected("editor.action.timeline.time-overflow", "Selected notify time overflowed");
            notify.time = time.value();
        }
        for (auto& state : track.states) {
            if (!selection_.contains(state.id.format())) continue;
            auto start = move(state.start);
            auto end   = move(state.end);
            if (!start || !end)
                return rejected("editor.action.timeline.time-overflow", "Selected notify-state time overflowed");
            state.start = start.value();
            state.end   = end.value();
        }
        sortTrack(track);
    }
    sortAnimationSections(candidate);
    return commit(std::move(candidate), "Move action timeline selection", "action.timeline.selection.move");
}

EditorResult<void> ActionTimelineEditor::alignSelectionStart() {
    auto range = selectionRange();
    if (!range.ok()) return EditorResult<void>::failure(range.status());
    action::ActionTimeline candidate = target_.timeline();
    const Duration anchor = range.value().start;
    for (auto& section : candidate.animationSections) {
        if (!selection_.contains(section.id.format())) continue;
        const Duration span = Duration::fromNanoseconds(section.end.nanoseconds() - section.start.nanoseconds());
        auto           end  = anchor.tryAdd(span);
        if (!end)
            return rejected("editor.action.timeline.time-overflow", "Aligned animation-section time overflowed");
        section.start = anchor;
        section.end   = end.value();
    }
    for (auto& track : candidate.tracks) {
        const bool touchesLocked = track.locked &&
            (std::any_of(track.notifies.begin(), track.notifies.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }) ||
             std::any_of(track.states.begin(), track.states.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }));
        if (touchesLocked)
            return rejected("editor.action.timeline.track-locked", "Selection contains an item on a locked track");
        for (auto& notify : track.notifies)
            if (selection_.contains(notify.id.format())) notify.time = anchor;
        for (auto& state : track.states) {
            if (!selection_.contains(state.id.format())) continue;
            const Duration span = Duration::fromNanoseconds(state.end.nanoseconds() - state.start.nanoseconds());
            auto           end  = anchor.tryAdd(span);
            if (!end)
                return rejected("editor.action.timeline.time-overflow", "Aligned notify-state time overflowed");
            state.start = anchor;
            state.end   = end.value();
        }
        sortTrack(track);
    }
    sortAnimationSections(candidate);
    return commit(std::move(candidate), "Align action timeline selection starts",
                  "action.timeline.selection.align-start");
}

EditorResult<void> ActionTimelineEditor::alignSelectionEnd() {
    auto range = selectionRange();
    if (!range.ok()) return EditorResult<void>::failure(range.status());
    action::ActionTimeline candidate = target_.timeline();
    const Duration anchor = range.value().end;
    for (auto& section : candidate.animationSections) {
        if (!selection_.contains(section.id.format())) continue;
        const auto span = section.end.nanoseconds() - section.start.nanoseconds();
        auto       start = anchor.tryAdd(Duration::fromNanoseconds(-span));
        if (!start)
            return rejected("editor.action.timeline.time-overflow", "Aligned animation-section time overflowed");
        section.start   = start.value();
        section.end     = anchor;
    }
    for (auto& track : candidate.tracks) {
        const bool touchesLocked = track.locked &&
            (std::any_of(track.notifies.begin(), track.notifies.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }) ||
             std::any_of(track.states.begin(), track.states.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }));
        if (touchesLocked)
            return rejected("editor.action.timeline.track-locked", "Selection contains an item on a locked track");
        for (auto& notify : track.notifies)
            if (selection_.contains(notify.id.format())) notify.time = anchor;
        for (auto& state : track.states) {
            if (!selection_.contains(state.id.format())) continue;
            const auto span = state.end.nanoseconds() - state.start.nanoseconds();
            auto       start = anchor.tryAdd(Duration::fromNanoseconds(-span));
            if (!start)
                return rejected("editor.action.timeline.time-overflow", "Aligned notify-state time overflowed");
            state.start     = start.value();
            state.end       = anchor;
        }
        sortTrack(track);
    }
    sortAnimationSections(candidate);
    return commit(std::move(candidate), "Align action timeline selection ends",
                  "action.timeline.selection.align-end");
}

EditorResult<void> ActionTimelineEditor::scaleSelection(Duration start, Duration end) {
    auto range = selectionRange();
    if (!range.ok()) return EditorResult<void>::failure(range.status());
    if (start < Duration::zero() || end <= start || end > target_.timeline().duration)
        return rejected("editor.action.timeline.selection-scale-range",
                        "Scaled selection range must be ordered and inside the timeline");
    const auto oldSpan = range.value().end.nanoseconds() - range.value().start.nanoseconds();
    if (oldSpan <= 0)
        return rejected("editor.action.timeline.selection-scale-point",
                        "A point-only selection cannot be proportionally scaled");
    const double rate = static_cast<double>(end.nanoseconds() - start.nanoseconds()) /
                        static_cast<double>(oldSpan);
    const auto map = [&](Duration value) -> Result<Duration> {
        const Duration offset = Duration::fromNanoseconds(value.nanoseconds() - range.value().start.nanoseconds());
        auto scaled = offset.scaled(rate);
        if (!scaled) return scaled;
        return start.tryAdd(scaled.value());
    };
    action::ActionTimeline candidate = target_.timeline();
    for (auto& section : candidate.animationSections) {
        if (!selection_.contains(section.id.format())) continue;
        auto mappedStart = map(section.start);
        auto mappedEnd   = map(section.end);
        auto blend       = section.blendIn.scaled(rate);
        if (!mappedStart || !mappedEnd || !blend)
            return rejected("editor.action.timeline.selection-scale-overflow", "Scaled selection time overflowed");
        section.start   = mappedStart.value();
        section.end     = mappedEnd.value();
        section.blendIn = blend.value();
    }
    for (auto& track : candidate.tracks) {
        const bool touchesLocked = track.locked &&
            (std::any_of(track.notifies.begin(), track.notifies.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }) ||
             std::any_of(track.states.begin(), track.states.end(), [&](const auto& item) {
                 return selection_.contains(item.id.format());
             }));
        if (touchesLocked)
            return rejected("editor.action.timeline.track-locked", "Selection contains an item on a locked track");
        for (auto& notify : track.notifies) {
            if (!selection_.contains(notify.id.format())) continue;
            auto mapped = map(notify.time);
            if (!mapped)
                return rejected("editor.action.timeline.selection-scale-overflow", "Scaled notify time overflowed");
            notify.time = mapped.value();
        }
        for (auto& state : track.states) {
            if (!selection_.contains(state.id.format())) continue;
            auto mappedStart = map(state.start);
            auto mappedEnd   = map(state.end);
            if (!mappedStart || !mappedEnd)
                return rejected("editor.action.timeline.selection-scale-overflow",
                                "Scaled notify-state time overflowed");
            state.start = mappedStart.value();
            state.end   = mappedEnd.value();
        }
        sortTrack(track);
    }
    sortAnimationSections(candidate);
    return commit(std::move(candidate), "Scale action timeline selection", "action.timeline.selection.scale");
}

}  // namespace eve::editor
