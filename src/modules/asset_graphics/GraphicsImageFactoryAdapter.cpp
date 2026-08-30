#include "asset_graphics/EvpackImageLoader.h"

#include "graphics/IResourceFactory.h"

#include <limits>

namespace eve::asset_graphics {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {},
                                                "asset.graphics.image.adapter"));
}

}  // namespace

Result<graphics::Texture*> GraphicsImageFactoryAdapter::uploadRgba8(
    std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels, bool srgb) {
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || !pixels)
        return failure<graphics::Texture*>(DiagnosticCode::InvalidArgument,
                                           "RGBA8 upload arguments exceed graphics limits");
    if (srgb)
        return failure<graphics::Texture*>(
            DiagnosticCode::Unsupported,
            "legacy graphics adapter requires Cooked linear RGBA8 pixels");
    graphics::Texture* texture = factory_.newTexture(static_cast<int>(width),
                                                      static_cast<int>(height), pixels);
    if (!texture)
        return failure<graphics::Texture*>(DiagnosticCode::Failed,
                                           "graphics backend rejected RGBA8 upload");
    return Result<graphics::Texture*>::success(texture);
}

Result<void> GraphicsImageFactoryAdapter::releaseImage(graphics::Texture* texture) {
    if (!texture)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "cannot release a null graphics texture");
    if (!factory_.releaseTexture(texture))
        return failure<void>(DiagnosticCode::Failed,
                             "graphics backend rejected texture release");
    return Result<void>::success();
}

}  // namespace eve::asset_graphics
