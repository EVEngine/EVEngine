#include "action/editor/ActionTimelineScriptBindings.h"

#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionParameterCurve.h"
#include "action/editor/ActionEditorModule.h"
#include "action/editor/ActionPreviewController.h"
#include "action/editor/ActionTimelineEditor.h"
#include "action/editor/ActionTimelinePayloadEditor.h"
#include "action/editor/ActionTimelineWidget.h"
#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimLayerMixer.h"
#include "animation/AnimPose.h"
#include "animation/MontageCoordinator.h"
#include "animation/MontagePlayer.h"
#include "common/Capability.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorAssetDatabase.h"
#include "editor/EditorDiskAssetCatalog.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorWorkspace.h"
#include "model3d/ModelData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <set>
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

const action::ActionNotify* findNotify(const action::ActionTimeline& timeline, const std::string& itemId) {
    for (const auto& track : timeline.tracks) {
        const auto found = std::find_if(track.notifies.begin(), track.notifies.end(),
                                        [&](const auto& notify) { return notify.id.format() == itemId; });
        if (found != track.notifies.end()) return &*found;
    }
    return nullptr;
}

const action::ActionAnimationSection* findSection(const action::ActionTimeline& timeline, const std::string& itemId) {
    const auto found = std::find_if(timeline.animationSections.begin(), timeline.animationSections.end(),
                                    [&](const auto& section) { return section.id.format() == itemId; });
    return found == timeline.animationSections.end() ? nullptr : &*found;
}

class ScriptActionTimelineAssetCatalog {
public:
    explicit ScriptActionTimelineAssetCatalog(std::filesystem::path projectRoot)
        : store_(projectRoot), catalog_(std::move(projectRoot), &database_) {}

    [[nodiscard]] EditorResult<std::size_t> refresh(std::string text) {
        auto scanned = catalog_.scan();
        if (!scanned.ok()) return EditorResult<std::size_t>::failure(scanned.status());
        AssetQuery query;
        query.typeIds = {"eve.action.timeline"};
        query.text    = std::move(text);
        auto page     = database_.query(query, 0, 512);
        if (!page.ok()) return EditorResult<std::size_t>::failure(page.status());

        entries_.clear();
        auto diagnostics = scanned.diagnostics();
        for (const auto& record : page.value().values) {
            auto stored = store_.read(record.logicalUri);
            if (!stored.ok()) {
                diagnostics.insert(diagnostics.end(), stored.diagnostics().begin(), stored.diagnostics().end());
                continue;
            }
            auto timeline = action::ActionTimeline::fromValue(toPresentationValue(stored.value().content));
            if (!timeline) {
                diagnostics.insert(diagnostics.end(), timeline.diagnostics().begin(), timeline.diagnostics().end());
                continue;
            }
            entries_.push_back(record);
        }
        generation_ = page.value().generation;
        return eve::editing::applied<std::size_t>(entries_.size(), std::move(diagnostics));
    }

    [[nodiscard]] EditorResult<void> registerDocument(const std::string& guid, const std::string& uri) {
        if (guid.empty() || !uri.starts_with("content://") || uri.size() <= std::string("content://").size())
            return eve::editing::failed<void>(EditorStatus::Rejected,
                                              RuleId("editor.action.asset-registration-invalid"),
                                              "Action asset registration requires a GUID and content URI");
        auto stored = store_.read(uri);
        if (!stored.ok()) return EditorResult<void>::failure(stored.status());
        auto timeline = action::ActionTimeline::fromValue(toPresentationValue(stored.value().content));
        if (!timeline) return EditorResult<void>::failure(timeline.status());
        return catalog_.writeSidecar(uri.substr(std::string("content://").size()), AssetGuid(guid),
                                     "eve.action.timeline");
    }

    [[nodiscard]] int count() const noexcept { return static_cast<int>(entries_.size()); }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::string guid(int index) const {
        const auto* entry = at(index);
        return entry ? entry->guid.value() : std::string{};
    }
    [[nodiscard]] std::string uri(int index) const {
        const auto* entry = at(index);
        return entry ? entry->logicalUri : std::string{};
    }
    [[nodiscard]] std::string title(int index) const {
        const auto* entry = at(index);
        if (!entry) return {};
        return std::filesystem::path(entry->logicalUri.substr(std::string("content://").size())).stem().string();
    }

private:
    [[nodiscard]] const AssetRecord* at(int index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= entries_.size()) return nullptr;
        return &entries_[static_cast<std::size_t>(index)];
    }

    MemoryAssetDatabase      database_;
    DiskAtomicDocumentStore  store_;
    DiskAssetCatalog         catalog_;
    std::vector<AssetRecord> entries_;
    std::uint64_t            generation_ = 0;
};

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
        const auto* montage = activeMontage();
        auto interrupted = blockRuntime_.interrupt(runtimeContext(montage ? montage->time() : Duration::zero()));
        interrupted.ignore();
    }

    ActionTimelineEditor& editor() noexcept { return editor_; }
    ActionTimelineWidget& widget() noexcept { return widget_; }

    const ActionTimelineEditor& editor() const noexcept { return editor_; }
    const ActionTimelineWidget& widget() const noexcept { return widget_; }
    const action::ActionNotifyRegistry& registry() const noexcept { return registry_; }

    [[nodiscard]] bool hasPreviewHost() const noexcept { return previewController_ != nullptr; }

    void attachDocument(std::unique_ptr<DiskAtomicDocumentStore> store,
                        std::unique_ptr<DocumentService> documents, DocumentSnapshot snapshot) {
        documentStore_              = std::move(store);
        documents_                  = std::move(documents);
        document_                   = std::move(snapshot);
        synchronizedEditorRevision_ = editor_.target().revision();
        savedEditorRevision_        = editor_.target().revision();
    }

    [[nodiscard]] bool documentDirty() const {
        if (!documents_ || document_.id.empty()) return false;
        auto snapshot = documents_->snapshot(document_.id);
        return !snapshot.ok() || snapshot.value().dirty() || editor_.target().revision() != savedEditorRevision_;
    }

    [[nodiscard]] EditorResult<void> setMontageSettings(action::ActionMontageSettings settings) {
        const auto previous = editor_.target().timeline().montage;
        auto       edited   = editor_.setMontageSettings(settings);
        if (!edited.ok()) return edited;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSettings(settings);
            if (!applied) {
                auto rolledBack = editor_.undo();
                if (rolledBack.ok()) {
                    runtimeRate_ = previous.basePlayRate;
                    if (runtimeTimeline_) runtimeTimeline_->montage = previous;
                }
                return EditorResult<void>::failure(applied.status());
            }
        }
        runtimeRate_ = settings.basePlayRate;
        if (runtimeTimeline_) runtimeTimeline_->montage = std::move(settings);
        return edited;
    }

    [[nodiscard]] EditorResult<void> addSectionSplit(Duration time) {
        const auto previous = editor_.target().timeline().splitTimestamps;
        auto edited = editor_.addSectionSplit(time);
        if (!edited.ok()) return edited;
        const auto& splits = editor_.target().timeline().splitTimestamps;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSectionSplits(splits);
            if (!applied) {
                auto rolledBack = editor_.undo();
                if (rolledBack.ok() && runtimeTimeline_) runtimeTimeline_->splitTimestamps = previous;
                return EditorResult<void>::failure(applied.status());
            }
        }
        if (runtimeTimeline_) runtimeTimeline_->splitTimestamps = splits;
        return edited;
    }

    [[nodiscard]] EditorResult<void> setSectionSplit(std::size_t index, Duration time) {
        const auto previous = editor_.target().timeline().splitTimestamps;
        auto edited = editor_.setSectionSplit(index, time);
        if (!edited.ok()) return edited;
        const auto& splits = editor_.target().timeline().splitTimestamps;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSectionSplits(splits);
            if (!applied) {
                auto rolledBack = editor_.undo();
                if (rolledBack.ok() && runtimeTimeline_) runtimeTimeline_->splitTimestamps = previous;
                return EditorResult<void>::failure(applied.status());
            }
        }
        if (runtimeTimeline_) runtimeTimeline_->splitTimestamps = splits;
        return edited;
    }

    [[nodiscard]] EditorResult<void> removeSectionSplit(std::size_t index) {
        const auto previous = editor_.target().timeline().splitTimestamps;
        auto edited = editor_.removeSectionSplit(index);
        if (!edited.ok()) return edited;
        const auto& splits = editor_.target().timeline().splitTimestamps;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSectionSplits(splits);
            if (!applied) {
                auto rolledBack = editor_.undo();
                if (rolledBack.ok() && runtimeTimeline_) runtimeTimeline_->splitTimestamps = previous;
                return EditorResult<void>::failure(applied.status());
            }
        }
        if (runtimeTimeline_) runtimeTimeline_->splitTimestamps = splits;
        return edited;
    }

    [[nodiscard]] EditorResult<TransactionReceipt> undo() {
        const auto previous = editor_.target().timeline().splitTimestamps;
        auto       undone   = editor_.undo();
        if (!undone.ok()) return undone;
        const auto& splits = editor_.target().timeline().splitTimestamps;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSectionSplits(splits);
            if (!applied) {
                auto rolledBack = editor_.redo();
                if (rolledBack.ok() && runtimeTimeline_) runtimeTimeline_->splitTimestamps = previous;
                return EditorResult<TransactionReceipt>::failure(applied.status());
            }
        }
        if (runtimeTimeline_) runtimeTimeline_->splitTimestamps = splits;
        return undone;
    }

    [[nodiscard]] EditorResult<TransactionReceipt> redo() {
        const auto previous = editor_.target().timeline().splitTimestamps;
        auto       redone   = editor_.redo();
        if (!redone.ok()) return redone;
        const auto& splits = editor_.target().timeline().splitTimestamps;
        if (auto* montage = activeMontage()) {
            auto applied = montage->setSectionSplits(splits);
            if (!applied) {
                auto rolledBack = editor_.undo();
                if (rolledBack.ok() && runtimeTimeline_) runtimeTimeline_->splitTimestamps = previous;
                return EditorResult<TransactionReceipt>::failure(applied.status());
            }
        }
        if (runtimeTimeline_) runtimeTimeline_->splitTimestamps = splits;
        return redone;
    }

    [[nodiscard]] EditorResult<DocumentSnapshot> saveDocument() {
        auto synchronized = synchronizeDocument();
        if (!synchronized.ok()) return synchronized;
        auto ticket = documents_->requestSave(document_.id);
        if (!ticket.ok()) return EditorResult<DocumentSnapshot>::failure(ticket.status());
        auto saved = documents_->executeSave(ticket.value());
        if (saved.ok()) {
            document_            = saved.value();
            savedEditorRevision_ = editor_.target().revision();
        }
        return saved;
    }

    [[nodiscard]] EditorResult<DocumentSnapshot> reconcileDocument() {
        auto synchronized = synchronizeDocument();
        if (!synchronized.ok()) return synchronized;
        auto reconciled = documents_->reconcileExternal(document_.id, [](const EditorValue& content) {
            auto timeline = action::ActionTimeline::fromValue(toPresentationValue(content));
            if (!timeline) return EditorResult<void>::failure(timeline.status());
            return eve::editing::applied<void>();
        });
        if (!reconciled.ok()) return reconciled;
        auto content = documents_->content(document_.id);
        if (!content.ok()) return EditorResult<DocumentSnapshot>::failure(content.status());
        if (content.value() != editor_.target().snapshotValue()) {
            auto loaded = editor_.reloadDocument(content.value());
            if (!loaded.ok()) return EditorResult<DocumentSnapshot>::failure(loaded.status());
            synchronizedEditorRevision_ = editor_.target().revision();
            savedEditorRevision_        = editor_.target().revision();
        }
        document_ = reconciled.value();
        return reconciled;
    }

    const DocumentSnapshot& document() const noexcept { return document_; }

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
        const auto found = std::find_if(runtimeClips_.begin(), runtimeClips_.end(),
                                        [&](const auto& asset) { return asset.uri == uri; });
        if (auto* montage = activeMontage()) {
            auto applied = montage->replaceClip(uri, clip->clone());
            if (!applied) return applied;
        }
        if (found == runtimeClips_.end()) runtimeClips_.push_back({std::move(uri), std::move(clip)});
        else found->clip = std::move(clip);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> replaceRuntimeClip(std::string uri, const animation::AnimClip& clip) {
        if (uri.empty())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "montage clip URI must not be empty", "uri"));
        const auto found = std::find_if(runtimeClips_.begin(), runtimeClips_.end(),
                                        [&](const auto& asset) { return asset.uri == uri; });
        if (found == runtimeClips_.end())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "montage runtime clip was not registered", uri));
        if (auto* montage = activeMontage()) {
            auto applied = montage->replaceClip(uri, clip.clone());
            if (!applied) return applied;
        }
        found->clip = clip.clone();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<void> beginRuntime(animation::AnimSkeleton& skeleton) {
        auto timeline = editor_.target().timeline();
        runtimeTimeline_ = timeline;
        coordinator_     = std::make_unique<animation::MontageCoordinator>(skeleton);
        montageHandle_   = animation::MontageHandle::invalid();
        tick_            = SimulationTick::zero();
        runtimeAdvance_.reset();
        runtimeRate_   = timeline.montage.basePlayRate;
        runtimePaused_ = false;
        sectionRates_.clear();
        return restartExecution();
    }

    [[nodiscard]] Result<animation::MontageAdvance> advanceRuntime(Duration delta) {
        auto* montage = activeMontage();
        if (!runtime_ || !montage)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        if (montage->isBlendingOut()) {
            tick_        = SimulationTick(tick_.value() + 1);
            auto settled = montage->advanceBlendOut(delta, tick_);
            if (settled) runtimeAdvance_ = settled.value();
            return settled;
        }
        if (runtimePaused_) {
            animation::MontageAdvance paused;
            paused.previous = paused.current = montage->time();
            paused.weight                    = montage->weight();
            return Result<animation::MontageAdvance>::success(std::move(paused), Status::success(StatusCode::NoOp));
        }
        double sectionRate  = 1.0;
        auto   sectionIndex = montage->physicalSectionIndex();
        if (sectionIndex) {
            const auto found = sectionRates_.find(sectionIndex.value());
            if (found != sectionRates_.end()) sectionRate = found->second;
        }
        auto scaledDelta = Duration::fromSeconds(delta.seconds() * runtimeRate_ * sectionRate);
        if (!scaledDelta) return Result<animation::MontageAdvance>::failure(scaledDelta.status());
        Duration                  remainingDelta = std::move(scaledDelta).takeValue();
        animation::MontageAdvance combined;
        combined.previous = montage->time();
        bool firstSlice   = true;
        while (true) {
            Duration remainingTimeline =
                Duration::fromNanoseconds(runtimeTimeline_->duration.nanoseconds() - montage->time().nanoseconds());
            if (runtimeTimeline_->montage.looping && remainingTimeline.isZero()) {
                auto restarted = restartExecution();
                if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
                montage = activeMontage();
                remainingTimeline = runtimeTimeline_->duration;
            }
            const Duration slice =
                runtimeTimeline_->montage.looping ? std::min(remainingDelta, remainingTimeline) : remainingDelta;
            tick_              = SimulationTick(tick_.value() + 1);
            auto actionAdvance = runtime_->advance(executionId_, tick_, slice);
            if (!actionAdvance) return Result<animation::MontageAdvance>::failure(actionAdvance.status());
            auto routed = routeAvailableBlocks(actionAdvance.value());
            if (!routed) return Result<animation::MontageAdvance>::failure(routed.status());
            auto presented = coordinator_->present(montageHandle_, actionAdvance.value(), tick_);
            if (!presented) return Result<animation::MontageAdvance>::failure(presented.status());
            auto faded = coordinator_->advanceBlendOuts(slice, tick_, montageHandle_);
            if (!faded) return Result<animation::MontageAdvance>::failure(faded.status());
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
            montage            = activeMontage();
            combined.weight    = montage ? montage->weight() : 0.0;
        }
        runtimeAdvance_ = combined;
        return Result<animation::MontageAdvance>::success(std::move(combined), Status::success(StatusCode::Pending));
    }

    [[nodiscard]] Result<void> setRuntimeBlockExternallyHandled(const std::string& type, bool external) {
        auto parsed = LogicalId::parse(type);
        if (!parsed)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "externally handled runtime block type is invalid", "type"));
        if (!registry_.hasHandler(type))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound,
                                  "runtime block type has no registered handler", type));
        if (external)
            externallyHandledTypes_.insert(type);
        else
            externallyHandledTypes_.erase(type);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] Result<animation::MontageAdvance> jumpRuntime(Duration target) {
        auto* montage = activeMontage();
        if (!runtimeTimeline_ || !montage)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        auto restarted = restartExecution(false);
        if (!restarted) return Result<animation::MontageAdvance>::failure(restarted.status());
        auto rebound = montage->rebindExecution(executionId_);
        if (!rebound) return Result<animation::MontageAdvance>::failure(rebound.status());
        tick_         = SimulationTick(tick_.value() + 1);
        auto advanced = runtime_->advance(executionId_, tick_, target);
        if (!advanced) return Result<animation::MontageAdvance>::failure(advanced.status());
        auto routed = routeAvailableBlocks(advanced.value());
        if (!routed) return Result<animation::MontageAdvance>::failure(routed.status());
        auto jumped = montage->jumpToTime(executionId_, target, tick_);
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

    [[nodiscard]] Result<animation::MontageAdvance> evaluateRuntimeSectionProgress(std::size_t index,
                                                                                    double progress) {
        if (!runtimeTimeline_)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "runtime section progress must be finite and within [0,1]", "progress"));
        auto range = runtimeTimeline_->sectionRange(index);
        if (!range) return Result<animation::MontageAdvance>::failure(range.status());
        const auto span = range.value().second.nanoseconds() - range.value().first.nanoseconds();
        return jumpRuntime(Duration::fromNanoseconds(
            range.value().first.nanoseconds() + static_cast<std::int64_t>(std::llround(span * progress))));
    }

    [[nodiscard]] Result<void> replayRuntimeCrossFade() {
        if (!runtimeTimeline_ || !activeMontage())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return restartExecution();
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

    [[nodiscard]] Result<animation::MontageAdvance> syncRuntimeSectionAndJump(std::size_t index,
                                                                              Duration targetDuration) {
        auto synchronized = syncRuntimeSection(index, targetDuration);
        if (!synchronized) return Result<animation::MontageAdvance>::failure(synchronized.status());
        return jumpRuntimeSection(index);
    }

    [[nodiscard]] Result<void> clearRuntimeSectionSync(std::size_t index) {
        if (!runtimeTimeline_)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        auto range = runtimeTimeline_->sectionRange(index);
        if (!range) return Result<void>::failure(range.status());
        return Result<void>::success(Status::success(sectionRates_.erase(index) > 0 ? StatusCode::Applied
                                                                                   : StatusCode::NoOp));
    }

    [[nodiscard]] Result<void> setRuntimeBoneMask(const animation::AnimBoneMask& mask) {
        if (!coordinator_ || !activeMontage())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return coordinator_->setLayerBoneMask(activeLayer_, mask);
    }

    [[nodiscard]] Result<void> clearRuntimeBoneMask() {
        if (!coordinator_ || !activeMontage())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return coordinator_->clearLayerBoneMask(activeLayer_);
    }

    [[nodiscard]] Result<void> setRuntimeLayerWeight(float weight) {
        if (!coordinator_ || !activeMontage())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return coordinator_->setLayerWeight(activeLayer_, weight);
    }

    [[nodiscard]] float runtimeLayerWeight() const {
        if (!coordinator_ || !activeMontage()) return 0.0f;
        auto weight = coordinator_->layerWeight(activeLayer_);
        return weight ? weight.value() : 0.0f;
    }

    [[nodiscard]] Result<void> setRuntimeLayerAdditive(bool additive) {
        if (!coordinator_ || !activeMontage())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return coordinator_->setLayerAdditive(activeLayer_, additive);
    }

    [[nodiscard]] bool runtimeLayerAdditive() const {
        if (!coordinator_ || !activeMontage()) return false;
        auto additive = coordinator_->layerAdditive(activeLayer_);
        return additive ? additive.value() : false;
    }

    [[nodiscard]] Result<animation::MontageAdvance> beginRuntimeBlendOut(Duration duration) {
        auto* montage = activeMontage();
        if (!montage)
            return Result<animation::MontageAdvance>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        tick_        = SimulationTick(tick_.value() + 1);
        auto stopped = montage->beginBlendOut(duration, tick_);
        if (stopped) runtimeAdvance_ = stopped.value();
        return stopped;
    }

    void               setRuntimePaused(bool paused) noexcept { runtimePaused_ = paused; }
    [[nodiscard]] bool runtimePaused() const noexcept { return runtimePaused_; }

    [[nodiscard]] Result<void> setRuntimeRate(double rate) {
        if (!runtimeTimeline_)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        if (!std::isfinite(rate) || rate <= 0.0)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "montage runtime rate must be finite and positive", "rate"));
        if (runtimeRate_ == rate) return Result<void>::success(Status::success(StatusCode::NoOp));
        runtimeRate_ = rate;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] double runtimeRate() const noexcept { return runtimeRate_; }

    [[nodiscard]] Result<void> cancelRuntime() {
        auto* montage = activeMontage();
        if (!runtime_ || !montage) return Result<void>::success(Status::success(StatusCode::NoOp));
        tick_          = SimulationTick(tick_.value() + 1);
        auto cancelled = runtime_->cancel(executionId_, tick_);
        if (!cancelled) return cancelled;
        auto interrupted = blockRuntime_.interrupt(runtimeContext(montage->time()));
        if (!interrupted) return interrupted;
        montage->stop();
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    [[nodiscard]] animation::AnimPose* runtimePose() noexcept {
        if (!coordinator_ || !montageHandle_.isValid()) return nullptr;
        auto pose = coordinator_->pose(activeLayer_);
        return pose ? &pose.value().get() : nullptr;
    }
    [[nodiscard]] animation::AnimPose* runtimePoseOverBase(const animation::AnimPose& basePose) {
        if (!coordinator_ || !montageHandle_.isValid()) return nullptr;
        auto pose = coordinator_->compose(basePose);
        return pose ? &pose.value().get() : nullptr;
    }
    [[nodiscard]] const std::optional<animation::MontageAdvance>& runtimeAdvance() const noexcept {
        return runtimeAdvance_;
    }
    [[nodiscard]] bool runtimePlaying() const noexcept {
        const auto* montage = activeMontage();
        return montage && montage->isPlaying();
    }

    [[nodiscard]] std::string runtimeState() const {
        if (!runtimeTimeline_) return "not-started";
        if (runtimeAdvance_ && runtimeAdvance_->completed) return "finished";
        const auto* montage = activeMontage();
        if (!montage) return "finished";
        if (montage->isBlendingOut()) return "stopping";
        if (runtimePaused_) return "paused";
        return montage->isPlaying() ? "playing" : "finished";
    }

    [[nodiscard]] double runtimeElapsedSeconds() const noexcept {
        const auto* montage = activeMontage();
        if (montage) return montage->time().seconds();
        return runtimeAdvance_ ? runtimeAdvance_->current.seconds() : 0.0;
    }

    [[nodiscard]] double runtimeDurationSeconds() const noexcept {
        return runtimeTimeline_ ? runtimeTimeline_->duration.seconds() : 0.0;
    }

    [[nodiscard]] int runtimeLayer() const noexcept {
        return montageHandle_.isValid() ? static_cast<int>(activeLayer_) : -1;
    }

    [[nodiscard]] int runtimeSlot() const noexcept {
        return montageHandle_.isValid() ? static_cast<int>(montageHandle_.index() % 2U) : -1;
    }

    [[nodiscard]] Result<std::size_t> runtimePhysicalSection() const {
        const auto* montage = activeMontage();
        if (!montage)
            return Result<std::size_t>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return montage->physicalSectionIndex();
    }

    [[nodiscard]] Result<double> runtimeSectionProgress(std::size_t index) const {
        const auto* montage = activeMontage();
        if (!montage)
            return Result<double>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "montage runtime has not been started", "runtime"));
        return montage->physicalSectionProgress(index);
    }

private:
    [[nodiscard]] animation::MontagePlayer* activeMontage() noexcept {
        if (!coordinator_ || !montageHandle_.isValid()) return nullptr;
        auto resolved = coordinator_->resolve(montageHandle_);
        return resolved ? &resolved.value().get() : nullptr;
    }

    [[nodiscard]] const animation::MontagePlayer* activeMontage() const noexcept {
        if (!coordinator_ || !montageHandle_.isValid()) return nullptr;
        auto resolved = static_cast<const animation::MontageCoordinator&>(*coordinator_).resolve(montageHandle_);
        return resolved ? &resolved.value().get() : nullptr;
    }

    [[nodiscard]] std::vector<animation::MontageClipAsset> cloneRuntimeClips() const {
        std::vector<animation::MontageClipAsset> clips;
        clips.reserve(runtimeClips_.size());
        for (const auto& asset : runtimeClips_)
            clips.push_back({asset.uri, asset.clip ? asset.clip->clone() : nullptr});
        return clips;
    }

    [[nodiscard]] EditorResult<DocumentSnapshot> synchronizeDocument() {
        if (!documents_ || document_.id.empty())
            return eve::editing::failed<DocumentSnapshot>(EditorStatus::Unsupported,
                                                          RuleId("editor.action.document.not-persistent"),
                                                          "Action timeline editor is not attached to a document");
        auto snapshot = documents_->snapshot(document_.id);
        if (!snapshot.ok()) return snapshot;
        if (editor_.target().revision() == synchronizedEditorRevision_) return snapshot;
        auto edited = documents_->edit(document_.id, editor_.target().snapshotValue(), snapshot.value().revision.edit);
        if (edited.ok()) {
            synchronizedEditorRevision_ = editor_.target().revision();
            document_                   = edited.value();
        }
        return edited;
    }

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
        const bool hasActiveMontage = activeMontage() != nullptr;
        const auto previousHandle   = montageHandle_;
        const auto previousLayer    = activeLayer_;
        if (!executionId_.isZero()) {
            const auto* montage = activeMontage();
            auto interrupted = blockRuntime_.interrupt(runtimeContext(montage ? montage->time() : Duration::zero()));
            if (!interrupted) return interrupted;
        }
        auto                  runtime    = std::make_unique<action::ActionRuntime>();
        auto                  definition = runtimeDefinition();
        action::ActionRequest request;
        request.actionId = definition.id;
        auto submitted   = runtime->submit(std::move(definition), std::move(request));
        if (!submitted) return Result<void>::failure(submitted.status());
        if (playMontage) {
            if (!coordinator_)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "montage coordinator has not been started", "runtime"));
            if (hasActiveMontage) tick_ = SimulationTick(tick_.value() + 1);
            const std::size_t layer = runtimeTimeline_->montage.animationLayer;
            auto played = coordinator_->play(layer, *runtimeTimeline_, cloneRuntimeClips(), submitted.value(), tick_);
            if (!played) return Result<void>::failure(played.status());
            montageHandle_ = played.value();
            activeLayer_   = layer;
            if (hasActiveMontage && previousLayer != layer) {
                auto previous = coordinator_->resolve(previousHandle);
                if (previous) previous.value().get().stop();
                coordinator_->collectFinished();
            }
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
            if (registry_.hasHandler(event.type.format()) &&
                !externallyHandledTypes_.contains(event.type.format()))
                handled.timelineEvents.push_back(event);
        for (const auto& block : advance.activeBlocks)
            if (registry_.hasHandler(block.type.format()) &&
                !externallyHandledTypes_.contains(block.type.format()))
                handled.activeBlocks.push_back(block);
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
    std::unique_ptr<animation::MontageCoordinator> coordinator_;
    animation::MontageHandle                       montageHandle_ = animation::MontageHandle::invalid();
    std::size_t                                    activeLayer_   = 0;
    action::ActionExecutionId                 executionId_{};
    SimulationTick                            tick_ = SimulationTick::zero();
    std::optional<animation::MontageAdvance>  runtimeAdvance_;
    std::optional<action::ActionTimeline>     runtimeTimeline_;
    std::unordered_map<std::size_t, double>   sectionRates_;
    std::set<std::string, std::less<>>        externallyHandledTypes_;
    double                                    runtimeRate_   = 1.0;
    bool                                      runtimePaused_ = false;
    std::unique_ptr<DiskAtomicDocumentStore>  documentStore_;
    std::unique_ptr<DocumentService>          documents_;
    DocumentSnapshot                         document_;
    std::uint64_t                            synchronizedEditorRevision_ = 0;
    std::uint64_t                            savedEditorRevision_        = 0;
};

std::optional<Value> itemPayloadField(ScriptActionTimelineEditor* self, const std::string& itemId,
                                      const std::string& field) {
    if (!self) return std::nullopt;
    auto parsed = LogicalId::parse(itemId);
    if (!parsed) return std::nullopt;
    ActionTimelinePayloadEditor payloads(self->editor(), self->registry());
    auto                        payload = payloads.payload(*parsed);
    if (!payload.ok()) return std::nullopt;
    const auto found = payload.value().find(field);
    return found == payload.value().end() ? std::nullopt : std::optional<Value>(found->second);
}

EditorResult<void> patchItemPayload(ScriptActionTimelineEditor* self, const std::string& itemId,
                                    Value::Object fields) {
    if (!self)
        return eve::editing::failed<void>(EditorStatus::Rejected,
                                          RuleId("editor.action.timeline.payload-editor-null"),
                                          "Action timeline editor is null");
    auto parsed = LogicalId::parse(itemId);
    if (!parsed)
        return eve::editing::failed<void>(EditorStatus::Rejected,
                                          RuleId("editor.action.timeline.payload-item-invalid"),
                                          "Action-block identity is invalid");
    ActionTimelinePayloadEditor payloads(self->editor(), self->registry());
    return payloads.patch(*parsed, std::move(fields));
}

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
    auto assetCatalog = table.addClass<ScriptActionTimelineAssetCatalog>(
        "ActionTimelineAssetCatalog",
        std::function<ScriptActionTimelineAssetCatalog*()>([]() -> ScriptActionTimelineAssetCatalog* {
            return nullptr;
        }),
        true);
    assetCatalog.addFunc("refresh", [vm](ScriptActionTimelineAssetCatalog* self, const std::string& text) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action asset catalog is null");
        auto refreshed = self->refresh(text);
        return project(vm, refreshed,
                       refreshed.ok() ? Value(static_cast<std::int64_t>(refreshed.value())) : Value{});
    });
    assetCatalog.addFunc("getAssetCount",
                         [](ScriptActionTimelineAssetCatalog* self) { return self ? self->count() : 0; });
    assetCatalog.addFunc("getGeneration", [](ScriptActionTimelineAssetCatalog* self) {
        return self ? static_cast<std::int64_t>(self->generation()) : std::int64_t{0};
    });
    assetCatalog.addFunc("getAssetGuid",
                         [](ScriptActionTimelineAssetCatalog* self, int index) { return self ? self->guid(index) : ""; });
    assetCatalog.addFunc("getAssetUri",
                         [](ScriptActionTimelineAssetCatalog* self, int index) { return self ? self->uri(index) : ""; });
    assetCatalog.addFunc("getAssetTitle", [](ScriptActionTimelineAssetCatalog* self, int index) {
        return self ? self->title(index) : "";
    });
    assetCatalog.addFunc("registerDocument", [vm](ScriptActionTimelineAssetCatalog* self, const std::string& guid,
                                                   const std::string& uri) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action asset catalog is null");
        return project(vm, self->registerDocument(guid, uri));
    });

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
    actionEditor.addFunc("addNotifyAtCursor", [vm](ScriptActionTimelineEditor* self, const std::string& trackId,
                                                     const std::string& type, const std::string& payloadJson) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto parsedTrack = LogicalId::parse(trackId);
        if (!parsedTrack)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
        auto parsedPayload = Value::fromJson(payloadJson);
        if (!parsedPayload) return script::projectStatusResult(vm, parsedPayload.status(), false, false);
        const auto* payload = parsedPayload.value().getIf<Value::Object>();
        if (!payload)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "notify payload must be a JSON object",
                                  "payloadJson");
        return project(vm, self->widget().addNotifyAtCursor(*parsedTrack, type, *payload));
    });
    actionEditor.addFunc("addStateAtCursor", [vm](ScriptActionTimelineEditor* self, const std::string& trackId,
                                                    const std::string& type, float durationSeconds,
                                                    const std::string& payloadJson) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto parsedTrack = LogicalId::parse(trackId);
        auto duration    = seconds(durationSeconds);
        if (!parsedTrack)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "track id is invalid", "trackId");
        if (!duration) return script::projectStatusResult(vm, duration.status(), false, false);
        auto parsedPayload = Value::fromJson(payloadJson);
        if (!parsedPayload) return script::projectStatusResult(vm, parsedPayload.status(), false, false);
        const auto* payload = parsedPayload.value().getIf<Value::Object>();
        if (!payload)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "notify-state payload must be a JSON object",
                                  "payloadJson");
        return project(vm, self->widget().addStateAtCursor(*parsedTrack, type, duration.value(), *payload));
    });
    actionEditor.addFunc("handleTimelineShortcut", [vm](ScriptActionTimelineEditor* self,
                                                         const std::string& shortcut) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        return project(vm, self->widget().handleShortcut(shortcut));
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
    actionEditor.addFunc("setItemTiming", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                float startSeconds, float endSeconds) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsedItemId = LogicalId::parse(itemId);
        auto start        = seconds(startSeconds);
        auto end          = seconds(endSeconds);
        if (!parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is not canonical", "itemId");
        if (!start) return script::projectStatusResult(vm, start.status(), false, false);
        if (!end) return script::projectStatusResult(vm, end.status(), false, false);
        const auto& timeline = self->editor().target().timeline();
        if (const auto* section = findSection(timeline, itemId))
            return project(vm, self->editor().resizeAnimationSection(*parsedItemId, start.value(), end.value(),
                                                                      section->blendIn));
        if (const auto* state = findState(timeline, itemId))
            return project(vm, self->editor().editItem(*parsedItemId, start.value(), end.value(), state->type,
                                                       state->payload));
        if (const auto* notify = findNotify(timeline, itemId))
            return project(vm, self->editor().editItem(*parsedItemId, start.value(), start.value(), notify->type,
                                                       notify->payload));
        return bindingFailure(vm, DiagnosticCode::NotFound, "timeline item was not found", "itemId");
    });
    actionEditor.addFunc("removeItem", [vm](ScriptActionTimelineEditor* self, const std::string& itemId) {
        auto parsedItemId = LogicalId::parse(itemId);
        if (!self || !parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is invalid", "itemId");
        return project(vm, self->editor().removeItem(*parsedItemId));
    });
    actionEditor.addFunc("setItemEnabled", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                 bool enabled) {
        auto parsedItemId = LogicalId::parse(itemId);
        if (!self || !parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is invalid", "itemId");
        return project(vm, self->editor().setItemEnabled(*parsedItemId, enabled));
    });
    actionEditor.addFunc("fitBlockToClip", [vm](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsedItemId = LogicalId::parse(itemId);
        if (!parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is invalid", "itemId");
        ActionTimelinePayloadEditor payloads(self->editor(), self->registry());
        return project(vm, payloads.fitBlockToClip(*parsedItemId));
    });
    actionEditor.addFunc("fitClipToBlock", [vm](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsedItemId = LogicalId::parse(itemId);
        if (!parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is invalid", "itemId");
        ActionTimelinePayloadEditor payloads(self->editor(), self->registry());
        return project(vm, payloads.fitClipToBlock(*parsedItemId));
    });
    actionEditor.addFunc("fitClipToNaturalDuration",
                         [vm](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto parsedItemId = LogicalId::parse(itemId);
        if (!parsedItemId)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item id is invalid", "itemId");
        const auto* provider = cap::query<action::IActionVfxDurationProvider>();
        ActionTimelinePayloadEditor payloads(self->editor(), self->registry());
        OptionalRef<const action::IActionVfxDurationProvider> borrowed;
        if (provider) borrowed = std::cref(*provider);
        return project(vm, payloads.fitClipToNaturalDuration(*parsedItemId, borrowed));
    });
    actionEditor.addFunc("hasVfxDurationProvider", [](ScriptActionTimelineEditor*) {
        return cap::query<action::IActionVfxDurationProvider>() != nullptr;
    });
    actionEditor.addFunc("editItemDetails", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                  const std::string& type, const std::string& payloadJson) {
        auto parsedItemId = LogicalId::parse(itemId);
        auto parsedType   = LogicalId::parse(type);
        if (!self || !parsedItemId || !parsedType)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item identity or type is invalid",
                                  "itemDetails");
        auto parsedPayload = Value::fromJson(payloadJson);
        if (!parsedPayload) return script::projectStatusResult(vm, parsedPayload.status(), false, false);
        const auto* payload = parsedPayload.value().getIf<Value::Object>();
        if (!payload)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "timeline item payload must be a JSON object",
                                  "payloadJson");
        const auto& timeline = self->editor().target().timeline();
        const auto* state    = findState(timeline, itemId);
        const auto* notify   = findNotify(timeline, itemId);
        if (!state && !notify)
            return bindingFailure(vm, DiagnosticCode::NotFound, "editable notify item was not found", "itemId");
        auto descriptor = self->registry().descriptor(type);
        if (!descriptor) return script::projectStatusResult(vm, descriptor.status(), false, false);
        const auto expected = state ? action::ActionNotifyShape::State : action::ActionNotifyShape::Instant;
        if (descriptor.value().shape != expected)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "notify type shape does not match the selected item", "type");
        const LogicalId* trackId = nullptr;
        for (const auto& track : timeline.tracks) {
            const auto contains = state ? std::any_of(track.states.begin(), track.states.end(),
                                                      [&](const auto& value) { return value.id == *parsedItemId; })
                                        : std::any_of(track.notifies.begin(), track.notifies.end(),
                                                      [&](const auto& value) { return value.id == *parsedItemId; });
            if (contains) {
                trackId = &track.id;
                break;
            }
        }
        if (!trackId)
            return bindingFailure(vm, DiagnosticCode::NotFound, "timeline item track was not found", "itemId");
        action::ActionTimelineEvent event{state ? action::ActionTimelineEventKind::StateEnter
                                                : action::ActionTimelineEventKind::Notify,
                                          *trackId,
                                          *parsedItemId,
                                          *parsedType,
                                          state ? state->start : notify->time,
                                          *payload};
        auto valid = self->registry().validate(event);
        if (!valid) return script::projectStatusResult(vm, valid.status(), false, false);
        return project(vm, self->editor().updateItem(*parsedItemId, *parsedType, *payload));
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
        auto result = self->undo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    actionEditor.addFunc("redo", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        auto result = self->redo();
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
    actionEditor.addFunc("snapshotJson", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor must not be null");
        auto snapshot = self->editor().target().timeline().toValue();
        if (!snapshot) return script::projectStatusResult(vm, snapshot.status(), false, false);
        auto encoded = snapshot.value().toJson();
        return script::projectResult(vm, std::move(encoded),
                                     [](std::string value) { return Value(std::move(value)); });
    });
    actionEditor.addFunc(
        "registerRuntimeClip", [vm](ScriptActionTimelineEditor* self, const std::string& uri, model3d::ModelData* model,
                                    animation::AnimSkeleton* skeleton, int animationIndex) {
            if (!self || !model || !skeleton)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "editor, model and skeleton must not be null", "runtimeClip");
            return script::projectResult(vm, self->registerRuntimeClip(uri, *model, *skeleton, animationIndex));
        });
    actionEditor.addFunc("replaceRuntimeClip",
                         [vm](ScriptActionTimelineEditor* self, const std::string& uri,
                              animation::AnimClip* clip) {
        if (!self || !clip)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "editor and animation clip must not be null", "runtimeClip");
        return script::projectResult(vm, self->replaceRuntimeClip(uri, *clip));
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
        "evaluateRuntimeSectionProgress",
        [vm](ScriptActionTimelineEditor* self, int sectionIndex, float progress) {
            if (!self || sectionIndex < 0)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "runtime section index is invalid", "index");
            return script::projectResult(
                vm, self->evaluateRuntimeSectionProgress(static_cast<std::size_t>(sectionIndex), progress),
                montageAdvanceValue);
        });
    actionEditor.addFunc("replayRuntimeCrossFade", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor must not be null");
        return script::projectResult(vm, self->replayRuntimeCrossFade());
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
    actionEditor.addFunc(
        "syncRuntimeSectionAndJump", [vm](ScriptActionTimelineEditor* self, int sectionIndex, float targetSeconds) {
            if (!self || sectionIndex < 0)
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "runtime section index is invalid", "index");
            auto target = seconds(targetSeconds);
            if (!target) return script::projectStatusResult(vm, target.status(), false, false);
            return script::projectResult(
                vm,
                self->syncRuntimeSectionAndJump(static_cast<std::size_t>(sectionIndex),
                                                std::move(target).takeValue()),
                montageAdvanceValue);
        });
    actionEditor.addFunc("clearRuntimeSectionSync", [vm](ScriptActionTimelineEditor* self, int sectionIndex) {
        if (!self || sectionIndex < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "runtime section index is invalid", "index");
        return script::projectResult(vm,
                                     self->clearRuntimeSectionSync(static_cast<std::size_t>(sectionIndex)));
    });
    actionEditor.addFunc("setRuntimeBoneMask",
                         [vm](ScriptActionTimelineEditor* self, animation::AnimBoneMask* mask) {
        if (!self || !mask)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor and bone mask must not be null", "mask");
        return script::projectResult(vm, self->setRuntimeBoneMask(*mask));
    });
    actionEditor.addFunc("clearRuntimeBoneMask", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor must not be null");
        return script::projectResult(vm, self->clearRuntimeBoneMask());
    });
    actionEditor.addFunc("setRuntimeLayerWeight", [vm](ScriptActionTimelineEditor* self, float weight) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->setRuntimeLayerWeight(weight));
    });
    actionEditor.addFunc("getRuntimeLayerWeight",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimeLayerWeight() : 0.0f; });
    actionEditor.addFunc("setRuntimeLayerAdditive", [vm](ScriptActionTimelineEditor* self, bool additive) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->setRuntimeLayerAdditive(additive));
    });
    actionEditor.addFunc("getRuntimeLayerAdditive",
                         [](ScriptActionTimelineEditor* self) { return self && self->runtimeLayerAdditive(); });
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
    actionEditor.addFunc("setRuntimeBlockExternallyHandled",
                         [vm](ScriptActionTimelineEditor* self, const std::string& type, bool external) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor must not be null");
        return script::projectResult(vm, self->setRuntimeBlockExternallyHandled(type, external));
    });
    actionEditor.addFunc("isRuntimePaused",
                         [](ScriptActionTimelineEditor* self) { return self && self->runtimePaused(); });
    actionEditor.addFunc("setRuntimeRate", [vm](ScriptActionTimelineEditor* self, float rate) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor must not be null");
        return script::projectResult(vm, self->setRuntimeRate(rate));
    });
    actionEditor.addFunc("getRuntimeRate", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->runtimeRate()) : 0.0f;
    });
    actionEditor.addFunc("cancelRuntime", [vm](ScriptActionTimelineEditor* self) {
        if (!self)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor must not be null");
        return script::projectResult(vm, self->cancelRuntime());
    });
    actionEditor.addFunc("getRuntimePose",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimePose() : nullptr; });
    actionEditor.addFunc("getRuntimePoseOverBase", [](ScriptActionTimelineEditor* self, animation::AnimPose* basePose) {
        return self && basePose ? self->runtimePoseOverBase(*basePose) : nullptr;
    });
    actionEditor.addFunc("isRuntimePlaying",
                         [](ScriptActionTimelineEditor* self) { return self && self->runtimePlaying(); });
    actionEditor.addFunc("getRuntimeState",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimeState() : "not-started"; });
    actionEditor.addFunc("getRuntimeElapsedSeconds", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->runtimeElapsedSeconds()) : 0.0f;
    });
    actionEditor.addFunc("getRuntimeDurationSeconds", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->runtimeDurationSeconds()) : 0.0f;
    });
    actionEditor.addFunc("getRuntimeLayer",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimeLayer() : -1; });
    actionEditor.addFunc("getRuntimeSlot",
                         [](ScriptActionTimelineEditor* self) { return self ? self->runtimeSlot() : -1; });
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
    actionEditor.addFunc("getRuntimeEventId", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->events.size())
            return std::string{};
        return self->runtimeAdvance()->events[static_cast<std::size_t>(index)].itemId.format();
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
    actionEditor.addFunc("getRuntimeEventPayloadJson", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || !self->runtimeAdvance() || index < 0 ||
            static_cast<std::size_t>(index) >= self->runtimeAdvance()->events.size())
            return std::string("{}");
        auto encoded = Value(self->runtimeAdvance()->events[static_cast<std::size_t>(index)].payload).toJson();
        return encoded ? encoded.value() : std::string("{}");
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
    actionEditor.addFunc("isDocumentBacked", [](ScriptActionTimelineEditor* self) {
        return self && !self->document().id.empty();
    });
    actionEditor.addFunc("isDirty",
                         [](ScriptActionTimelineEditor* self) { return self && self->documentDirty(); });
    actionEditor.addFunc("saveDocument", [vm](ScriptActionTimelineEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto saved = self->saveDocument();
        return project(vm, saved,
                       saved.ok() ? Value(static_cast<std::int64_t>(saved.value().revision.saved)) : Value{});
    });
    actionEditor.addFunc("reconcileDocument", [vm](ScriptActionTimelineEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto reconciled = self->reconcileDocument();
        return project(vm, reconciled,
                       reconciled.ok() ? Value(static_cast<std::int64_t>(reconciled.value().revision.edit)) : Value{});
    });
    actionEditor.addFunc("getDocumentId", [](ScriptActionTimelineEditor* self) {
        return self ? self->document().id.value() : std::string{};
    });
    actionEditor.addFunc("getDocumentTitle", [](ScriptActionTimelineEditor* self) {
        return self ? self->document().title : std::string{};
    });
    actionEditor.addFunc("getDocumentUri", [](ScriptActionTimelineEditor* self) {
        return self ? self->document().resourceUri : std::string{};
    });
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
    actionEditor.addFunc("setMontageSettings", [vm](ScriptActionTimelineEditor* self, float basePlayRate,
                                                     bool looping, bool footIk, int animationLayer,
                                                     float blendInSeconds, float blendOutSeconds,
                                                     float blendOutOffsetSeconds, bool rootHorizontal,
                                                     bool rootVertical, bool rootRotation) {
        if (!self || animationLayer < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "montage editor and non-negative animation layer are required", "montage");
        auto blendIn        = seconds(blendInSeconds);
        auto blendOut       = seconds(blendOutSeconds);
        auto blendOutOffset = seconds(blendOutOffsetSeconds);
        if (!blendIn) return script::projectStatusResult(vm, blendIn.status(), false, false);
        if (!blendOut) return script::projectStatusResult(vm, blendOut.status(), false, false);
        if (!blendOutOffset) return script::projectStatusResult(vm, blendOutOffset.status(), false, false);
        action::ActionMontageSettings settings;
        settings.basePlayRate         = basePlayRate;
        settings.looping              = looping;
        settings.footIk               = footIk;
        settings.animationLayer       = static_cast<std::uint32_t>(animationLayer);
        settings.defaultBlendIn       = blendIn.value();
        settings.defaultBlendOut      = blendOut.value();
        settings.blendOutOffset       = blendOutOffset.value();
        settings.rootMotionHorizontal = rootHorizontal;
        settings.rootMotionVertical   = rootVertical;
        settings.rootMotionRotation   = rootRotation;
        return project(vm, self->setMontageSettings(std::move(settings)));
    });
    actionEditor.addFunc("getMontageBasePlayRate", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().target().timeline().montage.basePlayRate) : 1.0f;
    });
    actionEditor.addFunc("getMontageLooping", [](ScriptActionTimelineEditor* self) {
        return self && self->editor().target().timeline().montage.looping;
    });
    actionEditor.addFunc("getMontageFootIk", [](ScriptActionTimelineEditor* self) {
        return self && self->editor().target().timeline().montage.footIk;
    });
    actionEditor.addFunc("getMontageAnimationLayer", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->editor().target().timeline().montage.animationLayer) : 0;
    });
    actionEditor.addFunc("getMontageBlendIn", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().target().timeline().montage.defaultBlendIn.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getMontageBlendOut", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().target().timeline().montage.defaultBlendOut.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getMontageBlendOutOffset", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<float>(self->editor().target().timeline().montage.blendOutOffset.seconds()) : 0.0f;
    });
    actionEditor.addFunc("getMontageRootMotionHorizontal", [](ScriptActionTimelineEditor* self) {
        return self && self->editor().target().timeline().montage.rootMotionHorizontal;
    });
    actionEditor.addFunc("getMontageRootMotionVertical", [](ScriptActionTimelineEditor* self) {
        return self && self->editor().target().timeline().montage.rootMotionVertical;
    });
    actionEditor.addFunc("getMontageRootMotionRotation", [](ScriptActionTimelineEditor* self) {
        return self && self->editor().target().timeline().montage.rootMotionRotation;
    });
    actionEditor.addFunc("getAnimationUri", [](ScriptActionTimelineEditor* self) {
        return self ? self->editor().target().timeline().animationUri : std::string{};
    });
    actionEditor.addFunc("getSectionSplitCount", [](ScriptActionTimelineEditor* self) {
        return self ? static_cast<int>(self->editor().target().timeline().splitTimestamps.size()) : 0;
    });
    actionEditor.addFunc("getSectionSplitTime", [](ScriptActionTimelineEditor* self, int index) {
        if (!self || index < 0 || static_cast<std::size_t>(index) >=
                                      self->editor().target().timeline().splitTimestamps.size())
            return 0.0f;
        return static_cast<float>(
            self->editor().target().timeline().splitTimestamps[static_cast<std::size_t>(index)].seconds());
    });
    actionEditor.addFunc("addSectionSplit", [vm](ScriptActionTimelineEditor* self, float timeSeconds) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "action timeline editor is null");
        auto time = seconds(timeSeconds);
        if (!time) return script::projectStatusResult(vm, time.status(), false, false);
        return project(vm, self->addSectionSplit(time.value()));
    });
    actionEditor.addFunc("setSectionSplit", [vm](ScriptActionTimelineEditor* self, int index, float timeSeconds) {
        if (!self || index < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor and non-negative split index are required");
        auto time = seconds(timeSeconds);
        if (!time) return script::projectStatusResult(vm, time.status(), false, false);
        return project(vm, self->setSectionSplit(static_cast<std::size_t>(index), time.value()));
    });
    actionEditor.addFunc("removeSectionSplit", [vm](ScriptActionTimelineEditor* self, int index) {
        if (!self || index < 0)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                  "action timeline editor and non-negative split index are required");
        return project(vm, self->removeSectionSplit(static_cast<std::size_t>(index)));
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
    actionEditor.addFunc("getTrackLocked", [](ScriptActionTimelineEditor* self, int index) {
        const auto* track = self ? trackAt(self->editor().target().timeline(), index) : nullptr;
        return track && track->locked;
    });
    actionEditor.addFunc("getInsertableTypeCount", [](ScriptActionTimelineEditor* self, bool state) {
        if (!self) return 0;
        return static_cast<int>(self->widget()
                                    .insertableTypes(state ? action::ActionNotifyShape::State
                                                           : action::ActionNotifyShape::Instant)
                                    .size());
    });
    actionEditor.addFunc("getInsertableType", [](ScriptActionTimelineEditor* self, bool state, int index) {
        if (!self || index < 0) return std::string{};
        const auto types = self->widget().insertableTypes(state ? action::ActionNotifyShape::State
                                                                : action::ActionNotifyShape::Instant);
        return static_cast<std::size_t>(index) < types.size() ? types[static_cast<std::size_t>(index)].type
                                                              : std::string{};
    });
    actionEditor.addFunc("getInsertableTypeLabel", [](ScriptActionTimelineEditor* self, bool state, int index) {
        if (!self || index < 0) return std::string{};
        const auto types = self->widget().insertableTypes(state ? action::ActionNotifyShape::State
                                                                : action::ActionNotifyShape::Instant);
        if (static_cast<std::size_t>(index) >= types.size()) return std::string{};
        const auto& descriptor = types[static_cast<std::size_t>(index)];
        return descriptor.category + " / " + descriptor.displayName;
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
    actionEditor.addFunc("getItemVisual", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? std::string(timelineItemVisualName(item->visual)) : std::string("custom");
    });
    actionEditor.addFunc("getItemLabel", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->displayName : std::string{};
    });
    actionEditor.addFunc("getItemDetail", [](ScriptActionTimelineEditor* self, int index) {
        const auto  layout = self ? self->widget().layout() : TimelineWidgetLayout{};
        const auto* item   = itemAt(layout, index);
        return item ? item->detail : std::string{};
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
    actionEditor.addFunc("getItemStart", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self) return 0.0f;
        const auto& timeline = self->editor().target().timeline();
        if (const auto* section = findSection(timeline, itemId)) return static_cast<float>(section->start.seconds());
        if (const auto* state = findState(timeline, itemId)) return static_cast<float>(state->start.seconds());
        if (const auto* notify = findNotify(timeline, itemId)) return static_cast<float>(notify->time.seconds());
        return 0.0f;
    });
    actionEditor.addFunc("getItemEnd", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self) return 0.0f;
        const auto& timeline = self->editor().target().timeline();
        if (const auto* section = findSection(timeline, itemId)) return static_cast<float>(section->end.seconds());
        if (const auto* state = findState(timeline, itemId)) return static_cast<float>(state->end.seconds());
        if (const auto* notify = findNotify(timeline, itemId)) return static_cast<float>(notify->time.seconds());
        return 0.0f;
    });
    actionEditor.addFunc("getItemEnabled", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self) return false;
        const auto& timeline = self->editor().target().timeline();
        if (findSection(timeline, itemId)) return true;
        if (const auto* state = findState(timeline, itemId)) return state->enabled;
        if (const auto* notify = findNotify(timeline, itemId)) return notify->enabled;
        return false;
    });
    actionEditor.addFunc("getItemPayloadJson", [](ScriptActionTimelineEditor* self, const std::string& itemId) {
        if (!self) return std::string("{}");
        const auto& timeline = self->editor().target().timeline();
        const Value::Object* payload = nullptr;
        if (const auto* state = findState(timeline, itemId)) payload = &state->payload;
        if (const auto* notify = findNotify(timeline, itemId)) payload = &notify->payload;
        if (!payload) return std::string("{}");
        auto encoded = Value(*payload).toJson();
        return encoded ? encoded.value() : std::string("{}");
    });
    actionEditor.addFunc("patchItemPayload", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                   const std::string& fieldsJson) {
        auto parsed = Value::fromJson(fieldsJson);
        if (!parsed) return script::projectStatusResult(vm, parsed.status(), false, false);
        const auto* fields = parsed.value().getIf<Value::Object>();
        if (!fields)
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "payload patch must be a JSON object",
                                  "fieldsJson");
        return project(vm, patchItemPayload(self, itemId, *fields));
    });
    actionEditor.addFunc("setItemPayloadText", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                     const std::string& field, const std::string& value) {
        return project(vm, patchItemPayload(self, itemId, {{field, value}}));
    });
    actionEditor.addFunc("setItemPayloadNumber", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                       const std::string& field, float value) {
        return project(vm, patchItemPayload(self, itemId, {{field, static_cast<double>(value)}}));
    });
    actionEditor.addFunc("setItemPayloadInteger", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                        const std::string& field, int value) {
        return project(vm, patchItemPayload(self, itemId, {{field, static_cast<std::int64_t>(value)}}));
    });
    actionEditor.addFunc("setItemPayloadBool", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                     const std::string& field, bool value) {
        return project(vm, patchItemPayload(self, itemId, {{field, value}}));
    });
    actionEditor.addFunc("setItemPayloadVector3", [vm](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                        const std::string& field, float x, float y, float z) {
        return project(vm, patchItemPayload(self, itemId,
                                            {{field, Value::Array{static_cast<double>(x), static_cast<double>(y),
                                                                  static_cast<double>(z)}}}));
    });
    actionEditor.addFunc("setItemPayloadTextList", [vm](ScriptActionTimelineEditor* self,
                                                         const std::string& itemId, const std::string& field,
                                                         const std::string& delimitedValues) {
        Value::Array values;
        std::size_t  cursor = 0;
        while (cursor <= delimitedValues.size()) {
            const auto separator = delimitedValues.find(';', cursor);
            auto value = delimitedValues.substr(cursor, separator == std::string::npos
                                                             ? std::string::npos
                                                             : separator - cursor);
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                const auto last = value.find_last_not_of(" \t\r\n");
                values.emplace_back(value.substr(first, last - first + 1));
            }
            if (separator == std::string::npos) break;
            cursor = separator + 1;
        }
        return project(vm, patchItemPayload(self, itemId, {{field, std::move(values)}}));
    });
    actionEditor.addFunc("getItemPayloadText", [](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                   const std::string& field, const std::string& fallback) {
        const auto value = itemPayloadField(self, itemId, field);
        const auto* text = value ? value->getIf<std::string>() : nullptr;
        return text ? *text : fallback;
    });
    actionEditor.addFunc("getItemPayloadNumber", [](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                     const std::string& field, float fallback) {
        const auto value = itemPayloadField(self, itemId, field);
        if (!value) return fallback;
        if (const auto* decimal = value->getIf<double>()) return static_cast<float>(*decimal);
        if (const auto* integer = value->getIf<std::int64_t>()) return static_cast<float>(*integer);
        return fallback;
    });
    actionEditor.addFunc("getItemPayloadBool", [](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                   const std::string& field, bool fallback) {
        const auto value = itemPayloadField(self, itemId, field);
        const auto* boolean = value ? value->getIf<bool>() : nullptr;
        return boolean ? *boolean : fallback;
    });
    actionEditor.addFunc("getItemPayloadTextList", [](ScriptActionTimelineEditor* self,
                                                       const std::string& itemId, const std::string& field) {
        const auto value = itemPayloadField(self, itemId, field);
        const auto* array = value ? value->getIf<Value::Array>() : nullptr;
        if (!array) return std::string{};
        std::string result;
        for (const auto& entry : *array) {
            const auto* text = entry.getIf<std::string>();
            if (!text) return std::string{};
            if (!result.empty()) result += "; ";
            result += *text;
        }
        return result;
    });
    actionEditor.addFunc("getItemPayloadVector", [](ScriptActionTimelineEditor* self, const std::string& itemId,
                                                     const std::string& field, int component, float fallback) {
        const auto value = itemPayloadField(self, itemId, field);
        const auto* array = value ? value->getIf<Value::Array>() : nullptr;
        if (!array || component < 0 || component >= static_cast<int>(array->size())) return fallback;
        const auto& entry = (*array)[static_cast<std::size_t>(component)];
        if (const auto* decimal = entry.getIf<double>()) return static_cast<float>(*decimal);
        if (const auto* integer = entry.getIf<std::int64_t>()) return static_cast<float>(*integer);
        return fallback;
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

    moduleClass.addFunc("createAssetCatalog", [vm](eve::action_editor::ActionEditorModule*,
                                                    const std::string& projectRoot) {
        if (projectRoot.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "project root must not be empty");
        auto object = script::makeOwnedSquirrelInstance<ScriptActionTimelineAssetCatalog>(
            vm, std::make_unique<ScriptActionTimelineAssetCatalog>(std::filesystem::path(projectRoot)));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned  = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
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
    moduleClass.addFunc(
        "openDocument",
        [vm, clipboard](eve::action_editor::ActionEditorModule*, const std::string& projectRoot,
                        const std::string& assetGuid, const std::string& title, const std::string& resourceUri,
                        const ssq::Object& initialTimelineObject) {
            if (projectRoot.empty() || assetGuid.empty() || resourceUri.empty())
                return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                      "project root, asset guid and resource URI are required");
            script::SquirrelValueOptions options;
            options.source = kBindingSource;
            auto value     = script::valueFromSquirrel(initialTimelineObject, options);
            if (!value) return script::projectStatusResult(vm, value.status(), false, false);
            auto initialTimeline = action::ActionTimeline::fromValue(value.value());
            if (!initialTimeline)
                return script::projectStatusResult(vm, initialTimeline.status(), false, false);
            auto initialValue = initialTimeline.value().toValue();
            if (!initialValue) return script::projectStatusResult(vm, initialValue.status(), false, false);

            auto store     = std::make_unique<DiskAtomicDocumentStore>(std::filesystem::path(projectRoot));
            auto documents = std::make_unique<DocumentService>(store.get());
            auto opened    = documents->open({DocumentKind::Timeline, AssetGuid(assetGuid)}, title, resourceUri,
                                              toEditorValue(initialValue.value()));
            if (!opened.ok()) return script::projectStatusResult(vm, opened.status(), false, false);
            auto content = documents->content(opened.value().id);
            if (!content.ok()) return script::projectStatusResult(vm, content.status(), false, false);
            auto timeline = action::ActionTimeline::fromValue(toPresentationValue(content.value()));
            if (!timeline) return script::projectStatusResult(vm, timeline.status(), false, false);
            auto registry = action::ActionNotifyRegistry::withBuiltins();
            if (!registry) return script::projectStatusResult(vm, registry.status(), false, false);
            auto instance = std::make_unique<ScriptActionTimelineEditor>(
                assetGuid, std::move(timeline).takeValue(), std::move(registry).takeValue(), clipboard);
            instance->attachDocument(std::move(store), std::move(documents), opened.value());
            auto object = script::makeOwnedSquirrelInstance<ScriptActionTimelineEditor>(vm, std::move(instance));
            if (!object) return script::projectStatusResult(vm, object.status(), false, false);
            ssq::Object owned  = std::move(object).takeValue();
            auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
            result.set("value", owned);
            result.set("ownership", std::string("owned"));
            return result;
        });
}

}  // namespace eve::editor
