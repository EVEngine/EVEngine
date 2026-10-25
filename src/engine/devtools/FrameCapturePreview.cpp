#include "devtools/FrameCapturePreview.hpp"
#include <Poco/Checksum.h>
#include <Poco/DeflatingStream.h>
#include <algorithm>
#include <cmath>
#include <sstream>
namespace eve::dev {
namespace {
template <class T>
Result<T> invalid(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "frame_capture"));
}
void be32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(value >> shift));
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& bytes) {
    be32(out, static_cast<uint32_t>(bytes.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    Poco::Checksum checksum(Poco::Checksum::TYPE_CRC32);
    checksum.update(reinterpret_cast<const char*>(out.data() + start), static_cast<unsigned>(out.size() - start));
    be32(out, checksum.checksum());
}
uint8_t unorm(double value) {
    return std::isfinite(value) ? static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0)) : 255;
}
}  // namespace
Result<FramePreview> frameCapturePreview(const FrameCaptureBuffer& b, const FramePreviewOptions& o) {
    if (!b.bytes || !b.width || !b.height || uint64_t(b.width) * b.height * 4 != b.bytes->size() || !o.maxWidth ||
        !o.maxHeight || o.maxWidth > 2048 || o.maxHeight > 2048 || !std::isfinite(o.rangeMin) ||
        !std::isfinite(o.rangeMax) || o.rangeMax <= o.rangeMin || !std::isfinite(o.exposure) ||
        std::abs(o.exposure) > 20 || o.channel < 0 || o.channel > 3)
        return invalid<FramePreview>("InvalidPreviewParametersOrStorage");
    FramePreview out;
    out.view = o.view;
    if (out.view == "auto") {
        if (b.format == FramePixelFormat::Depth32Float || b.attachment == FrameAttachment::Depth)
            out.view = "scalar";
        else if (b.attachment == FrameAttachment::Normal)
            out.view = "normalWorld";
        else if (b.attachment == FrameAttachment::Velocity)
            out.view = "velocity";
        else
            out.view = "rawChannels";
    }
    if (out.view != "linearDepth" && out.view != "scalar" && out.view != "rawChannels" && out.view != "normalWorld" &&
        out.view != "velocity" && out.view != "hdrExposure")
        return invalid<FramePreview>("UnsupportedView");
    if ((out.view == "normalWorld" && b.attachment != FrameAttachment::Normal) ||
        (out.view == "velocity" && b.attachment != FrameAttachment::Velocity))
        return invalid<FramePreview>("UnsupportedView");
    if ((out.view == "linearDepth" || (out.view == "scalar" && b.format == FramePixelFormat::Depth32Float)) &&
        o.channel != 0)
        return invalid<FramePreview>("UnsupportedChannel");
    if (out.view == "hdrExposure" && b.attachment != FrameAttachment::Final &&
        b.attachment != FrameAttachment::Albedo && b.attachment != FrameAttachment::Emissive)
        return invalid<FramePreview>("UnsupportedView");
    const double scale = (std::min)({1.0, double(o.maxWidth) / b.width, double(o.maxHeight) / b.height});
    out.width          = (std::max)(1u, static_cast<uint32_t>(b.width * scale));
    out.height         = (std::max)(1u, static_cast<uint32_t>(b.height * scale));
    std::vector<uint8_t> scanlines((size_t(out.width) * 4 + 1) * out.height);
    for (uint32_t y = 0; y < out.height; ++y) {
        const size_t row = size_t(y) * (size_t(out.width) * 4 + 1);
        for (uint32_t x = 0; x < out.width; ++x) {
            auto pixel = frameCapturePixel(b, uint32_t(uint64_t(x) * b.width / out.width),
                                           uint32_t(uint64_t(y) * b.height / out.height));
            if (!pixel) return Result<FramePreview>::failure(pixel.status());
            auto values = pixel.value().decoded;
            if (out.view == "linearDepth") {
                auto depth =
                    frameCaptureLinearDepth(b, uint32_t(uint64_t(x) * b.width / out.width),
                                            uint32_t(uint64_t(y) * b.height / out.height), o.inverseProjection);
                if (!depth) return Result<FramePreview>::failure(depth.status());
                values[0] = depth.value();
            }
            if (out.view == "normalWorld")
                for (size_t c = 0; c < 3; ++c) values[c] = values[c] * 0.5 + 0.5;
            if (out.view == "velocity") values = {values[1] * 0.5 + 0.5, values[2] * 0.5 + 0.5, 0.5, 1};
            if (out.view == "rawChannels") {
                for (size_t c = 0; c < 4; ++c) {
                    values[c] = b.format == FramePixelFormat::Depth32Float ? pixel.value().stored[0]
                                                                           : pixel.value().stored[c] / 255.0;
                }
            }
            if (out.view == "scalar" || out.view == "linearDepth") {
                const double value = (values[static_cast<size_t>(o.channel)] - o.rangeMin) / (o.rangeMax - o.rangeMin);
                values             = {value, value, value, 1};
            }
            if (out.view == "hdrExposure")
                for (size_t c = 0; c < 3; ++c)
                    values[c] = 1.0 - std::exp(-(std::max)(0.0, values[c]) * std::exp2(o.exposure));
            const size_t offset = row + 1 + size_t(x) * 4;
            for (size_t c = 0; c < 3; ++c) scanlines[offset + c] = unorm(values[c]);
            scanlines[offset + 3] = 255;  // Packed metadata alpha never becomes preview transparency.
        }
    }
    std::ostringstream          compressed;
    Poco::DeflatingOutputStream zlib(compressed, Poco::DeflatingStreamBuf::STREAM_ZLIB);
    zlib.write(reinterpret_cast<const char*>(scanlines.data()), static_cast<std::streamsize>(scanlines.size()));
    zlib.close();
    const auto data = compressed.str();
    out.png         = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> header;
    be32(header, out.width);
    be32(header, out.height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(out.png, "IHDR", header);
    chunk(out.png, "IDAT", std::vector<uint8_t>(data.begin(), data.end()));
    chunk(out.png, "IEND", {});
    if (((out.png.size() + 2) / 3) * 4 > o.maxBytes) return invalid<FramePreview>("ImageTooLarge");
    return Result<FramePreview>::success(std::move(out));
}
}  // namespace eve::dev
