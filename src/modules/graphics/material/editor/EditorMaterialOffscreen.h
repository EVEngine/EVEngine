#pragma once

#include "graphics/material/editor/EditorMaterialPreview.h"
#include "graphics/editor/EditorOffscreenPreview.h"

#include <functional>

namespace eve::editor {

/** @brief Material renderer adapter backed by the shared bound offscreen Canvas service. */
class EVENGINE_API_EDITORS OffscreenMaterialPreviewRenderer final : public IMaterialPreviewRenderer {
public:
    using DrawCallback = std::function<Result<void>(const MaterialPreviewRenderRequest&,
                                                           graphics::Graphics*, graphics::Canvas*)>;
    /** @brief Offscreen material preview renderer. */
    OffscreenMaterialPreviewRenderer(GraphicsOffscreenPreviewService* previews, DrawCallback draw)
        /** @brief Previews. */
        : previews_(previews), draw_(std::move(draw)) {}
    /** @brief Renders . */
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override;
private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    DrawCallback draw_;
};

}  // namespace eve::editor
