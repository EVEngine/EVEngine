#pragma once

#include "common/Result.h"
#include "fluids/VolumeFluid.h"

#include <cstdint>
#include <vector>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::fluids {

/** @brief One-draw particle shadow caster matching Fluid3D ShadowmapExposer behavior.
 * @ownership Borrows Graphics for its lifetime and owns one Graphics-managed dynamic mesh.
 * @thread Construction, update and destruction are render-thread affine.
 * @reentrancy Invokes no script callbacks; destruction must occur outside shadow traversal.
 */
class VolumeFluidShadowCaster final {
public:
    /** @param graphics Initialized backend that must outlive this caster. */
    explicit VolumeFluidShadowCaster(graphics::Graphics& graphics);
    ~VolumeFluidShadowCaster();
    VolumeFluidShadowCaster(const VolumeFluidShadowCaster&)            = delete;
    VolumeFluidShadowCaster& operator=(const VolumeFluidShadowCaster&) = delete;

    /** @brief Rebuild one actor's bounded octahedral shadow batch.
     * @param fluid Borrowed solver, not mutated or retained.
     * @param actorGroup Stable emitter actor identity.
     * @param radiusScale Positive particle radius multiplier.
     * @param alpha Fixed-step interpolation fraction in [0,1].
     * @param maxParticles Complete-output budget in [1,65536].
     * @return Success, or failure preserving the prior visible batch.
     * @details Reuses CPU arrays and one dynamic GPU mesh after capacity warm-up.
     * The registered CSM callback issues at most one indexed draw per cascade.
     */
    [[nodiscard]] Result<void> update(const VolumeFluid& fluid, unsigned actorGroup, float radiusScale,
                                      float alpha = 1.f, unsigned maxParticles = 4096);
    /** @brief Number of particles in the currently published shadow batch. */
    [[nodiscard]] unsigned particleCount() const noexcept { return particleCount_; }

private:
    graphics::Graphics*                      graphics_      = nullptr;
    graphics::Mesh*                          mesh_          = nullptr;
    uint64_t                                 drawerToken_   = 0;
    unsigned                                 particleCount_ = 0;
    std::vector<VolumeFluidParticleInstance> instances_;
    std::vector<float>                       positions_;
    std::vector<float>                       normals_;
    std::vector<uint32_t>                    indices_;
};

}  // namespace eve::fluids
