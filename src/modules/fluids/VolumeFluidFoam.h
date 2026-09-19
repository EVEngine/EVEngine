#pragma once

#include "common/Export.h"
#include <cstdint>
#include <string>
#include "common/Result.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidDiffuse.h"

namespace eve::fluids {

/** @brief Foam source thresholds in the native sampled field, plus bounded emission settings. */
struct VolumeFluidFoamSettings {
    float    rate               = 500.f;
    float    randomness         = .01f;
    float    vorticityThreshold = 10.f;
    float    densityThreshold   = 2000.f;
    float    lifetime           = 2.f;
    unsigned maxPerStep         = 256;
};
/** @brief Owning strict version-1 foam controller state; checkpoint the diffuse pool separately. */
struct VolumeFluidFoamSnapshot {
    std::string             schema  = "eve.volume-fluid-foam";
    unsigned                version = 1;
    VolumeFluidFoamSettings settings;
    /** @brief Decimal double text preserves fractional emission credit in float32 VMs. */
    std::string credit = "0";
    /** @brief Nonzero state of the isolated foam RNG stream; restore to inject a seed. */
    uint32_t randomState = 1;
};

/** @brief Generates secondary particles at high-curl, low-density fluid source positions.
 * @ownership Owns settings, credit and RNG only; neither source nor pool is retained.
 * @thread Simulation-thread affine, including snapshot/restore; no concurrent calls.
 * @reentrancy No callbacks or script invocation. No global time or RNG access.
 * @details Same checkpoint and call ordering repeats on one build; floating-point
 * field/position comparisons across platforms require tolerance. No ECS or links.
 */
class EVENGINE_API_DOMAINS VolumeFluidFoam final {
public:
    /** @brief Emits after the caller steps the source and advects existing diffuse particles.
     * @param source Borrowed fluid; all its Liquid/Gas particles are eligible source sites.
     * @param pool Borrowed destination; no pressure or rendering work is performed.
     * @param seconds Injected dt in [0,1/30].
     * @return Admitted count; failure preserves pool and controller state.
     * @details Whole credit beyond capacity/maxPerStep/eligible sites is discarded.
     * Queries at most 65536 source particles with the shared 4M candidate-visit
     * limit. Zero budget/full pool skips copying and field sampling. Successful
     * empty eligibility consumes whole credit but not RNG. Eligible sites are
     * counted and selected without allocating a separate per-site index array.
     * Source positions, field samples and output batch retain peak capacity, so
     * stable-size productive frames allocate none of these working arrays.
     * Settings use native
     * field units, not a claim of numerical Fluid3D threshold calibration.
     */
    [[nodiscard]] Result<unsigned> advance(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds);
    /** @brief Emits only from particles owned by one emitter actor group.
     * @param source Borrowed fluid containing the emitter's particles.
     * @param pool Borrowed diffuse destination.
     * @param seconds Injected dt in [0,1/30].
     * @param actorGroup Explicit native identity corresponding to Fluid3DEmitter::solverIndices.
     * @return Admitted count; failure preserves pool and controller state.
     * @details Fuses the actor comparison into the existing source collection pass,
     * so it adds no traversal, allocation, query or GPU submission. The identity is
     * transient and is not retained in snapshots.
     */
    [[nodiscard]] Result<unsigned> advanceFromActor(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds,
                                                    unsigned actorGroup);
    /** @brief Emits from one actor using fixed-step interpolated source positions.
     * @param alpha Finite render interpolation fraction in [0,1].
     * @details Matches Fluid3DFoamGenerator's solver.renderablePositions source. Field values
     * are sampled at those positions without another source traversal or GPU work.
     */
    [[nodiscard]] Result<unsigned> advanceFromActorInterpolated(const VolumeFluid& source, VolumeFluidDiffuse& pool,
                                                                float seconds, unsigned actorGroup, float alpha);
    /** @brief Copies owning settings, fractional credit and isolated RNG state. */
    [[nodiscard]] VolumeFluidFoamSnapshot snapshot() const;
    /** @brief Validates all settings/schema/credit/RNG before atomic replacement. */
    [[nodiscard]] Result<void> restore(const VolumeFluidFoamSnapshot& state);

private:
    VolumeFluidFoamSettings                 settings_;
    double                                  credit_      = 0;
    uint32_t                                randomState_ = 1;
    std::vector<glm::vec3>                  positionScratch_;
    std::vector<VolumeFluidFieldSample>     sampleScratch_;
    std::vector<VolumeFluidDiffuseParticle> batchScratch_;
    [[nodiscard]] Result<unsigned> advanceFiltered(const VolumeFluid& source, VolumeFluidDiffuse& pool, float seconds,
                                                   bool filterActor, unsigned actorGroup, float alpha);
};
}  // namespace eve::fluids
