#pragma once
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "graphics/editor/EditorOffscreenPreview.h"
#include "stylize/editing/StylizeTarget.h"


namespace eve::editor {
using StylizePassValue     = stylize_editing::StylizePassValue;
using StylizeRecipeTarget  = stylize_editing::StylizeRecipeTarget;
using StylizeRecipeRuntime = stylize_editing::StylizeRecipeRuntime;

/** @brief Host presentation adapter for isolated stylize preview readback. */
class StylizeOffscreenPreviewService {
public:
    /** @brief Constructs a StylizeOffscreenPreviewService. */
    StylizeOffscreenPreviewService(GraphicsOffscreenPreviewService* previews, graphics::Graphics* graphics)
        /** @brief Previews. */
        : previews_(previews), graphics_(graphics) {}
    /** @brief Renders . */
    Result<OffscreenPreviewArtifact> render(const StylizeRecipeTarget& document, graphics::Texture* source,
                                                  const StableId& previewId, int width, int height) const;

private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    graphics::Graphics*              graphics_ = nullptr;
};
}  // namespace eve::editor
