#include "devtools/FrameContactSheet.hpp"

#include <algorithm>

namespace eve::dev {
Result<FramePreview> frameContactSheet(std::span<const FrameCaptureBuffer> frames,
                                       const FrameContactSheetOptions&     options) {
    const auto invalid = [](std::string message) {
        return Result<FramePreview>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "frame_contact_sheet"));
    };
    if (frames.empty() || frames.size() > 16 || options.columns == 0 || options.columns > frames.size() ||
        !options.cellWidth || !options.cellHeight || options.cellWidth > 2048 || options.cellHeight > 2048 ||
        options.maxBytes < 1024 || options.maxBytes > 16777216)
        return invalid("InvalidContactSheetOptions");
    const auto     rows  = static_cast<uint32_t>((frames.size() + options.columns - 1) / options.columns);
    const uint32_t width = options.columns * options.cellWidth, height = rows * options.cellHeight;
    if (width > 2048 || height > 2048) return invalid("ContactSheetPixelBudgetExceeded");
    for (const auto& frame : frames) {
        if (frame.attachment != FrameAttachment::Final ||
            (frame.format != FramePixelFormat::Rgba8 && frame.format != FramePixelFormat::Bgra8) || !frame.bytes ||
            !frame.width || !frame.height || frame.bytes->size() % 4 != 0 ||
            uint64_t(frame.width) * frame.height != frame.bytes->size() / 4)
            return invalid("InvalidFinalColorStorage");
    }
    auto pixels = std::make_shared<std::vector<uint8_t>>(size_t(width) * height * 4, 0);
    for (size_t p = 3; p < pixels->size(); p += 4) (*pixels)[p] = 255;
    for (size_t index = 0; index < frames.size(); ++index) {
        const auto&  frame = frames[index];
        const double scale =
            (std::min)({1.0, double(options.cellWidth) / frame.width, double(options.cellHeight) / frame.height});
        const auto drawWidth  = (std::max)(1u, static_cast<uint32_t>(frame.width * scale));
        const auto drawHeight = (std::max)(1u, static_cast<uint32_t>(frame.height * scale));
        const auto left =
            static_cast<uint32_t>(index % options.columns) * options.cellWidth + (options.cellWidth - drawWidth) / 2;
        const auto top =
            static_cast<uint32_t>(index / options.columns) * options.cellHeight + (options.cellHeight - drawHeight) / 2;
        for (uint32_t y = 0; y < drawHeight; ++y) {
            for (uint32_t x = 0; x < drawWidth; ++x) {
                const size_t source = (size_t(uint64_t(y) * frame.height / drawHeight) * frame.width +
                                       size_t(uint64_t(x) * frame.width / drawWidth)) *
                                      4;
                const size_t destination   = (size_t(top + y) * width + left + x) * 4;
                const bool   bgra          = frame.format == FramePixelFormat::Bgra8;
                (*pixels)[destination]     = (*frame.bytes)[source + (bgra ? 2 : 0)];
                (*pixels)[destination + 1] = (*frame.bytes)[source + 1];
                (*pixels)[destination + 2] = (*frame.bytes)[source + (bgra ? 0 : 2)];
            }
        }
    }
    FrameCaptureBuffer combined;
    combined.width  = width;
    combined.height = height;
    combined.bytes  = std::move(pixels);
    FramePreviewOptions preview;
    preview.maxWidth  = width;
    preview.maxHeight = height;
    preview.maxBytes  = options.maxBytes;
    preview.view      = "rawChannels";
    return frameCapturePreview(combined, preview);
}
}  // namespace eve::dev
