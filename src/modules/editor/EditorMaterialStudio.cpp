#include "editor/EditorMaterialStudio.h"

#include <cmath>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> studioError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

EditorResult<void> noStudioChange() {
    EditorResult<void> result;
    result.status = EditorStatus::NoOp;
    return result;
}

}  // namespace

MaterialStudioController::MaterialStudioController(DocumentId document, MaterialPublishingTarget& target,
                                                   IEditorTransactionBackend& transactions,
                                                   MaterialPreviewService& previews, IMaterialPreviewRenderer& renderer)
    : document_(std::move(document)),
      target_(target),
      transactions_(transactions),
      previews_(previews),
      renderer_(renderer) {}

EditorResult<void> MaterialStudioController::setPreviewSettings(MaterialPreviewSettings settings) {
    settings_     = std::move(settings);
    previewDirty_ = true;
    return EditorResult<void>::applied();
}

EditorResult<void> MaterialStudioController::beginInteraction(PropertyPath path) {
    if (activeProperty_ || transactions_.active())
        return studioError<void>(EditorStatus::Conflict, "editor.material.studio-interaction-active",
                                 "Finish or cancel the active material edit first");
    const auto selection  = selectionFor(target_.authoringTarget());
    const auto descriptor = target_.authoringTarget().schema(selection).find(path);
    if (!descriptor)
        return studioError<void>(EditorStatus::NotFound, "editor.material.studio-property",
                                 "Material property is not present in the schema: " + path.value());
    draft_          = std::make_unique<MaterialDocumentTarget>(target_.authoringTarget());
    activeProperty_ = std::move(path);
    finalValue_.reset();
    diagnostics_.clear();
    return EditorResult<void>::applied();
}

EditorResult<void> MaterialStudioController::updateInteraction(EditorValue value) {
    if (!draft_ || !activeProperty_)
        return studioError<void>(EditorStatus::Rejected, "editor.material.studio-no-interaction",
                                 "Begin a material interaction before updating it");
    auto operation = draft_->makeSet(selectionFor(*draft_), *activeProperty_, value, PropertySetMode::Absolute);
    if (!operation.isAccepted() || !operation.value) {
        EditorResult<void> failed;
        failed.status      = operation.status;
        failed.diagnostics = std::move(operation.diagnostics);
        diagnostics_       = failed.diagnostics;
        return failed;
    }
    auto applied = draft_->applyDomainOperation(*operation.value);
    if (!applied.isAccepted()) {
        diagnostics_ = applied.diagnostics;
        return applied;
    }
    finalValue_   = std::move(value);
    diagnostics_  = draft_->validate();
    previewDirty_ = true;
    return EditorResult<void>::applied();
}

EditorResult<TransactionReceipt> MaterialStudioController::commitInteraction() {
    if (!draft_ || !activeProperty_ || !finalValue_)
        return studioError<TransactionReceipt>(EditorStatus::Rejected, "editor.material.studio-no-final-value",
                                               "The material interaction has no value to commit");
    auto operation = target_.authoringTarget().makeSet(selectionFor(target_.authoringTarget()), *activeProperty_,
                                                       *finalValue_, PropertySetMode::Absolute);
    if (!operation.isAccepted() || !operation.value) {
        EditorResult<TransactionReceipt> failed;
        failed.status      = operation.status;
        failed.diagnostics = std::move(operation.diagnostics);
        return failed;
    }
    TransactionSpec specification;
    specification.id           = TransactionId("material-studio-" + std::to_string(++transactionSequence_));
    specification.label        = "Edit material " + activeProperty_->value();
    specification.target       = TargetId(target_.targetId());
    specification.baseRevision = target_.revision();
    specification.mergeKey     = operation.value->mergeKey;
    auto begun                 = transactions_.begin(std::move(specification));
    if (!begun.isAccepted())
        return studioError<TransactionReceipt>(begun.status, "editor.material.studio-begin",
                                               "Could not begin the material transaction");
    auto appended = transactions_.append(std::move(*operation.value));
    if (!appended.isAccepted()) {
        [[maybe_unused]] auto discarded = transactions_.discard();
        return studioError<TransactionReceipt>(appended.status, "editor.material.studio-append",
                                               "Could not append the material edit");
    }
    auto previewed = transactions_.preview();
    if (!previewed.isAccepted()) {
        [[maybe_unused]] auto discarded = transactions_.discard();
        return studioError<TransactionReceipt>(previewed.status, "editor.material.studio-preflight",
                                               "Material transaction preflight failed");
    }
    auto committed = transactions_.commit();
    if (!committed.isAccepted()) return committed;
    clearInteraction();
    previewDirty_ = true;
    return committed;
}

EditorResult<void> MaterialStudioController::cancelInteraction() {
    if (!draft_) return noStudioChange();
    clearInteraction();
    previewDirty_ = true;
    diagnostics_  = target_.authoringTarget().validate();
    return EditorResult<void>::applied();
}

EditorResult<void> MaterialStudioController::tick(std::uint64_t monotonicMilliseconds) {
    if (!previewDirty_) return noStudioChange();
    if (hasPreviewTimestamp_ && monotonicMilliseconds < lastPreviewMilliseconds_)
        return studioError<void>(EditorStatus::Rejected, "editor.material.studio-time-regressed",
                                 "Material preview time must be monotonic");
    if (hasPreviewTimestamp_ && monotonicMilliseconds - lastPreviewMilliseconds_ < previewIntervalMilliseconds_)
        return noStudioChange();
    auto rendered = refreshPreview();
    if (rendered.isAccepted()) {
        lastPreviewMilliseconds_ = monotonicMilliseconds;
        hasPreviewTimestamp_     = true;
    }
    return rendered;
}

EditorResult<void> MaterialStudioController::refreshPreview() {
    return renderPreview(draft_ ? *draft_ : target_.authoringTarget());
}

EditorResult<void> MaterialStudioController::setPreviewRate(double framesPerSecond) {
    if (!std::isfinite(framesPerSecond) || framesPerSecond < 1.0 || framesPerSecond > 240.0)
        return studioError<void>(EditorStatus::Rejected, "editor.material.studio-preview-rate",
                                 "Material preview rate must be between 1 and 240 Hz");
    previewIntervalMilliseconds_ = static_cast<std::uint64_t>(std::ceil(1000.0 / framesPerSecond));
    return EditorResult<void>::applied();
}

MaterialStudioState MaterialStudioController::state() const {
    MaterialStudioState result;
    result.interactionActive = draft_ != nullptr;
    result.previewDirty      = previewDirty_;
    result.activeProperty    = activeProperty_.value_or(PropertyPath{});
    result.documentRevision  = target_.revision();
    result.previewRevision   = previewRevision_;
    result.previewArtifact   = previewArtifact_;
    result.diagnostics       = diagnostics_;
    return result;
}

SelectionSnapshot MaterialStudioController::selectionFor(const MaterialDocumentTarget& material) const {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(material.targetId());
    item.item   = StableId(material.targetId());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

EditorResult<void> MaterialStudioController::renderPreview(const MaterialDocumentTarget& material) {
    auto task = previews_.render(document_, material, settings_, renderer_);
    if (!task.isAccepted() || !task.value) {
        diagnostics_ = std::move(task.diagnostics);
        return studioError<void>(task.status, "editor.material.studio-preview",
                                 "Material preview request was rejected");
    }
    auto result = previews_.result(*task.value);
    if (!result.isAccepted() || !result.value) {
        diagnostics_ = std::move(result.diagnostics);
        return studioError<void>(result.status, "editor.material.studio-preview-result",
                                 "Material preview result is unavailable");
    }
    auto published = previews_.publish(document_, material.revision(), *task.value);
    diagnostics_   = result.value->diagnostics;
    if (!published.isAccepted()) {
        diagnostics_.insert(diagnostics_.end(), published.diagnostics.begin(), published.diagnostics.end());
        return published;
    }
    previewArtifact_ = result.value->artifact;
    previewRevision_ = material.revision();
    previewDirty_    = false;
    return EditorResult<void>::applied();
}

void MaterialStudioController::clearInteraction() {
    draft_.reset();
    activeProperty_.reset();
    finalValue_.reset();
}

}  // namespace eve::editor
