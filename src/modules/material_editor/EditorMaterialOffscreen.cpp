#include "material_editor/EditorMaterialOffscreen.h"

namespace eve::editor {

MaterialPreviewRenderResult OffscreenMaterialPreviewRenderer::render(
    const MaterialPreviewRenderRequest& request) {
    MaterialPreviewRenderResult result;
    if (!previews_ || !draw_) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            DiagnosticCode::PreconditionViolation, RuleId("editor.material.offscreen-renderer"),
            DiagnosticSeverity::Error,
            "Material offscreen preview requires service and draw callback"));
        return result;
    }
    OffscreenPreviewRequest offscreen;
    offscreen.previewId = request.sceneId;
    offscreen.sourceRevision = request.documentRevision;
    offscreen.width = request.settings.width;
    offscreen.height = request.settings.height;
    auto rendered = previews_->render(offscreen, [&](graphics::Graphics* graphics, graphics::Canvas* canvas) {
        return draw_(request, graphics, canvas);
    });
    result.status = rendered.code();
    result.diagnostics = rendered.diagnostics();
    if (rendered.ok()) result.artifact = rendered.value().handle;
    return result;
}

}  // namespace eve::editor
