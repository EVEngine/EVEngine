#pragma once

#include "editor/EditorOffscreenPreview.h"
#include "editor/EditorUiDocumentTarget.h"
#include "editor/EditorUiSkinPreview.h"

namespace eve::graphics { class ISolidRectRenderer; }

namespace eve::editor {

/** @brief Concrete UI box-model preview renderer using the shared offscreen Canvas. */
class UiOffscreenPreviewRenderer {
public:
    UiOffscreenPreviewRenderer(GraphicsOffscreenPreviewService* previews,
                               graphics::ISolidRectRenderer* rectangles,
                               const IUiSkinAssetResolver* assets = nullptr,
                               IUiSkinPlanRenderer* skins = nullptr)
        : previews_(previews), rectangles_(rectangles), assets_(assets), skins_(skins) {}
    /** @brief Layout and rasterize visible widget tint rectangles into a readback artifact. */
    EditorResult<OffscreenPreviewArtifact> render(const UiDocumentTarget& document,
                                                   int width, int height) const;
private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    graphics::ISolidRectRenderer* rectangles_ = nullptr;
    const IUiSkinAssetResolver* assets_ = nullptr;
    IUiSkinPlanRenderer* skins_ = nullptr;
};

}  // namespace eve::editor
