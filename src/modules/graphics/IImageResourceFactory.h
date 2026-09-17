#pragma once

/** @file IImageResourceFactory.h @brief Result-based graphics boundary for runtime image assets. */

#include "common/Result.h"

#include <cstdint>
#include <span>

namespace eve::graphics {

class Texture;

/** @brief Backend-owned RGBA8 texture upload and release operations. */
class IImageResourceFactory {
public:
    virtual ~IImageResourceFactory() = default;

    /**
     * @brief Upload one tightly packed, top-down RGBA8 image.
     * @return Structured failure or a borrowed backend-owned texture.
     * @ownership The backend factory owns the returned texture; release it through this factory.
     * @lifetime Valid until `releaseImage`, backend shutdown, or graphics-device loss.
     * @thread Must run on the graphics thread; pixels are borrowed only for this call.
     */
    [[nodiscard]] virtual Result<Texture*> uploadRgba8(std::uint32_t width,
                                                       std::uint32_t height,
                                                       const std::uint8_t* pixels,
                                                       bool srgb) = 0;

    /** @brief Upload complete explicit linear RGBA8 mips without regeneration.
     * @param width Positive base width.
     * @param height Positive base height.
     * @param levels Full halving chain including 1x1.
     * @param pixels Borrowed top-down RGBA8 levels in increasing LOD order; not retained.
     * @return Failure without publication or a factory-owned texture, released by releaseImage.
     * @lifetime Returned texture lives until release, shutdown or device loss.
     * @thread Graphics thread, synchronous, non-reentrant, no callbacks. Default is Unsupported.
     */
    [[nodiscard]] virtual Result<Texture*> uploadRgba8MipChain(uint32_t width, uint32_t height, uint32_t levels,
                                                               std::span<const uint8_t> pixels) {
        (void)width;
        (void)height;
        (void)levels;
        (void)pixels;
        return Result<Texture*>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                           "image factory does not support explicit mips", {}, {},
                                                           "graphics.image.mips"));
    }

    /** @brief Upload one tightly packed linear RGBA8 volume.
     * @param width Positive X extent.
     * @param
     * height Positive Y extent.
     * @param depth Positive Z extent.
     * @param pixels Borrowed X-fastest RGBA8
     * voxels; not retained.
     * @return Failure without publication or a factory-owned texture, released by
     * releaseImage.
     * @lifetime Returned texture lives until release, shutdown or device loss.
     * @thread
     * Graphics thread, synchronous, non-reentrant, no callbacks. Default is Unsupported.
     */
    [[nodiscard]] virtual Result<Texture*> uploadRgba8Volume(uint32_t width, uint32_t height, uint32_t depth,
                                                             std::span<const uint8_t> pixels) {
        (void)width;
        (void)height;
        (void)depth;
        (void)pixels;
        return Result<Texture*>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                           "image factory does not support RGBA8 volumes", {}, {},
                                                           "graphics.image.volume"));
    }

    /**
     * @brief Release a texture previously returned by this factory.
     * @param texture Borrowed live factory-owned texture, not retained after the call.
     * @thread Must run on the graphics thread.
     */
    [[nodiscard]] virtual Result<void> releaseImage(Texture* texture) = 0;
};

}  // namespace eve::graphics
