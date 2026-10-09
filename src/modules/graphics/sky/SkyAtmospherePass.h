#pragma once
#include <array>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include "common/Result.h"

namespace eve::graphics {
class Graphics;
struct SkyWispsLayer;

/** @brief Analytic background height fog in metre units, independent of legacy scene fog.
 * @details Density zero disables the layer. Ambient lighting is derived from the same atmosphere and sun.
 * This value is not a scene-geometry fog pass or a volumetric scattering volume. */
struct SkyHeightFogParameters {
    /** @brief Optional directional fog fade over sun direction Y in [-1,1].
     * Equal endpoints disable fading; otherwise lower/upper endpoints produce zero/full authored color.
     * Only directionalInscattering is scaled; physical atmospheric transmittance remains independent. */
    std::array<float, 2> directionalElevationRange{0, 0};
    float                densityPerMetre       = 0;
    float                heightFalloffPerMetre = .006f;
    float                baseHeightMetres      = -1.5f;
    float                startDistanceMetres   = 73.4803369f;
    std::array<float, 3> inscattering{0, 0, 0};
    float                maximumOpacity = 1;
    std::array<float, 3> directionalInscattering{.78809762f, .64244735f, .55544519f};
    float                directionalExponent            = 5;
    float                directionalStartDistanceMetres = 100;
    float                atmosphereContribution         = 2.3f;
    float                skyDistanceMetres              = 100000;
    /** @brief Validate finite bounded fog coefficients without mutation. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Spherical atmospheric coefficients in inverse kilometres and kilometre heights.
 * @details Defaults are resolved from the local UDS 5.8 clear-noon SkyAtmosphere component.
 * This runtime value is not a persistent schema. */
struct SkyAtmosphereParameters {
    std::array<float, 3>   rayleigh{.0067450796f, .0163137194f, .04f};
    float                  rayleighHeightKm = 8;
    std::array<float, 3>   mieScattering{.0112259542f, .0123162284f, .013996f};
    float                  mieHeightKm = 1.2f;
    std::array<float, 3>   mieAbsorption{.000444f, .000444f, .000444f};
    float                  mieAnisotropy = .75f;
    std::array<float, 3>   ozoneAbsorption{.001794476f, .002f, .000190614f};
    float                  ozoneCenterKm      = 25;
    float                  ozoneHalfWidthKm   = 15;
    float                  groundRadiusKm     = 6360;
    float                  atmosphereHeightKm = 60;
    std::array<float, 3>   groundAlbedo{.40197778f, .40197778f, .40197778f};
    float                  multiScatteringFactor = 1;
    SkyHeightFogParameters heightFog;
    /** @brief Validate finite optical coefficients, bounded geometry and energy controls without mutation. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Explicit celestial lighting snapshot; unit direction points towards the light. */
struct SkyCelestialLight {
    glm::vec3 direction{-.5f, .8660254f, 0};
    glm::vec3 irradiance{5.f};
};

/** @brief One atomic render snapshot, independent of view count or wall-clock time. */
struct SkyAtmosphereFrame {
    SkyCelestialLight sun;
    float             wispsMorphPhase = 0;  // Normalized periodic phase in [0,1).
    /** @brief Injected solar-day fraction in [0,1); zero is midnight, independent of render count. */
    float dayPhase = 0;
};

/**
 * @brief Prepared HDR atmospheric sky draw with independent optical parameters and lookup tables.
 * @details Main-view and reflection contributors use the same program and lighting snapshot.
 * Includes two-direction finite-order multiple scattering and optional authored thin-cloud compositing.
 * Authored layers may include a solar disk with separate reflection scaling.
 * The optional daylight layer adds tiling stars and source-driven cloud/glow lighting.
 * The lunar option adds phase-normal shading, star occultation and glow through the same sky composite.
 * Volumetric clouds remain separate; this pass does not claim full UDS equivalence.
 * @thread All operations and destruction run on the graphics thread outside contributor dispatch.
 * @lifetime The pass owns its registrations; Graphics owns GPU allocations. Provider-first
 * destruction invalidates a weak token, and later draws fail without dereferencing it.
 * Pass-first destruction unregisters callbacks and releases mesh/shader allocations.
 * The fixed-size view Canvas follows the existing Graphics-owned Canvas lifetime.
 */
class SkyAtmospherePass {
public:
    /** @brief Prepare an independent atmospheric program and immutable coefficient buffer.
     * @param wisps Optional authored thin-cloud inputs, borrowed only until preparation returns.
     * @return Owning prepared pass or structured failure; no contributors are published on failure.
     * @cost GPU pipeline creation and upload, plus 81 CPU optical bakes for the optical cycle;
     * amortize over the lifetime of a sky configuration (about 22 MiB of shared optical volume data,
     * plus about 21.3 MiB for the two optional 1024-square RGBA16F lunar mip chains). */
    [[nodiscard]] static Result<std::unique_ptr<SkyAtmospherePass>> prepare(Graphics&                      graphics,
                                                                            const SkyAtmosphereParameters& parameters,
                                                                            const SkyWispsLayer* wisps = nullptr);
    ~SkyAtmospherePass();
    SkyAtmospherePass(const SkyAtmospherePass&)            = delete;
    SkyAtmospherePass& operator=(const SkyAtmospherePass&) = delete;
    /** @brief Atomically set explicit light direction and RGB irradiance; rejects nonfinite or negative energy. */
    [[nodiscard]] Result<void> setLight(const SkyCelestialLight& light);
    /** @brief Validate and atomically publish solar lighting, normalized cloud phase and solar-day fraction.
     * @thread Graphics thread outside drawing. Copies the snapshot; no callbacks or GPU allocation.
     * @return Failure leaves lighting and both phases unchanged. */
    [[nodiscard]] Result<void> setFrame(const SkyAtmosphereFrame& frame);
    /** @brief Register the prepared pass for main view and reflection captures, idempotently.
     * @return Success or failure without partial registration. Must be outside draw callbacks. */
    [[nodiscard]] Result<void> attach();
    /** @brief Cancel both registrations; does not mutate legacy DayNight state. */
    void detach() noexcept;
    /** @brief Prepare the cached sky-view texture before opening a destination pass.
     * @param viewProjection Invertible camera matrix, borrowed for this call only.
     * @param eyeMetres Camera world position; only the optical camera is clamped above ground.
     * @return Success or a structured error; failure invalidates the candidate view.
     * @thread Graphics thread, outside any open 3D render pass; no callbacks.
     * @cost One 192x104 atmospheric integration pass for changed camera/light inputs;
     * identical inputs reuse the cache. The existing offscreen backend waits for submission. */
    [[nodiscard]] Result<void> prepareView(const glm::mat4& viewProjection, const glm::vec3& eyeMetres);
    /** @brief Draw a prepared view into an already-open HDR 3D pass; sky writes only far-depth background pixels.
     * @param viewProjection Invertible current camera transform.
     * @param eyeMetres Current camera position; world Y=0 is the reference ground surface.
     * @return Success, or invalid/provider-expired status before submission. */
    [[nodiscard]] Result<void> draw(const glm::mat4& viewProjection, const glm::vec3& eyeMetres);

private:
    [[nodiscard]] Result<void> drawView(const glm::mat4&, const glm::vec3&, bool reflection);
    SkyAtmospherePass();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::graphics
