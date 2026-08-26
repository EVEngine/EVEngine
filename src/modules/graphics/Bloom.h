#pragma once

#include <array>

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/**
 * @brief Linear-HDR bloom pyramid with Karis downsampling and tent reconstruction.
 *
 * The returned texture remains owned by Graphics and is valid until the source
 * dimensions change or Graphics is destroyed.
 */
class Bloom {
public:
    /** @brief Create the cross-backend bloom shaders. */
    explicit Bloom(Graphics *gfx);

    /**
     * @brief Build a four-level bloom pyramid from an HDR source.
     * @param source Linear HDR source texture.
     * @param threshold Linear soft-knee threshold.
     * @param scatter Tent reconstruction radius in source texels.
     * @return Reconstructed half-resolution linear HDR bloom texture.
     */
    Texture *build(Texture *source, float threshold, float scatter = 1.f);

    /**
     * @brief Add the reconstructed bloom to the HDR source in a full-size target.
     * @param source Linear HDR source texture.
     * @param intensity Bloom contribution multiplier.
     * @param threshold Linear soft-knee threshold.
     * @param scatter Tent reconstruction radius.
     * @return Full-resolution linear HDR source plus bloom.
     */
    Texture *apply(Texture *source, float intensity, float threshold, float scatter = 1.f);

private:
    void ensureTargets(int sourceWidth, int sourceHeight);
    void configureDownsample(Texture *source, bool firstPass, float threshold);
    void configureUpsample(Texture *source, float scatter);

    Graphics *gfx_ = nullptr;
    Shader *downsample_ = nullptr;
    Shader *upsample_ = nullptr;
    std::array<Canvas *, 4> levels_{};
    Canvas *composite_ = nullptr;
    int sourceWidth_ = 0;
    int sourceHeight_ = 0;
};

}  // namespace eve::graphics
