#include "editor/EditorMaterialPreview.h"

#include <iterator>
#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> previewError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

std::vector<EditorDiagnostic> validateSettings(const MaterialPreviewSettings& settings) {
    std::vector<EditorDiagnostic> diagnostics;
    static const std::set<std::string> geometries{"sphere", "cube", "plane", "custom"};
    if (!geometries.contains(settings.geometry))
        diagnostics.push_back({RuleId("editor.material.preview-geometry"), DiagnosticSeverity::Error,
                               "Material preview geometry is unsupported: " + settings.geometry});
    if (settings.geometry == "custom" && settings.customMeshAsset.empty())
        diagnostics.push_back({RuleId("editor.material.preview-mesh-required"), DiagnosticSeverity::Error,
                               "Custom material preview requires a mesh asset"});
    if (settings.width < 16 || settings.height < 16 || settings.width > 4096 || settings.height > 4096)
        diagnostics.push_back({RuleId("editor.material.preview-resolution"), DiagnosticSeverity::Error,
                               "Material preview resolution must be between 16 and 4096"});
    if (settings.distance <= 0.0)
        diagnostics.push_back({RuleId("editor.material.preview-distance"), DiagnosticSeverity::Error,
                               "Material preview camera distance must be positive"});
    return diagnostics;
}

}  // namespace

EditorResult<TaskId> MaterialPreviewService::render(const DocumentId& document,
                                                    const MaterialDocumentTarget& material,
                                                    MaterialPreviewSettings settings,
                                                    IMaterialPreviewRenderer& renderer) {
    if (document.empty())
        return previewError<TaskId>(EditorStatus::Rejected, "editor.material.preview-document",
                                    "Material preview document id is required");
    auto diagnostics = material.validate();
    auto settingsDiagnostics = validateSettings(settings);
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(settingsDiagnostics.begin()),
                       std::make_move_iterator(settingsDiagnostics.end()));
    for (const EditorDiagnostic& diagnostic : diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            EditorResult<TaskId> failed;
            failed.status = EditorStatus::Rejected;
            failed.diagnostics = std::move(diagnostics);
            return failed;
        }
    const TaskId task("material-preview-" + std::to_string(++sequence_));
    MaterialPreviewRenderRequest request;
    request.sceneId = StableId(task.value() + ":isolated-scene");
    request.document = document;
    request.documentRevision = material.revision();
    request.material = material.snapshotValue();
    request.settings = std::move(settings);
    MaterialPreviewRenderResult rendered = renderer.render(request);
    rendered.diagnostics.insert(rendered.diagnostics.begin(), diagnostics.begin(), diagnostics.end());
    if (rendered.status == EditorStatus::Applied && rendered.artifact.empty()) {
        rendered.status = EditorStatus::Failed;
        rendered.diagnostics.push_back({RuleId("editor.material.preview-artifact-required"),
                                        DiagnosticSeverity::Error,
                                        "Preview renderer succeeded without an artifact"});
    }
    tasks_.emplace(task, Task{document, request.documentRevision, std::move(rendered)});
    return EditorResult<TaskId>::applied(task);
}

EditorResult<MaterialPreviewRenderResult> MaterialPreviewService::result(const TaskId& task) const {
    const auto found = tasks_.find(task);
    if (found == tasks_.end())
        return previewError<MaterialPreviewRenderResult>(EditorStatus::NotFound,
                                                         "editor.material.preview-task-not-found",
                                                         "Material preview task was not found");
    return EditorResult<MaterialPreviewRenderResult>::applied(found->second.result);
}

EditorResult<void> MaterialPreviewService::publish(const DocumentId& document, Revision currentRevision,
                                                   const TaskId& task) {
    const auto found = tasks_.find(task);
    if (found == tasks_.end())
        return previewError<void>(EditorStatus::NotFound, "editor.material.preview-task-not-found",
                                  "Material preview task was not found");
    if (found->second.document != document)
        return previewError<void>(EditorStatus::Rejected, "editor.material.preview-document-mismatch",
                                  "Material preview belongs to another document");
    if (found->second.revision != currentRevision)
        return previewError<void>(EditorStatus::Conflict, "editor.material.preview-stale",
                                  "Material changed after the preview was rendered");
    if (found->second.result.status != EditorStatus::Applied || found->second.result.artifact.empty()) {
        EditorResult<void> failed;
        failed.status = found->second.result.status == EditorStatus::Applied ? EditorStatus::Failed
                                                                             : found->second.result.status;
        failed.diagnostics = found->second.result.diagnostics;
        return failed;
    }
    published_[document] = found->second.result.artifact;
    return EditorResult<void>::applied();
}

std::string MaterialPreviewService::publishedArtifact(const DocumentId& document) const {
    const auto found = published_.find(document);
    return found == published_.end() ? std::string{} : found->second;
}

}  // namespace eve::editor
