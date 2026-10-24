#pragma once
#include "common/FrameCaptureImage.h"
namespace eve::dev {
/** @brief Explicit CPU preview transformation, separate from costly GPU capture. */
struct FramePreviewOptions {
    std::string           view     = "auto";
    int                   channel  = 0;
    uint32_t              maxWidth = 1024, maxHeight = 1024;
    float                 rangeMin = 0.f, rangeMax = 1.f, exposure = 0.f;
    size_t                maxBytes = 2u * 1024u * 1024u;
    std::array<float, 16> inverseProjection{};
};
/** @brief Encoded preview; width and height may differ from the original attachment. */
struct FramePreview {
    uint32_t             width = 0, height = 0;
    std::string          view;
    std::vector<uint8_t> png;
};
/** @internal @brief Encode a CPU visualization as a stage of the MCP worker job. @cost Work scales with output pixels,
 * amortized by preview cache. @thread Worker with an immutable snapshot lease. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<FramePreview> frameCapturePreview(const FrameCaptureBuffer&  buffer,
                                                                               const FramePreviewOptions& options);
}  // namespace eve::dev
