#include "editor/EditorUiOffscreen.h"

#include "graphics/ISolidRectRenderer.h"

namespace eve::editor {

EditorResult<OffscreenPreviewArtifact> UiOffscreenPreviewRenderer::render(
    const UiDocumentTarget& document, int width, int height) const {
    if (!previews_ || !rectangles_)
        return EditorResult<OffscreenPreviewArtifact>::error(EditorStatus::Rejected,
            RuleId("editor.ui.offscreen-renderer"), "UI offscreen preview requires Canvas and 2D graphics services");
    if ((assets_ == nullptr) != (skins_ == nullptr))
        return EditorResult<OffscreenPreviewArtifact>::error(EditorStatus::Rejected,
            RuleId("editor.ui.offscreen-skin-services"),
            "UI skin preview requires both an asset resolver and plan renderer");
    UiDocumentPreviewService layout;
    const UiPreviewSnapshot snapshot = layout.build(document, width, height);
    if (snapshot.status != EditorStatus::Applied) {
        EditorResult<OffscreenPreviewArtifact> failed; failed.status = snapshot.status;
        failed.diagnostics = snapshot.diagnostics; return failed;
    }
    UiSkinDrawPlan skinPlan;
    if (assets_ && skins_) {
        skinPlan = UiSkinPreviewPlanner().build(document, snapshot, *assets_);
        if (skinPlan.status != EditorStatus::Applied) {
            EditorResult<OffscreenPreviewArtifact> failed; failed.status = skinPlan.status;
            failed.diagnostics = skinPlan.diagnostics; return failed;
        }
    }
    OffscreenPreviewRequest request;
    request.previewId = StableId(document.targetId() + ":ui-preview");
    request.sourceRevision = document.revision(); request.width = width; request.height = height;
    return previews_->render(request, [&](graphics::Graphics*, graphics::Canvas*) {
        for (const auto& rectangle : snapshot.widgets) {
            if (!rectangle.visible) continue;
            auto widget = document.widget(rectangle.id);
            if (!widget.value) return EditorResult<void>::error(EditorStatus::Conflict,
                RuleId("editor.ui.offscreen-widget-stale"), "UI widget changed during offscreen rendering");
            const UiStyleValue& style = widget.value->style;
            rectangles_->drawSolidRectRGBA(static_cast<float>(rectangle.x), static_cast<float>(rectangle.y),
                static_cast<float>(rectangle.width), static_cast<float>(rectangle.height),
                static_cast<float>(style.tintR), static_cast<float>(style.tintG),
                static_cast<float>(style.tintB), static_cast<float>(style.tintA));
        }
        if (skins_) {
            auto rendered = skins_->render(skinPlan);
            if (!rendered.isAccepted()) return rendered;
        }
        return EditorResult<void>::applied();
    });
}

}  // namespace eve::editor
