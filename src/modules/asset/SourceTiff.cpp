#include "asset/SourceTiff.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>

namespace eve::asset::detail {
namespace {
struct Invalid {};
struct Unsupported {};
struct Reader {
    std::span<const std::uint8_t> bytes;
    bool                          little;
    std::uint32_t                 read(std::size_t at, unsigned count) const {
        if (at > bytes.size() || count > bytes.size() - at) throw Invalid{};
        std::uint32_t result = 0;
        for (unsigned i = 0; i < count; ++i)
            result |= std::uint32_t(bytes[at + i]) << ((little ? i : count - 1 - i) * 8);
        return result;
    }
};
struct Tag {
    std::uint32_t type, count, offset;
};

std::vector<std::uint8_t> lzw(std::span<const std::uint8_t> source, std::size_t expected) {
    std::array<std::uint16_t, 4096> prefix{};
    std::array<std::uint8_t, 4096>  suffix{}, stack{};
    std::vector<std::uint8_t>       out;
    out.reserve(expected);
    std::size_t  bit   = 0;
    unsigned     width = 9, next = 258, previous = 4096;
    std::uint8_t first   = 0;
    bool         cleared = false;
    while (true) {
        if (bit > source.size() * 8 || width > source.size() * 8 - bit) throw Invalid{};
        unsigned code = 0;
        for (unsigned i = 0; i < width; ++i, ++bit) code = (code << 1) | ((source[bit / 8] >> (7 - bit % 8)) & 1);
        if (code == 256) {
            width    = 9;
            next     = 258;
            previous = 4096;
            cleared  = true;
            continue;
        }
        if (!cleared) throw Invalid{};
        if (code == 257) {
            if (out.size() != expected) throw Invalid{};
            return out;
        }
        const unsigned original = code;
        std::size_t    length   = 0;
        if (code == next && previous != 4096) {
            stack[length++] = first;
            code            = previous;
        } else if (code >= next)
            throw Invalid{};
        while (code >= 258) {
            if (code >= next || length >= stack.size()) throw Invalid{};
            stack[length++] = suffix[code];
            code            = prefix[code];
        }
        if (code >= 256 || length >= stack.size()) throw Invalid{};
        first           = std::uint8_t(code);
        stack[length++] = first;
        if (length > expected - out.size()) throw Invalid{};
        while (length) out.push_back(stack[--length]);
        if (previous != 4096 && next < 4096) {
            prefix[next] = std::uint16_t(previous);
            suffix[next] = first;
            ++next;
            // TIFF uses early-change code widths, unlike GIF's late-change LZW.
            if (width < 12 && next == (1u << width) - 1) ++width;
        }
        previous = original;
    }
}
}  // namespace

Result<TiffPixels> decodeTiffRgba8(std::span<const std::uint8_t> bytes, std::uint64_t maximumDecodedBytes) {
    try {
        if (bytes.size() < 8 || !((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M')))
            throw Invalid{};
        Reader r{bytes, bytes[0] == 'I'};
        if (r.read(2, 2) != 42) throw Unsupported{};
        const auto ifd = r.read(4, 4), count = r.read(ifd, 2);
        if (ifd < 8 || count > 4096 || std::uint64_t(ifd) + 2 + 12ull * count + 4 > bytes.size()) throw Invalid{};
        std::map<unsigned, Tag> tags;
        for (unsigned i = 0; i < count; ++i) {
            const auto at = ifd + 2 + i * 12, id = r.read(at, 2), type = r.read(at + 2, 2), n = r.read(at + 4, 4);
            unsigned   size = type == 1 || type == 2 || type == 6 || type == 7 ? 1
                              : type == 3 || type == 8                         ? 2
                              : type == 4 || type == 9 || type == 11           ? 4
                              : type == 5 || type == 10 || type == 12          ? 8
                                                                               : 0;
            if (!size) throw Unsupported{};
            const auto offset = std::uint64_t(n) * size <= 4 ? at + 8 : r.read(at + 8, 4);
            if (offset > bytes.size() || std::uint64_t(n) * size > bytes.size() - offset) throw Invalid{};
            if (!tags.emplace(id, Tag{type, n, offset}).second) throw Invalid{};
        }
        if (r.read(ifd + 2 + 12 * count, 4) != 0) throw Unsupported{};
        auto values = [&](unsigned id, std::vector<std::uint32_t> fallback = {}) {
            const auto it = tags.find(id);
            if (it == tags.end()) return fallback;
            const auto& t = it->second;
            if ((t.type != 3 && t.type != 4) || t.count > maximumDecodedBytes / 4) throw Invalid{};
            std::vector<std::uint32_t> result;
            result.reserve(t.count);
            const auto step = t.type == 3 ? 2 : 4;
            for (unsigned i = 0; i < t.count; ++i) result.push_back(r.read(t.offset + std::size_t(i) * step, step));
            return result;
        };
        auto scalar = [&](unsigned id, unsigned fallback) {
            auto v = values(id, {fallback});
            if (v.size() != 1) throw Invalid{};
            return v[0];
        };
        const auto w = scalar(256, 0), h = scalar(257, 0), channels = scalar(277, 1), compression = scalar(259, 1);
        const auto rows = scalar(278, UINT32_MAX), orientation = scalar(274, 1), predictor = scalar(317, 1);
        if (!w || !h || !rows || maximumDecodedBytes < 24 || std::uint64_t(w) * h > (maximumDecodedBytes - 24) / 4)
            throw Invalid{};
        if (scalar(262, 0) != 2 || scalar(284, 1) != 1 || (channels != 3 && channels != 4) || orientation < 1 ||
            orientation > 4 || (predictor != 1 && predictor != 2) || scalar(266, 1) != 1)
            throw Unsupported{};
        auto bits = values(258), format = values(339, {1}), extra = values(338);
        if (bits.size() != channels || (bits[0] != 8 && bits[0] != 16) ||
            (format.size() != 1 && format.size() != channels) ||
            !std::all_of(bits.begin(), bits.end(), [&](auto b) { return b == bits[0]; }) ||
            !std::all_of(format.begin(), format.end(), [](auto f) { return f == 1; }))
            throw Unsupported{};
        if (channels == 4 && (extra.size() != 1 || (extra[0] != 0 && extra[0] != 2))) throw Unsupported{};
        if (channels == 3 && !extra.empty()) throw Unsupported{};
        if (compression != 1 && compression != 5 && compression != 8 && compression != 32946) throw Unsupported{};
        auto offsets = values(273), sizes = values(279);
        if (offsets.size() != (std::uint64_t(h) + rows - 1) / rows || sizes.size() != offsets.size()) throw Invalid{};
        TiffPixels     out{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        const unsigned sampleBytes = bits[0] / 8;
        for (std::size_t strip = 0; strip < offsets.size(); ++strip) {
            const auto          start = strip * rows, lines = std::min<std::size_t>(rows, h - start);
            const std::uint64_t decodedSize = std::uint64_t(w) * lines * channels * sampleBytes;
            if (decodedSize > maximumDecodedBytes || decodedSize > std::numeric_limits<uLongf>::max() ||
                offsets[strip] > bytes.size() || sizes[strip] > bytes.size() - offsets[strip])
                throw Invalid{};
            auto                      source = bytes.subspan(offsets[strip], sizes[strip]);
            std::vector<std::uint8_t> raw;
            if (compression == 1) {
                if (source.size() != decodedSize) throw Invalid{};
                raw.assign(source.begin(), source.end());
            } else if (compression == 5)
                raw = lzw(source, std::size_t(decodedSize));
            else {
                raw.resize(std::size_t(decodedSize));
                uLongf length = static_cast<uLongf>(decodedSize);
                uLong  input  = static_cast<uLong>(source.size());
                if (uncompress2(raw.data(), &length, source.data(), &input) != Z_OK || length != decodedSize ||
                    input != source.size())
                    throw Invalid{};
            }
            Reader                  pixels{raw, r.little};
            const auto              rowSamples = std::size_t(w) * channels;
            std::array<unsigned, 4> previous{};
            for (std::size_t y = 0; y < lines; ++y) {
                previous.fill(0);
                for (std::size_t x = 0; x < w; ++x) {
                    const auto dstX = (orientation == 2 || orientation == 3) ? w - 1 - x : x;
                    const auto srcY = start + y, dstY = orientation >= 3 ? h - 1 - srcY : srcY;
                    const auto dst = (dstY * w + dstX) * 4;
                    for (unsigned c = 0; c < channels; ++c) {
                        auto v = pixels.read((y * rowSamples + x * channels + c) * sampleBytes, sampleBytes);
                        if (predictor == 2) v = (v + previous[c]) & (sampleBytes == 1 ? 255 : 65535);
                        previous[c]       = v;
                        out.rgba[dst + c] = std::uint8_t(sampleBytes == 1 ? v : (v + 128) / 257);
                    }
                    if (channels == 3) out.rgba[dst + 3] = 255;
                }
            }
        }
        return Result<TiffPixels>::success(std::move(out));
    } catch (const Unsupported&) {
        return Result<TiffPixels>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported,
            "unsupported TIFF layout; requires unsigned 8/16-bit RGB(A) strips with raw, LZW or Deflate compression",
            {}, {}, "asset.image.tiff"));
    } catch (const Invalid&) {
        return Result<TiffPixels>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "malformed TIFF or decoded image budget exceeded", {}, {}, "asset.image.tiff"));
    }
}
}  // namespace eve::asset::detail
