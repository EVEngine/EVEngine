#include "WebPHandler.h"

#include "common/Exception.h"

#include <webp/demux.h>

#include <cstring>
#include <limits>
#include <memory>

namespace eve::image {

bool WebPHandler::canDecode(const char *data, size_t size) {
    return data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0;
}

medialoader::FormatHandler::DecodedImage WebPHandler::decode(const char *data, size_t size) {
    if (!canDecode(data, size)) throw eve::Exception("Invalid WebP container");
    const WebPData encoded{reinterpret_cast<const uint8_t *>(data), size};
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&encoded, nullptr),
                                                                               WebPAnimDecoderDelete);
    if (!decoder) throw eve::Exception("Could not decode WebP container");
    WebPAnimInfo info{};
    if (!WebPAnimDecoderGetInfo(decoder.get(), &info) || info.canvas_width == 0 || info.canvas_height == 0 ||
        info.canvas_width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        info.canvas_height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        info.canvas_width > std::numeric_limits<size_t>::max() / 4 / info.canvas_height)
        throw eve::Exception("Invalid WebP dimensions");
    uint8_t *frame     = nullptr;
    int      timestamp = 0;
    if (!WebPAnimDecoderGetNext(decoder.get(), &frame, &timestamp) || frame == nullptr)
        throw eve::Exception("Could not decode WebP first frame");
    DecodedImage result;
    result.width  = static_cast<int>(info.canvas_width);
    result.height = static_cast<int>(info.canvas_height);
    result.format = medialoader::PIXELFORMAT_RGBA8;
    result.size   = static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 4;
    auto pixels   = std::make_unique<unsigned char[]>(result.size);
    std::memcpy(pixels.get(), frame, result.size);
    result.data = pixels.release();
    return result;
}

}  // namespace eve::image
