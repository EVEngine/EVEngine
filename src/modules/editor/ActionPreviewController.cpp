#include "editor/ActionPreviewController.h"

#include <cmath>
#include <utility>

namespace eve::editor {
namespace {

EditorResult<void> previewError(EditorStatus status, std::string rule, std::string message) {
    return EditorResult<void>::error(status, RuleId(std::move(rule)), std::move(message));
}

template <typename T>
EditorResult<T> previewErrorValue(EditorStatus status, std::string rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(std::move(rule)), std::move(message));
}

std::string diagnosticMessage(const Status& status, std::string fallback) {
    if (!status.diagnostics().empty() && !status.diagnostics().front().message().empty())
        return status.diagnostics().front().message();
    return fallback;
}

}  // namespace

ActionPreviewController::ActionPreviewController(ActionTimelineEditor& editor, action::IActionPreviewSink& sink,
                                                 const action::IActionRootMotionSource* rootMotion)
    : editor_(editor), sink_(sink), rootMotion_(rootMotion) {}

EditorResult<void> ActionPreviewController::setRootMotionSampleCount(std::uint32_t sampleCount) {
    if (sampleCount < 2 || sampleCount > 1024)
        return previewError(EditorStatus::Rejected, "editor.action.preview.root-samples",
                            "Root-motion sample count must be between 2 and 1024");
    rootMotionSampleCount_ = sampleCount;
    return EditorResult<void>::applied();
}

action::ActionPreviewCueKind ActionPreviewController::cueKind(const action::ActionTimelineEvent& event) noexcept {
    if (event.kind == action::ActionTimelineEventKind::StateEnter) return action::ActionPreviewCueKind::StateEnter;
    if (event.kind == action::ActionTimelineEventKind::StateExit) return action::ActionPreviewCueKind::StateExit;
    const std::string_view type = event.type.format();
    if (type == "presentation:vfx") return action::ActionPreviewCueKind::Vfx;
    if (type == "presentation:audio") return action::ActionPreviewCueKind::Audio;
    if (type == "presentation:camera") return action::ActionPreviewCueKind::Camera;
    return action::ActionPreviewCueKind::Gameplay;
}

Result<action::ActionPreviewFrame> ActionPreviewController::buildFrame(
    action::ActionPreviewReason reason, Duration previous, Duration current,
    const std::vector<action::ActionTimelineEvent>& events) const {
    action::ActionPreviewFrame frame;
    frame.reason       = reason;
    frame.animationUri = editor_.target().timeline().animationUri;
    frame.previous     = previous;
    frame.current      = current;
    frame.cues.reserve(events.size());
    for (const auto& event : events)
        frame.cues.push_back({cueKind(event), event.itemId, event.type, event.time, event.payload});
    if (rootMotion_) {
        auto path = rootMotion_->sampleRootMotion(frame.animationUri, editor_.target().timeline().duration,
                                                  rootMotionSampleCount_);
        if (!path) return Result<action::ActionPreviewFrame>::failure(path.status());
        frame.rootMotionState = action::RootMotionPreviewState::Available;
        frame.rootMotionPath  = std::move(path).takeValue();
    }
    return Result<action::ActionPreviewFrame>::success(std::move(frame));
}

EditorResult<void> ActionPreviewController::refresh() {
    auto frame = buildFrame(action::ActionPreviewReason::Refresh, editor_.previewTime(), editor_.previewTime(), {});
    if (!frame)
        return previewError(EditorStatus::Rejected, "editor.action.preview.root-motion",
                            diagnosticMessage(frame.status(), "Could not sample root motion"));
    auto prepared = sink_.prepare(frame.value());
    if (!prepared)
        return previewError(EditorStatus::Rejected, "editor.action.preview.prepare",
                            diagnosticMessage(prepared.status(), "Preview host rejected the frame"));
    sink_.present(frame.value());
    lastFrame_ = std::move(frame).takeValue();
    return EditorResult<void>::applied();
}

EditorResult<void> ActionPreviewController::seek(Duration time) {
    if (time < Duration::zero() || time > editor_.target().timeline().duration)
        return previewError(EditorStatus::Rejected, "editor.action.preview.seek-range",
                            "Preview seek is outside the timeline");
    auto frame = buildFrame(action::ActionPreviewReason::Seek, editor_.previewTime(), time, {});
    if (!frame)
        return previewError(EditorStatus::Rejected, "editor.action.preview.root-motion",
                            diagnosticMessage(frame.status(), "Could not sample root motion"));
    auto prepared = sink_.prepare(frame.value());
    if (!prepared)
        return previewError(EditorStatus::Rejected, "editor.action.preview.prepare",
                            diagnosticMessage(prepared.status(), "Preview host rejected the frame"));
    auto changed = editor_.seek(time);
    if (!changed.accepted()) {
        sink_.discardPrepared();
        return changed;
    }
    sink_.present(frame.value());
    lastFrame_ = std::move(frame).takeValue();
    return EditorResult<void>::applied();
}

EditorResult<std::size_t> ActionPreviewController::update(Duration delta) {
    auto plan = editor_.planPreview(delta);
    if (!plan.accepted())
        return previewErrorValue<std::size_t>(plan.status, "editor.action.preview.plan",
                                              "Could not prepare timeline preview advance");
    if (plan.status == EditorStatus::NoOp) {
        EditorResult<std::size_t> result;
        result.status = EditorStatus::NoOp;
        result.value  = 0;
        return result;
    }
    auto frame =
        buildFrame(action::ActionPreviewReason::Advance, plan.value->previous, plan.value->current, plan.value->events);
    if (!frame)
        return previewErrorValue<std::size_t>(EditorStatus::Rejected, "editor.action.preview.root-motion",
                                              diagnosticMessage(frame.status(), "Could not sample root motion"));
    auto prepared = sink_.prepare(frame.value());
    if (!prepared)
        return previewErrorValue<std::size_t>(EditorStatus::Rejected, "editor.action.preview.prepare",
                                              diagnosticMessage(prepared.status(), "Preview host rejected the frame"));
    auto advanced = editor_.applyPreviewPlan(std::move(*plan.value));
    if (!advanced.accepted()) {
        sink_.discardPrepared();
        return advanced;
    }
    sink_.present(frame.value());
    lastFrame_ = std::move(frame).takeValue();
    return advanced;
}

void ActionPreviewController::drawRootMotion(IEditorOverlay& overlay, OverlayPoint origin, float scale) const {
    if (!lastFrame_ || lastFrame_->rootMotionState != action::RootMotionPreviewState::Available ||
        !std::isfinite(scale) || scale <= 0.0f)
        return;
    for (std::size_t index = 1; index < lastFrame_->rootMotionPath.size(); ++index) {
        const auto& previous = lastFrame_->rootMotionPath[index - 1];
        const auto& current  = lastFrame_->rootMotionPath[index];
        overlay.line({origin.x + previous.x * scale, origin.y + previous.y * scale, origin.z + previous.z * scale},
                     {origin.x + current.x * scale, origin.y + current.y * scale, origin.z + current.z * scale},
                     {0x55d6beffU, 2.0f, false});
    }
}

}  // namespace eve::editor
