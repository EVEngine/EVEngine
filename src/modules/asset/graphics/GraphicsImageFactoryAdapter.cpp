#include "asset/graphics/EvpackImageLoader.h"

#include "graphics/IResourceFactory.h"
#include "graphics/Texture.h"

#include <limits>

namespace eve::asset_graphics {
namespace {

}  // namespace

Result<graphics::Texture*> GraphicsImageFactoryAdapter::uploadRgba8(
    std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels, bool srgb) {
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || !pixels)
        return Result<graphics::Texture*>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "RGBA8 upload arguments exceed graphics limits",
                                                                     {}, {}, "asset.graphics.image.adapter"));
    if (srgb)
        return Result<graphics::Texture*>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "legacy graphics adapter requires Cooked linear RGBA8 pixels", {}, {},
            "asset.graphics.image.adapter"));
    graphics::Texture* texture = factory_.newTexture(static_cast<int>(width),
                                                      static_cast<int>(height), pixels);
    if (!texture)
        return Result<graphics::Texture*>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "graphics backend rejected RGBA8 upload", {}, {}, "asset.graphics.image.adapter"));
    return Result<graphics::Texture*>::success(texture);
}

Result<graphics::Texture*> GraphicsImageFactoryAdapter::uploadRgba8MipChain(uint32_t width, uint32_t height,
                                                                            uint32_t                 levels,
                                                                            std::span<const uint8_t> pixels) {
    return factory_.newTextureMipChain(width, height, levels, pixels);
}

Result<graphics::Texture*> GraphicsImageFactoryAdapter::uploadRgba8Volume(uint32_t width, uint32_t height,
                                                                          uint32_t                 depth,
                                                                          std::span<const uint8_t> pixels) {
    return factory_.newTexture3DRgba8(width, height, depth, pixels);
}

Result<void> GraphicsImageFactoryAdapter::releaseImage(graphics::Texture* texture) {
    if (!texture)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "cannot release a null graphics texture", {}, {},
                                                       "asset.graphics.image.adapter"));
    if (!factory_.releaseTexture(texture))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "graphics backend rejected texture release", {}, {},
                                                       "asset.graphics.image.adapter"));
    // Successful backend release transfers the detached CPU facade to this caller.
    delete texture;
    return Result<void>::success();
}

}  // namespace eve::asset_graphics
