#pragma once

#include "editor/EditorMaterialPreview.h"
#include "editor/EditorOffscreenPreview.h"

#include <functional>

namespace eve::editor {

/** @brief Material renderer adapter backed by the shared bound offscreen Canvas service. */
class OffscreenMaterialPreviewRenderer final : public IMaterialPreviewRenderer {
public:
    using DrawCallback = std::function<EditorResult<void>(const MaterialPreviewRenderRequest&,
                                                           graphics::Graphics*, graphics::Canvas*)>;
    OffscreenMaterialPreviewRenderer(GraphicsOffscreenPreviewService* previews, DrawCallback draw)
        : previews_(previews), draw_(std::move(draw)) {}
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override;
private:
    GraphicsOffscreenPreviewService* previews_ = nullptr;
    DrawCallback draw_;
};

}  // namespace eve::editor
