#pragma once

/**
 * @brief Screen-space fluid surface reconstruction (SSF).
 *
 * Splats particles into a depth/thickness buffer, bilaterally smooths the
 * depth (curvature-flow style), reconstructs normals from depth gradients and
 * shades the result as either water (Fresnel + refraction look) or mud
 * (diffuse, thickness-attenuated). Projected particle discs carry spherical-cap
 * depth and chord thickness. The CPU reference mirrors the GLSL algorithms in
 * FluidSsfKernels.h; GPU depth and thickness use fixed-point quantization.
 * This disc approximation is not an exact perspective ray/sphere intersection.
 * The GPU path runs on the gpgpu compute queue.
 */

#include <glm/glm.hpp>
#include "common/Result.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::gpgpu {
class ComputeShader;
class Gpgpu;
class GpuBuffer;
class Sequence;
}  // namespace eve::gpgpu

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::fluids {

class FluidSimulator;
class VolumeFluid;
class VolumeFluidDiffuse;

/** @brief Unity/Fluid3D blend factors used by fluid render settings. */
enum class FluidBlendFactor : int {
    Zero                     = 0,
    One                      = 1,
    DestinationColor         = 2,
    SourceColor              = 3,
    OneMinusDestinationColor = 4,
    SourceAlpha              = 5,
    OneMinusSourceColor      = 6,
    DestinationAlpha         = 7,
    OneMinusDestinationAlpha = 8,
    SourceAlphaSaturate      = 9,
    OneMinusSourceAlpha      = 10,
};

/** @brief Camera + reconstruction tuning for the SSF pipeline. */
struct FluidSurfaceParams {
    glm::vec3 eye{0.f, 0.f, -2.6f};
    glm::vec3 target{0.f};
    glm::vec3 up{0.f, 1.f, 0.f};
    float     fovYDeg          = 55.f;
    bool      orthographic     = false;
    float     orthographicSize = 1.f;
    float     aspect           = 1.f;
    float     nearZ            = 0.05f;
    float     farZ             = 20.f;
    int       width            = 160;
    int       height           = 160;
    float     particleRadius   = 0.05f;
    int       smoothIterations = 2;
    float     depthFalloff     = 0.03f;
    float     thicknessScale   = 1.f;
    /** @brief 0 = water, 1 = mud. */
    int mode = 0;
};

/** @brief Complete owning mirror of Fluid3DRendererSettings with package defaults. */
struct FluidRendererSettings {
    int   blendSource = 5, blendDestination = 10;
    int   particleBlendSource = 2, particleBlendDestination = 0;
    bool  particleZWrite      = false;
    float thicknessCutoff     = 1.2f;
    int   thicknessDownsample = 2;
    bool  generateSurface     = true;
    float blurRadius          = .02f;
    int   surfaceDownsample   = 1;
    bool  lighting            = true;
    float smoothness = .8f, metalness = 0.f, ambientMultiplier = 1.f;
    bool  generateReflection = true;
    float reflection         = .2f;
    bool  generateRefraction = true;
    float transparency = 1.f, absorption = 5.f, refraction = .01f;
    int   refractionDownsample = 1;
    bool  generateFoam         = true;
    int   foamDownsample       = 1;
};

/** @brief Buffer-level screen-space fluid renderer. */
class FluidSurfaceRenderer {
public:
    /**
     * @param params camera / resolution / material tuning.
     * @param preferGpu use compute kernels when a device is available.
     */
    FluidSurfaceRenderer(const FluidSurfaceParams& params, bool preferGpu);
    ~FluidSurfaceRenderer();

    FluidSurfaceRenderer(const FluidSurfaceRenderer&)            = delete;
    FluidSurfaceRenderer& operator=(const FluidSurfaceRenderer&) = delete;

    /** @brief Reconstruct a frame from particle positions. */
    void render(const std::vector<glm::vec3>& positions, float particleRadius);

    /** @brief Reconstruct a frame from a simulator's live particles. */
    void render(const FluidSimulator& sim);
    /** @brief Reconstructs a volume-fluid snapshot and shades its diffusing particle colors. */
    void renderVolume(const VolumeFluid& sim);
    /** @brief Reconstructs from fixed-step interpolated volume positions using the existing SSF pipeline.
     * @param sim Borrowed solver; no pointer is retained.
     * @param alpha Finite fraction in [0,1] between previous and current successful step endpoints.
     * @return Success, or invalid argument before replacing renderer outputs.
     * @details Position interpolation is fused into the existing CPU upload-data copy. It adds no
     * reconstruction pass, buffer, texture, descriptor or GPU submission. Render-thread affine.
     */
    [[nodiscard]] Result<void> renderVolumeInterpolated(const VolumeFluid& sim, float alpha);
    /**
     * @brief Reconstructs only the RGBA output for display, skipping auxiliary GPU readback when color is uniform.
     * @param sim Borrowed simulator read during this call only.
     * @lifetime The simulator is borrowed for the duration of this call and is never retained.
     * @details Render-thread affine. depth(), normals(), and thickness() remain unchanged; do not call
     * compositeDiffuse() after this method. Multicolor frames transparently use the full renderVolume path.
     */
    void renderVolumeColorOnly(const VolumeFluid& sim);
    /**
     * @brief Renders gas particles as a blurred, translucent thickness volume.
     * @param sim Borrowed simulator read during this call only.
     * @param absorption Beer-Lambert extinction coefficient in [0.01,30].
     * @return Success, or a structured error when inputs or the fixed frame budget are exceeded.
     * @lifetime The simulator is borrowed for this call and is never retained.
     * @details Render-thread affine. Selects Gas particles only and does not run liquid surface
     * reconstruction or submit GPU work. The frame is limited to 65536 particles and four million
     * quarter-resolution projected bounding-box pixels; a failure preserves the previous output. Reuses frame scratch.
     */
    [[nodiscard]] Result<void> renderGasVolume(const VolumeFluid& sim, float absorption = 5.f);
    /**
     * @brief Renders every material phase as a blurred color/thickness volume without reconstructing a surface.
     * @param sim Borrowed simulator read during this call only.
     * @param absorption Beer-Lambert extinction coefficient in [0.01,30].
     * @return Success, or a structured error when inputs or the fixed frame budget are exceeded.
     * @lifetime The simulator and its particle view are borrowed for this call and are never retained.
     * @details Implements Fluid3D's generateSurface=false presentation contract. Render-thread affine; performs no
     * GPU work and disables surface lighting implicitly by producing a volume frame directly. The frame is limited
     * to 65536 particles and four million quarter-resolution projected bounding-box pixels. Scratch storage is reused,
     * and a budget failure preserves the previous output.
     */
    [[nodiscard]] Result<void> renderVolumeWithoutSurface(const VolumeFluid& sim, float absorption = 5.f);
    /** @brief Renders through Fluid3D's configured generateSurface policy.
     * @param sim Borrowed simulator read only for this render-thread call.
     * @return Success, or a structured budget failure from the surface-disabled volume path.
     * @details Enabled uses the existing SSF reconstruction. Disabled uses the existing bounded
     * all-phase thickness volume, skipping surface depth smoothing, normals, lighting, reflection,
     * refraction and GPU work. No simulator or callback is retained.
     */
    [[nodiscard]] Result<void> renderConfiguredVolume(const VolumeFluid& sim);
    /** @brief Prepares the preferred reconstruction backend before frame timing begins.
     * @details Render-thread affine. GPU resource and shader creation is attempted once;
     * an unavailable GPU leaves the renderer on its CPU implementation. No simulation or
     * output state is changed. Call usingGpu() afterwards to observe the selected path.
     */
    [[nodiscard]] Result<void> prepare();
    /** @brief Present the current RGBA8 result into an existing graphics texture.
     * @param graphics Borrowed active graphics module.
     * @param texture Borrowed matching single-mip texture owned by graphics.
     * @return Success after presentation, or a structured argument/upload failure.
     * @details Uses an allocation-free device-local copy when the current frame was shaded
     * on the GPU without CPU post-processing. Other backends and modified frames use the
     * existing host color upload. No resource or pointer is retained.
     * @thread Render-thread affine and synchronous.
     */
    [[nodiscard]] Result<void> copyToTexture(graphics::Graphics* graphics, graphics::Texture* texture);
    /** @brief Reconstruct and present a uniform-color volume without host color readback when supported.
     * @param sim Borrowed solver used only during this call.
     * @param graphics Borrowed active graphics module.
     * @param texture Borrowed matching destination texture.
     * @return Success after reconstruction and presentation, or a structured failure.
     * @details Vulkan keeps the shaded RGBA8 buffer device-local. Unsupported backends retain the
     * normal host-visible color contract followed by texture upload. Do not insert CPU color compositing
     * between reconstruction and presentation; use renderVolume plus copyToTexture for those modes.
     */
    [[nodiscard]] Result<void> renderVolumeColorToTexture(const VolumeFluid& sim, graphics::Graphics* graphics,
                                                          graphics::Texture* texture);
    /** @brief Composites white secondary particles over the last reconstructed frame.
     * @param pool Borrowed secondary particles; no pointer is retained or state advanced.
     * @param radius World-space splat radius in (0,1].
     * @param opacity Peak alpha in [0,1].
     * @param fadeSeconds Remaining-life interval in (0,86400] used for fading.
     * @details Render-thread affine, no concurrent calls or callbacks. Call after
     * render/renderVolume with the same camera, before image upload. Modifies only
     * RGBA output, preserving liquid depth/normals/thickness. Tests liquid depth,
     * not external scene geometry. Repeated calls accumulate until the next render.
     * Preflights at most 4M covered bounding-box pixels; failure leaves color intact.
     * Uses CPU buffers already read back by SSF; no new GPU submission or readback.
     */
    [[nodiscard]] Result<void> compositeDiffuse(const VolumeFluidDiffuse& pool, float radius, float opacity,
                                                float fadeSeconds);
    /** @brief Configures Fluid3D-compatible foam generation visibility and render-target downsampling.
     * @param enabled When false, compositeDiffuse returns success without touching the current frame.
     * @param downsample Integer foam target divisor in [1,4].
     * @return Success, or invalid argument without changing prior configuration.
     * @details Render-thread affine. Configuration allocates nothing. Factors above one reduce particle splat
     * visits on each axis, reuse one owned scalar coverage target, then composite through one linear output pass.
     */
    [[nodiscard]] Result<void> configureFoam(bool enabled, int downsample = 1);

    /** @brief Removes fluid and diffuse color hidden by borrowed linear scene depth.
     * @param sceneDepth One finite, nonnegative view-space depth per output pixel; use a large finite value for no
     * geometry.
     * @param depthBias Nonnegative view-space tolerance in [0,1] metres for coincident surfaces.
     * @return Success, or a structured error before any color mutation.
     * @lifetime sceneDepth is borrowed for this call and is never retained.
     * @details Render-thread affine. Call after full render/renderVolume/renderGasVolume and optional
     * compositeDiffuse. This performs one allocation-free linear pixel pass and changes only RGBA.
     * The color-only GPU fast path has no current host depth and is rejected explicitly.
     */
    [[nodiscard]] Result<void> occludeWithSceneDepth(std::span<const float> sceneDepth, float depthBias = 0.001f);

    /** @brief Refracts a matching opaque RGBA8 scene through the current liquid surface.
     * @param sceneColor Borrowed width*height*4 RGBA8 background; it is never retained.
     * @param distortion Maximum screen-space displacement in pixels, in [0,64].
     * @param absorption Beer-Lambert absorption coefficient in [0,30].
     * @return Success, or a structured error before any output mutation.
     * @details Render-thread affine. Call after a full render/renderVolume. Performs one
     * allocation-free bounded pixel pass, modifies only RGBA output, and submits no GPU work.
     * The color-only GPU fast path is rejected because it has no current host auxiliaries.
     */
    [[nodiscard]] Result<void> compositeSceneRefraction(std::span<const uint8_t> sceneColor, float distortion = 8.f,
                                                        float absorption = 3.f);
    /** @brief Configure Fluid3D-compatible scene refraction controls atomically.
     * @param transparency Blend between particle color and refracted scene in [0,1].
     * @param absorption Per-channel Beer-Lambert coefficient in [0,30].
     * @param coefficient Signed normalized-screen bend coefficient in [-0.1,0.1].
     * @param downsample Refraction scene target divisor in [1,4].
     * @return Success, or invalid argument without changing prior configuration.
     */
    [[nodiscard]] Result<void> configureRefraction(float transparency, float absorption, float coefficient,
                                                   int downsample);
    /** @brief Enables or disables Fluid3D's configured refraction stage without losing its controls.
     * @param enabled When false, compositeConfiguredSceneRefraction is a zero-work no-op.
     * @return Success. The current frame and owned scratch are unchanged.
     * @details Render-thread affine; no allocation, scene copy or GPU work occurs during configuration.
     */
    [[nodiscard]] Result<void> configureRefractionEnabled(bool enabled);
    /** @brief Apply the configured Fluid3D-compatible refraction to a matching opaque RGBA8 scene.
     * @param sceneColor Borrowed width*height*4 background bytes.
     * @return Success after one bounded composition, or failure before color mutation.
     * @details Reuses an owned target no larger than the output, submits no GPU work and retains no input.
     */
    [[nodiscard]] Result<void> compositeConfiguredSceneRefraction(std::span<const uint8_t> sceneColor);

    /** @brief Atomically configures Fluid3D's final surface source and destination blend factors.
     * @param source Unity BlendMode integer in [0,10].
     * @param destination Unity BlendMode integer in [0,10].
     * @return Success, or invalid argument without changing the prior pair.
     * @details Configuration allocates nothing and creates no graphics pipeline. The package defaults are
     * SourceAlpha value 5 and OneMinusSourceAlpha value 10.
     * @ownership No external resource is accepted or retained.
     * @lifetime The selected value pair lives with this renderer.
     * @thread Render-thread affine; do not race configuration with rendering.
     */
    [[nodiscard]] Result<void> configureSurfaceBlend(int source, int destination);
    /** @brief Composites the current surface over a matching RGBA8 scene using the configured Fluid3D factors.
     * @param sceneColor Borrowed width*height*4 destination bytes; never retained.
     * @return Success after one bounded linear host pass, or failure before output mutation.
     * @details Supports every Unity BlendMode factor without creating parameterized Vulkan pipelines.
     * Render-thread affine. The resident color-only GPU path is rejected because host RGBA is not current.
     * @ownership No ownership transfer occurs.
     * @lifetime The input span is borrowed only for this synchronous call and is never retained.
     * @thread Render-thread affine; do not race with rendering or presentation.
     */
    [[nodiscard]] Result<void> compositeConfiguredSurfaceBlend(std::span<const uint8_t> sceneColor);
    /** @brief Configures Fluid3D's particle-color target blending and depth writes atomically.
     * @param source Unity BlendMode integer in [0,10].
     * @param destination Unity BlendMode integer in [0,10].
     * @param depthWrite When true, only the nearest particle-color fragment at each pixel survives.
     * @return Success, or invalid argument without changing the prior configuration.
     * @details Enabling this explicit package path disables the uniform-color shortcut because overlap order
     * affects the result. It reuses the existing tint and weight arrays and creates no GPU resources or passes.
     * @ownership No external resource is accepted or retained.
     * @lifetime The values live with this renderer.
     * @thread Render-thread affine; do not race configuration with rendering.
     */
    [[nodiscard]] Result<void> configureParticleBlend(int source, int destination, bool depthWrite);

    /** @brief Atomically configures thickness reconstruction and surface smoothing.
     * @param thicknessScale Thickness multiplier in [0,16].
     * @param thicknessCutoff Fluid3D normalized thickness discard threshold in [0,5]; compares against thickness*10.
     * @param depthFalloff Bilateral depth falloff in (0,1].
     * @param smoothIterations Bounded bilateral pass count in [0,8].
     * @return Success, or a structured error without changing prior configuration.
     */
    [[nodiscard]] Result<void> configureSurface(float thicknessScale, float thicknessCutoff, float depthFalloff,
                                                int smoothIterations);
    /** @brief Enables or disables Fluid3D's generateSurface policy for renderConfiguredVolume.
     * @param enabled True selects existing SSF; false selects existing bounded thickness volume.
     * @return Success without allocating or changing the current frame.
     * @details Render-thread affine; the configured value is owned by this renderer.
     */
    [[nodiscard]] Result<void> configureSurfaceEnabled(bool enabled);
    /** @brief Validates and atomically applies every Fluid3DRendererSettings field.
     * @param settings Owning value borrowed for this render-thread call only.
     * @return Success, or invalid argument without changing any renderer configuration.
     * @details Resets existing lazy reduced renderers once after commit; allocates and submits no GPU work.
     */
    [[nodiscard]] Result<void> configureRendererSettings(const FluidRendererSettings& settings);
    /** @brief Returns an owning snapshot of the current Fluid3D renderer settings without allocation. */
    [[nodiscard]] FluidRendererSettings rendererSettings() const noexcept;
    /** @brief Sets Fluid3D's world-space surface blur radius for subsequent reconstruction.
     * @param radius Finite world-space radius in [0,0.1] metres.
     * @return Success, or invalid argument without changing the prior radius.
     * @details The radius is converted to a bounded 0-4 pixel kernel per depth sample inside the existing
     * bilateral smoothing passes. It adds no pass, resource, descriptor or submission. Render-thread affine.
     */
    [[nodiscard]] Result<void> configureSurfaceBlurRadius(float radius);
    /** @brief Atomically configures lighting and material response for subsequent liquid frames.
     * @param lighting Enables diffuse and specular lighting.
     * @param smoothness Specular smoothness in [0,1].
     * @param metalness Reflection tint metalness in [0,1].
     * @param ambientMultiplier Ambient contribution in [0,6].
     * @param reflection Fresnel reflection coefficient in [0,1].
     * @param opacity Thickness-to-alpha coefficient in [0,30].
     * @return Success, or a structured error without changing prior configuration.
     */
    [[nodiscard]] Result<void> configureMaterial(bool lighting, float smoothness, float metalness,
                                                 float ambientMultiplier, float reflection, float opacity);
    /** @brief Enables or disables the configured reflection contribution without losing its coefficient.
     * @param enabled Matches Fluid3D's generateReflection renderer setting.
     * @return Success. No resource is created and the current output is unchanged until the next render.
     * @details Render-thread affine. Disabling reflection makes the next shaded frame omit its Fresnel reflection;
     * enabling it restores the coefficient last supplied to configureMaterial.
     */
    [[nodiscard]] Result<void> configureReflection(bool enabled);
    /** @brief Sets the linear RGB base and reflection colors atomically.
     * @param baseColor Linear RGB base color with finite channels in [0,1].
     * @param reflectionColor Linear RGB reflection tint with finite channels in [0,1].
     * @return Success, or a structured error without changing prior configuration.
     */
    [[nodiscard]] Result<void> configureColors(const glm::vec3& baseColor, const glm::vec3& reflectionColor);
    /** @brief Enables oriented-ellipsoid projection for volume-fluid particles.
     * @param enabled When true, renderVolume uses particle radii and orientations.
     * @return Success. GPU resources are created lazily on the next nonempty frame.
     * @details The disabled path performs no shape copy, anisotropy preprocessing,
     * extra upload or dispatch. Enabling replaces the spherical splat dispatch; it
     * does not add a second dispatch. Render-thread affine.
     */
    [[nodiscard]] Result<void> configureAnisotropy(bool enabled);
    /** @brief Configures Fluid3D-style surface target downsampling.
     * @param factor Integer reconstruction divisor in [1,4].
     * @return Success, or a structured error without changing the prior factor.
     * @details Factors above one run splat, smoothing, normals and shading at the
     * reduced dimensions, then expand outputs in one bounded linear host pass.
     * Resources are created lazily and retained. Render-thread affine.
     */
    [[nodiscard]] Result<void> configureSurfaceDownsample(int factor);
    /** @brief Sets the independently reconstructed liquid-thickness target divisor.
     * @param factor Integer divisor in [1,4].
     * @return Success, or an invalid-argument diagnostic without changing configuration.
     * @details renderVolume reconstructs thickness at dimensions ceil-divided by factor,
     * expands it once, then performs material shading. The target
     * is independent of surfaceDownsample and created lazily. Render-thread affine.
     * @ownership No external object or pointer is retained.
     * @lifetime The owned reduced target lives until reconfiguration or renderer destruction.
     * @thread Render thread only; do not race configuration with rendering.
     * @reentrancy Must not be called from renderer callbacks.
     */
    [[nodiscard]] Result<void> configureThicknessDownsample(int factor);
    /** @brief Selects perspective or orthographic reconstruction for subsequent frames.
     * @param orthographic Enables an orthographic camera matching Fluid3D camera setup.
     * @param verticalHalfSize Positive orthographic world-space half height in [0.001,10000].
     * @return Success, or invalid argument without changing the prior projection.
     * @details Render-thread affine. Reuses existing buffers and push constants; no GPU resource is created.
     */
    [[nodiscard]] Result<void> configureProjection(bool orthographic, float verticalHalfSize = 1.f);

    /** @brief Script-friendly wrapper: render(*sim) with a null check.
     * @param sim Borrowed simulator; null performs no work.
     * @ownership The simulator is never retained.
     * @lifetime The borrow lasts only for this call.
     */
    void renderFrom(FluidSimulator* sim);

    int getWidth() const { return params_.width; }
    int getHeight() const { return params_.height; }

    /** @brief Linear view depth per pixel (FLT_MAX where no fluid). */
    const std::vector<float>& depth() const { return depth_; }

    /** @brief View-space normals (0 where no fluid). */
    const std::vector<glm::vec3>& normals() const { return normals_; }

    /** @brief Accumulated thickness per pixel. */
    const std::vector<float>& thickness() const { return thickness_; }

    /** @brief RGBA8 shaded output. */
    const std::vector<uint8_t>& color() const { return color_; }

    /** @return true when the GPU compute path is active. */
    bool usingGpu() const { return reducedRenderer_ ? reducedRenderer_->usingGpu() : gpuOk_; }

    /** @brief Switch shading: 0 water, 1 mud. */
    void setMode(int mode) {
        params_.mode = mode;
        resetReducedRenderers();
    }

    /** @brief Update the camera. */
    void setCamera(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up, float fovYDeg);

    /** @brief Write the color buffer as a PPM image (debug artifact). */
    void writePpm(const std::string& path) const;

private:
    void                       renderInternal(const std::vector<glm::vec3>& positions, float particleRadius);
    void                       renderVolumeInternal(const VolumeFluid& sim, bool colorOnly, float alpha);
    [[nodiscard]] Result<void> renderVolumeCloud(const VolumeFluid& sim, float absorption, bool gasOnly);
    void renderCpu();
    bool ensureGpu();
    bool                       ensureMulticolorGpu();
    bool                       ensureAnisotropyGpu();
    void                       releaseGpu() noexcept;
    void uploadParticles();
    void                       buildAnisotropicSplats();
    void setCommonConstants(gpgpu::ComputeShader* shader, float falloff);
    void                       applyConfiguredShading();
    void                       refreshCustomShadingFlag();
    void                       ensureReducedRenderer();
    void                       expandReducedOutputs(bool colorOnly);
    void                       ensureThicknessRenderer();
    void                       replaceThicknessFromReduced();
    void                       resetReducedRenderers();
    static glm::vec4 blendFluidColor(FluidBlendFactor factor, const glm::vec4& source, const glm::vec4& destination);

    FluidSurfaceParams params_;
    bool               preferGpu_ = false;
    /** @brief Prevents repeated resource creation after an unavailable or failed GPU attempt. */
    bool gpuAttempted_ = false;
    bool gpuOk_        = false;

    std::vector<glm::vec3> positions_;
    /** @brief Reused volume-fluid particle colors and per-pixel tint accumulation. */
    std::vector<glm::vec4> volumeColors_;
    /** @brief Uniform volume tint consumed directly by the GPU shade pass when present. */
    glm::vec4 uniformVolumeColor_{0.f};
    bool      uniformVolumeGpuShade_    = false;
    bool      multicolorVolumeGpuShade_ = false;
    bool      multicolorGpuReady_       = false;
    /** @brief Whether host depth belongs to the current color frame. */
    bool auxiliaryCurrent_ = false;
    /** @brief Current RGBA8 output is still authoritative in bufColor_. */
    bool residentColorCurrent_ = false;
    /** @brief One-call hint allowing the Vulkan color download to be omitted. */
    bool                                  skipHostColorReadback_ = false;
    bool                                  customShading_         = false;
    bool                                  anisotropyEnabled_     = false;
    bool                                  anisotropicFrame_      = false;
    int                                   surfaceDownsample_     = 1;
    int                                   thicknessDownsample_   = 1;
    std::unique_ptr<FluidSurfaceRenderer> reducedRenderer_;
    std::unique_ptr<FluidSurfaceRenderer> thicknessRenderer_;
    float                                 thicknessCutoff_ = 0.f;
    bool                                  surfaceEnabled_  = true;
    /** @brief Negative retains the established fixed 5x5 native kernel until explicitly configured. */
    float            surfaceBlurRadius_ = -1.f;
    bool             lighting_          = true;
    float            smoothness_        = .8f;
    float            metalness_         = 0.f;
    float            ambientMultiplier_ = .55f;
    float            reflection_        = .75f;
    bool             reflectionEnabled_ = true;
    float            opacity_           = .35f;
    glm::vec3        baseColor_{.05f, .32f, .72f};
    glm::vec3        reflectionColor_{.55f, .72f, 1.f};
    float            refractionTransparency_   = 1.f;
    bool             refractionEnabled_        = true;
    float            refractionAbsorption_     = 5.f;
    float            refractionCoefficient_    = .01f;
    int              refractionDownsample_     = 1;
    FluidBlendFactor surfaceBlendSource_       = FluidBlendFactor::SourceAlpha;
    FluidBlendFactor surfaceBlendDestination_  = FluidBlendFactor::OneMinusSourceAlpha;
    FluidBlendFactor particleBlendSource_      = FluidBlendFactor::DestinationColor;
    FluidBlendFactor particleBlendDestination_ = FluidBlendFactor::Zero;
    bool             particleDepthWrite_       = false;
    bool             particleBlendConfigured_  = false;
    /** @brief Reused downsampled opaque scene for configured refraction. */
    std::vector<uint8_t>   refractionScratch_;
    std::vector<glm::vec4> volumeTint_;
    std::vector<float>     volumeTintWeights_;
    /** @brief Reused premultiplied gas tint and separable-blur scratch. */
    std::vector<glm::vec4> gasTintScratch_;
    std::vector<float>     gasDensityScratch_;
    struct GasSplat {
        float cx = 0.f, cy = 0.f, r = 0.f, z = 0.f;
        int   x0 = 0, x1 = -1, y0 = 0, y1 = -1;
    };
    std::vector<GasSplat> gasSplats_;
    /** @brief Reused packed GPU upload and normal readback storage. */
    std::vector<float> gpuParticles_;
    /** @brief Reused 3-vec4 projected ellipsoid upload, populated only when enabled. */
    std::vector<float>     anisotropicSplats_;
    std::vector<glm::vec3> particleRadii_;
    std::vector<glm::vec4> particleOrientations_;
    std::vector<glm::vec4> gpuNormals_;
    std::vector<float>     depth_;
    /** @brief Reused CPU bilateral-filter ping-pong storage. */
    std::vector<float>     depthScratch_;
    std::vector<float>     thickness_;
    std::vector<glm::vec3> normals_;
    std::vector<uint8_t>   color_;
    /** @brief Reused CPU handoff storage for diffuse position.xyz/life. */
    std::vector<glm::vec4> diffuseParticles_;
    bool                   foamEnabled_    = true;
    int                    foamDownsample_ = 1;
    /** @brief Reused reduced foam coverage target; empty for the full-resolution path. */
    std::vector<float> foamCoverageScratch_;
    struct DiffuseSplat {
        float x = 0.f, y = 0.f, r = 0.f, z = 0.f, alpha = 0.f;
        int   x0 = 0, x1 = -1, y0 = 0, y1 = -1;
    };
    /** @brief Reused preflight/projected splats; content is derived frame scratch. */
    std::vector<DiffuseSplat> diffuseSplats_;

    eve::gpgpu::Gpgpu*         gpgpu_               = nullptr;
    eve::gpgpu::ComputeShader* shClear_             = nullptr;
    eve::gpgpu::ComputeShader* shSplat_             = nullptr;
    eve::gpgpu::ComputeShader* shAnisotropicSplat_  = nullptr;
    eve::gpgpu::ComputeShader* shSmooth_            = nullptr;
    eve::gpgpu::ComputeShader* shNormal_            = nullptr;
    eve::gpgpu::ComputeShader* shShade_             = nullptr;
    eve::gpgpu::ComputeShader* shColorClear_        = nullptr;
    eve::gpgpu::ComputeShader* shColorSplat_        = nullptr;
    eve::gpgpu::GpuBuffer*     bufParts_            = nullptr;
    eve::gpgpu::GpuBuffer*     bufAnisotropicParts_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufDepthA_           = nullptr;
    eve::gpgpu::GpuBuffer*     bufDepthB_           = nullptr;
    eve::gpgpu::GpuBuffer*     bufThick_            = nullptr;
    eve::gpgpu::GpuBuffer*     bufNormal_           = nullptr;
    eve::gpgpu::GpuBuffer*     bufColor_            = nullptr;
    eve::gpgpu::GpuBuffer*     bufParticleColors_   = nullptr;
    eve::gpgpu::GpuBuffer*     bufColorAccum_       = nullptr;
    eve::gpgpu::GpuBuffer*     stDepth_             = nullptr;
    eve::gpgpu::GpuBuffer*     stThick_             = nullptr;
    eve::gpgpu::GpuBuffer*     stNormal_            = nullptr;
    eve::gpgpu::GpuBuffer*     stColor_             = nullptr;
    eve::gpgpu::Sequence*      seq_                 = nullptr;
};

}  // namespace eve::fluids
