#pragma once
#include "common/Export.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "graphics/editor/EditorOffscreenPreview.h"
#include "stylize/editing/StylizeTarget.h"


namespace eve::editor {
using StylizePassValue     = stylize_editing::StylizePassValue;
using StylizeRecipeTarget  = stylize_editing::StylizeRecipeTarget;
using StylizeRecipeRuntime = stylize_editing::StylizeRecipeRuntime;

/** @brief Host presentation adapter for isolated stylize preview readback. */
class EVENGINE_API_EDITORS StylizeOffscreenPreviewService {
public:
    /**
     * @brief Borrow the preview service and graphics backend; neither is owned.
     * @param previews Borrowed service that must outlive this adapter; nullable for unavailable-provider handling.
     * @param graphics Borrowed backend that must outlive this adapter; nullable for unavailable-provider handling.
     * @thread Render-owner thread only.
     */
    StylizeOffscreenPreviewService(GraphicsOffscreenPreviewService* previews, graphics::Graphics* graphics)
        /** @brief Previews. */
        : previews_(previews), graphics_(graphics) {}
    /**
     * @brief Render a revision-bound preview without changing the source document.
     * @param document Borrowed authoring document, valid for this synchronous call.
     * @param source Borrowed source texture, valid until the preview call returns.
     * @param previewId Stable identity of the preview artifact.
     * @param width Requested output width.
     * @param height Requested output height.
     * @return Owning artifact or structured admission, publication or backend failure.
     * @thread Render-owner thread only; the draw callback runs synchronously through the preview service.
     * @cost Expensive GPU rendering and readback per requested preview; amortize by source revision.
     */
    Result<OffscreenPreviewArtifact> render(const StylizeRecipeTarget& document, graphics::Texture* source,
                                                  const StableId& previewId, int width, int height) const;

private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    graphics::Graphics*              graphics_ = nullptr;
};
}  // namespace eve::editor
