#pragma once

#include "physics/SimulationBackend.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::physics {

class World3D;

/**
 * @brief Interactive 3D cloth — Verlet particles + distance constraints.
 * Meter space, +Y up, grid laid out in the XZ plane with the top row pinned.
 *
 * Supports self-collision (spatial-hash proximity), a dihedral fold-angle
 * limit between adjacent triangles (avoids excessive creasing), and
 * particle-vs-rigid-body collision when attached to a World3D.
 * Script-owned, independent of the Box3D world.
 */
class Cloth3D : public ISimulationBackend {
public:
    /**
     * @param cols grid columns (>= 2), along +X
     * @param rows grid rows (>= 2), along +Z
     * @param spacing particle spacing in meters
     * @param originX top-left particle X (meters)
     * @param originY top-left particle Y (meters)
     * @param originZ top-left particle Z (meters)
     */
    Cloth3D(int cols, int rows, float spacing, float originX, float originY, float originZ);
    ~Cloth3D();

    Cloth3D(const Cloth3D &)            = delete;
    Cloth3D &operator=(const Cloth3D &) = delete;

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

    void  setGravity(float gx, float gy, float gz);
    float getGravityX() const { return gravityX_; }
    float getGravityY() const { return gravityY_; }
    float getGravityZ() const { return gravityZ_; }

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
     * @brief Particle radius in meters (default 0.1): self-collision separation,
     * draw scale and rigid-body collision thickness.
     */
    void  setParticleSize(float size);
    float getParticleSize() const { return particleSize_; }

    /**
     * @brief Enable proximity-based self-collision between non-adjacent particles
     * and triangles (default true). Triangle-level pass keeps non-adjacent faces
     * at least 2*particleSize apart, so the sheet cannot slip through itself
     * between particles.
     */
    void  setSelfCollision(bool on);
    bool  getSelfCollision() const { return selfCollision_; }

    /**
     * @brief Implicit particle mass in kg (default 0.1). Used for mass-proportional
     * momentum exchange when colliding with dynamic rigid bodies.
     */
    void  setParticleMass(float mass);
    float getParticleMass() const { return particleMass_; }

    /** @brief Strength of the dihedral fold clamp [0,1] (default 0.5). */
    void  setFoldStiffness(float k);
    float getFoldStiffness() const { return foldStiffness_; }

    /**
     * @brief Maximum fold angle between adjacent triangles in degrees
     * Range is 0..180 degrees; default is 120. Zero is fully flat and 180 is
     * folded onto itself;
     * larger values allow stronger draping without creasing.
     */
    void  setMaxFoldAngle(float degrees);
    float getMaxFoldAngle() const { return maxFoldAngle_ * 180.f / 3.14159265f; }

    /** @brief Axis-aligned box (origin + extents, meters); particles bounce inside. */
    void setBounds(float x, float y, float z, float w, float h, float d);
    void clearBounds();

    void pin(int index);
    void unpin(int index);
    void pinTopRow();
    bool isPinned(int index) const;

    /**
     * @brief Grab nearest free particle within radius (meters).
     * Returns particle index, or -1 if none.
     */
    int  grabAt(float x, float y, float z, float radius = 0.3f);
    void moveGrab(float x, float y, float z);
    void releaseGrab();
    bool isGrabbing() const { return grabIndex_ >= 0; }
    int  getGrabIndex() const { return grabIndex_; }

    /** Uniform wind / force impulse applied this frame (m/s²). */
    void applyForce(float fx, float fy, float fz);

    /**
     * @brief Pointer-field interaction like Fluid2D::interactAt (3D): positive
     * strength attracts, negative repels within radius (meters).
     */
    void interactAt(float x, float y, float z, float radius, float strength);

    /**
     * @brief Attach a World3D so free particles collide with its non-sensor
     * shapes in meter space. Passing null detaches it. Dynamic bodies receive a
     * small push.
     */
    /**
     * @brief Attach a borrowed 3D world for collision queries; null detaches it.
     * @ownership Cloth3D never owns the World3D pointer; the physics registry owns it.
     * @lifetime The association is valid only while the world remains alive; clear it before world destruction.
     * @thread Call on the owning physics thread.
     * @reentrancy Does not invoke callbacks; do not destroy the world re-entrantly.
     */
    void    setCollideWorld(World3D *world);
    /**
     * @brief Returns the attached collision world, or null when detached.
     * @return Borrowed nullable World3D pointer owned by the physics registry.
     * @ownership Cloth3D does not own the world and callers must not delete it.
     * @lifetime Valid until the world is destroyed or detached.
     * @thread Call on the owning physics thread.
     * @reentrancy The accessor invokes no callbacks and is invalid across world mutation.
     */
    World3D *getCollideWorld() const { return world_; }

    /** @brief Restore the flat grid pose (top row pinned) and clear transient state. */
    void reset();

    void  setColor(float r, float g, float b, float a = 1.f);
    float getColorR() const { return colorR_; }
    float getColorG() const { return colorG_; }
    float getColorB() const { return colorB_; }
    float getColorA() const { return colorA_; }

    /** @brief Draw the cloth as a triangle mesh (requires an open 3D frame). */
    void draw(graphics::Graphics *gfx);

    int   getCols() const { return cols_; }
    int   getRows() const { return rows_; }
    int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    float getParticleX(int index) const;
    float getParticleY(int index) const;
    float getParticleZ(int index) const;
    void  setParticlePosition(int index, float x, float y, float z);

    float getSpacing() const { return spacing_; }
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }
    float getOriginZ() const { return originZ_; }

    void destroy();

private:
    struct Particle {
        float x = 0.f, y = 0.f, z = 0.f;
        float px = 0.f, py = 0.f, pz = 0.f;
        bool  pinned = false;
    };
    struct Link {
        int   a = 0, b = 0;
        float rest = 0.f;
    };
    struct Tri {
        int v[3] = {0, 0, 0};
    };
    // Dihedral pair: shared edge (a,b), opposite vertices k (tri 1) and l (tri 2).
    struct FoldPair {
        int a = 0, b = 0, k = 0, l = 0;
    };

    void rebuildLinks();
    void    updateSubsteps(float dt, int substeps);
    void rebuildTriangles();
    void buildLinkKeys();
    bool areLinked(int a, int b) const;
    void integrate(float dt);
    void solveConstraints();
    void solveFoldConstraint();
    void solveSelfCollision();
    void solveSelfCollisionTriangles();
    void collideWorld(float dt);
    void collideBounds();
    void rebuildHash();
    bool validIndex(int index) const;
    int64_t cellKey(int cx, int cy, int cz) const;

    int   cols_ = 0;
    int   rows_ = 0;
    float spacing_ = 0.4f;
    float originX_ = 0.f, originY_ = 0.f, originZ_ = 0.f;

    float gravityX_ = 0.f;
    float gravityY_ = -9.8f;
    float gravityZ_ = 0.f;
    float stiffness_ = 0.85f;
    float damping_   = 0.01f;
    int   iterations_ = 4;
    float particleSize_ = 0.1f;
    float particleMass_ = 0.1f;
    bool  selfCollision_ = true;
    float foldStiffness_ = 0.5f;
    float maxFoldAngle_ = 120.f * 3.14159265f / 180.f;

    bool  hasBounds_ = false;
    float boundX_ = 0.f, boundY_ = 0.f, boundZ_ = 0.f;
    float boundW_ = 0.f, boundH_ = 0.f, boundD_ = 0.f;

    int   grabIndex_ = -1;
    float grabX_ = 0.f, grabY_ = 0.f, grabZ_ = 0.f;
    float forceX_ = 0.f, forceY_ = 0.f, forceZ_ = 0.f;
    float interactX_ = 0.f, interactY_ = 0.f, interactZ_ = 0.f;
    float interactRadius_ = 0.f;
    float interactStrength_ = 0.f;

    World3D *world_ = nullptr;

    float colorR_ = 0.75f, colorG_ = 0.82f, colorB_ = 0.95f, colorA_ = 1.f;

    bool destroyed_ = false;
    SimulationObservation observation_;

    std::vector<Particle> particles_;
    std::vector<Link>     links_;
    std::vector<Tri>      triangles_;
    std::vector<FoldPair> foldPairs_;
    std::unordered_set<int64_t> linkKeys_;
    std::unordered_map<int64_t, std::vector<int>> hash_;

    graphics::Mesh *mesh_ = nullptr;
    int             meshVertexCount_ = 0;
    int             meshIndexCount_ = 0;
};

}  // namespace eve::physics
