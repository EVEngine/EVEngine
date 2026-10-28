#include "graphics/material/editor/EditorMaterialPreview.h"
#include <cmath>
#include <iterator>
#include <set>
#include <utility>
#include "graphics/material/editor/EditorMaterialStudio.h"

// EditorMaterialPreview.cpp
namespace eve::editor {
namespace {

std::vector<EditorDiagnostic> validateSettings(const MaterialPreviewSettings& settings) {
    std::vector<EditorDiagnostic> diagnostics;
    const auto                    error = [&](const char* rule, std::string message) {
        diagnostics.push_back(eve::editing::ruleDiagnostic(DiagnosticCode::PreconditionViolation, RuleId(rule),
                                                                              DiagnosticSeverity::Error, std::move(message)));
    };
    static const std::set<std::string> geometries{"sphere", "cube", "plane", "custom"};
    if (!geometries.contains(settings.geometry))
        error("editor.material.preview-geometry", "Material preview geometry is unsupported: " + settings.geometry);
    if (settings.geometry == "custom" && settings.customMeshAsset.empty())
        error("editor.material.preview-mesh-required", "Custom material preview requires a mesh asset");
    if (settings.width < 16 || settings.height < 16 || settings.width > 4096 || settings.height > 4096)
        error("editor.material.preview-resolution", "Material preview resolution must be between 16 and 4096");
    if (settings.distance <= 0.0)
        error("editor.material.preview-distance", "Material preview camera distance must be positive");
    return diagnostics;
}

}  // namespace

Result<TaskId> MaterialPreviewService::render(const DocumentId& document, const MaterialDocumentTarget& material,
                                              MaterialPreviewSettings settings, IMaterialPreviewRenderer& renderer) {
    if (document.empty())
        return eve::editing::failed<TaskId>(EditorStatus::Rejected, RuleId("editor.material.preview-document"),
                                            "Material preview document id is required");
    auto diagnostics         = material.validate();
    auto settingsDiagnostics = validateSettings(settings);
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(settingsDiagnostics.begin()),
                       std::make_move_iterator(settingsDiagnostics.end()));
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity() == DiagnosticSeverity::Error)
            return Result<TaskId>::failure(Status(EditorStatus::Rejected, std::move(diagnostics)));
    const TaskId                 task("material-preview-" + std::to_string(++sequence_));
    MaterialPreviewRenderRequest request;
    request.sceneId                      = StableId(task.value() + ":isolated-scene");
    request.document                     = document;
    request.documentRevision             = material.revision();
    request.material                     = material.snapshotValue();
    request.settings                     = std::move(settings);
    MaterialPreviewRenderResult rendered = renderer.render(request);
    rendered.diagnostics.insert(rendered.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
    if (rendered.status == EditorStatus::Applied && rendered.artifact.empty()) {
        rendered.status = EditorStatus::Failed;
        rendered.diagnostics.push_back(
            eve::editing::ruleDiagnostic(DiagnosticCode::Failed, RuleId("editor.material.preview-artifact-required"),
                                         DiagnosticSeverity::Error, "Preview renderer succeeded without an artifact"));
    }
    tasks_.emplace(task, Task{document, request.documentRevision, std::move(rendered)});
    return eve::editing::applied<TaskId>(task);
}

Result<MaterialPreviewRenderResult> MaterialPreviewService::result(const TaskId& task) const {
    const auto found = tasks_.find(task);
    if (found == tasks_.end())
        return eve::editing::failed<MaterialPreviewRenderResult>(EditorStatus::NotFound,
                                                                 RuleId("editor.material.preview-task-not-found"),
                                                                 "Material preview task was not found");
    return eve::editing::applied<MaterialPreviewRenderResult>(found->second.result);
}

Result<void> MaterialPreviewService::publish(const DocumentId& document, Revision currentRevision, const TaskId& task) {
    const auto found = tasks_.find(task);
    if (found == tasks_.end())
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.material.preview-task-not-found"),
                                          "Material preview task was not found");
    if (found->second.document != document)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.preview-document-mismatch"),
                                          "Material preview belongs to another document");
    if (found->second.revision != currentRevision)
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.material.preview-stale"),
                                          "Material changed after the preview was rendered");
    if (found->second.result.status != EditorStatus::Applied || found->second.result.artifact.empty()) {
        const EditorStatus status =
            found->second.result.status == EditorStatus::Applied ? EditorStatus::Failed : found->second.result.status;
        return Result<void>::failure(Status(status, found->second.result.diagnostics));
    }
    published_[document] = found->second.result.artifact;
    return eve::editing::applied<void>();
}

std::string MaterialPreviewService::publishedArtifact(const DocumentId& document) const {
    const auto found = published_.find(document);
    return found == published_.end() ? std::string{} : found->second;
}

}  // namespace eve::editor

// EditorMaterialStudio.cpp
namespace eve::editor {
namespace {

Result<void> noStudioChange() { return eve::editing::noOp(); }

}  // namespace

MaterialStudioController::MaterialStudioController(DocumentId document, MaterialPublishingTarget& target,
                                                   IEditorTransactionBackend& transactions,
                                                   MaterialPreviewService& previews, IMaterialPreviewRenderer& renderer)
    : document_(std::move(document)),
      target_(target),
      transactions_(transactions),
      previews_(previews),
      renderer_(renderer) {}

Result<void> MaterialStudioController::setPreviewSettings(MaterialPreviewSettings settings) {
    settings_     = std::move(settings);
    previewDirty_ = true;
    return eve::editing::applied<void>();
}

Result<void> MaterialStudioController::beginInteraction(PropertyPath path) {
    if (activeProperty_ || transactions_.active())
        return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.material.studio-interaction-active"),
                                          "Finish or cancel the active material edit first");
    const auto selection  = selectionFor(target_.authoringTarget());
    const auto descriptor = target_.authoringTarget().schema(selection).find(path);
    if (!descriptor)
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.material.studio-property"),
                                          "Material property is not present in the schema: " + path.value());
    draft_          = std::make_unique<MaterialDocumentTarget>(target_.authoringTarget());
    activeProperty_ = std::move(path);
    finalValue_.reset();
    diagnostics_.clear();
    return eve::editing::applied<void>();
}

Result<void> MaterialStudioController::updateInteraction(EditorValue value) {
    if (!draft_ || !activeProperty_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.studio-no-interaction"),
                                          "Begin a material interaction before updating it");
    auto operation = draft_->makeSet(selectionFor(*draft_), *activeProperty_, value, PropertySetMode::Absolute);
    if (!operation.ok()) {
        diagnostics_ = operation.diagnostics();
        return Result<void>::failure(operation.status());
    }
    auto applied = draft_->applyDomainOperation(operation.value());
    if (!applied.ok()) {
        diagnostics_ = applied.diagnostics();
        return applied;
    }
    finalValue_   = std::move(value);
    diagnostics_  = draft_->validate();
    previewDirty_ = true;
    return eve::editing::applied<void>();
}

Result<TransactionReceipt> MaterialStudioController::commitInteraction() {
    if (!draft_ || !activeProperty_ || !finalValue_)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Rejected,
                                                        RuleId("editor.material.studio-no-final-value"),
                                                        "The material interaction has no value to commit");
    auto operation = target_.authoringTarget().makeSet(selectionFor(target_.authoringTarget()), *activeProperty_,
                                                       *finalValue_, PropertySetMode::Absolute);
    if (!operation.ok()) return Result<TransactionReceipt>::failure(operation.status());
    TransactionSpec specification;
    specification.id           = TransactionId("material-studio-" + std::to_string(++transactionSequence_));
    specification.label        = "Edit material " + activeProperty_->value();
    specification.target       = TargetId(target_.targetId());
    specification.baseRevision = target_.revision();
    specification.mergeKey     = operation.value().mergeKey;
    auto begun                 = transactions_.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("editor.material.studio-begin"),
                                                        "Could not begin the material transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        [[maybe_unused]] auto discarded = transactions_.discard();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("editor.material.studio-append"),
                                                        "Could not append the material edit");
    }
    auto previewed = transactions_.preview();
    if (!previewed.ok()) {
        [[maybe_unused]] auto discarded = transactions_.discard();
        return eve::editing::failed<TransactionReceipt>(previewed.code(), RuleId("editor.material.studio-preflight"),
                                                        "Material transaction preflight failed");
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return committed;
    clearInteraction();
    previewDirty_ = true;
    return committed;
}

Result<void> MaterialStudioController::cancelInteraction() {
    if (!draft_) return noStudioChange();
    clearInteraction();
    previewDirty_ = true;
    diagnostics_  = target_.authoringTarget().validate();
    return eve::editing::applied<void>();
}

Result<void> MaterialStudioController::tick(std::uint64_t monotonicMilliseconds) {
    if (!previewDirty_) return noStudioChange();
    if (hasPreviewTimestamp_ && monotonicMilliseconds < lastPreviewMilliseconds_)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.studio-time-regressed"),
                                          "Material preview time must be monotonic");
    if (hasPreviewTimestamp_ && monotonicMilliseconds - lastPreviewMilliseconds_ < previewIntervalMilliseconds_)
        return noStudioChange();
    auto rendered = refreshPreview();
    if (rendered.ok()) {
        lastPreviewMilliseconds_ = monotonicMilliseconds;
        hasPreviewTimestamp_     = true;
    }
    return rendered;
}

Result<void> MaterialStudioController::refreshPreview() {
    return renderPreview(draft_ ? *draft_ : target_.authoringTarget());
}

Result<void> MaterialStudioController::setPreviewRate(double framesPerSecond) {
    if (!std::isfinite(framesPerSecond) || framesPerSecond < 1.0 || framesPerSecond > 240.0)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material.studio-preview-rate"),
                                          "Material preview rate must be between 1 and 240 Hz");
    previewIntervalMilliseconds_ = static_cast<std::uint64_t>(std::ceil(1000.0 / framesPerSecond));
    return eve::editing::applied<void>();
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
    item.item   = StableId(material.targetId().value());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

Result<void> MaterialStudioController::renderPreview(const MaterialDocumentTarget& material) {
    auto task = previews_.render(document_, material, settings_, renderer_);
    if (!task.ok()) {
        diagnostics_ = task.diagnostics();
        return eve::editing::failed<void>(task.code(), RuleId("editor.material.studio-preview"),
                                          "Material preview request was rejected");
    }
    auto result = previews_.result(task.value());
    if (!result.ok()) {
        diagnostics_ = result.diagnostics();
        return eve::editing::failed<void>(result.code(), RuleId("editor.material.studio-preview-result"),
                                          "Material preview result is unavailable");
    }
    auto published = previews_.publish(document_, material.revision(), task.value());
    diagnostics_   = result.value().diagnostics;
    if (!published.ok()) {
        diagnostics_.insert(diagnostics_.end(), published.diagnostics().begin(), published.diagnostics().end());
        return published;
    }
    previewArtifact_ = result.value().artifact;
    previewRevision_ = material.revision();
    previewDirty_    = false;
    return eve::editing::applied<void>();
}

void MaterialStudioController::clearInteraction() {
    draft_.reset();
    activeProperty_.reset();
    finalValue_.reset();
}

}  // namespace eve::editor
