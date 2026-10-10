#pragma once
#include <array>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::graphics {
struct SkyAtmosphereParameters;
namespace detail {
/** @brief Internal owning RGBA32F table candidates, never partially published. */
struct SkyAtmosphereLuts {
    static constexpr unsigned TransmittanceWidth = 256, TransmittanceHeight = 64, MultiWidth = 32, MultiHeight = 32;
    /** @brief Owned table pixels, allocated during the explicit bake.
     * @cost Copies scale with the fixed LUT texel count; move candidates during preparation. */
    std::vector<float> transmittance, multiScattering;
};
/** @brief Bake coefficients using UE's default 10/15-sample, two-direction approximation.
 * @return Owning table data or a structured validation/baking failure.
 * @thread CPU-only, with no global state or callbacks. Input is borrowed only for this call.
 * @cost Hundreds of thousands of medium evaluations, amortized over a configuration lifetime. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<SkyAtmosphereLuts> bakeSkyAtmosphereLuts(
    const SkyAtmosphereParameters& parameters);
/** @brief Integrate distant-sky ambient radiance per unit incident RGB irradiance at six kilometres.
 * @details Uses 64 fixed stratified directions, ten samples per ray, isotropic phase and no ground bounce.
 * The caller must supply tables baked from the same immutable parameters. Direction is Y-up towards the light.
 * @return Linear RGB radiance, or a structured invalid-input/numerical failure.
 * @thread CPU-only; no global RNG, provider access or retained references.
 * @cost Hundreds of medium evaluations; run during lighting preparation, never per pixel or draw. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<std::array<float, 3>> bakeDistantSkyAmbient(
    const SkyAtmosphereParameters& parameters, const SkyAtmosphereLuts& tables,
    const std::array<float, 3>& lightDirection);
}  // namespace detail
}  // namespace eve::graphics
