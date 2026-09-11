#pragma once

#include <array>
#include "common/Result.h"

namespace eve::graphics {

class Canvas;
class Graphics;
class Shader;
class Texture;

/** @brief Linear HDR reconstruction filter; KarisTent preserves the default pipeline. */
enum class BloomFilter { KarisTent, GaussianScatter };

/** @brief Value-owned Gaussian bloom settings, independent of camera intensity and threshold. */
struct BloomFilterSettings {
    BloomFilter filter        = BloomFilter::KarisTent;
    float       scatter       = 0.68f;
    int         maxIterations = 6;
    float       clamp         = 65472.f;
};

/** @brief Validate enum, finite scatter [0,1], iterations [1,16], and finite clamp (0,65504].
 * @return
 * InvalidArgument on invalid input, success otherwise; no mutation or allocation.
 * @thread Any thread; pure and
 * reentrant, retaining no references.
 */
[[nodiscard]] Result<void> validateBloomFilterSettings(const BloomFilterSettings& settings);

/**
 * @brief Linear-HDR bloom pyramid with selectable Karis/tent or Gaussian/scatter reconstruction.
 *
 * The returned texture remains owned by Graphics and is valid until the source
 * dimensions change or Graphics is destroyed.
 */
class Bloom {
public:
    /** @brief Create the cross-backend bloom shaders. */
    explicit Bloom(Graphics *gfx);

    /** @brief Select a reconstruction filter for subsequent builds.
     * @return InvalidArgument without mutation for
     * invalid settings, success otherwise.
     * @ownership Copies settings; GPU targets remain owned by Graphics.

     * * @thread Render thread only; invokes no callbacks. Target allocation is deferred to build.
     */
    [[nodiscard]] Result<void> configureFilter(const BloomFilterSettings& settings);
    /** @brief Return a value snapshot. Render thread only; retains no references. */
    BloomFilterSettings filterSettings() const { return filterSettings_; }

    /**
     * @brief Build the selected bloom pyramid from an HDR source.
     * @param source Linear HDR source texture.
     * @param threshold Linear soft-knee threshold.
     * @param scatter Tent reconstruction radius in source texels; Gaussian uses filterSettings().scatter.
     *
     * @return Reconstructed half-resolution linear HDR bloom texture.
     * @lifetime The returned texture is owned by this Bloom and remains valid until its targets resize.
     */
    Texture *build(Texture *source, float threshold, float scatter = 1.f);

    /**
     * @brief Add the reconstructed bloom to the HDR source in a full-size target.
     * @param source Linear HDR source texture.
     * @param intensity Bloom contribution multiplier.
     * @param threshold Linear soft-knee threshold.
     * @param scatter Tent reconstruction radius.
     * @return Full-resolution linear HDR source plus bloom.
     * @lifetime The returned texture is owned by this Bloom and remains valid until its targets resize.
     */
    Texture *apply(Texture *source, float intensity, float threshold, float scatter = 1.f);

private:
    Texture*                buildGaussian(Texture* source, float threshold);
    Texture*                compositeGaussian(Texture* source, Texture* bloom, float intensity);
    BloomFilterSettings     filterSettings_{};
    Shader*                 gaussianShader_ = nullptr;
    std::array<Canvas*, 16> gaussianDown_{};
    std::array<Canvas*, 16> gaussianUp_{};
    int                     gaussianWidth_  = 0;
    int                     gaussianHeight_ = 0;
    int                     gaussianLevels_ = 0;
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
