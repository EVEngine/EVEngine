#include "ui_editor/EditorUiOffscreen.h"

#include "graphics/ISolidRectRenderer.h"

namespace eve::editor {

EditorResult<OffscreenPreviewArtifact> UiOffscreenPreviewRenderer::render(const UiDocumentTarget& document, int width,
                                                                          int height) const {
    if (!previews_ || !rectangles_)
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Rejected, RuleId("editor.ui.offscreen-renderer"),
            "UI offscreen preview requires Canvas and 2D graphics services");
    if ((assets_ == nullptr) != (skins_ == nullptr))
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Rejected, RuleId("editor.ui.offscreen-skin-services"),
            "UI skin preview requires both an asset resolver and plan renderer");
    UiDocumentPreviewService layout;
    const UiPreviewSnapshot  snapshot = layout.build(document, width, height);
    if (snapshot.status != EditorStatus::Applied)
        return EditorResult<OffscreenPreviewArtifact>::failure(
            Status(snapshot.status, snapshot.diagnostics));
    UiSkinDrawPlan skinPlan;
    if (assets_ && skins_) {
        skinPlan = UiSkinPreviewPlanner().build(document, snapshot, *assets_);
        if (skinPlan.status != EditorStatus::Applied)
            return EditorResult<OffscreenPreviewArtifact>::failure(
                Status(skinPlan.status, skinPlan.diagnostics));
    }
    OffscreenPreviewRequest request;
    request.previewId      = StableId(document.targetId().value() + ":ui-preview");
    request.sourceRevision = document.revision();
    request.width          = width;
    request.height         = height;
    return previews_->render(request, [&](graphics::Graphics*, graphics::Canvas*) {
        for (const auto& rectangle : snapshot.widgets) {
            if (!rectangle.visible) continue;
            auto widget = document.widget(rectangle.id);
            if (!widget.ok())
                return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("editor.ui.offscreen-widget-stale"),
                                                  "UI widget changed during offscreen rendering");
            const UiStyleValue& style = widget.value().style;
            rectangles_->drawSolidRectRGBA(static_cast<float>(rectangle.x), static_cast<float>(rectangle.y),
                                           static_cast<float>(rectangle.width), static_cast<float>(rectangle.height),
                                           static_cast<float>(style.tintR), static_cast<float>(style.tintG),
                                           static_cast<float>(style.tintB), static_cast<float>(style.tintA));
        }
        if (skins_) {
            auto rendered = skins_->render(skinPlan);
            if (!rendered.ok()) return rendered;
        }
        return eve::editing::applied<void>();
    });
}

}  // namespace eve::editor
