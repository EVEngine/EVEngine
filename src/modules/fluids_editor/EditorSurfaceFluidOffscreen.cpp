#include "fluids_editor/EditorSurfaceFluidPreview.h"

namespace eve::editor {

EditorResult<OffscreenPreviewArtifact> SurfaceFluidOffscreenPreviewService::render(
    const SurfaceFluidTarget& target, const SurfaceFluidPreviewRequest& request) const {
    if (!previews_ || !renderer_ || request.previewId.empty())
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Rejected, RuleId("editor.surface-fluid.offscreen-services"),
            "Surface fluid offscreen preview requires identity, Canvas and renderer services");
    SurfaceFluidPreviewSnapshot snapshot = SurfaceFluidPreviewService().build(target, request);
    if (snapshot.status != EditorStatus::Applied)
        return EditorResult<OffscreenPreviewArtifact>::failure(
            Status(snapshot.status, std::move(snapshot.diagnostics)));
    OffscreenPreviewRequest offscreen;
    offscreen.previewId      = request.previewId;
    offscreen.sourceRevision = request.documentRevision;
    offscreen.width          = request.width;
    offscreen.height         = request.height;
    return previews_->render(offscreen,
                             [&](graphics::Graphics*, graphics::Canvas*) { return renderer_->draw(snapshot); });
}

}  // namespace eve::editor
