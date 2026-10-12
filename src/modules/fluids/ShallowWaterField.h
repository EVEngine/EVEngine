#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace eve::gpgpu {
class ComputeShader;
class Gpgpu;
class GpuBuffer;
class Sequence;
}  // namespace eve::gpgpu

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::fluids {

/** @brief Analytic obstacle geometry sampled into the shallow-water grid. */
enum class ShallowWaterColliderShape : uint8_t { Sphere = 0, Capsule = 1, Box = 2 };

/**
 * @brief World-space interactor used for character legs / props.
 *
 * Capsules are vertical by default: `halfExtent.y` is half the cylinder height and
 * `radius` is the shaft radius (matching VolumeFluidCollider capsule semantics).
 * Callers update moving samples each frame before `step`.
 */
struct ShallowWaterCollider {
    /** @brief Application label; must be unique within one `setColliders` call. */
    unsigned label = 0;
    /** @brief Analytic primitive. */
    ShallowWaterColliderShape shape = ShallowWaterColliderShape::Sphere;
    /** @brief World center; capsule Y axis is vertical. */
    glm::vec3 center{0.f, 1.f, 0.f};
    /** @brief Box half extents; capsule uses `halfExtent.y` as half height. */
    glm::vec3 halfExtent{0.25f};
    /** @brief Sphere or capsule radius. */
    float radius = 0.25f;
    /** @brief World velocity sampled at the collider center. */
    glm::vec3 velocity{0.f};
    /** @brief Scales displaced water volume. */
    float displacementScale = 1.f;
    /** @brief Scales horizontal momentum transfer from collider velocity. */
    float dragScale = 1.f;
};

/** @brief Authoritative settings for a shallow-water heightfield. */
struct ShallowWaterSettings {
    /** @brief Grid resolution along world X, in [8, 512]. */
    int resolutionX = 64;
    /** @brief Grid resolution along world Z, in [8, 512]. */
    int resolutionZ = 64;
    /** @brief Cell edge length in meters, in (0, 10]. */
    float cellSize = 0.25f;
    /** @brief World-space origin of cell (0,0) corner (Y is bed baseline). */
    glm::vec3 origin{0.f};
    /** @brief Gravity magnitude used by Saint-Venant fluxes. */
    float gravity = 9.81f;
    /** @brief Artificial viscosity for Lax-Friedrichs stability, in [0, 1]. */
    float viscosity = 0.08f;
    /** @brief Linear momentum friction per second, in [0, 8]. */
    float friction = 0.12f;
    /** @brief Depth treated as dry. */
    float minDepth = 1e-4f;
    /** @brief Hard clamp on water column height. */
    float maxDepth = 2.5f;
    /** @brief Courant number used to pick substeps. */
    float cfl = 0.45f;
    /** @brief Upper bound on substeps per `step` call. */
    int maxSubsteps = 8;
    /** @brief Prefer the gpgpu path when a device is available. */
    bool preferGpu = true;
    /** @brief Initial water depth filled on create. */
    float initialDepth = 0.55f;
    /** @brief Flat bed height filled on create. */
    float bedHeight = 0.f;
};

/**
 * @brief GPU-first shallow-water (2D Saint-Venant) free-surface solver.
 *
 * Owns height `h`, momentum `(hu, hv)` and bed `b` on a Cartesian XZ grid. The
 * CPU reference mirrors the GLSL kernels 1:1 and is selected automatically when
 * compute is unavailable. Character-leg capsules write displacement / momentum
 * sources before each integrate pass.
 *
 * @thread Graphics/device owner thread for GPU path; CPU path is reentrant for
 *         distinct instances only.
 */
class EVENGINE_API_DOMAINS ShallowWaterField {
public:
    /**
     * @brief Validate settings and allocate a filled lake.
     * @param settings Borrowed immutable configuration.
     * @return Owned solver, or InvalidArgument when ranges fail.
     */
    [[nodiscard("check shallow-water create")]] static Result<std::unique_ptr<ShallowWaterField>> create(
        const ShallowWaterSettings& settings);

    ~ShallowWaterField();

    ShallowWaterField(const ShallowWaterField&)            = delete;
    ShallowWaterField& operator=(const ShallowWaterField&) = delete;

    /** @brief Advance the free surface by `dt` seconds (CFL substeps). */
    [[nodiscard("check shallow-water step")]] Result<void> step(float dt);

    /**
     * @brief Replace analytic interactors atomically.
     * @param colliders Borrowed samples valid only for the call duration.
     */
    [[nodiscard("check shallow-water colliders")]] Result<void> setColliders(
        std::span<const ShallowWaterCollider> colliders);

    /** @brief Replace bed heights (size must equal cellCount()). */
    [[nodiscard("check shallow-water bed")]] Result<void> setBed(std::span<const float> beds);

    /** @brief Fill every cell with a uniform water depth (momentum cleared). */
    [[nodiscard("check shallow-water fill")]] Result<void> fillDepth(float depth);

    /**
     * @brief Inject a localized height bump and radial outflow (mouse / splash).
     * @param worldX World X of the disturbance center.
     * @param worldZ World Z of the disturbance center.
     * @param radius Influence radius in meters.
     * @param heightDelta Added water column height.
     * @param radialSpeed Outward horizontal speed applied under the bump.
     */
    [[nodiscard("check shallow-water disturb")]] Result<void> disturbAt(float worldX, float worldZ, float radius,
                                                                        float heightDelta, float radialSpeed = 0.f);

    /** @brief Convenience splash: positive strength raises water, negative digs. */
    [[nodiscard("check shallow-water impulse")]] Result<void> addImpulse(float worldX, float worldZ, float radius,
                                                                         float strength);

    /** @return Grid resolution along X. */
    [[nodiscard]] int resolutionX() const noexcept { return resX_; }
    /** @return Grid resolution along Z. */
    [[nodiscard]] int resolutionZ() const noexcept { return resZ_; }
    /** @return Cell edge length in meters. */
    [[nodiscard]] float cellSize() const noexcept { return cellSize_; }
    /** @return Cell count (`resolutionX * resolutionZ`). */
    [[nodiscard]] int cellCount() const noexcept { return resX_ * resZ_; }
    /** @return World origin of cell (0,0). */
    [[nodiscard]] glm::vec3 origin() const noexcept { return origin_; }
    /** @return True when the GPU compute path is active. */
    [[nodiscard]] bool usingGpu() const noexcept { return gpuOk_; }
    /** @return True when GPU was requested but CPU was selected. */
    [[nodiscard]] bool usedBackendFallback() const noexcept { return backendFallback_; }

    /** @return Water-column heights (authoritative CPU mirror). */
    [[nodiscard]] std::span<const float> depths() const noexcept { return heights_; }
    /** @return Bed heights. */
    [[nodiscard]] std::span<const float> beds() const noexcept { return beds_; }
    /** @return X momentum `hu`. */
    [[nodiscard]] std::span<const float> momentumX() const noexcept { return momentumX_; }
    /** @return Z momentum `hv`. */
    [[nodiscard]] std::span<const float> momentumZ() const noexcept { return momentumZ_; }
    /** @return Live collider samples. */
    [[nodiscard]] std::span<const ShallowWaterCollider> colliders() const noexcept { return colliders_; }

    /** @brief Bilinear free-surface height `b + h` at a world XZ point. */
    [[nodiscard]] float surfaceHeightAt(float worldX, float worldZ) const noexcept;
    /** @brief Bilinear water depth at a world XZ point. */
    [[nodiscard]] float depthAt(float worldX, float worldZ) const noexcept;
    /** @brief Bilinear horizontal velocity at a world XZ point. */
    [[nodiscard]] glm::vec2 velocityAt(float worldX, float worldZ) const noexcept;

    /**
     * @brief Build a Y-up triangle mesh of the free surface for Graphics upload.
     * @param[out] positionsXYZ Vertex positions (3 floats each).
     * @param[out] normalsXYZ Vertex normals (3 floats each).
     * @param[out] uvsST Vertex UVs in [0,1]^2 (2 floats each).
     * @param[out] indices Triangle indices.
     * @param visualHeightGain Display-only scale for `(h - rest)` relative to mean depth; 1 keeps
     *        physical heights. Values in (0, 32] exaggerate centimetre waves for readability.
     * @param[out] colorsRGBA Optional per-vertex RGBA (4 floats each) for coloured meshes;
     *             pass nullptr to skip. Packs the same channel layout as `copyFieldToImage` with
     *             rest-relative depth (`depthContrast = 0.12 * meanH`, `velocityScale = 1.5`).
     */
    [[nodiscard("check shallow-water mesh")]] Result<void> buildSurfaceMesh(
        std::vector<float>& positionsXYZ, std::vector<float>& normalsXYZ, std::vector<float>& uvsST,
        std::vector<uint32_t>& indices, float visualHeightGain = 1.f, std::vector<float>* colorsRGBA = nullptr) const;

    /**
     * @brief Pack per-cell depth / velocity into an RGBA8 image (`resolutionX` × `resolutionZ`).
     * @param image Destination; must be RGBA8 with width=`resolutionX` and height=`resolutionZ`.
     * @param velocityScale Horizontal speed that maps to the full channel range, in (0, 50].
     * @param depthContrast When `<= 0`, R stores absolute `h / maxDepth`. When `> 0`, R stores a
     *        rest-relative anomaly `0.5 + 0.5 * clamp((h - meanH) / depthContrast, -1, 1)` so centimetre
     *        waves still paint the red channel (range `(0, maxDepth]`).
     * @details Channel layout (row `z`, column `x`, matching mesh UVs):
     *          - R: water depth (absolute or rest-relative; see `depthContrast`)
     *          - G: `0.5 + 0.5 * clamp(vx / velocityScale, -1, 1)`
     *          - B: `0.5 + 0.5 * clamp(vz / velocityScale, -1, 1)`
     *          - A: `clamp(|v| / velocityScale, 0, 1)` (foam / speed mask)
     * @ownership Does not retain `image`; caller owns the pixel buffer for the call duration.
     * @return Success, or InvalidArgument when the image shape/format/scale is rejected.
     */
    [[nodiscard("check shallow-water field image")]] Result<void> copyFieldToImage(image::ImageData& image,
                                                                                   float velocityScale = 2.f,
                                                                                   float depthContrast = 0.f) const;

    /**
     * @brief Write a shaded water albedo (RGBA8, same size as the grid) for Material upload.
     * @param image Destination RGBA8 `resolutionX` × `resolutionZ`.
     * @param restDepth Reference depth used to exaggerate crests/troughs; typically `initialDepth`.
     * @param velocityScale Speed scale for foam / flow tint, in (0, 50].
     * @details Deep water is dark cyan; crests lighten; horizontal flow shifts green/magenta;
     *          foam (near-white) follows speed. Intended for `setAlbedoTexture` / `setTexture`.
     * @ownership Does not retain `image`.
     */
    [[nodiscard("check shallow-water albedo image")]] Result<void> copyShadedAlbedoToImage(
        image::ImageData& image, float restDepth, float velocityScale = 2.f) const;

    /**
     * @brief Write tangent-space normals from free-surface slopes (RGBA8, same grid size).
     * @param image Destination RGBA8 `resolutionX` × `resolutionZ`.
     * @param normalStrength Slope exaggeration in (0, 64]; higher values make small waves catch light.
     * @details RGB stores `0.5 + 0.5 * n` with `n` in tangent space (Z-up in texture); A = 1.
     * @ownership Does not retain `image`.
     */
    [[nodiscard("check shallow-water normal image")]] Result<void> copyNormalToImage(image::ImageData& image,
                                                                                     float normalStrength = 6.f) const;

    /** @return Mutable tuning copy (does not resize the grid). */
    [[nodiscard]] ShallowWaterSettings settings() const noexcept;

private:
    explicit ShallowWaterField(const ShallowWaterSettings& settings);

    [[nodiscard]] Result<void> validateSettings(const ShallowWaterSettings& settings) const;
    void                       stepCpu(float dt);
    void                       rasterizeCollidersCpu(float dt);
    void                       integrateCpu(float dt);
    [[nodiscard]] bool         ensureGpu();
    [[nodiscard]] Result<void> stepGpu(float dt);
    void                       uploadState();
    void                       downloadState();
    void                       uploadColliders();
    void                       setPushConstants(gpgpu::ComputeShader* shader, float dt) const;
    [[nodiscard]] int          index(int x, int z) const noexcept { return z * resX_ + x; }
    [[nodiscard]] bool         inside(int x, int z) const noexcept {
        return x >= 0 && z >= 0 && x < resX_ && z < resZ_;
    }
    void worldToCell(float worldX, float worldZ, float& fx, float& fz) const noexcept;

    int         resX_      = 0;
    int         resZ_      = 0;
    float       cellSize_  = 0.25f;
    glm::vec3   origin_{0.f};
    float       gravity_   = 9.81f;
    float       viscosity_ = 0.08f;
    float       friction_  = 0.12f;
    float       minDepth_  = 1e-4f;
    float       maxDepth_  = 8.f;
    float       cfl_       = 0.45f;
    int         maxSubsteps_ = 8;
    bool        preferGpu_ = true;
    bool        gpuOk_     = false;
    bool        backendFallback_ = false;

    std::vector<float>                 heights_;
    std::vector<float>                 momentumX_;
    std::vector<float>                 momentumZ_;
    std::vector<float>                 beds_;
    std::vector<float>                 srcH_;
    std::vector<float>                 srcHu_;
    std::vector<float>                 srcHv_;
    std::vector<float>                 scratchH_;
    std::vector<float>                 scratchHu_;
    std::vector<float>                 scratchHv_;
    std::vector<ShallowWaterCollider>  colliders_;

    eve::gpgpu::Gpgpu*         gpgpu_      = nullptr;
    eve::gpgpu::ComputeShader* shClear_    = nullptr;
    eve::gpgpu::ComputeShader* shRaster_   = nullptr;
    eve::gpgpu::ComputeShader* shIntegrate_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufStateA_  = nullptr;
    eve::gpgpu::GpuBuffer*     bufStateB_  = nullptr;
    eve::gpgpu::GpuBuffer*     bufBed_     = nullptr;
    eve::gpgpu::GpuBuffer*     bufSources_ = nullptr;
    eve::gpgpu::GpuBuffer*     bufColliders_ = nullptr;
    eve::gpgpu::GpuBuffer*     stageState_ = nullptr;
    eve::gpgpu::Sequence*      seq_        = nullptr;
    bool                       stateOnGpu_ = false;
    bool                       pingA_      = true;
};

}  // namespace eve::fluids
