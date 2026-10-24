#pragma once
#include <array>
#include <memory>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::gpgpu {
class Gpgpu;
}
namespace eve::graphics {
class Graphics;
}
namespace eve::physics {
/** @brief Vulkan-resident multi-sheet 3D XPBD cloth with explicit backend errors.
 * @ownership Owns buffers, compute shader and sequence. Graphics owns the draw mesh.
 * @lifetime Borrowed Gpgpu and Graphics/device must outlive this object, including destruction.
 * Destroy only after the last draw frame is presented, never inside an open pass.
 * @thread Render/submission thread only. Call update before render-pass recording.
 * @reentrancy No callbacks; update/draw/diagnostics must not overlap.
 * Grid topology only; tolerance-bounded swept point-face/edge-edge contacts, no rigid-body coupling.
 * GPU floating-point/contact-list ordering is tolerance-bounded, not bit-exact.
 */
class EVENGINE_API_DOMAINS LayeredClothGPU {
public:
    /** @brief Allocate grids, bake adjacency and compile compute; throws on invalid input or unavailable backend.
     * @param compute Borrowed active Vulkan compute provider, call-only reference becomes lifetime dependency.
     * @param graphics Borrowed same-device rendering owner, must remain alive until this object is destroyed.
     * @cost One-time shader compilation, topology upload and GPU allocation, amortized across all steps.
     */
    LayeredClothGPU(gpgpu::Gpgpu& compute, graphics::Graphics& graphics, int layers, int cols, int rows, float spacing,
                    float gap, float x, float y, float z);
    /** @brief Wait for resident draw retirement and release compute resources; never throws. */
    ~LayeredClothGPU();
    LayeredClothGPU(const LayeredClothGPU&)            = delete;
    LayeredClothGPU& operator=(const LayeredClothGPU&) = delete;
    /** @brief Execute finite dt in [1e-6,0.05] on GPU; zero is a no-op. Input failure leaves state unchanged.
     * @return Explicit failure; a device failure requires reset/recreation before continuing.
     * @cost One synchronous submission per call, with substeps times iterations dispatches and no readback.
     */
    [[nodiscard]] Result<void> update(float dt);
    /** @brief Record direct resident-buffer drawing inside an open 3D pass; no host vertex transfer. */
    [[nodiscard]] Result<void> draw();
    /** @brief Restore initial pinned pose and material/particle overrides; synchronously uploads once.
     * @cost Linear topology/state upload; intended for user reset, not per-frame use.
     */
    void reset();
    /** @brief Set substeps [1,16], iterations [1,64]; throws before mutation on invalid budget. */
    void setSolver(int substeps, int iterations);
    /** @brief Set finite world gravity; throws on invalid input. */
    void setGravity(float x, float y, float z);
    /** @brief Set finite uniform wind acceleration; not triangle aerodynamics. */
    void setWind(float x, float y, float z);
    /** @brief Enable inter-layer point-face and edge-edge contact, independently of self contact. */
    void setInterCollision(bool enabled);
    /** @brief Enable nonlocal point-face and edge-edge contact within each sheet. */
    void setSelfCollision(bool enabled);
    /** @brief Set separation >0, friction [0,1]; throws on nonfinite/out-of-range input. */
    void setContact(float thickness, float friction);
    /** @brief Set finite floor height, default -3 metres. */
    void setFloor(float y);
    /** @brief Change one particle's inverse mass, effective on the next update; zero pins.
     * @cost One tiny upload plus synchronization; use for authoring/interaction, not a particle loop per frame.
     */
    void setParticleInverseMass(int index, float inverseMass);
    /** @brief Return total simulation particles (render stream duplicates each for two sides). */
    [[nodiscard]] int getParticleCount() const;
    /** @brief Explicit diagnostic readback, XYZW per particle; W is inverse mass.
     * @cost Full state download plus GPU wait. Never called by update or draw.
     */
    [[nodiscard]] std::vector<float> readPositions();
    /** @brief Explicit readback: hash references attempted, last overflow flag, contact projections, overflow events.
     * @cost One tiny download plus GPU wait; inspect only on demand.
     */
    [[nodiscard]] std::array<unsigned, 4> readDiagnostics();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::physics
