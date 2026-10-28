#include "asset/SourcePng.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <string_view>
#include "asset/SourceBc3.h"
#include "asset/SourceTga.h"
#include "asset/SourceTiff.h"

// SourcePng.cpp
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

// SourceTga.cpp
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

// SourceBc3.cpp
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

// SourceTiff.cpp
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
