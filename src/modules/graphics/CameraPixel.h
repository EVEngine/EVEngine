#pragma once

#include <algorithm>
#include <cmath>

namespace eve::graphics {

/**
 * @brief Shared sizing for perfect-pixel orthographic / 2D cameras.
 *
 * `pixelsPerUnit` is framebuffer pixels per world unit at scale 1. When a
 * reference resolution is set, an integer upscale fits inside the live
 * viewport so texels stay on whole pixels. `orthoHeight` is the full vertical
 * world span for Camera3D; `zoom2D` is Camera2D's scale factor.
 */
struct PixelPerfectScale {
    int   scale         = 1;
    float orthoHeight   = 1.f;
    float zoom2D        = 1.f;
    float worldPerPixel = 1.f;
};

/**
 * @brief Compute integer scale and projection spans for a perfect-pixel camera.
 * @param pixelsPerUnit Screen pixels per world unit at scale 1; must be > 0.
 * @param refWidth Reference/virtual width in pixels; 0 skips integer fitting.
 * @param refHeight Reference/virtual height in pixels; 0 skips integer fitting.
 * @param viewWidth Live drawable width in pixels.
 * @param viewHeight Live drawable height in pixels.
 */
inline PixelPerfectScale computePixelPerfectScale(float pixelsPerUnit, int refWidth, int refHeight, int viewWidth,
                                                  int viewHeight) {
    PixelPerfectScale out;
    const float       ppu = std::max(1e-4f, pixelsPerUnit);
    if (refWidth > 0 && refHeight > 0 && viewWidth > 0 && viewHeight > 0) {
        out.scale       = std::max(1, std::min(viewWidth / refWidth, viewHeight / refHeight));
        out.orthoHeight = float(refHeight) / ppu;
        out.zoom2D      = float(out.scale) * ppu;
    } else if (viewHeight > 0) {
        out.scale       = 1;
        out.orthoHeight = float(viewHeight) / ppu;
        out.zoom2D      = ppu;
    } else {
        out.scale       = 1;
        out.orthoHeight = 1.f / ppu;
        out.zoom2D      = ppu;
    }
    out.worldPerPixel = 1.f / (ppu * float(out.scale));
    return out;
}

/** @brief Snap a world coordinate onto the pixel grid for the given zoom. */
inline float snapWorldToPixel(float world, float zoom) {
    const float z = zoom <= 0.f ? 1e-4f : zoom;
    return std::round(world * z) / z;
}

}  // namespace eve::graphics
