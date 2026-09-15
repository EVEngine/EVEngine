#pragma once
#include <memory>
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainSedimentSettings;
struct TerrainThermalSettings;

/** @brief Stored scalar channels; Pcg erosion masks read FluxRight or VelocityX, not vector magnitude. */
enum class TerrainWaterChannel { Depth, VelocityX, VelocityZ, FluxRight, FluxLeft, FluxBottom, FluxTop };

/** @brief Explicit constants for the source hydraulic water-flow equations, after UI unit conversion. */
struct TerrainWaterSettings {
    float spacingX = 1, spacingZ = 1, heightScale = 1, waterScale = 1;
    float dt = 0.05F, precipitation = 0.000004F, evaporation = 0.000004F;
    float flowAcceleration = -0.00049F;  ///< Signed flowRate*gravity; source default is negative.
};

/** @brief Value snapshot of one water cell; direction order follows Hydraulic.compute (Z+ is bottom). */
struct TerrainWaterSample {
    float depth = 0, velocityX = 0, velocityZ = 0;
    float fluxRight = 0, fluxLeft = 0, fluxBottom = 0, fluxTop = 0;
};

/** @brief Controls Pcg's legacy offline WaterFlowMap droplet tracer. */
struct TerrainWaterFlowMapSettings {
    float dropletVolume = 0.3F;
    float absorptionRate = 0.05F;
    int smoothIterations = 1;
};

/**
 * @brief Generate Pcg's legacy transposed water-flow accumulation map.
 * @param target Exclusively borrowed output sized source-height by source-width; aliases are rejected.
 * @param source Borrowed finite terrain heights, copied because pooling raises a private working terrain.
 * @param settings Finite nonnegative volume, positive absorption and nonnegative smoothing iterations.
 * @return Changed output sample count or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; no callbacks, RNG, retained references, or implicit time.
 * Each interior source sample traces one droplet. Strict-lower neighbor selection uses X-major then Z-major
 * order, a pooled step raises only the private terrain, the last step deposits the full absorption amount,
 * and smoothing is the source's order-dependent in-place clamped four-neighbor pass.
 */
[[nodiscard]] Result<int> generateTerrainWaterFlowMap(Heightmap& target, const Heightmap& source,
                                                       const TerrainWaterFlowMapSettings& settings);

/**
 * @brief Generate Pcg HeightMap.FlowMap's normalized four-direction velocity magnitude.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite terrain heights, copied before computation and never retained.
 * @param iterations Nonnegative count of fixed TIME=0.2 flow steps.
 * @return Changed output samples or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; deterministic with no RNG, callbacks, or implicit time.
 */
[[nodiscard]] Result<int> generateTerrainVelocityFlowMap(Heightmap& target, const Heightmap& source, int iterations);

/**
 * @brief Owned water/flux/velocity state for deterministic execution of the Pcg water stage.
 * No terrain pointer is retained. Each advance reads an immutable terrain and previous
 * field, computes all fluxes, then computes velocities and depth, publishing once.
 * This deliberately retains the source's final rain/evaporation overwrite of transported
 * depth and its velocity signs. It is not a conservative shallow-water solver. Undefined
 * source behavior is resolved explicitly: current flux is fully computed before neighbor
 * reads, and zero average depth produces zero velocity instead of division by zero.
 * Thread affinity is caller-owned: exclusive reset/advance, immutable sampling when idle;
 * no callbacks, borrowed output pointers, ECS handles, implicit clock or RNG.
 */
class TerrainWaterField {
public:
    /** @brief Construct an empty field; reset must succeed before advance or sample. */
    TerrainWaterField();
    /** @brief Destroy all owned simulation buffers; no external registrations are retained. */
    ~TerrainWaterField();
    /** @brief Transfer owned buffers; a moved-from field is empty and can be reset. */
    TerrainWaterField(TerrainWaterField&&) noexcept;
    /** @brief Transfer owned buffers, releasing this field's previous buffers. */
    TerrainWaterField& operator=(TerrainWaterField&&) noexcept;
    /** @brief State has one mutable owner; implicit copying is prohibited. */
    TerrainWaterField(const TerrainWaterField&) = delete;
    /** @brief State has one mutable owner; implicit copy assignment is prohibited. */
    TerrainWaterField& operator=(const TerrainWaterField&) = delete;

    /**
     * @brief Reset dimensions, uniform depth and zero flux/velocity atomically.
     * @param width Positive X sample count.
     * @param height Positive Z sample count; product must fit int.
     * @param depth Finite nonnegative initial stored water depth.
     * @return Initialized cell count or InvalidArgument; failure leaves old state intact.
     * @throws std::bad_alloc Old state remains intact.
     */
    [[nodiscard]] Result<int> reset(int width, int height, float depth = 0);
    /**
     * @brief Advance one injected time step, with clamped neighbor indices.
     * @param terrain Borrowed matching finite height raster; never modified or retained.
     * @param settings Finite positive spacing/scales, nonnegative dt/precipitation/evaporation;
     * flowAcceleration is signed. dt=0 is identity. No hidden UI scaling is applied.
     * @return Number of cells with any changed output, or InvalidArgument. All state is
     * unchanged on invalid input, overflow or allocation failure.
     * @throws std::bad_alloc Old state remains intact.
     */
    [[nodiscard]] Result<int> advance(const Heightmap& terrain, const TerrainWaterSettings& settings);
    /**
     * @brief Advance water, source sediment and thermal stages as one transaction.
     * @param heights Exclusively borrowed matching nonnegative scalar heights, never retained.
     * @param sediment Exclusively borrowed matching signed sediment, distinct from heights.
     * @param water Water step constants; dt and spacing also drive sediment transport.
     * @param reaction Effective source sediment coefficients.
     * @param thermal Thermal constants with already-computed substep dt; zero thermal iterations disables it.
     * @param iterations Nonnegative number of complete iterations. Zero validates raster ownership/shape and leaves
     * state unchanged.
     * @return Cells changed in water, height or sediment, or InvalidArgument. All three owners
     * remain unchanged on any stage failure, including after earlier successful iterations.
     * @throws std::bad_alloc All caller-observable state remains unchanged.
     * Each iteration uses the latest completed height state and publishes the last iteration,
     * rather than the source wrapper's fixed buffer-1 output and reset thermal buffer index.
     * Caller must hold exclusive access to this field and both rasters; no callbacks or retained borrows.
     */
    [[nodiscard]] Result<int> advanceHydraulic(Heightmap& heights, Heightmap& sediment,
                                               const TerrainWaterSettings&    water,
                                               const TerrainSedimentSettings& reaction,
                                               const TerrainThermalSettings& thermal, int iterations);
    /** @brief Read an owned value snapshot; invalid coordinates or empty state return InvalidArgument. */
    [[nodiscard]] Result<TerrainWaterSample> sample(int x, int z) const;
    /**
     * @brief Copy one stored channel into an existing matching raster for mask processing.
     * @param target Exclusively borrowed finite matching raster; no resize or retained reference.
     * @param channel Explicit scalar channel, without normalization, absolute value or clamping.
     * @return Changed sample count or InvalidArgument; failure leaves target and field unchanged.
     * @throws std::bad_alloc Target remains unchanged.
     * @thread Synchronous immutable field access and exclusive target access; no callbacks.
     */
    [[nodiscard]] Result<int> exportChannel(Heightmap& target, TerrainWaterChannel channel) const;
    /** @brief Return X sample count, or zero for an empty/moved-from field. */
    int getWidth() const noexcept;
    /** @brief Return Z sample count, or zero for an empty/moved-from field. */
    int getHeight() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen
