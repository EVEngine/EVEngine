#include "stylize_editor/EditorStylizeTarget.h"

#include <utility>

namespace eve::editor {

EditorResult<OffscreenPreviewArtifact> StylizeOffscreenPreviewService::render(
    const StylizeRecipeTarget& document, graphics::Texture* source,
    const StableId& previewId, int width, int height) const {
    if (!previews_ || !graphics_ || !source || previewId.empty())
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Rejected, RuleId("editor.stylize.offscreen"),
            "Stylize preview requires Canvas, Graphics, source and identity");
    StylizeRecipeRuntime candidate;
    auto published = candidate.publish(document, graphics_);
    if (!published.ok())
        return EditorResult<OffscreenPreviewArtifact>::failure(published.status());
    OffscreenPreviewRequest request;
    request.previewId = previewId;
    request.sourceRevision = document.revision();
    request.width = width;
    request.height = height;
    return previews_->render(request, [&](graphics::Graphics* graphics, graphics::Canvas* canvas) {
        return candidate.apply(graphics, source, canvas, document.revision());
    });
}

}  // namespace eve::editor
