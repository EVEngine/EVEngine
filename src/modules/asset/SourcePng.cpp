#include "asset/SourcePng.h"
#include <zlib.h>
#include <algorithm>
#include <limits>
namespace eve::asset::detail {
Result<std::vector<std::uint8_t>> encodeSourcePng(std::uint32_t width, std::uint32_t height,
                                                  std::span<const std::uint8_t> pixels, std::uint64_t maximumBytes) {
    auto fail = [] {
        return Result<std::vector<std::uint8_t>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid PNG pixel dimensions or encoding budget", {},
                              {}, "asset.image.png"));
    };
    const std::uint64_t count = std::uint64_t(width) * height;
    if (!width || !height || count > pixels.size() / 4 || count * 4 != pixels.size() ||
        pixels.size() > std::numeric_limits<uLong>::max() - std::uint64_t(height))
        return fail();
    const std::uint64_t row = std::uint64_t(width) * 4, total = pixels.size() + std::uint64_t(height);
    if (total > maximumBytes || total > std::numeric_limits<uLong>::max()) return fail();
    uLongf bound = compressBound(static_cast<uLong>(total));
    if (bound > maximumBytes || maximumBytes - bound < 57 || bound > UINT32_MAX - 4) return fail();
    std::vector<std::uint8_t> scanlines;
    scanlines.reserve(std::size_t(total));
    for (std::size_t y = 0; y < height; ++y) {
        scanlines.push_back(0);
        scanlines.insert(scanlines.end(), pixels.begin() + y * row, pixels.begin() + (y + 1) * row);
    }
    std::vector<std::uint8_t> compressed(bound);
    if (compress2(compressed.data(), &bound, scanlines.data(), static_cast<uLong>(total), 6) != Z_OK) return fail();
    compressed.resize(bound);
    std::vector<std::uint8_t> result{137, 80, 78, 71, 13, 10, 26, 10};
    auto                      put = [](auto& out, std::uint32_t v) {
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(std::uint8_t(v >> shift));
    };
    auto chunk = [&](const char* type, std::span<const std::uint8_t> payload) {
        put(result, std::uint32_t(payload.size()));
        const auto at = result.size();
        result.insert(result.end(), type, type + 4);
        result.insert(result.end(), payload.begin(), payload.end());
        put(result, crc32(0, result.data() + at, static_cast<uInt>(payload.size() + 4)));
    };
    std::vector<std::uint8_t> header;
    put(header, width);
    put(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk("IHDR", header);
    chunk("IDAT", compressed);
    chunk("IEND", {});
    return Result<std::vector<std::uint8_t>>::success(std::move(result));
}
}  // namespace eve::asset::detail
