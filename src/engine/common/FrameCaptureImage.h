#pragma once
#include "common/FrameCapture.h"

namespace eve {
/** @brief One pixel with native storage and decoded attachment semantics. */
struct FramePixel {
    std::array<double, 4>           stored{};
    std::array<double, 4>           decoded{};
    std::array<std::string_view, 4> labels{};  // Static semantic names, independent of snapshot memory.
    size_t                          channels = 4;
};
/** @brief Read CPU snapshot only; no callback, rendering, or GPU wait. @thread Any thread with a snapshot lease. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<FramePixel> frameCapturePixel(const FrameCaptureBuffer& buffer, uint32_t x,
                                                                           uint32_t y);
/** @brief Reconstruct positive view depth using this snapshot's projection. @thread Any thread with CPU leases. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<double> frameCaptureLinearDepth(
    const FrameCaptureBuffer& buffer, uint32_t x, uint32_t y, const std::array<float, 16>& inverseProjection);
}  // namespace eve
