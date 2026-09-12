#include "action/editor/ActionTimelineScriptBindings.h"

#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionParameterCurve.h"
#include "action/editor/ActionEditorModule.h"
#include "action/editor/ActionPreviewController.h"
#include "action/editor/ActionTimelineEditor.h"
#include "action/editor/ActionTimelineWidget.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPose.h"
#include "animation/MontagePlayer.h"
#include "common/Capability.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorWorkspace.h"
#include "model3d/ModelData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace eve::editor {
namespace {

constexpr const char* kBindingSource = "editor.action.timeline.squirrel";

Status statusFrom(const EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
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

const action::ActionAnimationSection* sectionAt(const action::ActionTimeline& timeline, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= timeline.animationSections.size()) return nullptr;
    return &timeline.animationSections[static_cast<std::size_t>(index)];
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

Value montageAdvanceValue(const animation::MontageAdvance& value) {
    Value::Object projected;
    projected["previousSeconds"]  = value.previous.seconds();
    projected["currentSeconds"]   = value.current.seconds();
    projected["sectionId"]        = value.sectionId ? value.sectionId->format() : "";
    projected["eventCount"]       = static_cast<std::int64_t>(value.events.size());
    projected["activeBlockCount"] = static_cast<std::int64_t>(value.activeBlocks.size());
    projected["rootMotionX"]      = static_cast<double>(value.rootMotion.px);
    projected["rootMotionY"]      = static_cast<double>(value.rootMotion.py);
    projected["rootMotionZ"]      = static_cast<double>(value.rootMotion.pz);
    projected["weight"]           = value.weight;
    projected["completed"]        = value.completed;
    return Value(std::move(projected));
}

void accumulateRootMotion(animation::TransformTRS& total, const animation::TransformTRS& delta) {
    total.px += delta.px;
    total.py += delta.py;
    total.pz += delta.pz;
    const float tx = total.qx;
    const float ty = total.qy;
    const float tz = total.qz;
    const float tw = total.qw;
    total.qx       = tw * delta.qx + tx * delta.qw + ty * delta.qz - tz * delta.qy;
    total.qy       = tw * delta.qy - tx * delta.qz + ty * delta.qw + tz * delta.qx;
    total.qz       = tw * delta.qz + tx * delta.qy - ty * delta.qx + tz * delta.qw;
    total.qw       = tw * delta.qw - tx * delta.qx - ty * delta.qy - tz * delta.qz;
}

class ComposedActionPreviewSink final : public action::IActionPreviewSink {
public:
    void add(std::unique_ptr<action::IActionPreviewSink> sink) { sinks_.push_back(std::move(sink)); }
    [[nodiscard]] bool empty() const noexcept { return sinks_.empty(); }

    Result<void> prepare(const action::ActionPreviewFrame& frame) override {
        std::size_t prepared = 0;
        for (; prepared < sinks_.size(); ++prepared) {
            auto result = sinks_[prepared]->prepare(frame);
            if (result) continue;
            for (std::size_t index = 0; index <= prepared; ++index) sinks_[index]->discardPrepared();
            return Result<void>::failure(result.status());
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    void present(const action::ActionPreviewFrame& frame) noexcept override {
        for (auto& sink : sinks_) sink->present(frame);
    }

    void discardPrepared() noexcept override {
        for (auto& sink : sinks_) sink->discardPrepared();
    }

private:
    std::vector<std::unique_ptr<action::IActionPreviewSink>> sinks_;
};

class ScriptActionTimelineEditor {
public:
    ScriptActionTimelineEditor(std::string targetId, action::ActionTimeline timeline,
                               action::ActionNotifyRegistry registry,
                               std::shared_ptr<ActionTimelineClipboard> clipboard)
        : registry_(std::move(registry)),
          blockRuntime_(registry_),
          editor_(std::move(targetId), std::move(timeline), std::move(clipboard)),
          widget_(editor_, registry_) {
        auto composed = std::make_unique<ComposedActionPreviewSink>();
        const auto providerCount = cap::listenerCount<action::IActionPreviewSinkProvider>();
        for (std::size_t index = 0; index < providerCount; ++index) {
            auto* provider = cap::listenerAt<action::IActionPreviewSinkProvider>(index);
            if (!provider) continue;
            auto sink = provider->createActionPreviewSink();
            if (!sink) {
                previewInitializationFailure_ = sink.status();
                return;
            }
            composed->add(std::move(sink).takeValue());
        }
        if (composed->empty()) return;
        previewSink_       = std::move(composed);
        previewController_ = std::make_unique<ActionPreviewController>(editor_, *previewSink_);
    }

    ~ScriptActionTimelineEditor() {
        if (executionId_.isZero()) return;
        auto interrupted = blockRuntime_.interrupt(runtimeContext(montage_ ? montage_->time() : Duration::zero()));
        interrupted.ignore();
    }

    ActionTimelineEditor& editor() noexcept { return editor_; }
    ActionTimelineWidget& widget() noexcept { return widget_; }

    const ActionTimelineEditor& editor() const noexcept { return editor_; }
    const ActionTimelineWidget& widget() const noexcept { return widget_; }

    [[nodiscard]] bool hasPreviewHost() const noexcept { return previewController_ != nullptr; }

    [[nodiscard]] EditorResult<void> seekPreview(Duration time) {
        if (previewInitializationFailure_)
            return EditorResult<void>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->seek(time) : editor_.seek(time);
    }

    [[nodiscard]] EditorResult<void> seekPreviewAt(float x) { return seekPreview(widget_.timeAt(x)); }

    [[nodiscard]] EditorResult<void> stopPreview() {
        if (previewInitializationFailure_)
            return EditorResult<void>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->stop() : editor_.stop();
    }

    [[nodiscard]] EditorResult<void> stepPreviewFrames(std::int64_t frames, double frameRate) {
        if (previewInitializationFailure_)
            return EditorResult<void>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->stepFrames(frames, frameRate)
                                  : editor_.stepFrames(frames, frameRate);
    }

    [[nodiscard]] EditorResult<void> jumpPreviewToEnd() {
        if (previewInitializationFailure_)
            return EditorResult<void>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->jumpToEnd() : editor_.jumpToEnd();
    }

    [[nodiscard]] EditorResult<std::size_t> updatePreview(Duration delta) {
        if (previewInitializationFailure_)
            return EditorResult<std::size_t>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->update(delta) : editor_.update(delta);
    }

    [[nodiscard]] EditorResult<void> refreshPreview() {
        if (previewInitializationFailure_)
            return EditorResult<void>::failure(*previewInitializationFailure_);
        return previewController_ ? previewController_->refresh() : eve::editing::noOp();
    }

    [[nodiscard]] Result<void> registerRuntimeClip(std::string uri, model3d::ModelData& model,
                                                   animation::AnimSkeleton& skeleton, int animationIndex) {
        if (uri.empty())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "montage clip URI must not be empty", "uri"));
        if (animationIndex < 0 || animationIndex >= animation::AnimImporter::getAnimationCountFromModel(&model))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "montage animation index is outside the model", "index"));
        std::unique_ptr<animation::AnimClip> clip(
            animation::AnimImporter::loadClipFromModel(&model, &skeleton, animationIndex));
        if (!clip)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "montage clip import produced no animation", uri));
        if (montage_) return montage_->replaceClip(uri, std::move(clip));
        const auto found = std::find_if(runtimeClips_.begin(), runtimeClips_.end(),
                                        [&](const auto& asset) { return asset.uri == uri; });
        if (found == runtimeClips_.end())
            runtimeClips_.push_back({std::move(uri), std::move(clip)});
        else
            found->clip = std::move(clip);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> beginRuntime(animation::AnimSkeleton& skeleton) {
        auto montage  = std::make_unique<animation::MontagePlayer>(skeleton);
        auto timeline = editor_.target().timeline();
        auto prepared = montage->prepare(timeline, std::move(runtimeClips_));
        if (!prepared) return Result<void>::failure(prepared.status());
        runtimeTimeline_ = timeline;
        montage_         = std::move(montage);
        tick_            = SimulationTick::zero();
        runtimeAdvance_.reset();
        runtimeRate_   = timeline.montage.basePlayRate;
        runtimePaused_ = false;
        sectionRates_.clear();
        return restartExecution();
    }

    [[nodiscard]] Result<animation::MontageAdvance> advanceRuntime(Duration delta) {
        if (!runtime_ || !montage_)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        if (montage_->isBlendingOut()) {
            tick_        = SimulationTick(tick_.value() + 1);
            auto settled = montage_->advanceBlendOut(delta, tick_);
            if (settled) runtimeAdvance_ = settled.value();
            return settled;
        }
        if (runtimePaused_) {
            animation::MontageAdvance paused;
            paused.previous = paused.current = montage_->time();
            paused.weight                    = montage_->weight();
            return Result<animation::MontageAdvance>::success(std::move(paused), Status::success(StatusCode::NoOp));
        }
        double sectionRate  = 1.0;
        auto   sectionIndex = montage_->physicalSectionIndex();
        if (sectionIndex) {
            const auto found = sectionRates_.find(sectionIndex.value());
            if (found != sectionRates_.end()) sectionRate = found->second;
        }
        auto scaledDelta = Duration::fromSeconds(delta.seconds() * runtimeRate_ * sectionRate);
        if (!scaledDelta) return Result<animation::MontageAdvance>::failure(scaledDelta.status());
        Duration                  remainingDelta = std::move(scaledDelta).takeValue();
        animation::MontageAdvance combined;
        combined.previous = montage_->time();
        bool firstSlice   = true;
        while (true) {
            Duration remainingTimeline =
                Duration::fromNanoseconds(runtimeTimeline_->duration.nanoseconds() - montage_->time().nanoseconds());
            if (runtimeTimeline_->montage.looping && remainingTimeline.isZero()) {
                auto restarted = restartExecution();
                if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
                remainingTimeline = runtimeTimeline_->duration;
            }
            const Duration slice =
                runtimeTimeline_->montage.looping ? std::min(remainingDelta, remainingTimeline) : remainingDelta;
            tick_              = SimulationTick(tick_.value() + 1);
            auto actionAdvance = runtime_->advance(executionId_, tick_, slice);
            if (!actionAdvance) return Result<animation::MontageAdvance>::failure(actionAdvance.status());
            auto routed = routeAvailableBlocks(actionAdvance.value());
            if (!routed) return Result<animation::MontageAdvance>::failure(routed.status());
            auto presented = montage_->present(actionAdvance.value(), tick_);
            if (!presented) return Result<animation::MontageAdvance>::failure(presented.status());
            auto value = std::move(presented).takeValue();
            if (firstSlice) {
                combined   = std::move(value);
                firstSlice = false;
            } else {
                combined.current   = value.current;
                combined.sectionId = value.sectionId;
                combined.events.insert(combined.events.end(), std::make_move_iterator(value.events.begin()),
                                       std::make_move_iterator(value.events.end()));
                combined.activeBlocks = std::move(value.activeBlocks);
                accumulateRootMotion(combined.rootMotion, value.rootMotion);
                combined.weight    = value.weight;
                combined.completed = value.completed;
            }
            if (!runtimeTimeline_->montage.looping || remainingDelta <= slice) break;

            remainingDelta = Duration::fromNanoseconds(remainingDelta.nanoseconds() - slice.nanoseconds());
            auto restarted = restartExecution();
            if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
        }
        if (runtimeTimeline_->montage.looping && combined.completed) {
            auto restarted = restartExecution();
            if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
            combined.current = Duration::zero();
            combined.sectionId.reset();
            combined.activeBlocks.clear();
            combined.completed = false;
            combined.weight    = montage_->weight();
        }
        runtimeAdvance_ = combined;
        return Result<animation::MontageAdvance>::success(std::move(combined), Status::success(StatusCode::Pending));
    }

    [[nodiscard]] Result<animation::MontageAdvance> jumpRuntime(Duration target) {
        if (!runtimeTimeline_ || !montage_)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        auto restarted = restartExecution(false);
        if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
        auto rebound = montage_->rebindExecution(executionId_);
        if (!rebound) return Result<animation::MontageAdvance>::failure(rebound.status());
        tick_         = SimulationTick(tick_.value() + 1);
        auto advanced = runtime_->advance(executionId_, tick_, target);
        if (!advanced) return Result<animation::MontageAdvance>::failure(advanced.status());
        auto routed = routeAvailableBlocks(advanced.value());
        if (!routed) return Result<animation::MontageAdvance>::failure(routed.status());
        auto jumped = montage_->jumpToTime(executionId_, target, tick_);
        if (jumped) runtimeAdvance_ = jumped.value();
        return jumped;
    }

    [[nodiscard]] Result<animation::MontageAdvance> jumpRuntimeSection(std::size_t index) {
        if (!runtimeTimeline_)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        auto range = runtimeTimeline_->sectionRange(index);
        if (!range) return Result<animation::MontageAdvance>::failure(range.status());
        return jumpRuntime(range.value().first);
    }

    [[nodiscard]] Result<void> syncRuntimeSection(std::size_t index, Duration targetDuration) {
        if (!runtimeTimeline_)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        if (targetDuration <= Duration::zero())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "target section duration must be positive", "duration"));
        auto range = runtimeTimeline_->sectionRange(index);
        if (!range) return Result<void>::failure(range.status());
        const double raw     = (range.value().second.seconds() - range.value().first.seconds());
        sectionRates_[index] = raw / targetDuration.seconds();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<animation::MontageAdvance> beginRuntimeBlendOut(Duration duration) {
        if (!montage_)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        tick_        = SimulationTick(tick_.value() + 1);
        auto stopped = montage_->beginBlendOut(duration, tick_);
        if (stopped) runtimeAdvance_ = stopped.value();
        return stopped;
    }

    void               setRuntimePaused(bool paused) noexcept { runtimePaused_ = paused; }
    [[nodiscard]] bool runtimePaused() const noexcept { return runtimePaused_; }

    [[nodiscard]] Result<void> cancelRuntime() {
        if (!runtime_ || !montage_) return Result<void>::success(Status::success(StatusCode::NoOp));
        tick_          = SimulationTick(tick_.value() + 1);
        auto cancelled = runtime_->cancel(executionId_, tick_);
        if (!cancelled) return cancelled;
        auto interrupted = blockRuntime_.interrupt(runtimeContext(montage_->time()));
        if (!interrupted) return interrupted;
        montage_->stop();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] animation::AnimPose* runtimePose() noexcept { return montage_ ? &montage_->pose() : nullptr; }
    [[nodiscard]] const std::optional<animation::MontageAdvance>& runtimeAdvance() const noexcept {
        return runtimeAdvance_;
    }
    [[nodiscard]] bool runtimePlaying() const noexcept { return montage_ && montage_->isPlaying(); }

    [[nodiscard]] Result<std::size_t> runtimePhysicalSection() const {
        if (!montage_)
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return montage_->physicalSectionIndex();
    }

    [[nodiscard]] Result<double> runtimeSectionProgress(std::size_t index) const {
        if (!montage_)
            return Result<double>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return montage_->physicalSectionProgress(index);
    }

private:
    [[nodiscard]] action::ActionDefinition runtimeDefinition() const {
        action::ActionDefinition definition;
        definition.id = runtimeTimeline_->actionId;
        if (runtimeTimeline_->splitTimestamps.size() >= 2) {
            definition.timing.windup  = runtimeTimeline_->splitTimestamps[0];
            definition.timing.active  = Duration::fromNanoseconds(runtimeTimeline_->splitTimestamps[1].nanoseconds() -
                                                                  runtimeTimeline_->splitTimestamps[0].nanoseconds());
            definition.timing.recover = Duration::fromNanoseconds(runtimeTimeline_->duration.nanoseconds() -
                                                                  runtimeTimeline_->splitTimestamps[1].nanoseconds());
        } else {
            definition.timing.active = runtimeTimeline_->duration;
        }
        definition.timeline = *runtimeTimeline_;
        return definition;
    }

    [[nodiscard]] Result<void> restartExecution(bool playMontage = true) {
        if (!executionId_.isZero()) {
            auto interrupted = blockRuntime_.interrupt(runtimeContext(montage_ ? montage_->time() : Duration::zero()));
            if (!interrupted) return interrupted;
        }
        auto                  runtime    = std::make_unique<action::ActionRuntime>();
        auto                  definition = runtimeDefinition();
        action::ActionRequest request;
        request.actionId = definition.id;
        auto submitted   = runtime->submit(std::move(definition), std::move(request));
        if (!submitted) return Result<void>::failure(submitted.status());
        if (playMontage) {
            auto played = montage_->play(submitted.value());
            if (!played) return Result<void>::failure(played.status());
        }
        runtime_     = std::move(runtime);
        executionId_ = submitted.value();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] action::ActionNotifyContext runtimeContext(Duration time) const {
        action::ActionNotifyContext context;
        context.executionId = executionId_;
        context.time        = time;
        return context;
    }

    [[nodiscard]] Result<void> routeAvailableBlocks(const action::ActionAdvance& advance) {
        action::ActionAdvance handled;
        handled.id           = advance.id;
        handled.phase        = advance.phase;
        handled.phaseElapsed = advance.phaseElapsed;
        handled.totalElapsed = advance.totalElapsed;
        for (const auto& event : advance.timelineEvents)
            if (registry_.hasHandler(event.type.format())) handled.timelineEvents.push_back(event);
        for (const auto& block : advance.activeBlocks)
            if (registry_.hasHandler(block.type.format())) handled.activeBlocks.push_back(block);
        return blockRuntime_.apply(handled, runtimeContext(advance.totalElapsed));
    }

    action::ActionNotifyRegistry registry_;
    action::ActionBlockRuntime   blockRuntime_;
    ActionTimelineEditor         editor_;
    ActionTimelineWidget         widget_;
    std::unique_ptr<action::IActionPreviewSink> previewSink_;
    std::unique_ptr<ActionPreviewController>    previewController_;
    std::optional<Status>                       previewInitializationFailure_;
    std::vector<animation::MontageClipAsset>  runtimeClips_;
    std::unique_ptr<action::ActionRuntime>    runtime_;
    std::unique_ptr<animation::MontagePlayer> montage_;
    action::ActionExecutionId                 executionId_{};
    SimulationTick                            tick_ = SimulationTick::zero();
    std::optional<animation::MontageAdvance>  runtimeAdvance_;
    std::optional<action::ActionTimeline>     runtimeTimeline_;
    std::unordered_map<std::size_t, double>   sectionRates_;
    double                                    runtimeRate_   = 1.0;
    bool                                      runtimePaused_ = false;
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

void exposeActionTimelineScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm           = table.getHandle();
    auto              clipboard    = std::make_shared<ActionTimelineClipboard>();
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
    actionEditor.addFunc("addTrack", [vm](ScriptActionTimelineEditor* self, const std::string& trackId,
                                          const std::string& label, const std::string& kind) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto parsedId   = LogicalId::parse(trackId);
        auto parsedKind = action::parseActionTrackKind(kind);
        if (!parsedId) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
        if (!parsedKind) return script::projectStatusResult(vm, parsedKind.status(), false, false);
        action::ActionTrack track;
        track.id    = std::move(*parsedId);
        track.label = label;
        track.kind  = parsedKind.value();
        return project(vm, self->editor().addTrack(std::move(track)));
    });
    actionEditor.addFunc("removeTrack", [vm](ScriptActionTimelineEditor* self, const std::string& trackId) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto parsed = LogicalId::parse(trackId);
        if (!parsed) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
        return project(vm, self->editor().removeTrack(*parsed));
    });
    actionEditor.addFunc(
        "renameTrack", [vm](ScriptActionTimelineEditor* self, const std::string& trackId, const std::string& label) {
            if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
            auto parsed = LogicalId::parse(trackId);
            if (!parsed) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
            return project(vm, self->editor().renameTrack(*parsed, label));
        });
    actionEditor.addFunc(
        "setTrackMuted", [vm](ScriptActionTimelineEditor* self, const std::string& trackId, bool muted) {
            if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
            auto parsed = LogicalId::parse(trackId);
            if (!parsed) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
            return project(vm, self->editor().setTrackMuted(*parsed, muted));
        });
    actionEditor.addFunc(
        "setTrackLocked", [vm](ScriptActionTimelineEditor* self, const std::string& trackId, bool locked) {
            if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
            auto parsed = LogicalId::parse(trackId);
            if (!parsed) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
            return project(vm, self->editor().setTrackLocked(*parsed, locked));
        });
    actionEditor.addFunc("copyTrack", [vm](ScriptActionTimelineEditor* self, const std::string& trackId) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto parsed = LogicalId::parse(trackId);
        if (!parsed) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
        return project(vm, self->editor().copyTrack(*parsed));
    });
    actionEditor.addFunc("pasteTrack", [vm](ScriptActionTimelineEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto pasted = self->editor().pasteTrack();
        return project(vm, pasted, pasted.ok() ? Value(pasted.value().format()) : Value{});
    });
    actionEditor.addFunc(
        "pasteSelectionToTrack",
        [vm](ScriptActionTimelineEditor* self, const std::string& trackId, float offsetSeconds) {
            if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
            auto parsed = LogicalId::parse(trackId);
            auto offset = seconds(offsetSeconds);
            if (!parsed)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
            if (!offset) return script::projectStatusResult(vm, offset.status(), false, false);
            auto pasted = self->editor().pasteToTrack(*parsed, offset.value());
            return project(vm, pasted,
                           pasted.ok() ? Value(static_cast<std::int64_t>(pasted.value())) : Value{});
        });
    actionEditor.addFunc("setSnapSeconds", [vm](ScriptActionTimelineEditor* self, float intervalSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto interval = seconds(intervalSeconds);
        if (!interval) return script::projectStatusResult(vm, interval.status(), false, false);
        return project(vm, self->widget().setSnapInterval(std::move(interval).takeValue()));
    });
    actionEditor.addFunc(
        "setVisibleSeconds", [vm](ScriptActionTimelineEditor* self, float startSeconds, float endSeconds) {
            if (!self)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
            auto start = seconds(startSeconds);
            auto end   = seconds(endSeconds);
            if (!start) return script::projectStatusResult(vm, start.status(), false, false);
            if (!end) return script::projectStatusResult(vm, end.status(), false, false);
            return project(vm, self->widget().setVisibleRange(start.value(), end.value()));
        });
    actionEditor.addFunc("zoomTimeline", [vm](ScriptActionTimelineEditor* self, float factor, float anchor) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->widget().zoom(factor, anchor));
    });
    actionEditor.addFunc("panTimelineSeconds", [vm](ScriptActionTimelineEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto delta = seconds(deltaSeconds);
        if (!delta) return script::projectStatusResult(vm, delta.status(), false, false);
        return project(vm, self->widget().pan(delta.value()));
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
        return project(vm, self->seekPreviewAt(x));
    });
    actionEditor.addFunc("seekSeconds", [vm](ScriptActionTimelineEditor* self, float value) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto duration = seconds(value);
        if (!duration) return script::projectStatusResult(vm, duration.status(), false, false);
        return project(vm, self->seekPreview(std::move(duration).takeValue()));
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
    actionEditor.addFunc("addParameterKey", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                  float time, float value, float inTangent, float outTangent,
                                                  const std::string& interpolation) {
        auto parsedId = LogicalId::parse(itemId);
        auto parsedInterpolation = action::actionParameterInterpolationFromName(interpolation);
        if (!self || !parsedId || !parsedInterpolation)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "parameter item id or interpolation is invalid", "parameterKey");
        return project(vm, self->editor().addParameterKey(
                               *parsedId, {time, value, inTangent, outTangent, *parsedInterpolation}));
    });
    actionEditor.addFunc("editParameterKey", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                   int index, float time, float value, float inTangent,
                                                   float outTangent, const std::string& interpolation) {
        auto parsedId = LogicalId::parse(itemId);
        auto parsedInterpolation = action::actionParameterInterpolationFromName(interpolation);
        if (!self || !parsedId || index < 0 || !parsedInterpolation)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "parameter key identity or interpolation is invalid", "parameterKey");
        return project(vm, self->editor().editParameterKey(
                               *parsedId, static_cast<std::size_t>(index),
                               {time, value, inTangent, outTangent, *parsedInterpolation}));
    });
    actionEditor.addFunc("removeParameterKey", [vm](ScriptActionTimelineEditor* self,
                                                     const std::string& itemId, int index) {
        auto parsedId = LogicalId::parse(itemId);
        if (!self || !parsedId || index < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "parameter key identity is invalid", "parameterKey");
        return project(vm, self->editor().removeParameterKey(*parsedId, static_cast<std::size_t>(index)));
    });
    actionEditor.addFunc("undo", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    actionEditor.addFunc("redo", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    actionEditor.addFunc("update", [vm](ScriptActionTimelineEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto delta = seconds(deltaSeconds);
        if (!delta) return script::projectStatusResult(vm, delta.status(), false, false);
        auto result = self->updatePreview(std::move(delta).takeValue());
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value()) : 0));
    });
    actionEditor.addFunc("refreshPreview", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return project(vm, self->refreshPreview());
    });
    actionEditor.addFunc("hasPreviewHost", [](ScriptActionTimelineEditor* self) {
        return self && self->hasPreviewHost();
    });
    actionEditor.addFunc("snapshot", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->editor().target().timeline().toValue(),
                                     [](Value value) { return value; });
    });
    actionEditor.addFunc(
        "registerRuntimeClip", [vm](ScriptActionTimelineEditor* self, const std::string& uri, model3d::ModelData* model,
                                    animation::AnimSkeleton* skeleton, int animationIndex) {
            if (!self || !model || !skeleton)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "editor, model and skeleton must not be null", "runtimeClip");
            return script::projectResult(vm, self->registerRuntimeClip(uri, *model, *skeleton, animationIndex));
        });
    actionEditor.addFunc("beginRuntime", [vm](ScriptActionTimelineEditor* self, animation::AnimSkeleton* skeleton) {
        if (!self || !skeleton)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "editor and skeleton must not be null",
                                  "skeleton");
        return script::projectResult(vm, self->beginRuntime(*skeleton));
    });
    actionEditor.addFunc("advanceRuntime", [vm](ScriptActionTimelineEditor* self, float deltaSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto delta = seconds(deltaSeconds);
        if (!delta) return script::projectStatusResult(vm, delta.status(), false, false);
        return script::projectResult(vm, self->advanceRuntime(std::move(delta).takeValue()), montageAdvanceValue);
    });
    actionEditor.addFunc("jumpRuntimeSeconds", [vm](ScriptActionTimelineEditor* self, float targetSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto target = seconds(targetSeconds);
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return script::projectResult(vm, self->jumpRuntime(std::move(target).takeValue()), montageAdvanceValue);
    });
    actionEditor.addFunc("jumpRuntimeSection", [vm](ScriptActionTimelineEditor* self, int sectionIndex) {
        if (!self || sectionIndex < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "runtime section index is invalid", "index");
        return script::projectResult(vm, self->jumpRuntimeSection(static_cast<std::size_t>(sectionIndex)),
                                     montageAdvanceValue);
    });
    actionEditor.addFunc(
        "syncRuntimeSection", [vm](ScriptActionTimelineEditor* self, int sectionIndex, float targetSeconds) {
            if (!self || sectionIndex < 0)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "runtime section index is invalid", "index");
            auto target = seconds(targetSeconds);
            if (!target) return script::projectStatusResult(vm, target.status(), false, false);
            return script::projectResult(
                vm, self->syncRuntimeSection(static_cast<std::size_t>(sectionIndex), std::move(target).takeValue()));
        });
    actionEditor.addFunc("beginRuntimeBlendOut", [vm](ScriptActionTimelineEditor* self, float durationSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto duration = seconds(durationSeconds);
        if (!duration) return script::projectStatusResult(vm, duration.status(), false, false);
        return script::projectResult(vm, self->beginRuntimeBlendOut(std::move(duration).takeValue()),
                                     montageAdvanceValue);
    });
    actionEditor.addFunc("setRuntimePaused", [](ScriptActionTimelineEditor* self, bool paused) {
        if (self) self->setRuntimePaused(paused);
    });
    actionEditor.addFunc("isRuntimePaused",
                         [](ScriptActionTimelineEditor* self) { return self && self->runtimePaused(); });
    actionEditor.addFunc("cancelRuntime", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->cancelRuntime());
    });
    actionEditor.addFunc("getRuntimePose",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimePose() : nullptr; });
    actionEditor.addFunc("isRuntimePlaying",
                         [](ScriptActionTimelineEditor* self) { return self && self->runtimePlaying(); });
    actionEditor.addFunc("getRuntimeSectionId", [](ScriptActionTimelineEditor* self) {
        if (!self || !self->runtimeAdvance() || !self->runtimeAdvance()->sectionId) return std::string{};
        return self->runtimeAdvance()->sectionId->format();
    });
    actionEditor.addFunc("getRuntimeRootMotionX", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? self->runtimeAdvance()->rootMotion.px : 0.0f;
    });
    actionEditor.addFunc("getRuntimeRootMotionY", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? self->runtimeAdvance()->rootMotion.py : 0.0f;
    });
    actionEditor.addFunc("getRuntimeRootMotionZ", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? self->runtimeAdvance()->rootMotion.pz : 0.0f;
    });
    actionEditor.addFunc("getRuntimeWeight", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? static_cast<float>(self->runtimeAdvance()->weight) : 0.0f;
    });
    actionEditor.addFunc("getRuntimeEventCount", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? static_cast<int>(self->runtimeAdvance()->events.size()) : 0;
    });
    actionEditor.addFunc("getRuntimeEventKind", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->events.size())
            return std::string{};
        return eventKind(self->runtimeAdvance()->events[static_cast<std::size_t>(index)].kind);
    });
    actionEditor.addFunc("getRuntimeEventType", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->events.size())
            return std::string{};
        return self->runtimeAdvance()->events[static_cast<std::size_t>(index)].type.format();
    });
    actionEditor.addFunc("getRuntimeEventSeconds", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->events.size())
            return 0.0f;
        return static_cast<float>(self->runtimeAdvance()->events[static_cast<std::size_t>(index)].time.seconds());
    });
    actionEditor.addFunc("getRuntimeActiveBlockCount", [](ScriptActionTimelineEditor* self) {
        return self && self->runtimeAdvance() ? static_cast<int>(self->runtimeAdvance()->activeBlocks.size()) : 0;
    });
    actionEditor.addFunc("getRuntimeActiveBlockType", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->activeBlocks.size())
            return std::string{};
        return self->runtimeAdvance()->activeBlocks[static_cast<std::size_t>(index)].type.format();
    });
    actionEditor.addFunc("getRuntimeActiveBlockLocalSeconds", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->activeBlocks.size())
            return 0.0f;
        return static_cast<float>(
            self->runtimeAdvance()->activeBlocks[static_cast<std::size_t>(index)].localTime.seconds());
    });
    actionEditor.addFunc("getRuntimePhysicalSection", [](ScriptActionTimelineEditor* self) {
        if (!self) return -1;
        auto result = self->runtimePhysicalSection();
        return result ? static_cast<int>(result.value()) : -1;
    });
    actionEditor.addFunc("getRuntimeSectionProgress", [](ScriptActionTimelineEditor* self, int sectionIndex) {
        if (!self || sectionIndex < 0) return 0.0f;
        auto result = self->runtimeSectionProgress(static_cast<std::size_t>(sectionIndex));
        return result ? static_cast<float>(result.value()) : 0.0f;
    });

    actionEditor.addFunc("play", [](ScriptActionTimelineEditor* self) {
        if (self) self->editor().play();
    });
    actionEditor.addFunc("pause", [](ScriptActionTimelineEditor* self) {
        if (self) self->editor().pause();
    });
    actionEditor.addFunc("stop", [vm](ScriptActionTimelineEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        return project(vm, self->stopPreview());
    });
    actionEditor.addFunc("stepFrames", [vm](ScriptActionTimelineEditor* self, int frames, float frameRate) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        return project(vm, self->stepPreviewFrames(frames, frameRate));
    });
    actionEditor.addFunc("jumpToEnd", [vm](ScriptActionTimelineEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        return project(vm, self->jumpPreviewToEnd());
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
    actionEditor.addFunc("getAnimationSectionCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->editor().target().timeline().animationSections.size()) : 0;
    });
    actionEditor.addFunc("getAnimationSectionId", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? section->id.format() : std::string{};
    });
    actionEditor.addFunc("getAnimationSectionUri", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? section->animationUri : std::string{};
    });
    actionEditor.addFunc("getAnimationSectionStart", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? static_cast<float>(section->start.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getAnimationSectionEnd", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? static_cast<float>(section->end.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getAnimationSectionBlendIn", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? static_cast<float>(section->blendIn.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getAnimationSectionSourceStart", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? static_cast<float>(section->sourceStart.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getAnimationSectionSourceEnd", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? static_cast<float>(section->sourceEnd.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getAnimationSectionBlendCurve", [](ScriptActionTimelineEditor* self, int index) {
        const auto* section = self ? sectionAt(self->editor().target().timeline(), index) : nullptr;
        return section ? std::string(action::actionBlendCurveName(section->blendCurve)) : std::string{};
    });
    actionEditor.addFunc("addAnimationSection", [vm](ScriptActionTimelineEditor* self, const std::string& sectionId,
                                                     const std::string& animationUri, float startSeconds,
                                                     float endSeconds, float blendInSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsed = LogicalId::parse(sectionId);
        if (!parsed)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation section id is not canonical",
                                  "sectionId");
        auto start = seconds(startSeconds);
        auto end   = seconds(endSeconds);
        auto blend = seconds(blendInSeconds);
        if (!start) return script::projectStatusResult(vm, start.status(), false, false);
        if (!end) return script::projectStatusResult(vm, end.status(), false, false);
        if (!blend) return script::projectStatusResult(vm, blend.status(), false, false);
        action::ActionAnimationSection section{*parsed, animationUri, start.value(), end.value(), blend.value()};
        return project(vm, self->editor().addAnimationSection(std::move(section)));
    });
    actionEditor.addFunc("editAnimationSection", [vm](ScriptActionTimelineEditor* self, const std::string& sectionId,
                                                      float startSeconds, float endSeconds, float blendInSeconds,
                                                      const std::string& animationUri) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsed = LogicalId::parse(sectionId);
        if (!parsed)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation section id is not canonical",
                                  "sectionId");
        auto start = seconds(startSeconds);
        auto end   = seconds(endSeconds);
        auto blend = seconds(blendInSeconds);
        if (!start) return script::projectStatusResult(vm, start.status(), false, false);
        if (!end) return script::projectStatusResult(vm, end.status(), false, false);
        if (!blend) return script::projectStatusResult(vm, blend.status(), false, false);
        return project(
            vm, self->editor().editAnimationSection(*parsed, start.value(), end.value(), blend.value(), animationUri));
    });
    actionEditor.addFunc(
        "removeAnimationSection", [vm](ScriptActionTimelineEditor* self, const std::string& sectionId) {
            if (!self)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
            auto parsed = LogicalId::parse(sectionId);
            if (!parsed)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument, "animation section id is not canonical",
                                      "sectionId");
            return project(vm, self->editor().removeAnimationSection(*parsed));
        });
    actionEditor.addFunc("editAnimationSectionSource", [vm](ScriptActionTimelineEditor* self,
                                                            const std::string& sectionId, float sourceStartSeconds,
                                                            float sourceEndSeconds, const std::string& blendCurve) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsed = LogicalId::parse(sectionId);
        auto curve  = action::actionBlendCurveFromName(blendCurve);
        if (!parsed || !curve)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "section id or blend curve is invalid",
                                  "sectionSource");
        auto sourceStart = seconds(sourceStartSeconds);
        auto sourceEnd   = seconds(sourceEndSeconds);
        if (!sourceStart) return script::projectStatusResult(vm, sourceStart.status(), false, false);
        if (!sourceEnd) return script::projectStatusResult(vm, sourceEnd.status(), false, false);
        return project(
            vm, self->editor().editAnimationSectionSource(*parsed, sourceStart.value(), sourceEnd.value(), *curve));
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
    actionEditor.addFunc("getRulerTickCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->widget().layout().rulerTicks.size()) : 0;
    });
    actionEditor.addFunc("getRulerTickX", [](ScriptActionTimelineEditor* self, int index) {
        const auto layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        return index >= 0 && static_cast<std::size_t>(index) < layout.rulerTicks.size()
                   ? layout.rulerTicks[static_cast<std::size_t>(index)].x
                   : 0.0f;
    });
    actionEditor.addFunc("getRulerTickMajor", [](ScriptActionTimelineEditor* self, int index) {
        const auto layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        return index >= 0 && static_cast<std::size_t>(index) < layout.rulerTicks.size() &&
               layout.rulerTicks[static_cast<std::size_t>(index)].major;
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
    actionEditor.addFunc("getParameterKeyCount", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        if (!state || state->type.format() != "presentation:parameter-curve") return 0;
        auto binding = action::ActionParameterCurveBinding::fromPayload(state->payload);
        return binding ? static_cast<int>(binding.value().keys.size()) : 0;
    });
    actionEditor.addFunc("getParameterKeyTime", [](ScriptActionTimelineEditor* self,
                                                   const std::string& itemId, int index) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        if (!state || index < 0) return 0.0f;
        auto binding = action::ActionParameterCurveBinding::fromPayload(state->payload);
        return binding && static_cast<std::size_t>(index) < binding.value().keys.size()
                   ? static_cast<float>(binding.value().keys[static_cast<std::size_t>(index)].time)
                   : 0.0f;
    });
    actionEditor.addFunc("getParameterKeyValue", [](ScriptActionTimelineEditor* self,
                                                    const std::string& itemId, int index) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        if (!state || index < 0) return 0.0f;
        auto binding = action::ActionParameterCurveBinding::fromPayload(state->payload);
        return binding && static_cast<std::size_t>(index) < binding.value().keys.size()
                   ? static_cast<float>(binding.value().keys[static_cast<std::size_t>(index)].value)
                   : 0.0f;
    });
    actionEditor.addFunc("getParameterKeyInterpolation", [](ScriptActionTimelineEditor* self,
                                                            const std::string& itemId, int index) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        if (!state || index < 0) return std::string{};
        auto binding = action::ActionParameterCurveBinding::fromPayload(state->payload);
        return binding && static_cast<std::size_t>(index) < binding.value().keys.size()
                   ? std::string(action::actionParameterInterpolationName(
                         binding.value().keys[static_cast<std::size_t>(index)].interpolation))
                   : std::string{};
    });
    actionEditor.addFunc("sampleParameterCurve", [](ScriptActionTimelineEditor* self,
                                                    const std::string& itemId, float progress) {
        const auto* state = self ? findState(self->editor().target().timeline(), itemId) : nullptr;
        if (!state || !std::isfinite(progress)) return 0.0f;
        auto binding = action::ActionParameterCurveBinding::fromPayload(state->payload);
        return binding ? static_cast<float>(binding.value().sample(progress)) : 0.0f;
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

    moduleClass.addFunc("create", [vm, clipboard](eve::action_editor::ActionEditorModule*, const std::string& targetId,
                                                  const ssq::Object& timelineObject) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline target id must not be empty",
                                  "targetId");
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
                                                             std::move(registry).takeValue(), clipboard));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned  = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::editor
