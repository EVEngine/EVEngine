#include "editor/ActionTimelineScriptBindings.h"

#include "action/ActionNotifyRegistry.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/ActionTimelineEditor.h"
#include "editor/ActionTimelineWidget.h"
#include "editor/Editor.h"
#include "editor/EditorWorkspace.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::editor {
namespace {

constexpr const char* kBindingSource = "editor.action.timeline.squirrel";

StatusCode statusCode(EditorStatus status) noexcept {
    switch (status) {
        case EditorStatus::Applied: return StatusCode::Applied;
        case EditorStatus::Pending: return StatusCode::Pending;
        case EditorStatus::NoOp: return StatusCode::NoOp;
        case EditorStatus::Rejected: return StatusCode::Rejected;
        case EditorStatus::Conflict: return StatusCode::Conflict;
        case EditorStatus::NotFound: return StatusCode::NotFound;
        case EditorStatus::Unsupported: return StatusCode::Unsupported;
        case EditorStatus::Cancelled: return StatusCode::Cancelled;
        case EditorStatus::Failed: return StatusCode::Failed;
    }
    return StatusCode::Failed;
}

Severity severity(DiagnosticSeverity value) noexcept {
    switch (value) {
        case DiagnosticSeverity::Info: return Severity::Info;
        case DiagnosticSeverity::Warning: return Severity::Warning;
        case DiagnosticSeverity::Error: return Severity::Error;
    }
    return Severity::Error;
}

Status statusFrom(const EditorResult<void>& result) {
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(result.diagnostics.size());
    for (const auto& diagnostic : result.diagnostics) {
        diagnostics.emplace_back(DiagnosticCode::Failed, severity(diagnostic.severity), diagnostic.message,
                                 diagnostic.rule.value(), DiagnosticDetails{}, kBindingSource);
    }
    return Status(statusCode(result.status), std::move(diagnostics));
}

template <class T>
Status statusFrom(const EditorResult<T>& result) {
    std::vector<Diagnostic> diagnostics;
    diagnostics.reserve(result.diagnostics.size());
    for (const auto& diagnostic : result.diagnostics) {
        diagnostics.emplace_back(DiagnosticCode::Failed, severity(diagnostic.severity), diagnostic.message,
                                 diagnostic.rule.value(), DiagnosticDetails{}, kBindingSource);
    }
    return Status(statusCode(result.status), std::move(diagnostics));
}

ssq::Table project(HSQUIRRELVM vm, const EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.accepted(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const EditorResult<T>& result, Value value) {
    const bool hasValue = result.accepted() && result.value.has_value();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

Result<Duration> seconds(float value) {
    auto converted = Duration::fromSeconds(static_cast<double>(value));
    if (!converted) return Result<Duration>::failure(converted.status());
    return Result<Duration>::success(std::move(converted).takeValue());
}

const action::ActionTrack* trackAt(const action::ActionTimeline& timeline, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= timeline.tracks.size()) return nullptr;
    return &timeline.tracks[static_cast<std::size_t>(index)];
}

const TimelineItemGeometry* itemAt(const TimelineWidgetLayout& layout, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= layout.items.size()) return nullptr;
    return &layout.items[static_cast<std::size_t>(index)];
}

const action::ActionNotifyState* findState(const action::ActionTimeline& timeline, const std::string& itemId) {
    for (const auto& track : timeline.tracks) {
        const auto found = std::find_if(track.states.begin(), track.states.end(),
                                        [&](const auto& state) { return state.id.format() == itemId; });
        if (found != track.states.end()) return &*found;
    }
    return nullptr;
}

class ScriptActionTimelineEditor {
public:
    ScriptActionTimelineEditor(std::string targetId, action::ActionTimeline timeline,
                               action::ActionNotifyRegistry registry)
        : registry_(std::move(registry)),
          editor_(std::move(targetId), std::move(timeline)),
          widget_(editor_, registry_) {}

    ActionTimelineEditor& editor() noexcept { return editor_; }
    ActionTimelineWidget& widget() noexcept { return widget_; }

    const ActionTimelineEditor& editor() const noexcept { return editor_; }
    const ActionTimelineWidget& widget() const noexcept { return widget_; }

private:
    action::ActionNotifyRegistry registry_;
    ActionTimelineEditor         editor_;
    ActionTimelineWidget         widget_;
};

std::string eventKind(action::ActionTimelineEventKind kind) {
    switch (kind) {
        case action::ActionTimelineEventKind::Notify: return "notify";
        case action::ActionTimelineEventKind::StateEnter: return "state_enter";
        case action::ActionTimelineEventKind::StateExit: return "state_exit";
    }
    return "unknown";
}

}  // namespace

void exposeActionTimelineScriptBindings(ssq::Table& table, ssq::Class& editorClass) {
    const HSQUIRRELVM vm           = table.getHandle();
    auto              actionEditor = table.addClass<ScriptActionTimelineEditor>(
        "ActionTimelineEditor",
        std::function<ScriptActionTimelineEditor*()>([]() -> ScriptActionTimelineEditor* { return nullptr; }), true);

    actionEditor.addFunc("configureWorkspace", [vm](ScriptActionTimelineEditor* self, EditorWorkspace* workspace) {
        if (!self || !workspace)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor and workspace must not be null", "workspace");
        return project(vm, self->editor().configureWorkspace(*workspace));
    });
    actionEditor.addFunc(
        "setViewport", [vm](ScriptActionTimelineEditor* self, float width, float rowHeight, float labelWidth) {
            if (!self)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
            return project(vm, self->widget().setViewport(width, rowHeight, labelWidth));
        });
    actionEditor.addFunc("pointerDown", [vm](ScriptActionTimelineEditor* self, float x, float y, bool additive) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->widget().pointerDown(x, y, additive));
    });
    actionEditor.addFunc("pointerMove", [vm](ScriptActionTimelineEditor* self, float x) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->widget().pointerMove(x));
    });
    actionEditor.addFunc("pointerUp", [vm](ScriptActionTimelineEditor* self, float x) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->widget().pointerUp(x));
    });
    actionEditor.addFunc("seekX", [vm](ScriptActionTimelineEditor* self, float x) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->widget().seek(x));
    });
    actionEditor.addFunc("seekSeconds", [vm](ScriptActionTimelineEditor* self, float value) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto duration = seconds(value);
        if (!duration) return script::projectStatusResult(vm, duration.status(), false, false);
        return project(vm, self->editor().seek(std::move(duration).takeValue()));
    });
    actionEditor.addFunc("resizeState", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                             float startSeconds, float endSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto start = seconds(startSeconds);
        if (!start) return script::projectStatusResult(vm, start.status(), false, false);
        auto end = seconds(endSeconds);
        if (!end) return script::projectStatusResult(vm, end.status(), false, false);
        auto parsedItemId = LogicalId::parse(itemId);
        if (!parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "state item id is not canonical", "itemId");
        return project(
            vm, self->editor().resizeState(*parsedItemId, std::move(start).takeValue(), std::move(end).takeValue()));
    });
    actionEditor.addFunc("undo", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result, Value(result.value ? static_cast<std::int64_t>(result.value->afterRevision) : 0));
    });
    actionEditor.addFunc("redo", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result, Value(result.value ? static_cast<std::int64_t>(result.value->afterRevision) : 0));
    });
    actionEditor.addFunc("update", [vm](ScriptActionTimelineEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto delta = seconds(deltaSeconds);
        if (!delta) return script::projectStatusResult(vm, delta.status(), false, false);
        auto result = self->editor().update(std::move(delta).takeValue());
        return project(vm, result, Value(result.value ? static_cast<std::int64_t>(*result.value) : 0));
    });
    actionEditor.addFunc("snapshot", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->editor().target().timeline().toValue(),
                                     [](Value value) { return value; });
    });

    actionEditor.addFunc("play", [](ScriptActionTimelineEditor* self) {
        if (self) self->editor().play();
    });
    actionEditor.addFunc("pause", [](ScriptActionTimelineEditor* self) {
        if (self) self->editor().pause();
    });
    actionEditor.addFunc("isPlaying",
                         [](ScriptActionTimelineEditor* self) { return self && self->editor().playing(); });
    actionEditor.addFunc("canUndo", [](ScriptActionTimelineEditor* self) { return self && self->editor().canUndo(); });
    actionEditor.addFunc("canRedo", [](ScriptActionTimelineEditor* self) { return self && self->editor().canRedo(); });
    actionEditor.addFunc("isDragging", [](ScriptActionTimelineEditor* self) {
        return self && self->widget().dragStatus() == TimelineDragStatus::Active;
    });
    actionEditor.addFunc("getDuration", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().target().timeline().duration.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getPreviewTime", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().previewTime().seconds()) : 0.0f;
    });
    actionEditor.addFunc("getRevision", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<std::int64_t>(self->editor().target().revision()) : std::int64_t{0};
    });
    actionEditor.addFunc("getAnimationUri", [](ScriptActionTimelineEditor* self) {
        return self ? self->editor().target().timeline().animationUri : std::string{};
    });
    actionEditor.addFunc("getTrackCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->editor().target().timeline().tracks.size()) : 0;
    });
    actionEditor.addFunc("getTrackId", [](ScriptActionTimelineEditor* self, int index) {
        const auto* track = self ? trackAt(self->editor().target().timeline(), index) : nullptr;
        return track ? track->id.format() : std::string{};
    });
    actionEditor.addFunc("getTrackLabel", [](ScriptActionTimelineEditor* self, int index) {
        const auto* track = self ? trackAt(self->editor().target().timeline(), index) : nullptr;
        return track ? track->label : std::string{};
    });
    actionEditor.addFunc("getTrackKind", [](ScriptActionTimelineEditor* self, int index) {
        const auto* track = self ? trackAt(self->editor().target().timeline(), index) : nullptr;
        return track ? std::string(action::actionTrackKindName(track->kind)) : std::string{};
    });
    actionEditor.addFunc("getTrackMuted", [](ScriptActionTimelineEditor* self, int index) {
        const auto* track = self ? trackAt(self->editor().target().timeline(), index) : nullptr;
        return track && track->muted;
    });

    actionEditor.addFunc("getLayoutWidth",
                         [](ScriptActionTimelineEditor* self) { return self ? self->widget().layout().width : 0.0f; });
    actionEditor.addFunc("getLayoutHeight",
                         [](ScriptActionTimelineEditor* self) { return self ? self->widget().layout().height : 0.0f; });
    actionEditor.addFunc("getPlayheadX", [](ScriptActionTimelineEditor* self) {
        return self ? self->widget().layout().playheadX : 0.0f;
    });
    actionEditor.addFunc("getItemCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->widget().layout().items.size()) : 0;
    });
    actionEditor.addFunc("getItemId", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->itemId.format() : std::string{};
    });
    actionEditor.addFunc("getItemType", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->type.format() : std::string{};
    });
    actionEditor.addFunc("getItemState", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item && item->state;
    });
    actionEditor.addFunc("getItemSelected", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item && item->selected;
    });
    actionEditor.addFunc("getItemMinX", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->minimumX : 0.0f;
    });
    actionEditor.addFunc("getItemMaxX", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->maximumX : 0.0f;
    });
    actionEditor.addFunc("getItemMinY", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->minimumY : 0.0f;
    });
    actionEditor.addFunc("getItemMaxY", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->maximumY : 0.0f;
    });

    actionEditor.addFunc("getStateStart", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        return state ? static_cast<float>(state->start.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getStateEnd", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        return state ? static_cast<float>(state->end.seconds()) : 0.0f;
    });

    actionEditor.addFunc("getEventCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->editor().previewEvents().size()) : 0;
    });
    actionEditor.addFunc("getEventItemId", [](ScriptActionTimelineEditor* self, int index) {
        if (!self) return std::string{};
        const auto& events = self->editor().previewEvents();
        return index >= 0 && static_cast<std::size_t>(index) < events.size()
                   ? events[static_cast<std::size_t>(index)].itemId.format()
                   : std::string{};
    });
    actionEditor.addFunc("getEventType", [](ScriptActionTimelineEditor* self, int index) {
        if (!self) return std::string{};
        const auto& events = self->editor().previewEvents();
        return index >= 0 && static_cast<std::size_t>(index) < events.size()
                   ? events[static_cast<std::size_t>(index)].type.format()
                   : std::string{};
    });
    actionEditor.addFunc("getEventTime", [](ScriptActionTimelineEditor* self, int index) {
        if (!self) return 0.0f;
        const auto& events = self->editor().previewEvents();
        return index >= 0 && static_cast<std::size_t>(index) < events.size()
                   ? static_cast<float>(events[static_cast<std::size_t>(index)].time.seconds())
                   : 0.0f;
    });
    actionEditor.addFunc("getEventKind", [](ScriptActionTimelineEditor* self, int index) {
        if (!self) return std::string{};
        const auto& events = self->editor().previewEvents();
        return index >= 0 && static_cast<std::size_t>(index) < events.size()
                   ? eventKind(events[static_cast<std::size_t>(index)].kind)
                   : std::string{};
    });

    editorClass.addFunc(
        "newActionTimelineEditor", [vm](Editor*, const std::string& targetId, const ssq::Object& timelineObject) {
            if (targetId.empty())
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "action timeline target id must not be empty", "targetId");
            script::SquirrelValueOptions options;
            options.source = kBindingSource;
            auto value     = script::valueFromSquirrel(timelineObject, options);
            if (!value) return script::projectStatusResult(vm, value.status(), false, false);
            auto timeline = action::ActionTimeline::fromValue(value.value());
            if (!timeline) return script::projectStatusResult(vm, timeline.status(), false, false);
            auto registry = action::ActionNotifyRegistry::withBuiltins();
            if (!registry) return script::projectStatusResult(vm, registry.status(), false, false);
            auto object = script::makeOwnedSquirrelInstance<ScriptActionTimelineEditor>(
                vm, std::make_unique<ScriptActionTimelineEditor>(targetId, std::move(timeline).takeValue(),
                                                                 std::move(registry).takeValue()));
            if (!object) return script::projectStatusResult(vm, object.status(), false, false);
            ssq::Object owned  = std::move(object).takeValue();
            auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
            result.set("value", owned);
            result.set("ownership", std::string("owned"));
            return result;
        });
}

}  // namespace eve::editor
