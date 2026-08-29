#pragma once

#include "physics/SimulationBackend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::gpgpu {
class Gpgpu;
class ComputeShader;
class GpuBuffer;
class Sequence;
}  // namespace eve::gpgpu

namespace eve::physics {

/**
 * @brief GPU-accelerated 2D cloth — Verlet integration and distance-constraint
 * relaxation run in a Vulkan compute shader; positions are read back each frame
 * for drawing. Same pixel-space conventions and script API shape as Cloth.
 *
 * One thread owns one particle and solves its fixed-size link list, so no
 * atomics or ping-pong buffers are needed. Requires the Gpgpu module and a
 * compute-capable Graphics backend; construction throws when unavailable.
 */
class ClothGPU : public ISimulationBackend {
public:
    static constexpr int kMaxLinksPerParticle = 16;

    /**
     * @param gpgpu active Gpgpu module (compute shader compiler + buffers)
     * @param cols grid columns (>= 2)
     * @param rows grid rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left particle X (pixels)
     * @param originY top-left particle Y (pixels)
     */
    ClothGPU(eve::gpgpu::Gpgpu *gpgpu, int cols, int rows, float spacing, float originX,
             float originY);
    ~ClothGPU();

    ClothGPU(const ClothGPU &)            = delete;
    ClothGPU &operator=(const ClothGPU &) = delete;

    void update(float dt);

    /** @brief Advances the production GPU cloth with the shared ticked contract. */
    [[nodiscard("check the GPU cloth step outcome")]]
    eve::Result<void> step(const eve::SimulationStep &step, const SimulationSettings &settings) override;
    /** @brief Returns completed tick/time observables. */
    [[nodiscard]] SimulationObservation observation() const noexcept override { return observation_; }
    /** @brief Identifies this real accelerator backend. */
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Gpu; }
    /** @brief GPU/CPU parity is numerically bounded, not bit exact. */
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
    /** @brief Restores tick/progress metadata after an owner-level restore. */
    [[nodiscard("check GPU cloth observation restore")]]
    eve::Result<void> restoreObservation(const SimulationObservation &observation) override;

    void  setGravity(float gx, float gy);
    float getGravityX() const { return gravityX_; }
    float getGravityY() const { return gravityY_; }

    /** @brief Constraint relaxation strength in [0,1] (default 0.85). */
    void  setStiffness(float stiffness);
    float getStiffness() const { return stiffness_; }

    /** @brief Constraint solver iterations per substep (default 4). */
    void setIterations(int iterations);
    int  getIterations() const { return iterations_; }

    /** @brief Damping applied to Verlet velocity [0,1] (default 0.01). */
    void  setDamping(float damping);
    float getDamping() const { return damping_; }

    /** @brief Particle draw size in pixels (default 3). */
    void  setParticleSize(float size);
    float getParticleSize() const { return particleSize_; }

    /**
     * @brief Enable proximity-based self-collision between non-adjacent particles
     * Default is false. Particles keep at least twice particleSize apart. When bounds
     * are set, uses a GPU spatial hash (atomicExchange linked lists per cell,
     * overflow-free) with 3x3 neighbor traversal — scales to tens of thousands
     * of particles. Without bounds it falls back to an O(n²) scan.
     */
    void  setSelfCollision(bool on);
    bool  getSelfCollision() const { return selfCollision_; }

    /** @brief Axis-aligned walls; particles are clamped (with a small bounce). */
    void setBounds(float x, float y, float w, float h);
    void clearBounds();

    void pin(int index);
    void unpin(int index);
    void pinTopRow();
    bool isPinned(int index) const;

    /** Uniform wind / force impulse applied this frame (pixels/s²). */
    void applyForce(float fx, float fy);

    /**
     * @brief Pointer-field interaction like Fluid2D::interactAt: positive strength
     * attracts, negative repels within radius (pixels) during the next update.
     */
    void interactAt(float x, float y, float radius, float strength);

    void  setColor(float r, float g, float b, float a = 1.f);
    float getColorR() const { return colorR_; }
    float getColorG() const { return colorG_; }
    float getColorB() const { return colorB_; }
    float getColorA() const { return colorA_; }

    /** @brief Draw links + particles from the latest GPU readback. */
    void draw(graphics::Graphics *gfx);

    int   getCols() const { return cols_; }
    int   getRows() const { return rows_; }
    int   getParticleCount() const { return cols_ * rows_; }
    float getParticleX(int index) const;
    float getParticleY(int index) const;

    float getSpacing() const { return spacing_; }
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }

    /** @brief Restore the flat grid pose (top row pinned) and re-upload state. */
    void reset();

    void destroy();

private:
    struct Link {
        int   other = -1;
        float rest = 0.f;
    };

    void rebuildLinks();
    void uploadInitialState();
    void uploadPinned();
    void ensureHashBuffers();
    eve::Result<void> stepGpu(float dt, int substeps);

    eve::gpgpu::Gpgpu *gpgpu_ = nullptr;
    int   cols_ = 0;
    int   rows_ = 0;
    float spacing_ = 10.f;
    float originX_ = 0.f;
    float originY_ = 0.f;

    float gravityX_ = 0.f;
    float gravityY_ = 980.f;
    float stiffness_ = 0.85f;
    int   iterations_ = 4;
    float damping_ = 0.01f;
    float particleSize_ = 3.f;

    bool  hasBounds_ = false;
    float boundX_ = 0.f, boundY_ = 0.f, boundW_ = 0.f, boundH_ = 0.f;

    float forceX_ = 0.f, forceY_ = 0.f;
    float interactX_ = 0.f, interactY_ = 0.f;
    float interactRadius_ = 0.f;
    float interactStrength_ = 0.f;

    float colorR_ = 0.75f, colorG_ = 0.82f, colorB_ = 0.95f, colorA_ = 1.f;

    bool destroyed_ = false;
    SimulationObservation observation_;

    std::vector<Link>   links_;      // particle-major, kMaxLinksPerParticle slots
    std::vector<float>  posCpu_;     // x,y,px,py per particle (readback)
    std::vector<float>  flagsCpu_;   // 1 = pinned
    std::vector<float>  linkCpu_;    // other,rest,0,0 per slot

    eve::gpgpu::ComputeShader *shader_ = nullptr;
    eve::gpgpu::Sequence *seq_ = nullptr;
    eve::gpgpu::GpuBuffer *posBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *posBufB_ = nullptr;
    eve::gpgpu::GpuBuffer *linkBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *flagBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *staging_ = nullptr;
    eve::gpgpu::GpuBuffer *cellHeadBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *cellNextBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *cellItemsBuf_ = nullptr;
    eve::gpgpu::GpuBuffer *cellSlotBuf_ = nullptr;
    int hashNx_ = 0;
    int hashNy_ = 0;
    bool selfCollision_ = false;
};

}  // namespace eve::physics
