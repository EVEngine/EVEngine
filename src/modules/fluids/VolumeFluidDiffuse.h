#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <string>
#include <vector>
#include "common/Result.h"
#include "fluids/VolumeFluid.h"

namespace eve::fluids {

/** @brief Owned secondary particle; it contributes no mass or pressure to the liquid solver. */
struct VolumeFluidDiffuseParticle {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    /** @brief Remaining lifetime in (0,86400] seconds. */
    float life = 2.f;
};

/** @brief Strict version-1 diffuse pool state; unknown fields and versions are rejected. */
struct VolumeFluidDiffuseSnapshot {
    std::string                             schema   = "eve.volume-fluid-diffuse";
    unsigned                                version  = 1;
    unsigned                                capacity = 2048;
    std::vector<VolumeFluidDiffuseParticle> particles;
};

/** @brief Bounded, one-way fluid-advected secondary particle pool.
 * @ownership Owns particles and capacity; no fluid/world pointers are retained.
 * @thread Simulation-thread only, including snapshots; no concurrent calls.
 * @reentrancy Invokes no callbacks. Advance borrows its fluid only for that call.
 * @details Fixed input/time ordering repeats on one build, with float tolerance
 * across platforms. No RNG or automatic emission is used by this pool.
 */
class VolumeFluidDiffuse final {
public:
    /** @brief Creates an empty pool with capacity 2048; use restore to configure a different capacity. */
    VolumeFluidDiffuse() = default;
    /** @brief Admits an entire batch or fails without mutation; finite speed must be <=100 m/s. */
    [[nodiscard]] Result<void> emit(std::span<const VolumeFluidDiffuseParticle> particles);
    /** @brief Advances once using current liquid/gas velocity, after the caller steps the fluid.
     * @param fluid Borrowed field source; it is not advanced or retained.
     * @param seconds Injected duration in (0,1/30].
     * @param minimumNeighbors Remove particles below this count, in [0,1000000].
     * @details Lifetime expires before sampling. Surviving particles use explicit
     * Euler advection and sampled velocity. No pressure, collision or rigid feedback
     * is applied. Field-query budget/invalid output failure preserves the whole pool.
     * Survivor and query-position scratch retain their peak capacity, so stable-size
     * frames reuse all three source-sized working arrays.
     */
    [[nodiscard]] Result<void> advance(const VolumeFluid& fluid, float seconds, unsigned minimumNeighbors = 4);
    /** @brief Returns an owning snapshot; later mutation cannot change its values. */
    [[nodiscard]] VolumeFluidDiffuseSnapshot snapshot() const;
    /** @brief Validates schema, capacity [1,65536] and all particles before replacing state. */
    [[nodiscard]] Result<void> restore(const VolumeFluidDiffuseSnapshot& snapshot);
    /** @brief Returns live count without copying particle data. */
    [[nodiscard]] unsigned particleCount() const;
    /** @brief Returns remaining admission capacity without allocating. */
    [[nodiscard]] unsigned availableCapacity() const;
    /** @brief Reuses caller storage to copy XY Z position and remaining life for rendering.
     * @details Simulation/render handoff only; returns owning values and retains no
     * caller reference. The caller must synchronize against concurrent mutation.
     */
    void copyRenderData(std::vector<glm::vec4>& positionLife) const;
    /** @brief Removes all particles, preserving configured capacity. */
    void clear();

private:
    unsigned                                capacity_ = 2048;
    std::vector<VolumeFluidDiffuseParticle> particles_;
    std::vector<VolumeFluidDiffuseParticle> candidateScratch_;
    std::vector<glm::vec3>                  positionScratch_;
    std::vector<VolumeFluidFieldSample>     sampleScratch_;
};
}  // namespace eve::fluids
