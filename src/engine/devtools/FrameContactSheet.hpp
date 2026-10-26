#pragma once

#include "devtools/FrameCapturePreview.hpp"

#include <span>

namespace eve::dev {
/** @brief Bounded, row-major contact sheet of immutable final-color captures. */
struct FrameContactSheetOptions {
    uint32_t columns   = 2;
    uint32_t cellWidth = 512, cellHeight = 512;
    size_t   maxBytes = 2u * 1024u * 1024u;
};

/**
 * @internal
 * @brief Compose final-color CPU leases without touching cameras or simulation.
 * @param frames Borrowed for this call; immutable storage remains owned by each lease.
 * @param options At most 16 cells and a 2048-by-2048 output; aspect is preserved with letterboxing.
 * @return Encoded PNG or a diagnostic; no partial result is published.
 * @thread Worker thread with immutable CPU leases; no callbacks or provider access.
 * @cost Linear in output pixels per worker job; reuse the job result for repeated reads.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<FramePreview> frameContactSheet(std::span<const FrameCaptureBuffer> frames,
                                                                             const FrameContactSheetOptions& options);
}  // namespace eve::dev
