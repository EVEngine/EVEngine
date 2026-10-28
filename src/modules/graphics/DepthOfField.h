#pragma once

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Separable Gaussian depth-of-field for the final HDR scene.
 *
 * Reads linear HDR color plus hardware depth (Vulkan NDC z). Blur radius grows
 * with view-space distance from `focusDistance` over `focusRange`, capped at
 * `maxBlurPx`. Zero max blur is a no-op. Used by HD-2D miniature looks; sprites
 * must write depth (masked/cutout) so characters stay sharp in the focus band.
 */
class DepthOfField {
public:
    /** @brief Create the cross-backend DOF shaders. */
    explicit DepthOfField(Graphics *gfx);

    /**
     * @brief Apply separable Gaussian DOF.
     * @param source Linear HDR scene color.
     * @param hwDepth Hardware depth (NDC z in .r). Null disables the effect.
     * @param focusDistance View-space focus plane distance.
     * @param maxBlurPx Maximum blur radius in source texels; <= 0 disables.
     * @param focusRange Distance from focus at which blur reaches maxBlurPx.
     * @param nearZ Camera near clip.
     * @param farZ Camera far clip.
     * @return Blurred HDR texture owned by this DepthOfField, or `source` when disabled.
     * @lifetime Returned texture remains valid until dimensions change or this object is destroyed.
     */
    Texture *apply(Texture *source, Texture *hwDepth, float focusDistance, float maxBlurPx,
                   float focusRange, float nearZ, float farZ);

private:
    void ensureTargets(int width, int height);
    void configure(float dirX, float dirY, float focusDistance, float maxBlurPx, float focusRange,
                   float nearZ, float farZ, int width, int height);

    Graphics *gfx_ = nullptr;
    Shader *shader_ = nullptr;
    Canvas *temp_ = nullptr;
    Canvas *output_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace eve::graphics
