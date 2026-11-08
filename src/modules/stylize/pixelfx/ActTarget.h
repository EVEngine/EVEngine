// Procedural target art and source raster composition, MIT (c) 2026 Patch; see NOTICE.txt.
#pragma once
#include <array>
#include <span>
#include "stylize/PixelFx.h"
namespace eve::stylize::pixelfx {
struct ActTargetState;
/** @brief Immutable process-lifetime target art, generated procedurally during cold Act preparation. */
struct TargetArt {
    std::array<uint32_t, 38 * 52> pixels{};
};
/** @brief Generate the shared immutable source target art on its first cold preparation call.
 * @cost One bounded 38x52 procedural build per process, amortized across all Act instances/draws. */
const TargetArt& targetArt();
/** @brief Borrow validated canvas and immutable states for synchronous target or straw rendering.
 * @note Owner-thread only; no retained borrows, allocation, callbacks or state mutation. */
void drawActTargets(const TargetArt& art, std::span<const ActTargetState> targets, PixelFxCanvas canvas,
                    PixelFxPlacement placement, bool straw);
}  // namespace eve::stylize::pixelfx
