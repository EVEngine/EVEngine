#pragma once

#include <algorithm>

namespace eve::ui {

/** @brief Per-host logical design canvas. Margins are display logical pixels, independent of DPI. */
struct HudCanvas {
    bool  enabled        = false;
    float referenceWidth = 1920.f, referenceHeight = 1080.f;
    float safeLeft = 0.f, safeTop = 0.f, safeRight = 0.f, safeBottom = 0.f;
    float userScale = 1.f;
};

/** @brief Resolved safe viewport and isotropic design-unit scale. */
struct HudCanvasTransform {
    float x = 0.f, y = 0.f, width = 0.f, height = 0.f, scale = 1.f;
};

/** @brief Resolves fit scaling without letterboxing anchors; callers validate positive viewport dimensions. */
inline HudCanvasTransform resolveHudCanvas(const HudCanvas& canvas, float width, float height) {
    HudCanvasTransform result{0.f, 0.f, width, height, 1.f};
    if (!canvas.enabled) return result;
    result.x      = canvas.safeLeft;
    result.y      = canvas.safeTop;
    result.width  = std::max(1.f, width - canvas.safeLeft - canvas.safeRight);
    result.height = std::max(1.f, height - canvas.safeTop - canvas.safeBottom);
    result.scale =
        std::min(result.width / canvas.referenceWidth, result.height / canvas.referenceHeight) * canvas.userScale;
    return result;
}

}  // namespace eve::ui
