#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::asset::detail {
/** @brief Owning top-down RGBA8 pixels decoded from a TGA source. */
struct TgaPixels {
    std::uint32_t             width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
/** @brief Decode true-color 24/32-bit raw or RLE TGA with bounded allocation.
 * @param bytes Borrowed encoded source; no pointers survive this call.
 * @param maximumDecodedBytes Output budget including the runtime image header.
 * @return Owning candidate or checked failure; no external mutation.
 * @thread Reentrant and callback-free. Source transfer is applied by the cooker.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<TgaPixels> decodeTgaRgba8(std::span<const std::uint8_t> bytes, std::uint64_t maximumDecodedBytes);
}  // namespace eve::asset::detail
