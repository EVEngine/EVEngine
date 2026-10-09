#pragma once

#include "physics/backend/SimulationBackend.h"

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
class EVENGINE_API_DOMAINS ClothGPU : public ISimulationBackend {
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
    /** @brief Cloth gpu. */
    ClothGPU(eve::gpgpu::Gpgpu *gpgpu, int cols, int rows, float spacing, float originX,
             float originY);
    /** @brief Cloth gpu. */
    ~ClothGPU();

    ClothGPU(const ClothGPU &)            = delete;
    ClothGPU &operator=(const ClothGPU &) = delete;

    /** @brief Updates . */
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
    /** @brief Return stable backend name for scripts and diagnostics. */
    [[nodiscard]] std::string getBackendName() const { return "gpu"; }
    /** @brief Query a stable cloth feature name; unsupported features never silently fall back. */
    [[nodiscard]] bool supportsFeature(const std::string &feature) const;
    /** @brief Restores tick/progress metadata after an owner-level restore. */
    [[nodiscard("check GPU cloth observation restore")]]
    eve::Result<void> restoreObservation(const SimulationObservation &observation) override;

    /** @brief Sets the gravity. */
    void  setGravity(float gx, float gy);
    /** @brief Returns the gravity x. */
    float getGravityX() const { return gravityX_; }
    /** @brief Returns the gravity y. */
    float getGravityY() const { return gravityY_; }

    /** @brief Constraint relaxation strength in [0,1] (default 0.85). */
    void  setStiffness(float stiffness);
    /** @brief Returns the stiffness. */
    float getStiffness() const { return stiffness_; }

    /** @brief Constraint solver iterations per substep (default 4). */
    void setIterations(int iterations);
    /** @brief Returns the iterations. */
    int  getIterations() const { return iterations_; }

    /** @brief Damping applied to Verlet velocity [0,1] (default 0.01). */
    void  setDamping(float damping);
    /** @brief Returns the damping. */
    float getDamping() const { return damping_; }

    /** @brief Particle draw size in pixels (default 3). */
    void  setParticleSize(float size);
    /** @brief Returns the particle size. */
    float getParticleSize() const { return particleSize_; }

    /**
     * @brief Enable proximity-based self-collision between non-adjacent particles
     * Default is false. Particles keep at least twice particleSize apart. When bounds
     * are set, uses a GPU spatial hash (atomicExchange linked lists per cell,
     * overflow-free) with 3x3 neighbor traversal — scales to tens of thousands
     * of particles. Without bounds it falls back to an O(n²) scan.
     */
    void  setSelfCollision(bool on);
    /** @brief Returns the self collision. */
    bool  getSelfCollision() const { return selfCollision_; }

    /** @brief Axis-aligned walls; particles are clamped (with a small bounce). */
    void setBounds(float x, float y, float w, float h);
    /** @brief Clears bounds. */
    void clearBounds();

    /** @brief Pin. */
    void pin(int index);
    /** @brief Unpin. */
    void unpin(int index);
    /** @brief Pin top row. */
    void pinTopRow();
    /** @brief True when pinned. */
    bool isPinned(int index) const;

    /** Uniform wind / force impulse applied this frame (pixels/s²). */
    /** @brief Applies force. */
    void applyForce(float fx, float fy);

    /**
     * @brief Pointer-field interaction like Fluid2D::interactAt: positive strength
     * attracts, negative repels within radius (pixels) during the next update.
     */
    void interactAt(float x, float y, float radius, float strength);

    /** @brief Sets the color. */
    void  setColor(float r, float g, float b, float a = 1.f);
    /** @brief Returns the color r. */
    float getColorR() const { return colorR_; }
    /** @brief Returns the color g. */
    float getColorG() const { return colorG_; }
    /** @brief Returns the color b. */
    float getColorB() const { return colorB_; }
    /** @brief Returns the color a. */
    float getColorA() const { return colorA_; }

    /** @brief Draw links + particles from the latest GPU readback. */
    void draw(graphics::Graphics *gfx);

    /** @brief Returns the cols. */
    int   getCols() const { return cols_; }
    /** @brief Returns the rows. */
    int   getRows() const { return rows_; }
    /** @brief Returns the particle count. */
    int   getParticleCount() const { return cols_ * rows_; }
    /** @brief Returns the particle x. */
    float getParticleX(int index) const;
    /** @brief Returns the particle y. */
    float getParticleY(int index) const;

    /** @brief Returns the spacing. */
    float getSpacing() const { return spacing_; }
    /** @brief Returns the origin x. */
    float getOriginX() const { return originX_; }
    /** @brief Returns the origin y. */
    float getOriginY() const { return originY_; }

    /** @brief Restore the flat grid pose (top row pinned) and re-upload state. */
    void reset();

    /** @brief Destroys . */
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
