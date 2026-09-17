#include "asset/SourceBc3.h"
#include <array>
#include <limits>

namespace eve::asset::detail {
Result<std::vector<std::uint8_t>> decodeBc3Rgba8(std::span<const std::uint8_t> bytes, std::uint32_t width,
                                                 std::uint32_t height, std::uint64_t maximumDecodedBytes) {
    const auto pixels  = std::uint64_t(width) * height;
    const auto columns = (std::uint64_t(width) + 3) / 4;
    const auto rows    = (std::uint64_t(height) + 3) / 4;
    if (!width || !height || pixels > maximumDecodedBytes / 4 || pixels > std::numeric_limits<std::size_t>::max() / 4 ||
        columns * rows > std::numeric_limits<std::uint64_t>::max() / 16 || columns * rows * 16 != bytes.size())
        return Result<std::vector<std::uint8_t>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "BC3 dimensions, byte count or decoded budget invalid",
                              {}, {}, "asset.bc3"));
    std::vector<std::uint8_t> output(std::size_t(pixels * 4));
    for (std::uint64_t by = 0; by < rows; ++by) {
        for (std::uint64_t bx = 0; bx < columns; ++bx) {
            const auto              block = bytes.subspan(std::size_t((by * columns + bx) * 16), 16);
            std::array<unsigned, 8> alpha{block[0], block[1]};
            if (alpha[0] > alpha[1]) {
                for (unsigned i = 1; i <= 6; ++i) alpha[i + 1] = ((7 - i) * alpha[0] + i * alpha[1]) / 7;
            } else {
                for (unsigned i = 1; i <= 4; ++i) alpha[i + 1] = ((5 - i) * alpha[0] + i * alpha[1]) / 5;
                alpha[6] = 0;
                alpha[7] = 255;
            }
            std::array<std::array<unsigned, 3>, 4> color{};
            for (unsigned i = 0; i < 2; ++i) {
                const unsigned rgb = block[8 + i * 2] | (unsigned(block[9 + i * 2]) << 8);
                const unsigned r = rgb >> 11, g = (rgb >> 5) & 63, b = rgb & 31;
                color[i] = {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
            }
            for (unsigned c = 0; c < 3; ++c) {
                color[2][c] = (2 * color[0][c] + color[1][c]) / 3;
                color[3][c] = (color[0][c] + 2 * color[1][c]) / 3;
            }
            std::uint64_t alphaIndices = 0;
            std::uint32_t colorIndices = 0;
            for (unsigned i = 0; i < 6; ++i) alphaIndices |= std::uint64_t(block[2 + i]) << (8 * i);
            for (unsigned i = 0; i < 4; ++i) colorIndices |= std::uint32_t(block[12 + i]) << (8 * i);
            for (unsigned i = 0; i < 16; ++i) {
                const auto x = bx * 4 + i % 4, y = by * 4 + i / 4;
                if (x >= width || y >= height) continue;
                const auto at = std::size_t((y * width + x) * 4);
                for (unsigned c = 0; c < 3; ++c) output[at + c] = std::uint8_t(color[(colorIndices >> (2 * i)) & 3][c]);
                output[at + 3] = std::uint8_t(alpha[(alphaIndices >> (3 * i)) & 7]);
            }
        }
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}
}  // namespace eve::asset::detail
