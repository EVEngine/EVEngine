#pragma once
#include "editor/EditorOffscreenPreview.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "stylize_editing/StylizeTarget.h"

namespace eve::editor {
using StylizePassValue=stylize_editing::StylizePassValue;
using StylizeRecipeTarget=stylize_editing::StylizeRecipeTarget;
using StylizeRecipeRuntime=stylize_editing::StylizeRecipeRuntime;

/** @brief Host presentation adapter for isolated stylize preview readback. */
class StylizeOffscreenPreviewService {
public:
    StylizeOffscreenPreviewService(GraphicsOffscreenPreviewService* previews, graphics::Graphics* graphics)
        : previews_(previews), graphics_(graphics) {}
    EditorResult<OffscreenPreviewArtifact> render(const StylizeRecipeTarget& document,
                                                   graphics::Texture* source,
                                                   const StableId& previewId,
                                                   int width, int height) const;
private:
    GraphicsOffscreenPreviewService* previews_=nullptr;
    graphics::Graphics* graphics_=nullptr;
};
}  // namespace eve::editor
