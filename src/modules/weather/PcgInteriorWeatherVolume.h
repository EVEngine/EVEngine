#pragma once

#include "common/Result.h"

namespace eve::weather {

/** @brief Shape used by Pcg's interior-weather containment test. */
enum class PcgInteriorWeatherShape { Box = 0, Sphere = 1 };

/** @brief Interior precipitation policy mirrored from Pcg. */
enum class PcgInteriorWeatherMode { Collision = 0, DisableVfx = 1 };

/** @brief Observable transition produced by an interior-volume sample. */
enum class PcgInteriorWeatherTransition { Unchanged = 0, Entered = 1, Exited = 2 };

/**
 * @brief Caller-owned, immutable-after-configuration Pcg interior-weather volume.
 * @ownership This value owns all configuration; Weather copies sampled state and never retains this object.
 * @thread Affine to the caller; configure and sample must not overlap.
 * @reentrancy Does not invoke callbacks.
 */
class PcgInteriorWeatherVolume {
public:
    /** @brief Configure an axis-aligned box atomically. Dimensions must be finite and positive. */
    [[nodiscard]] Result<void> configureBox(float centerX, float centerY, float centerZ,
                                            float sizeX, float sizeY, float sizeZ,
                                            int mode, int interiorReverbPreset,
                                            int exteriorReverbPreset);
    /** @brief Configure a sphere atomically. Radius must be finite and positive. */
    [[nodiscard]] Result<void> configureSphere(float centerX, float centerY, float centerZ,
                                               float radius, int mode,
                                               int interiorReverbPreset,
                                               int exteriorReverbPreset);
    /** @brief Return whether a finite world-space position lies inside, including the boundary. */
    bool contains(float x, float y, float z) const noexcept;

    PcgInteriorWeatherShape shape() const noexcept { return shape_; }
    PcgInteriorWeatherMode mode() const noexcept { return mode_; }
    int interiorReverbPreset() const noexcept { return interiorReverbPreset_; }
    int exteriorReverbPreset() const noexcept { return exteriorReverbPreset_; }

private:
    PcgInteriorWeatherShape shape_ = PcgInteriorWeatherShape::Box;
    PcgInteriorWeatherMode mode_ = PcgInteriorWeatherMode::Collision;
    float centerX_ = 0.f, centerY_ = 0.f, centerZ_ = 0.f;
    float extentX_ = 15.f, extentY_ = 15.f, extentZ_ = 15.f;
    int interiorReverbPreset_ = 0;
    int exteriorReverbPreset_ = 0;
};

}  // namespace eve::weather
