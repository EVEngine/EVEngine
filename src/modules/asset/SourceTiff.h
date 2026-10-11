#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::asset::detail {
/** @brief Owning top-down RGB(A) TIFF pixels; unspecified fourth samples remain alpha data. */
struct TiffPixels {
    std::uint32_t             width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
/** @brief Decode bounded strip-based unsigned RGB TIFF (8/16 bits, raw/LZW/Deflate).
 * @param bytes Borrowed encoded bytes, used synchronously only.
 * @param maximumDecodedBytes Limit for output and each decompressed strip.
 * @return Owning pixels or checked malformed/unsupported diagnostic. No partial publication.
 * @thread Reentrant, no callbacks or global state.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<TiffPixels> decodeTiffRgba8(std::span<const std::uint8_t> bytes,
                                                                         std::uint64_t maximumDecodedBytes);
}  // namespace eve::asset::detail
