#pragma once

/** @file IImageResourceFactory.h @brief Result-based graphics boundary for runtime image assets. */

#include "common/Result.h"

#include <cstdint>

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

    /**
     * @brief Release a texture previously returned by this factory.
     * @param texture Borrowed live factory-owned texture, not retained after the call.
     * @thread Must run on the graphics thread.
     */
    [[nodiscard]] virtual Result<void> releaseImage(Texture* texture) = 0;
};

}  // namespace eve::graphics
