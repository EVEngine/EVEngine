#pragma once

#include "medialoader/image/FormatHandler.h"

namespace eve::image {

/** @brief Internal decoder adapter; each call owns its output and borrows input synchronously. */
class WebPHandler final : public medialoader::FormatHandler {
public:
    /** @brief Recognizes a RIFF WebP container without decoding or allocating pixels. */
    bool canDecode(const char *data, size_t size) override;
    /**
     * @brief Decodes the first composited frame to caller-owned RGBA8 pixels.
     * @param data Encoded bytes borrowed only for this call.
     * @param size Number of accessible bytes.
     * @return Decoded pixels released through freeRawPixels().
     * @throws eve::Exception on malformed input or unsupported dimensions.
     * @thread Calls use independent decoder state and may run concurrently.
     * @reentrancy Does not invoke callbacks.
     */
    DecodedImage decode(const char *data, size_t size) override;
};

}  // namespace eve::image
