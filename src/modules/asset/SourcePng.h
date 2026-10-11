#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::asset::detail {
/** @brief Encode top-down RGBA8 source pixels without transfer conversion.
 * @param width Pixel width; must be positive.
 * @param height Pixel height; must be positive.
 * @param pixels Borrowed tightly packed RGBA8 pixels, observed only for this call.
 * @param maximumBytes Limit for scanline storage and encoded output.
 * @return Owning PNG source or checked diagnostic. Reentrant, no callbacks or external mutation.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<std::uint8_t>> encodeSourcePng(
    std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> pixels, std::uint64_t maximumBytes);
}  // namespace eve::asset::detail
