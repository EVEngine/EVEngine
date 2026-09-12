#pragma once

#include "common/Result.h"

namespace eve::image {

/** @brief Reusable renderer-neutral bounds for one normalized UV paint operation. */
struct UvPaintRegion {
    int   targetWidth = 0, targetHeight = 0;
    int   centerX = 0, centerY = 0;
    int   minX = 0, minY = 0, maxX = 0, maxY = 0;
    float u = 0.f, v = 0.f, radiusU = 0.f, radiusV = 0.f;
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;

    /** @brief Inclusive clipped width in pixels. */
    [[nodiscard]] int width() const noexcept { return maxX - minX + 1; }
    /** @brief Inclusive clipped height in pixels. */
    [[nodiscard]] int height() const noexcept { return maxY - minY + 1; }
    /**
     * @brief Atomically replace this reusable scratch with a validated region.
     * @param width Positive target width in pixels.
     * @param height Positive target height in pixels.
     * @param centerU Normalized horizontal center; values outside [0,1] are clamped.
     * @param centerV Normalized vertical center; values outside [0,1] are clamped.
     * @param brushRadiusU Non-negative normalized horizontal radius.
     * @param brushRadiusV Non-negative normalized vertical radius.
     * @param red Red channel in [0,1].
     * @param green Green channel in [0,1].
     * @param blue Blue channel in [0,1].
     * @param alpha Alpha/coverage channel in [0,1].
     * @param flipV Convert bottom-left UV V to top-left image coordinates when true.
     * @return Success after commit, or a structured diagnostic with this value unchanged.
     * @ownership Retains no input or backend object.
     * @thread Not safe to mutate the same instance concurrently.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] Result<void> prepareResult(int width, int height, float centerU, float centerV, float brushRadiusU,
                                             float brushRadiusV, float red, float green, float blue, float alpha,
                                             bool flipV = true);
};

/**
 * @brief Validate and prepare the canonical clipped region for a UV brush.
 * @param targetWidth Positive target width in pixels.
 * @param targetHeight Positive target height in pixels.
 * @param u Normalized horizontal center; values outside [0,1] are clamped.
 * @param v Normalized vertical center; values outside [0,1] are clamped.
 * @param radiusU Non-negative normalized horizontal radius.
 * @param radiusV Non-negative normalized vertical radius.
 * @param r Red channel in [0,1].
 * @param g Green channel in [0,1].
 * @param b Blue channel in [0,1].
 * @param a Alpha/coverage channel in [0,1].
 * @param flipV Convert bottom-left UV V to top-left image coordinates when true.
 * @return Owning region, or a structured diagnostic without side effects.
 * @ownership Returned value owns all its state and retains no backend objects.
 * @thread Pure and thread-safe.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] Result<UvPaintRegion> prepareUvPaintRegionResult(int targetWidth, int targetHeight, float u, float v,
                                                               float radiusU, float radiusV, float r, float g, float b,
                                                               float a, bool flipV = true);

}  // namespace eve::image
