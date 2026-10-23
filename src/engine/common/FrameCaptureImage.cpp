#include "common/FrameCaptureImage.h"
#include <cmath>
#include <cstring>
namespace eve {
namespace {
template <class T>
Result<T> invalid(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "frame_capture"));
}
}  // namespace
Result<FramePixel> frameCapturePixel(const FrameCaptureBuffer& b, uint32_t x, uint32_t y) {
    if (!b.bytes || x >= b.width || y >= b.height || uint64_t(b.width) * b.height * 4 != b.bytes->size())
        return invalid<FramePixel>("PixelOutOfBoundsOrUnavailable");
    FramePixel  p;
    const auto* src = b.bytes->data() + (size_t(y) * b.width + x) * 4;
    p.labels        = {"r", "g", "b", "a"};
    if (b.format == FramePixelFormat::Depth32Float) {
        float depth;
        std::memcpy(&depth, src, sizeof(depth));
        p.channels  = 1;
        p.stored[0] = p.decoded[0] = depth;
        p.labels[0]                = "deviceDepth";
    } else {
        for (size_t i = 0; i < 4; ++i) {
            const auto c = b.format == FramePixelFormat::Bgra8 && i % 2 == 0 ? 2 - i : i;
            p.stored[i]  = src[c];
            p.decoded[i] = src[c] / 255.0;
        }
        if (b.attachment == FrameAttachment::Normal) {
            p.labels = {"worldNormalX", "worldNormalY", "worldNormalZ", "legacyPbr"};
            for (size_t i = 0; i < 3; ++i) p.decoded[i] = p.decoded[i] * 2.0 - 1.0;
        } else if (b.attachment == FrameAttachment::Velocity || b.attachment == FrameAttachment::Depth) {
            p.labels     = {"linearDepth01", "motionNdcX", "motionNdcY", "unused"};
            p.decoded[1] = p.decoded[1] * 2.0 - 1.0;
            p.decoded[2] = p.decoded[2] * 2.0 - 1.0;
        } else if (b.attachment == FrameAttachment::PbrParams) {
            p.labels = {"metallic", "roughness", "occlusion", "specularFactor"};
        } else if (b.attachment == FrameAttachment::Albedo) {
            p.labels[3] = "linearDepth01";
        }
    }
    return Result<FramePixel>::success(std::move(p));
}
Result<double> frameCaptureLinearDepth(const FrameCaptureBuffer& b, uint32_t x, uint32_t y,
                                       const std::array<float, 16>& matrix) {
    if (b.attachment != FrameAttachment::HwDepth || b.format != FramePixelFormat::Depth32Float)
        return invalid<double>("UnsupportedView");
    auto pixel = frameCapturePixel(b, x, y);
    if (!pixel) return Result<double>::failure(pixel.status());
    const double nx = (double(x) + 0.5) / b.width * 2 - 1;
    const double ny = (double(y) + 0.5) / b.height * 2 - 1;
    const double z  = pixel.value().stored[0];
    const double vz = matrix[2] * nx + matrix[6] * ny + matrix[10] * z + matrix[14];
    const double vw = matrix[3] * nx + matrix[7] * ny + matrix[11] * z + matrix[15];
    if (!std::isfinite(vz) || !std::isfinite(vw) || std::abs(vw) < 1e-12)
        return invalid<double>("InvalidDepthProjection");
    return Result<double>::success(std::abs(vz / vw));
}
}  // namespace eve
