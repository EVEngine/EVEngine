#pragma once

#include "physics/SimulationBackend.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

class World;

/**
 * @brief Interactive 2D cloth — Verlet particles + distance constraints.
 * Pixel space (same convention as Box2D World). Script-owned.
 *
 * Supports optional self-collision (spatial-hash proximity), a fold-angle
 * limit that prevents sharp creases, and particle-vs-rigid-body collision
 * when attached to a 2D World via setCollideWorld.
 */
class Cloth : public ISimulationBackend {
public:
    /**
     * @param cols grid columns (>= 2)
     * @param rows grid rows (>= 2)
     * @param spacing particle spacing in pixels
     * @param originX top-left particle X (pixels)
     * @param originY top-left particle Y (pixels)
     */
    Cloth(int cols, int rows, float spacing, float originX, float originY);
    ~Cloth();

    Cloth(const Cloth &)            = delete;
    Cloth &operator=(const Cloth &) = delete;

    void update(float dt);

    /** @brief Advances cloth with the shared ticked backend contract. */
    [[nodiscard("check the cloth step outcome")]]
    eve::Result<void> step(const eve::SimulationStep &step, const SimulationSettings &settings) override;
    /** @brief Returns completed tick/time observables. */
    [[nodiscard]] SimulationObservation observation() const noexcept override { return observation_; }
    /** @brief Identifies this production CPU cloth backend. */
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Cpu; }
    /** @brief CPU cloth uses bounded floating-point determinism. */
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
    /** @brief Restores tick/progress metadata after an owner-level restore. */
    [[nodiscard("check cloth observation restore")]]
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

    /**
     * @brief Particle radius used for self-collision, draw size and body collision
     * Default is 3. Two particles keep at least twice this size apart when
     * self-collision is on.
     */
    void  setParticleSize(float size);
    float getParticleSize() const { return particleSize_; }

    /**
     * @brief Implicit particle mass in kg (default 0.1). Used for mass-proportional
     * momentum exchange when colliding with dynamic rigid bodies.
     */
    void  setParticleMass(float mass);
    float getParticleMass() const { return particleMass_; }

    /**
     * @brief Enable proximity-based self-collision between non-adjacent particles
     * Default is true.
     */
    void  setSelfCollision(bool on);
    bool  getSelfCollision() const { return selfCollision_; }

    /** @brief Strength of the fold-angle clamp [0,1] (default 0.5). */
    void  setFoldStiffness(float k);
    float getFoldStiffness() const { return foldStiffness_; }

    /**
     * @brief Maximum bend deviation from a straight row/column segment in degrees
     * Range is 0..180 degrees; default is 90. Prevents sharp creases and
     * excessive folding.
     */
    void  setMaxFoldAngle(float degrees);
    float getMaxFoldAngle() const { return maxFoldAngle_ * 180.f / 3.14159265f; }

    /** @brief Axis-aligned walls; particles bounce inside. Disabled if w/h <= 0. */
    void setBounds(float x, float y, float w, float h);
    void clearBounds();

    void pin(int index);
    void unpin(int index);
    void pinTopRow();
    bool isPinned(int index) const;

    /**
     * @brief Grab nearest free particle within radius (pixels).
     * Returns particle index, or -1 if none.
     */
    int  grabAt(float x, float y, float radius = 24.f);
    void moveGrab(float x, float y);
    void releaseGrab();
    bool isGrabbing() const { return grabIndex_ >= 0; }
    int  getGrabIndex() const { return grabIndex_; }

    /** Uniform wind / force impulse applied this frame (pixels/s² * mass). */
    void applyForce(float fx, float fy);

    /**
     * @brief Pointer-field interaction like Fluid::interactAt: positive strength
     * attracts, negative repels. Applied as acceleration within radius (pixels)
     * during the next update.
     */
    void interactAt(float x, float y, float radius, float strength);

    /**
     * @brief Attach a 2D World so free particles collide with its non-sensor
     * fixtures in pixel space. Passing null detaches it. Dynamic bodies receive
     * a small push.
     */
    /**
     * @brief Attach a borrowed 2D world for collision queries; null detaches it.
     * @ownership Cloth never owns the World pointer; the physics registry owns it.
     * @lifetime The association is valid only while the world remains alive; clear it before world destruction.
     * @thread Call on the owning physics thread.
     * @reentrancy Does not invoke callbacks; do not destroy the world re-entrantly.
     */
    void  setCollideWorld(World *world);
    /**
     * @brief Returns the attached collision world, or null when detached.
     * @return Borrowed nullable World pointer owned by the physics registry.
     * @ownership Cloth does not own the world and callers must not delete it.
     * @lifetime Valid until the world is destroyed or detached.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    World *getCollideWorld() const { return world_; }

    /** @brief Restore the flat grid pose (top row pinned) and clear transient state. */
    void reset();

    void  setColor(float r, float g, float b, float a = 1.f);
    float getColorR() const { return colorR_; }
    float getColorG() const { return colorG_; }
    float getColorB() const { return colorB_; }
    float getColorA() const { return colorA_; }

    void draw(graphics::Graphics *gfx);

    int   getCols() const { return cols_; }
    int   getRows() const { return rows_; }
    int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    float getParticleX(int index) const;
    float getParticleY(int index) const;
    void  setParticlePosition(int index, float x, float y);

    float getSpacing() const { return spacing_; }
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }

    void destroy();

private:
    struct Particle {
        float x = 0.f, y = 0.f;
        float px = 0.f, py = 0.f;
        bool  pinned = false;
    };
    struct Link {
        int   a = 0, b = 0;
        float rest = 0.f;
    };

    void rebuildLinks();
    void    updateSubsteps(float dt, int substeps);
    void integrate(float dt);
    void solveConstraints();
    void solveFoldConstraint();
    void solveSelfCollision();
    void collideWorld(float dt);
    void collideBounds();
    void rebuildHash();
    void buildLinkKeys();
    bool areLinked(int a, int b) const;
    bool validIndex(int index) const;
    int64_t cellKey(int cx, int cy) const;

    int   cols_ = 0;
    int   rows_ = 0;
    float spacing_ = 10.f;
    float originX_ = 0.f;
    float originY_ = 0.f;

    float gravityX_ = 0.f;
    float gravityY_ = 980.f;
    float stiffness_ = 0.85f;
    float damping_   = 0.01f;
    int   iterations_ = 4;
    float particleSize_ = 3.f;
    float particleMass_ = 0.1f;
    bool  selfCollision_ = true;
    float foldStiffness_ = 0.5f;
    float maxFoldAngle_ = 90.f * 3.14159265f / 180.f;

    bool  hasBounds_ = false;
    float boundX_ = 0.f, boundY_ = 0.f, boundW_ = 0.f, boundH_ = 0.f;

    int   grabIndex_ = -1;
    float grabX_ = 0.f, grabY_ = 0.f;
    float forceX_ = 0.f, forceY_ = 0.f;
    float interactX_ = 0.f, interactY_ = 0.f;
    float interactRadius_ = 0.f;
    float interactStrength_ = 0.f;

    World *world_ = nullptr;

    float colorR_ = 0.75f, colorG_ = 0.82f, colorB_ = 0.95f, colorA_ = 1.f;

    bool destroyed_ = false;
    SimulationObservation observation_;

    std::vector<Particle> particles_;
    std::vector<Link>     links_;
    std::unordered_set<int64_t> linkKeys_;
    std::unordered_map<int64_t, std::vector<int>> hash_;
};

}  // namespace eve::physics
