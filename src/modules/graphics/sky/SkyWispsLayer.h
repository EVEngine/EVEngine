#pragma once
#include <array>
#include <span>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::graphics {
struct ShaderImageInput;
/** @brief Immutable clear-sky daylight material controls, copied during sky preparation.
 * @details Source RGB scattering keys are linear [angleDegrees,r,g,b], with constant endpoints.
 * All colors are linear HDR. Orbit is [pitchDegrees,yawDegrees,verticalOffset,phaseTurns].
 * This is an upload value, not a clock, mutable authority or persistent schema.
 * @thread Validation is pure and worker-safe; no callbacks or retained references. */
struct SkyDaylightLayer {
    /** @brief Source clear-sky optical tints; rayDay.w is scattering scale, absorption w is its scale. */
    std::array<float, 4> rayDay{.168627f, .407843f, 1, .04f}, rayDusk{.17f, .409234f, 1, 0},
        rayNight{.241211f, .347328f, .609375f, 0};
    std::array<float, 4> absorptionDay{.198069f, .095307f, 1, .002f}, absorptionNight{.030354f, .181178f, 1, .004f};
    std::array<std::array<float, 4>, 21> scatteringCurve{};
    std::array<float, 4>                 sun{1, 1, 1, 5}, moon{.445801f, .557475f, .864583f, .15f};
    std::array<float, 4>                 moonOrbit{35, 0, .04f, 0};
    std::array<float, 4>                 dayTint{.476966f, .473985f, .526042f, 1};
    std::array<float, 4>                 duskTint{1, .587119f, .395833f, 1};
    std::array<float, 4>                 nightTint{.367187f, .428386f, .489583f, 1};
    std::array<float, 4>                 glow{.093045f, .141694f, .255208f, .6f};
    /** @brief Night brightness, absent-light boost, illuminated moon fraction and overall intensity. */
    std::array<float, 4> controls{1, 2, 1, 1};
    /** @brief Linear star tint and source intensity, before the night filter. */
    std::array<float, 4> stars{.484375f, .639062f, 1, .75f};
    /** @brief Tiling, per-day UV movement, rotation in turns and fixed star phase. */
    std::array<float, 4> starUv{2.5f, 5, 0, 0};
    /** @brief Noise floor/strength, glow reflection scale and reserved zero. Twinkle time is frozen. */
    std::array<float, 4> twinkle{.7f, 1.5f, 2, 0};
    /** @brief Linear lunar surface tint; w enables the disk (0 or 1), disabled in legacy assets. */
    std::array<float, 4> moonDiskColor{.486328f, .574971f, .864583f, 0};
    /** @brief UV diameter in radians, horizon enlargement, texture rotation degrees and earthlight. */
    std::array<float, 4> moonDiskShape{.033161256f, 1, 0, .005f};
    /** @brief Night/day texture intensity, fixed lunar phase days [0,29.53], phase contrast.
     * @details Phase is authored independently of the shared daily orbital clock; it does not advance a second clock.
     */
    std::array<float, 4> moonDiskLighting{.3f, .6f, 0, 1.6f};
    /** @brief Glow intensity, angular projection scale and two reserved zeros. */
    std::array<float, 4> moonDiskGlow{.05f, .20212766f, 0, 0};
    /** @brief Reject nonfinite, out-of-range or unordered authored data without mutation. */
    [[nodiscard]] EVENGINE_API_BACKENDS Result<void> validate() const;
};
/** @brief Borrowed authored thin-cloud layer, consumed synchronously by sky preparation.
 * @details Triangle-list corners are x,y,z,u,v,r,g,b,a in Unreal local centimetres.
 * Coordinates convert to native (x,z,y) metres, with the explicit uniform dome scale.
 * Texture storage/sRGB interpretation is explicit in densityTexture; its binding is reassigned.
 * No file loading, provider pointers, clocks or callbacks are retained. All values are copied to GPU storage.
 * This is a runtime upload description, not a persistent file schema. */
struct SkyWispsLayer {
    /** @brief Opt-in daylight chain. Disabled for legacy manifests; no independent time source. */
    bool daylightEnabled = false;
    /** @brief Opt-in source optical cycle, prepared as an immutable height-sampled LUT volume. */
    bool             opticalCycleEnabled = false;
    SkyDaylightLayer daylight;
    /** @brief Optional star/noise uploads, borrowed through preparation only; required by daylightEnabled. */
    const ShaderImageInput* starsTexture      = nullptr;
    const ShaderImageInput* starsNoiseTexture = nullptr;
    /** @brief Lunar color/alpha and phase-normal uploads; borrowed only until prepare returns.
     * Required when daylight.moonDiskColor.w is 1; ignored otherwise. */
    const ShaderImageInput* moonColorTexture  = nullptr;
    const ShaderImageInput* moonNormalTexture = nullptr;
    /** @brief Optional RGB elevation curves: four (height,value,arrive tangent,leave tangent) keys per channel.
     * Values scale disk radiance relative to each channel's final key; heights are (1+sunDirection.y)/2.
     * Unweighted cubic Hermite interpolation with constant endpoint extrapolation; no clock is retained. */
    std::array<std::array<float, 4>, 12> sunDiskCurve{};
    bool                                 sunDiskCurveEnabled = false;
    /** @brief Solar disk RGB radiance; zero disables it. Copied at preparation, independent of light irradiance. */
    std::array<float, 3> sunDiskColor{0, 0, 0};
    /** @brief Angular radius in radians, positive edge exponent and nonnegative reflection multiplier. */
    std::array<float, 3> sunDiskShape{.021380283f, 3.f, 0.f};
    /** @brief Parent material contrast, midpoint and intensity, applied after sky/cloud composition. */
    std::array<float, 3> materialContrast{0, 1, 1};
    /** @brief Apply authored sun/moon intensity gradients to the physical sky before composition. */
    bool                    authoredSkyLighting = false;
    std::span<const float>  corners;
    const ShaderImageInput* densityTexture = nullptr;
    float                   domeScale      = 5000;
    std::array<float, 3>    color{.355900f, .399502f, .469850f};
    std::array<float, 4>    gradient{.5f, 0, -.866025f, 5};
    std::array<float, 2>    movement{-1, 0};
    std::array<float, 3>    moonForward{0, 0, 1};
    float                   morphRate = 1, morphPeriod = 10, morphAmount = .4f;
    float                   opacity = .2f, litIntensity = .8f;
    float                   cloudTime    = 0;
    bool                    moonGradient = true, staticClouds = false;
};
}  // namespace eve::graphics
