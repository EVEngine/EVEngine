#include "graphics_editing/OffscreenPreview.h"

#include "graphics/Canvas.h"
#include "graphics/ICanvasFactory.h"
#include "graphics/ICanvasTarget.h"
#include "image/ImageData.h"

#include <cmath>
#include <map>
#include <unordered_map>

namespace eve::graphics_editing {

namespace {
struct CanvasTargetRestore {
    graphics::ICanvasTarget* target;
    graphics::Canvas*        previous;
    ~CanvasTargetRestore() { target->setCanvas(previous); }
};
}  // namespace

struct GraphicsOffscreenPreviewService::Impl {
    struct Artifact {
        OffscreenPreviewArtifact          metadata;
        std::unique_ptr<image::ImageData> pixels;
    };
    graphics::Graphics*                              graphics = nullptr;
    graphics::ICanvasFactory*                        canvases = nullptr;
    graphics::ICanvasTarget*                         targets  = nullptr;
    std::map<std::pair<int, int>, graphics::Canvas*> canvasCache;
    std::unordered_map<std::string, Artifact>        artifacts;
    std::uint64_t                                    sequence = 0;
};

GraphicsOffscreenPreviewService::GraphicsOffscreenPreviewService(graphics::Graphics*       graphics,
                                                                 graphics::ICanvasFactory* canvases,
                                                                 graphics::ICanvasTarget*  targets)
    : impl_(std::make_unique<Impl>()) {
    impl_->graphics = graphics;
    impl_->canvases = canvases;
    impl_->targets  = targets;
}
GraphicsOffscreenPreviewService::~GraphicsOffscreenPreviewService() = default;

EditorResult<OffscreenPreviewArtifact> GraphicsOffscreenPreviewService::render(const OffscreenPreviewRequest& request,
                                                                               const DrawCallback&            draw,
                                                                               int           maximumDimension,
                                                                               std::uint64_t maximumPixels) {
    if (!impl_->graphics || !impl_->canvases || !impl_->targets || request.previewId.empty() || !draw ||
        request.sourceRevision == 0 || request.width <= 0 || request.height <= 0 || maximumDimension <= 0 ||
        request.width > maximumDimension || request.height > maximumDimension ||
        static_cast<std::uint64_t>(request.width) * static_cast<std::uint64_t>(request.height) > maximumPixels)
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Rejected, RuleId("editor.preview.invalid-offscreen-request"),
            "Offscreen preview requires graphics, identity, revision and bounded dimensions");
    for (double component : request.clearColor)
        if (!std::isfinite(component) || component < 0.0 || component > 1.0)
            return eve::editing::failed<OffscreenPreviewArtifact>(EditorStatus::Rejected,
                                                                  RuleId("editor.preview.invalid-clear-color"),
                                                                  "Offscreen clear color must be normalized");
    const auto         key    = std::make_pair(request.width, request.height);
    graphics::Canvas*& canvas = impl_->canvasCache[key];
    if (!canvas) canvas = impl_->canvases->newCanvas(request.width, request.height);
    if (!canvas)
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Failed, RuleId("editor.preview.canvas-allocation"),
            "Graphics backend could not allocate an offscreen Canvas");
    graphics::Canvas* previous = impl_->targets->getCanvas();
    impl_->targets->setCanvas(canvas);
    CanvasTargetRestore restore{impl_->targets, previous};
    canvas->clear(graphics::Color(static_cast<float>(request.clearColor[0]), static_cast<float>(request.clearColor[1]),
                                  static_cast<float>(request.clearColor[2]), static_cast<float>(request.clearColor[3])),
                  0, 1.0);
    EditorResult<void> drawn = [&]() -> EditorResult<void> {
        try {
            return draw(impl_->graphics, canvas);
        } catch (const std::exception& exception) {
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.preview.draw-exception"),
                                               exception.what());
        } catch (...) {
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.preview.draw-exception"),
                                               "Offscreen preview draw callback threw an exception");
        }
    }();
    if (!drawn.ok()) {
        return EditorResult<OffscreenPreviewArtifact>::failure(drawn.status());
    }
    std::unique_ptr<image::ImageData> pixels(canvas->newImageData());
    if (!pixels)
        return eve::editing::failed<OffscreenPreviewArtifact>(
            EditorStatus::Failed, RuleId("editor.preview.readback-failed"),
            "Graphics backend could not read back the preview Canvas");
    OffscreenPreviewArtifact metadata;
    metadata.previewId      = request.previewId;
    metadata.sourceRevision = request.sourceRevision;
    metadata.width          = request.width;
    metadata.height         = request.height;
    metadata.handle = "preview://" + request.previewId.value() + "/" + std::to_string(request.sourceRevision) + "/" +
                      std::to_string(++impl_->sequence);
    impl_->artifacts.insert_or_assign(metadata.handle, Impl::Artifact{metadata, std::move(pixels)});
    return eve::editing::applied<OffscreenPreviewArtifact>(std::move(metadata));
}

EditorResult<const image::ImageData*> GraphicsOffscreenPreviewService::image(const std::string& handle,
                                                                             Revision           currentRevision) const {
    const auto found = impl_->artifacts.find(handle);
    if (found == impl_->artifacts.end())
        return eve::editing::failed<const image::ImageData*>(EditorStatus::NotFound,
                                                             RuleId("editor.preview.artifact-not-found"),
                                                             "Offscreen preview artifact was not found");
    if (found->second.metadata.sourceRevision != currentRevision)
        return eve::editing::failed<const image::ImageData*>(
            EditorStatus::Conflict, RuleId("editor.preview.stale-artifact"),
            "Offscreen preview artifact belongs to an older source revision");
    return eve::editing::applied<const image::ImageData*>(found->second.pixels.get());
}

EditorResult<void> GraphicsOffscreenPreviewService::release(const std::string& handle) {
    if (!impl_->artifacts.erase(handle))
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.preview.artifact-not-found"),
                                          "Offscreen preview artifact was not found");
    return eve::editing::applied<void>();
}

}  // namespace eve::graphics_editing
