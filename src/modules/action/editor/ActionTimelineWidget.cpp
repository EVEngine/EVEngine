#include "action/editor/ActionTimelineWidget.h"

#include "action/ActionAudioBlock.h"
#include "action/ActionAudioWaveform.h"
#include "action/ActionParameterCurve.h"
#include "common/Capability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::editor {
namespace {

constexpr float kHandleRadius = 6.0f;
constexpr float kNotifyWidth  = 8.0f;
constexpr std::size_t kMaximumWaveformBuckets = 512;
constexpr std::size_t kMaximumCurveSegments   = 128;

EditorResult<void> widgetError(std::string rule, std::string message, EditorStatus status = EditorStatus::Rejected) {
    return eve::editing::failed<void>(status, RuleId(std::move(rule)), std::move(message));
}

struct ItemView {
    LogicalId     trackId;
    LogicalId     itemId;
    LogicalId     type;
    Duration      start;
    Duration      end;
    Value::Object payload;
    bool          state            = false;
    bool          locked           = false;
    bool          animationSection = false;
};

std::optional<ItemView> findItem(const action::ActionTimeline& timeline, const LogicalId& itemId) {
    for (const auto& section : timeline.animationSections) {
        if (section.id != itemId) continue;
        return ItemView{
            *LogicalId::fromParts("action-track", "animation"),
            section.id,
            *LogicalId::fromParts("animation", "section"),
            section.start,
            section.end,
            {{"animationUri", Value(section.animationUri)}, {"blendInNs", Value(section.blendIn.nanoseconds())}},
            true,
            false,
            true};
    }
    for (const auto& track : timeline.tracks) {
        for (const auto& notify : track.notifies)
            if (notify.id == itemId)
                return ItemView{track.id,    notify.id,      notify.type, notify.time,
                                notify.time, notify.payload, false,       track.locked};
        for (const auto& state : track.states)
            if (state.id == itemId)
                return ItemView{track.id,  state.id,      state.type, state.start,
                                state.end, state.payload, true,       track.locked};
    }
    return std::nullopt;
}

Duration difference(Duration left, Duration right) {
    return Duration::fromNanoseconds(left.nanoseconds() - right.nanoseconds());
}

EditorResult<void> adapt(const EditorResult<std::size_t>& result, std::string rule, std::string message) {
    if (result.ok()) return eve::editing::applied<void>();
    if (!result.diagnostics().empty()) return EditorResult<void>::failure(result.status());
    return eve::editing::failed<void>(result.code(), RuleId(std::move(rule)), std::move(message));
}

}  // namespace

ActionTimelineWidget::ActionTimelineWidget(ActionTimelineEditor& editor, const action::ActionNotifyRegistry& registry)
    : editor_(editor), registry_(registry) {}

EditorResult<void> ActionTimelineWidget::setViewport(float width, float rowHeight, float labelWidth) {
    if (!std::isfinite(width) || !std::isfinite(rowHeight) || !std::isfinite(labelWidth) || width <= 0.0f ||
        rowHeight < 12.0f || labelWidth < 0.0f || labelWidth + 16.0f >= width)
        return widgetError("editor.action.timeline.widget.viewport", "Timeline widget dimensions are invalid");
    width_      = width;
    rowHeight_  = rowHeight;
    labelWidth_ = labelWidth;
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::setSnapInterval(Duration interval) {
    if (interval < Duration::zero())
        return widgetError("editor.action.timeline.widget.snap", "Timeline snap interval must be non-negative");
    snapInterval_ = interval;
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::setVisibleRange(Duration start, Duration end) {
    const Duration duration = editor_.target().timeline().duration;
    if (start < Duration::zero() || end <= start || end > duration)
        return widgetError("editor.action.timeline.widget.visible-range",
                           "Timeline visible range must be ordered and inside the asset duration");
    visibleStart_ = start;
    visibleEnd_   = end;
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::zoom(double factor, double normalizedAnchor) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(normalizedAnchor) || normalizedAnchor < 0.0 ||
        normalizedAnchor > 1.0)
        return widgetError("editor.action.timeline.widget.zoom", "Timeline zoom factor or anchor is invalid");
    const Duration duration    = editor_.target().timeline().duration;
    const Duration start       = visibleEnd_ > visibleStart_ ? visibleStart_ : Duration::zero();
    const Duration end         = visibleEnd_ > visibleStart_ ? visibleEnd_ : duration;
    const double   oldSpan     = static_cast<double>(end.nanoseconds() - start.nanoseconds());
    const double   minimumSpan = std::max(1.0, static_cast<double>(duration.nanoseconds()) / 10000.0);
    const auto     newSpan     = static_cast<std::int64_t>(
        std::llround(std::clamp(oldSpan / factor, minimumSpan, static_cast<double>(duration.nanoseconds()))));
    const auto anchor   = start.nanoseconds() + static_cast<std::int64_t>(std::llround(oldSpan * normalizedAnchor));
    auto       newStart = anchor - static_cast<std::int64_t>(std::llround(newSpan * normalizedAnchor));
    newStart            = std::clamp<std::int64_t>(newStart, 0, duration.nanoseconds() - newSpan);
    visibleStart_       = Duration::fromNanoseconds(newStart);
    visibleEnd_         = Duration::fromNanoseconds(newStart + newSpan);
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::pan(Duration delta) {
    const Duration duration = editor_.target().timeline().duration;
    const Duration start    = visibleEnd_ > visibleStart_ ? visibleStart_ : Duration::zero();
    const Duration end      = visibleEnd_ > visibleStart_ ? visibleEnd_ : duration;
    const auto     span     = end.nanoseconds() - start.nanoseconds();
    const auto     next =
        std::clamp<std::int64_t>(start.nanoseconds() + delta.nanoseconds(), 0, duration.nanoseconds() - span);
    visibleStart_ = Duration::fromNanoseconds(next);
    visibleEnd_   = Duration::fromNanoseconds(next + span);
    return eve::editing::applied<void>();
}

float ActionTimelineWidget::timeToX(Duration time) const noexcept {
    const auto duration = editor_.target().timeline().duration;
    const auto start    = visibleEnd_ > visibleStart_ ? visibleStart_ : Duration::zero();
    const auto end      = visibleEnd_ > visibleStart_ ? visibleEnd_ : duration;
    const auto span     = end.nanoseconds() - start.nanoseconds();
    if (span <= 0) return labelWidth_;
    const double fraction = static_cast<double>(time.nanoseconds() - start.nanoseconds()) / static_cast<double>(span);
    return labelWidth_ + static_cast<float>(fraction * static_cast<double>(width_ - labelWidth_));
}

Duration ActionTimelineWidget::xToTime(float x) const noexcept {
    const float clamped  = std::clamp(x, labelWidth_, width_);
    const float fraction = (clamped - labelWidth_) / (width_ - labelWidth_);
    const auto   assetDuration = editor_.target().timeline().duration;
    const auto   start         = visibleEnd_ > visibleStart_ ? visibleStart_ : Duration::zero();
    const auto   end           = visibleEnd_ > visibleStart_ ? visibleEnd_ : assetDuration;
    const auto   span          = end.nanoseconds() - start.nanoseconds();
    std::int64_t time =
        start.nanoseconds() + static_cast<std::int64_t>(std::llround(static_cast<double>(fraction) * span));
    if (snapInterval_.nanoseconds() > 0) {
        const double steps = static_cast<double>(time) / static_cast<double>(snapInterval_.nanoseconds());
        time               = static_cast<std::int64_t>(std::llround(steps)) * snapInterval_.nanoseconds();
        time               = std::clamp<std::int64_t>(time, start.nanoseconds(), end.nanoseconds());
    }
    return Duration::fromNanoseconds(time);
}

TimelineWidgetLayout ActionTimelineWidget::layout() const {
    TimelineWidgetLayout result;
    result.width                       = width_;
    result.audioWaveformsAvailable     = eve::cap::query<action::IActionAudioWaveformProvider>() != nullptr;
    const bool        hasAnimationLane = !editor_.target().timeline().animationSections.empty();
    const std::size_t rowOffset        = hasAnimationLane ? 1U : 0U;
    result.height         = rowHeight_ * static_cast<float>(editor_.target().timeline().tracks.size() + rowOffset);
    result.playheadX      = timeToX(editor_.previewTime());
    const auto selected   = editor_.selectedItemIds();
    auto       isSelected = [&](const LogicalId& id) {
        return std::find(selected.begin(), selected.end(), id) != selected.end();
    };
    if (snapInterval_.nanoseconds() > 0) {
        const std::int64_t duration     = editor_.target().timeline().duration.nanoseconds();
        const std::int64_t interval     = snapInterval_.nanoseconds();
        const auto         visibleStart = visibleEnd_ > visibleStart_ ? visibleStart_.nanoseconds() : 0;
        const auto         visibleEnd   = visibleEnd_ > visibleStart_ ? visibleEnd_.nanoseconds() : duration;
        std::int64_t       time         = (visibleStart / interval) * interval;
        if (time < visibleStart) time += interval;
        for (std::int64_t index = time / interval; time <= visibleEnd; time += interval, ++index) {
            const Duration tick = Duration::fromNanoseconds(time);
            result.rulerTicks.push_back({tick, timeToX(tick), index % 5 == 0});
            if (duration - time < interval) break;
        }
    }
    if (hasAnimationLane) {
        const auto animationTrack = *LogicalId::fromParts("action-track", "animation");
        const auto animationType  = *LogicalId::fromParts("animation", "section");
        for (const auto& section : editor_.target().timeline().animationSections) {
            Duration start = section.start;
            Duration end   = section.end;
            if (drag_ && drag_->itemId == section.id) {
                start = drag_->previewStart;
                end   = drag_->previewEnd;
            }
            result.items.push_back({animationTrack, section.id, animationType, true, isSelected(section.id),
                                    timeToX(start), std::max(timeToX(end), timeToX(start) + 4.0f), 3.0f,
                                    rowHeight_ - 3.0f});
        }
    }
    for (std::size_t row = 0; row < editor_.target().timeline().tracks.size(); ++row) {
        const auto& track = editor_.target().timeline().tracks[row];
        const float top   = static_cast<float>(row + rowOffset) * rowHeight_;
        for (const auto& notify : track.notifies) {
            Duration time = notify.time;
            if (drag_ && drag_->itemId == notify.id) time = drag_->previewStart;
            const float center = timeToX(time);
            result.items.push_back({track.id, notify.id, notify.type, false, isSelected(notify.id),
                                    center - kNotifyWidth * 0.5f, center + kNotifyWidth * 0.5f, top + 3.0f,
                                    top + rowHeight_ - 3.0f});
        }
        for (const auto& state : track.states) {
            Duration start = state.start;
            Duration end   = state.end;
            if (drag_ && drag_->itemId == state.id) {
                start = drag_->previewStart;
                end   = drag_->previewEnd;
            }
            const float minimum = timeToX(start);
            const float maximum = std::max(timeToX(end), minimum + 4.0f);
            result.items.push_back({track.id, state.id, state.type, true, isSelected(state.id), minimum, maximum,
                                    top + 3.0f, top + rowHeight_ - 3.0f});
        }
    }
    return result;
}

void ActionTimelineWidget::draw(IEditorOverlay& overlay) const {
    const auto&       timeline         = editor_.target().timeline();
    const bool        hasAnimationLane = !timeline.animationSections.empty();
    const std::size_t rowOffset        = hasAnimationLane ? 1U : 0U;
    if (hasAnimationLane) {
        overlay.rectangle({0.0f, 0.0f, 0.0f}, {width_, rowHeight_, 0.0f}, {0x20252cffU, 1.0f, true});
        overlay.line({0.0f, rowHeight_, 0.0f}, {width_, rowHeight_, 0.0f}, {0x4a5260ffU, 1.0f, false});
        overlay.text({4.0f, 4.0f, 0.0f}, "Animation", {0xd8dee9ffU, 1.0f, false});
    }
    for (std::size_t row = 0; row < timeline.tracks.size(); ++row) {
        const float top    = static_cast<float>(row + rowOffset) * rowHeight_;
        const float bottom = top + rowHeight_;
        overlay.rectangle({0.0f, top, 0.0f}, {width_, bottom, 0.0f}, {0x20252cffU, 1.0f, true});
        overlay.line({0.0f, bottom, 0.0f}, {width_, bottom, 0.0f}, {0x4a5260ffU, 1.0f, false});
        overlay.text({4.0f, top + 4.0f, 0.0f}, timeline.tracks[row].label,
                     {timeline.tracks[row].muted ? 0x7f8792ffU : 0xd8dee9ffU, 1.0f, false});
    }
    for (const auto& item : layout().items) {
        const bool         animationSection = item.type.format() == "animation:section";
        const unsigned int color =
            item.selected ? 0xf2b84bffU : (animationSection ? 0xa66bd4ffU : (item.state ? 0x568bd7ffU : 0x61c28bffU));
        overlay.rectangle({item.minimumX, item.minimumY, 0.0f}, {item.maximumX, item.maximumY, 0.0f},
                          {color, 1.0f, true});
        if (item.state && item.type.format() == "presentation:audio-state") {
            const auto view      = findItem(timeline, item.itemId);
            auto*      waveforms = eve::cap::query<action::IActionAudioWaveformProvider>();
            if (view && waveforms) {
                auto binding = action::ActionAudioBinding::fromPayload(
                    view->payload, action::ActionAudioShape::State);
                const auto pixels = static_cast<std::size_t>(
                    std::max(1.0f, std::floor(item.maximumX - item.minimumX)));
                if (binding) {
                    action::ActionAudioWaveformRequest request{
                        std::move(binding).takeValue(), difference(view->end, view->start),
                        std::min(pixels, kMaximumWaveformBuckets)};
                    auto waveform = waveforms->waveform(request);
                    if (waveform && !waveform.value().buckets.empty()) {
                        const float middle = (item.minimumY + item.maximumY) * 0.5f;
                        const float amplitude = std::max(1.0f, (item.maximumY - item.minimumY) * 0.42f);
                        const float step = (item.maximumX - item.minimumX) /
                                           static_cast<float>(waveform.value().buckets.size());
                        for (std::size_t index = 0; index < waveform.value().buckets.size(); ++index) {
                            const auto& bucket = waveform.value().buckets[index];
                            const float x = item.minimumX + (static_cast<float>(index) + 0.5f) * step;
                            overlay.line({x, middle - std::clamp(bucket.maximum, -1.0f, 1.0f) * amplitude, 0.0f},
                                         {x, middle - std::clamp(bucket.minimum, -1.0f, 1.0f) * amplitude, 0.0f},
                                         {0xe7f4ffffU, 1.0f, false});
                        }
                    }
                }
            }
        }
        if (item.state && item.type.format() == "presentation:parameter-curve") {
            const auto view = findItem(timeline, item.itemId);
            auto binding = view ? action::ActionParameterCurveBinding::fromPayload(view->payload)
                                : Result<action::ActionParameterCurveBinding>::failure(
                                      Diagnostic::error(DiagnosticCode::NotFound, "curve item is unavailable"));
            if (binding) {
                double minimum = binding.value().keys.front().value;
                double maximum = minimum;
                for (const auto& key : binding.value().keys) {
                    minimum = std::min(minimum, key.value);
                    maximum = std::max(maximum, key.value);
                }
                if (maximum - minimum < 1e-9) {
                    minimum -= 0.5;
                    maximum += 0.5;
                }
                const auto point = [&](double time, double value) {
                    const float x = item.minimumX + static_cast<float>(time) * (item.maximumX - item.minimumX);
                    const float normalized = static_cast<float>((value - minimum) / (maximum - minimum));
                    const float y = item.maximumY - 2.0f - normalized * (item.maximumY - item.minimumY - 4.0f);
                    return OverlayPoint{x, y, 0.0f};
                };
                const auto segments = std::min<std::size_t>(
                    kMaximumCurveSegments,
                    std::max<std::size_t>(2, static_cast<std::size_t>(item.maximumX - item.minimumX)));
                auto previous = point(0.0, binding.value().sample(0.0));
                for (std::size_t segment = 1; segment <= segments; ++segment) {
                    const double time = static_cast<double>(segment) / static_cast<double>(segments);
                    const auto current = point(time, binding.value().sample(time));
                    overlay.line(previous, current, {0xffdc7affU, 1.5f, false});
                    previous = current;
                }
                for (const auto& key : binding.value().keys)
                    overlay.circle(point(key.time, key.value), 2.5f,
                                   {item.selected ? 0xffffffffU : 0xffdc7affU, 1.0f, true});
            }
        }
        if (item.state) {
            overlay.line({item.minimumX, item.minimumY, 0.0f}, {item.minimumX, item.maximumY, 0.0f},
                         {0xffffffffU, 2.0f, false});
            overlay.line({item.maximumX, item.minimumY, 0.0f}, {item.maximumX, item.maximumY, 0.0f},
                         {0xffffffffU, 2.0f, false});
        }
    }
    const float height = rowHeight_ * static_cast<float>(timeline.tracks.size() + rowOffset);
    overlay.line({timeToX(editor_.previewTime()), 0.0f, 0.0f}, {timeToX(editor_.previewTime()), height, 0.0f},
                 {0xff5b5bffU, 2.0f, false});
}

std::optional<TimelineHit> ActionTimelineWidget::hitTest(float x, float y) const {
    const auto projected = layout();
    const auto containsY = [&](const TimelineItemGeometry& item) {
        return y >= item.minimumY && y <= item.maximumY;
    };
    // Resize handles remain the most precise affordance even when items overlap.
    for (auto it = projected.items.rbegin(); it != projected.items.rend(); ++it) {
        if (!containsY(*it) || !it->state) continue;
        if (it->state && std::abs(x - it->minimumX) <= kHandleRadius)
            return TimelineHit{it->itemId, TimelineHitPart::StartHandle};
        if (it->state && std::abs(x - it->maximumX) <= kHandleRadius)
            return TimelineHit{it->itemId, TimelineHitPart::EndHandle};
    }
    // A point notify drawn over a state span must remain directly selectable.
    for (auto it = projected.items.rbegin(); it != projected.items.rend(); ++it) {
        if (!containsY(*it) || it->state) continue;
        if (x >= it->minimumX && x <= it->maximumX) return TimelineHit{it->itemId, TimelineHitPart::Body};
    }
    for (auto it = projected.items.rbegin(); it != projected.items.rend(); ++it) {
        if (!containsY(*it) || !it->state) continue;
        if (x >= it->minimumX && x <= it->maximumX) return TimelineHit{it->itemId, TimelineHitPart::Body};
    }
    return std::nullopt;
}

EditorResult<void> ActionTimelineWidget::pointerDown(float x, float y, bool additiveSelection) {
    if (drag_) return widgetError("editor.action.timeline.widget.drag-active", "A timeline drag is already active");
    const auto hit = hitTest(x, y);
    if (!hit) {
        if (!additiveSelection) editor_.clearSelection();
        return eve::editing::noOp();
    }
    auto item = findItem(editor_.target().timeline(), hit->itemId);
    if (!item)
        return widgetError("editor.action.timeline.widget.item-missing", "Timeline item no longer exists",
                           EditorStatus::Conflict);
    if (item->locked) return widgetError("editor.action.timeline.track-locked", "Action track is locked");
    const auto selectedIds     = editor_.selectedItemIds();
    const bool alreadySelected = std::find(selectedIds.begin(), selectedIds.end(), item->itemId) != selectedIds.end();
    auto selected = editor_.selectItem(item->itemId, additiveSelection || alreadySelected);
    if (!selected.ok()) return selected;
    drag_ = DragState{item->itemId, hit->part, xToTime(x), item->start, item->end, item->start, item->end, item->state};
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::updateDrag(float x) {
    if (!drag_) return widgetError("editor.action.timeline.widget.drag-missing", "No timeline drag is active");
    const Duration cursor   = xToTime(x);
    const Duration duration = editor_.target().timeline().duration;
    const Duration delta    = difference(cursor, drag_->anchorTime);
    if (!drag_->state) {
        auto moved = drag_->originalStart.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "Notify drag overflowed");
        drag_->previewStart = std::clamp(moved.value(), Duration::zero(), duration);
        drag_->previewEnd   = drag_->previewStart;
        return eve::editing::applied<void>();
    }
    if (drag_->part == TimelineHitPart::StartHandle) {
        auto moved = drag_->originalStart.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State start drag overflowed");
        drag_->previewStart = std::clamp(moved.value(), Duration::zero(), drag_->originalEnd);
        drag_->previewEnd   = drag_->originalEnd;
        return eve::editing::applied<void>();
    }
    if (drag_->part == TimelineHitPart::EndHandle) {
        auto moved = drag_->originalEnd.tryAdd(delta);
        if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State end drag overflowed");
        drag_->previewStart = drag_->originalStart;
        drag_->previewEnd   = std::clamp(moved.value(), drag_->originalStart, duration);
        return eve::editing::applied<void>();
    }
    const auto span  = drag_->originalEnd.nanoseconds() - drag_->originalStart.nanoseconds();
    auto       moved = drag_->originalStart.tryAdd(delta);
    if (!moved) return widgetError("editor.action.timeline.widget.drag-overflow", "State drag overflowed");
    const auto latestStart = Duration::fromNanoseconds(duration.nanoseconds() - span);
    drag_->previewStart    = std::clamp(moved.value(), Duration::zero(), latestStart);
    drag_->previewEnd      = Duration::fromNanoseconds(drag_->previewStart.nanoseconds() + span);
    return eve::editing::applied<void>();
}

EditorResult<void> ActionTimelineWidget::pointerMove(float x) { return updateDrag(x); }

EditorResult<void> ActionTimelineWidget::pointerUp(float x) {
    auto updated = updateDrag(x);
    if (!updated.ok()) return updated;
    const DragState completed = *drag_;
    drag_.reset();
    const auto item = findItem(editor_.target().timeline(), completed.itemId);
    if (completed.part == TimelineHitPart::Body && editor_.selectionCount() > 1)
        return editor_.moveSelection(difference(completed.previewStart, completed.originalStart));
    if (item && item->animationSection) {
        if (completed.part == TimelineHitPart::Body)
            return editor_.moveAnimationSection(completed.itemId,
                                                difference(completed.previewStart, completed.originalStart));
        const auto section = std::find_if(editor_.target().timeline().animationSections.begin(),
                                          editor_.target().timeline().animationSections.end(),
                                          [&](const auto& value) { return value.id == completed.itemId; });
        if (section == editor_.target().timeline().animationSections.end())
            return widgetError("editor.action.timeline.animation-section-not-found",
                               "Animation section no longer exists", EditorStatus::Conflict);
        return editor_.resizeAnimationSection(completed.itemId, completed.previewStart, completed.previewEnd,
                                              section->blendIn);
    }
    if (completed.state && completed.part != TimelineHitPart::Body)
        return editor_.resizeState(completed.itemId, completed.previewStart, completed.previewEnd);
    return editor_.moveItem(completed.itemId, difference(completed.previewStart, completed.originalStart));
}

EditorResult<void> ActionTimelineWidget::seek(float x) { return editor_.seek(xToTime(x)); }

EditorResult<void> ActionTimelineWidget::inspectSelection(IEditorInspector& inspector) {
    const auto selected = editor_.selectedItemIds();
    if (selected.size() > 1) {
        auto range = editor_.selectionRange();
        if (!range.ok()) return EditorResult<void>::failure(range.status());
        float       startSeconds = static_cast<float>(range.value().start.seconds());
        float       endSeconds   = static_cast<float>(range.value().end.seconds());
        const float maximum = static_cast<float>(editor_.target().timeline().duration.seconds());
        inspector.beginGroup("action.timeline.selection", "Selection");
        bool changed = inspector.scalar("start", "Start", startSeconds, 0.0f, maximum);
        changed      = inspector.scalar("end", "End", endSeconds, 0.0f, maximum) || changed;
        inspector.endGroup();
        if (!changed) return eve::editing::noOp();
        auto start = Duration::fromSeconds(startSeconds);
        auto end   = Duration::fromSeconds(endSeconds);
        if (!start || !end)
            return widgetError("editor.action.timeline.widget.selection-time-invalid",
                               "Selection bounds are invalid");
        return editor_.scaleSelection(start.value(), end.value());
    }
    if (selected.empty()) return eve::editing::noOp();
    auto item = findItem(editor_.target().timeline(), selected.front());
    if (!item)
        return widgetError("editor.action.timeline.widget.item-missing", "Selected item no longer exists",
                           EditorStatus::Conflict);

    if (item->animationSection) {
        const auto section = std::find_if(editor_.target().timeline().animationSections.begin(),
                                          editor_.target().timeline().animationSections.end(),
                                          [&](const auto& value) { return value.id == item->itemId; });
        if (section == editor_.target().timeline().animationSections.end())
            return widgetError("editor.action.timeline.animation-section-not-found",
                               "Animation section no longer exists", EditorStatus::Conflict);
        float       startSeconds       = static_cast<float>(section->start.seconds());
        float       endSeconds         = static_cast<float>(section->end.seconds());
        float       blendSeconds       = static_cast<float>(section->blendIn.seconds());
        float       sourceStartSeconds = static_cast<float>(section->sourceStart.seconds());
        float       sourceEndSeconds   = static_cast<float>(section->sourceEnd.seconds());
        std::string animationUri       = section->animationUri;
        std::string blendCurve         = std::string(action::actionBlendCurveName(section->blendCurve));
        const float maximum            = static_cast<float>(editor_.target().timeline().duration.seconds());
        inspector.beginGroup("action.timeline.animation-section", "Animation Section");
        bool changed = inspector.scalar("start", "Start", startSeconds, 0.0f, maximum);
        changed      = inspector.scalar("end", "End", endSeconds, 0.0f, maximum) || changed;
        changed      = inspector.scalar("blendIn", "Blend In", blendSeconds, 0.0f, maximum) || changed;
        changed =
            inspector.scalar("sourceStart", "Trim In", sourceStartSeconds, 0.0f, std::numeric_limits<float>::max()) ||
            changed;
        changed = inspector.scalar("sourceEnd", "Trim Out (0 = clip end)", sourceEndSeconds, 0.0f,
                                   std::numeric_limits<float>::max()) ||
                  changed;
        changed = inspector.string("blendCurve", "Blend Curve", blendCurve) || changed;
        changed = inspector.string("animationUri", "Animation", animationUri) || changed;
        inspector.endGroup();
        if (!changed) return eve::editing::applied<void>();
        auto start       = Duration::fromSeconds(startSeconds);
        auto end         = Duration::fromSeconds(endSeconds);
        auto blend       = Duration::fromSeconds(blendSeconds);
        auto sourceStart = Duration::fromSeconds(sourceStartSeconds);
        auto sourceEnd   = Duration::fromSeconds(sourceEndSeconds);
        auto parsedCurve = action::actionBlendCurveFromName(blendCurve);
        if (!start || !end || !blend || !sourceStart || !sourceEnd || !parsedCurve)
            return widgetError("editor.action.timeline.widget.time-invalid", "Animation section time is invalid");
        action::ActionAnimationSection edited = *section;
        edited.start                          = start.value();
        edited.end                            = end.value();
        edited.blendIn                        = blend.value();
        edited.sourceStart                    = sourceStart.value();
        edited.sourceEnd                      = sourceEnd.value();
        edited.blendCurve                     = *parsedCurve;
        edited.animationUri                   = std::move(animationUri);
        return editor_.editAnimationSectionFull(std::move(edited));
    }

    if (item->state && item->type.format() == "presentation:parameter-curve") {
        auto binding = action::ActionParameterCurveBinding::fromPayload(item->payload);
        if (!binding)
            return widgetError("editor.action.timeline.widget.parameter-curve-invalid",
                               "Selected parameter curve payload is invalid");
        std::string target = binding.value().target.format();
        std::string operation = std::string(action::actionParameterOperationName(binding.value().operation));
        Value::Array keys;
        bool changed = false;
        inspector.beginGroup("action.timeline.parameter-curve", "Parameter Curve");
        changed = inspector.string("target", "Target", target) || changed;
        changed = inspector.string("operation", "Operation", operation) || changed;
        for (std::size_t index = 0; index < binding.value().keys.size(); ++index) {
            auto key = binding.value().keys[index];
            float time = static_cast<float>(key.time);
            float value = static_cast<float>(key.value);
            float inTangent = static_cast<float>(key.inTangent);
            float outTangent = static_cast<float>(key.outTangent);
            std::string interpolation = std::string(action::actionParameterInterpolationName(key.interpolation));
            inspector.beginGroup("action.timeline.parameter-key." + std::to_string(index),
                                 "Key " + std::to_string(index));
            const bool endpoint = index == 0 || index + 1 == binding.value().keys.size();
            const float minimumTime = endpoint ? static_cast<float>(key.time)
                                               : static_cast<float>(binding.value().keys[index - 1].time);
            const float maximumTime = endpoint ? static_cast<float>(key.time)
                                               : static_cast<float>(binding.value().keys[index + 1].time);
            changed = inspector.scalar("time", "Time", time, minimumTime, maximumTime) || changed;
            changed = inspector.scalar("value", "Value", value, std::numeric_limits<float>::lowest(),
                                       std::numeric_limits<float>::max()) || changed;
            changed = inspector.scalar("inTangent", "In Tangent", inTangent,
                                       std::numeric_limits<float>::lowest(),
                                       std::numeric_limits<float>::max()) || changed;
            changed = inspector.scalar("outTangent", "Out Tangent", outTangent,
                                       std::numeric_limits<float>::lowest(),
                                       std::numeric_limits<float>::max()) || changed;
            changed = inspector.string("interpolation", "Interpolation", interpolation) || changed;
            inspector.endGroup();
            keys.emplace_back(Value::Object{{"time", static_cast<double>(time)},
                                            {"value", static_cast<double>(value)},
                                            {"inTangent", static_cast<double>(inTangent)},
                                            {"outTangent", static_cast<double>(outTangent)},
                                            {"interpolation", std::move(interpolation)}});
        }
        inspector.endGroup();
        if (!changed) return eve::editing::applied<void>();
        auto payload = item->payload;
        payload["target"] = std::move(target);
        payload["operation"] = std::move(operation);
        payload["keys"] = Value(std::move(keys));
        auto validated = action::ActionParameterCurveBinding::fromPayload(payload);
        if (!validated)
            return widgetError("editor.action.timeline.widget.parameter-curve-contract",
                               "Parameter curve fields violate the block contract");
        return editor_.updateItem(item->itemId, item->type,
                                  validated.value().toPayload(std::move(payload)));
    }

    float       startSeconds = static_cast<float>(item->start.seconds());
    float       endSeconds   = static_cast<float>(item->end.seconds());
    std::string type         = item->type.format();
    auto        payloadJson  = Value(item->payload).toJson();
    if (!payloadJson)
        return widgetError("editor.action.timeline.widget.payload-encode", "Could not encode item payload");
    std::string payload = payloadJson.value();

    inspector.beginGroup("action.timeline.item", item->state ? "Notify State" : "Notify");
    const float maximum       = static_cast<float>(editor_.target().timeline().duration.seconds());
    bool        timingChanged = inspector.scalar("start", item->state ? "Start" : "Time", startSeconds, 0.0f, maximum);
    if (item->state) timingChanged = inspector.scalar("end", "End", endSeconds, 0.0f, maximum) || timingChanged;
    const bool typeChanged    = inspector.string("type", "Type", type);
    const bool payloadChanged = inspector.string("payload", "Payload JSON", payload);
    inspector.endGroup();

    if (!timingChanged && !typeChanged && !payloadChanged) return eve::editing::applied<void>();
    Result<Duration> parsedStart =
        timingChanged ? Duration::fromSeconds(startSeconds) : Result<Duration>::success(item->start);
    Result<Duration> parsedEnd =
        timingChanged ? Duration::fromSeconds(endSeconds) : Result<Duration>::success(item->end);
    if (!parsedStart || !parsedEnd)
        return widgetError("editor.action.timeline.widget.time-invalid", "Inspector time is invalid");
    auto parsedType = LogicalId::parse(type);
    if (!parsedType) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    auto parsedPayload = Value::fromJson(payload);
    if (!parsedPayload || !parsedPayload.value().getIf<Value::Object>())
        return widgetError("editor.action.timeline.widget.payload-invalid", "Payload must be a JSON object");
    auto descriptor = registry_.descriptor(type);
    if (!descriptor)
        return widgetError("editor.action.timeline.widget.type-unregistered", "Notify type is not registered");
    const auto expected = item->state ? action::ActionNotifyShape::State : action::ActionNotifyShape::Instant;
    if (descriptor.value().shape != expected)
        return widgetError("editor.action.timeline.widget.shape-mismatch", "Notify type shape does not match item");
    const auto*                 object = parsedPayload.value().getIf<Value::Object>();
    action::ActionTimelineEvent event{
        item->state ? action::ActionTimelineEventKind::StateEnter : action::ActionTimelineEventKind::Notify,
        item->trackId,
        item->itemId,
        *parsedType,
        parsedStart.value(),
        *object};
    if (!registry_.validate(event))
        return widgetError("editor.action.timeline.widget.payload-contract", "Payload violates notify contract");
    return editor_.editItem(item->itemId, parsedStart.value(), item->state ? parsedEnd.value() : parsedStart.value(),
                            std::move(*parsedType), *object);
}

std::vector<TimelineWidgetCommandDescriptor> ActionTimelineWidget::commands() const {
    const bool selected = editor_.selectionCount() > 0;
    const bool multi    = editor_.selectionCount() > 1;
    return {{TimelineWidgetCommand::Copy, "Copy", "Ctrl+C", selected},
            {TimelineWidgetCommand::Paste, "Paste at Playhead", "Ctrl+V", clipboardAnchor_.has_value()},
            {TimelineWidgetCommand::DeleteSelection, "Delete", "Delete", selected},
            {TimelineWidgetCommand::AlignSelectionStart, "Align Starts", "Shift+[", multi},
            {TimelineWidgetCommand::AlignSelectionEnd, "Align Ends", "Shift+]", multi},
            {TimelineWidgetCommand::Undo, "Undo", "Ctrl+Z", editor_.canUndo()},
            {TimelineWidgetCommand::Redo, "Redo", "Ctrl+Y", editor_.canRedo()},
            {TimelineWidgetCommand::PlayPause, editor_.playing() ? "Pause" : "Play", "Space", true}};
}

EditorResult<void> ActionTimelineWidget::invoke(TimelineWidgetCommand command) {
    switch (command) {
        case TimelineWidgetCommand::Copy: {
            const auto selected = editor_.selectedItemIds();
            if (selected.empty())
                return widgetError("editor.action.timeline.selection-empty", "No timeline items are selected");
            std::optional<Duration> earliest;
            for (const auto& id : selected) {
                const auto item = findItem(editor_.target().timeline(), id);
                if (item && (!earliest || item->start < *earliest)) earliest = item->start;
            }
            auto copied = editor_.copySelection();
            if (copied.ok()) clipboardAnchor_ = earliest;
            return adapt(copied, "editor.action.timeline.widget.copy", "Could not copy timeline selection");
        }
        case TimelineWidgetCommand::Paste: {
            if (!clipboardAnchor_)
                return widgetError("editor.action.timeline.clipboard-empty", "Action timeline clipboard is empty");
            return adapt(editor_.paste(difference(editor_.previewTime(), *clipboardAnchor_)),
                         "editor.action.timeline.widget.paste", "Could not paste timeline selection");
        }
        case TimelineWidgetCommand::DeleteSelection: return editor_.deleteSelection();
        case TimelineWidgetCommand::AlignSelectionStart: return editor_.alignSelectionStart();
        case TimelineWidgetCommand::AlignSelectionEnd: return editor_.alignSelectionEnd();
        case TimelineWidgetCommand::Undo: {
            auto result = editor_.undo();
            if (result.ok()) return eve::editing::applied<void>();
            return EditorResult<void>::failure(result.status());
        }
        case TimelineWidgetCommand::Redo: {
            auto result = editor_.redo();
            if (result.ok()) return eve::editing::applied<void>();
            return EditorResult<void>::failure(result.status());
        }
        case TimelineWidgetCommand::PlayPause:
            if (editor_.playing())
                editor_.pause();
            else
                editor_.play();
            return eve::editing::applied<void>();
    }
    return widgetError("editor.action.timeline.widget.command", "Timeline command is unsupported",
                       EditorStatus::Unsupported);
}

EditorResult<void> ActionTimelineWidget::handleShortcut(std::string_view shortcut) {
    if (shortcut == "Ctrl+C") return invoke(TimelineWidgetCommand::Copy);
    if (shortcut == "Ctrl+V") return invoke(TimelineWidgetCommand::Paste);
    if (shortcut == "Delete" || shortcut == "Backspace") return invoke(TimelineWidgetCommand::DeleteSelection);
    if (shortcut == "Shift+[") return invoke(TimelineWidgetCommand::AlignSelectionStart);
    if (shortcut == "Shift+]") return invoke(TimelineWidgetCommand::AlignSelectionEnd);
    if (shortcut == "Ctrl+Z") return invoke(TimelineWidgetCommand::Undo);
    if (shortcut == "Ctrl+Y" || shortcut == "Ctrl+Shift+Z") return invoke(TimelineWidgetCommand::Redo);
    if (shortcut == "Space") return invoke(TimelineWidgetCommand::PlayPause);
    return widgetError("editor.action.timeline.widget.shortcut", "Timeline shortcut is unsupported",
                       EditorStatus::Unsupported);
}

std::vector<action::ActionNotifyDescriptor> ActionTimelineWidget::insertableTypes(
    action::ActionNotifyShape shape) const {
    auto descriptors = registry_.descriptors();
    std::erase_if(descriptors, [&](const auto& descriptor) { return descriptor.shape != shape; });
    return descriptors;
}

LogicalId ActionTimelineWidget::generatedItemId() {
    for (;;) {
        auto id = LogicalId::fromParts("editor", "timeline-item." + std::to_string(++generatedSequence_));
        if (!id) continue;
        if (!findItem(editor_.target().timeline(), *id)) return std::move(*id);
    }
}

EditorResult<void> ActionTimelineWidget::addNotifyAtCursor(const LogicalId& trackId, std::string_view type,
                                                           Value::Object payload) {
    auto parsed = LogicalId::parse(type);
    if (!parsed) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    action::ActionTimelineEvent event{
        action::ActionTimelineEventKind::Notify, trackId, generatedItemId(), *parsed, editor_.previewTime(), payload};
    auto valid = registry_.validate(event);
    if (!valid) return widgetError("editor.action.timeline.widget.notify-invalid", "Notify payload is invalid");
    return editor_.addNotify(trackId, {event.itemId, event.type, event.time, std::move(payload)});
}

EditorResult<void> ActionTimelineWidget::addStateAtCursor(const LogicalId& trackId, std::string_view type,
                                                          Duration duration, Value::Object payload) {
    if (duration < Duration::zero())
        return widgetError("editor.action.timeline.widget.state-duration", "Notify-state duration is negative");
    auto parsed = LogicalId::parse(type);
    if (!parsed) return widgetError("editor.action.timeline.widget.type-invalid", "Notify type is invalid");
    auto end = editor_.previewTime().tryAdd(duration);
    if (!end || end.value() > editor_.target().timeline().duration)
        return widgetError("editor.action.timeline.widget.state-range", "Notify-state exceeds the timeline");
    action::ActionTimelineEvent event{action::ActionTimelineEventKind::StateEnter,
                                      trackId,
                                      generatedItemId(),
                                      *parsed,
                                      editor_.previewTime(),
                                      payload};
    auto                        valid = registry_.validate(event);
    if (!valid) return widgetError("editor.action.timeline.widget.state-invalid", "Notify-state payload is invalid");
    return editor_.addState(trackId, {event.itemId, event.type, event.time, end.value(), std::move(payload)});
}

}  // namespace eve::editor
