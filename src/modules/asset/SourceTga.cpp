#include "asset/SourceTga.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace eve::asset::detail {
Result<TgaPixels> decodeTgaRgba8(std::span<const std::uint8_t> b, std::uint64_t budget) {
    auto fail = [](DiagnosticCode code, const char* message) {
        return Result<TgaPixels>::failure(Diagnostic::error(code, message, {}, {}, "asset.image.tga"));
    };
    if (b.size() < 18) return fail(DiagnosticCode::ParseError, "truncated TGA header");
    auto           u16 = [&](std::size_t p) { return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8); };
    auto           u32 = [&](std::size_t p) { return u16(p) | (u16(p + 2) << 16); };
    const auto     w = u16(12), h = u16(14);
    const unsigned channels = b[16] / 8, attributes = b[17] & 15;
    if (b[1] != 0 || (b[2] != 2 && b[2] != 10) || (b[16] != 24 && b[16] != 32) || (b[17] & 192) != 0 ||
        (attributes != 0 && !(channels == 4 && attributes == 8)))
        return fail(DiagnosticCode::Unsupported, "TGA requires non-interleaved 24/32-bit true-color raw or RLE pixels");
    const std::uint64_t count = std::uint64_t(w) * h;
    if (!w || !h || budget < 24 || count > (budget - 24) / 4)
        return fail(DiagnosticCode::InvalidArgument, "TGA dimensions exceed decoded budget");
    std::size_t                end = b.size(), cursor = 18 + b[0];
    constexpr std::string_view signature("TRUEVISION-XFILE.\0", 18);
    if (b.size() >= 26 && std::equal(signature.begin(), signature.end(), b.end() - 18)) {
        end                  = b.size() - 26;
        const auto extension = u32(end), developer = u32(end + 4);
        if (extension) {
            if (extension < cursor || extension > end || end - extension < 495 || u16(extension) != 495)
                return fail(DiagnosticCode::ParseError, "invalid TGA extension area");
            if (b[extension + 494] > 3)
                return fail(DiagnosticCode::Unsupported, "premultiplied or unknown TGA alpha semantics");
            end = extension;
        }
        if (developer) {
            if (developer < cursor || developer > b.size() - 26 || b.size() - 26 - developer < 2)
                return fail(DiagnosticCode::ParseError, "invalid TGA developer area");
            end = std::min(end, std::size_t(developer));
        }
    }
    if (cursor > end) return fail(DiagnosticCode::ParseError, "truncated TGA image ID");
    TgaPixels   out{w, h, std::vector<std::uint8_t>(std::size_t(count) * 4)};
    std::size_t written = 0;
    auto        pixel   = [&](std::size_t p) {
        const std::size_t x = written % w, y = written / w;
        const auto        dx = (b[17] & 16) ? w - 1 - x : x, dy = (b[17] & 32) ? y : h - 1 - y;
        const auto        dst = (dy * w + dx) * 4;
        out.rgba[dst]         = b[p + 2];
        out.rgba[dst + 1]     = b[p + 1];
        out.rgba[dst + 2]     = b[p];
        out.rgba[dst + 3]     = channels == 4 ? b[p + 3] : 255;
        ++written;
    };
    while (written < count) {
        std::size_t run      = 1;
        bool        repeated = false;
        if (b[2] == 10) {
            if (cursor == end) return fail(DiagnosticCode::ParseError, "truncated TGA RLE packet");
            auto header = b[cursor++];
            run         = (header & 127) + 1;
            repeated    = (header & 128) != 0;
        }
        if (run > count - written || (repeated ? channels : run * channels) > end - cursor)
            return fail(DiagnosticCode::ParseError, "TGA packet exceeds image or source bounds");
        for (std::size_t i = 0; i < run; ++i) pixel(cursor + (repeated ? 0 : i * channels));
        cursor += (repeated ? 1 : run) * channels;
    }
    return Result<TgaPixels>::success(std::move(out));
}
}  // namespace eve::asset::detail
