#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "common/Result.h"

namespace eve::asset::detail {
/** @brief Decode one BC3/DXT5 mip level into tightly packed RGBA8 in source row order.
 * @param bytes Borrowed blocks; exactly ceil(width/4)*ceil(height/4)*16 bytes.
 * @param width Positive mip width.
 * @param height Positive mip height.
 * @param maximumDecodedBytes Maximum output allocation.
 * @return Owning pixels or checked diagnostic. Reentrant; no callbacks or retained pointers.
 * @remarks No transfer conversion or vertical flip; the source importer owns these semantics.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> decodeBc3Rgba8(std::span<const std::uint8_t> bytes, std::uint32_t width,
                                                               std::uint32_t height, std::uint64_t maximumDecodedBytes);
}  // namespace eve::asset::detail
