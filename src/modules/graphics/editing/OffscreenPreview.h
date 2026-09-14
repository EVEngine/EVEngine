#pragma once

#include "editing/EditingProtocol.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Canvas;
class ICanvasFactory;
class ICanvasTarget;
}
namespace eve::image {
class ImageData;
}

namespace eve::graphics_editing {
using EditorStatus=editing::Status; using StableId=editing::StableId;
using Revision=editing::Revision; using RuleId=editing::RuleId;
template<class T>using EditorResult=editing::Result<T>;

/** @brief Immutable request for one isolated Canvas render and CPU readback. */
struct OffscreenPreviewRequest {
    StableId previewId;
    Revision sourceRevision = 0;
    int width = 0;
    int height = 0;
    std::array<double, 4> clearColor{0.08, 0.08, 0.10, 1.0};
};

/** @brief In-memory artifact metadata returned after synchronous readback. */
struct OffscreenPreviewArtifact {
    std::string handle;
    StableId previewId;
    Revision sourceRevision = 0;
    int width = 0;
    int height = 0;
};

/** @brief Concrete Graphics/Canvas preview bridge with revision-safe artifact storage. */
class GraphicsOffscreenPreviewService {
public:
    using DrawCallback = std::function<EditorResult<void>(graphics::Graphics*, graphics::Canvas*)>;
    /** @brief Bind borrowed draw, allocation and target-control interfaces. */
    GraphicsOffscreenPreviewService(graphics::Graphics* graphics,
                                    graphics::ICanvasFactory* canvases,
                                    graphics::ICanvasTarget* targets);
    ~GraphicsOffscreenPreviewService();
    GraphicsOffscreenPreviewService(const GraphicsOffscreenPreviewService&) = delete;
    GraphicsOffscreenPreviewService& operator=(const GraphicsOffscreenPreviewService&) = delete;
    /** @brief Clear, draw and read back one isolated offscreen Canvas. */
    EditorResult<OffscreenPreviewArtifact> render(const OffscreenPreviewRequest& request,
                                                  const DrawCallback& draw,
                                                  int maximumDimension = 4096,
                                                  std::uint64_t maximumPixels = 16777216);
    /** @brief Borrow pixels only when the requested source revision still matches. */
    EditorResult<const image::ImageData*> image(const std::string& handle,
                                                Revision currentRevision) const;
    /** @brief Remove an in-memory readback artifact. */
    EditorResult<void> release(const std::string& handle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::graphics_editing
