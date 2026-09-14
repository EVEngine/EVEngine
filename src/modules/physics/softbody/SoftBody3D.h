#pragma once

#include "physics/backend/SimulationBackend.h"
#include "physics/softbody/SoftBody3DDefinition.h"
#include "physics/softbody/SoftBodyCollision.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::physics {

class World3D;

/**
 * @brief Volumetric particle soft body using overlapping shape-matching clusters.
 *
 * Particles form a regular 3D lattice in meter space (+Y up). Every lattice
 * cell is an overlapping eight-particle cluster, which preserves local volume
 * and orientation while allowing elastic and optional plastic deformation.
 * The object is simulation-thread affine and owns all particle/constraint
 * state. A World3D supplied through setCollideWorld() is borrowed and must
 * outlive this object or be cleared before world destruction.
 */
class SoftBody3D final : public ISimulationBackend {
public:
    /**
     * @brief Creates a soft body from the canonical versioned definition.
     * @param definition Validated owning creation data copied into runtime state.
     * @return An owning object, or a structured validation diagnostic.
     */
    [[nodiscard("check soft-body creation")]]
    static eve::Result<std::unique_ptr<SoftBody3D>> create(const SoftBody3DDefinition& definition);

    /**
     * @brief Creates a validated volumetric soft body.
     * @param cols Particle columns along +X, at least 2.
     * @param rows Particle rows along +Y, at least 2.
     * @param layers Particle layers along +Z, at least 2.
     * @param spacing Rest spacing in meters, greater than zero.
     * @param originX Minimum rest X coordinate.
     * @param originY Minimum rest Y coordinate.
     * @param originZ Minimum rest Z coordinate.
     * @return An owning object or InvalidArgument diagnostic; no state is published on failure.
     * @thread Call and use the result on the owning simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard("check soft-body creation")]]
    static eve::Result<std::unique_ptr<SoftBody3D>> create(int cols, int rows, int layers, float spacing, float originX,
                                                           float originY, float originZ);

    ~SoftBody3D() override;
    SoftBody3D(const SoftBody3D&)            = delete;
    SoftBody3D& operator=(const SoftBody3D&) = delete;

    /** @brief Advances using a legacy variable timestep clamped to 50 ms. */
    void update(float dt);
    /** @brief Advances through the checked fixed-step backend contract. */
    [[nodiscard("check the soft-body step outcome")]]
    eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override;
    /** @brief Returns completed fixed-step metadata by value. */
    [[nodiscard]] SimulationObservation observation() const noexcept override { return observation_; }
    /** @brief Identifies the built-in CPU implementation. */
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Cpu; }
    /** @brief Results are repeatable within documented floating-point tolerance. */
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
    /** @brief Restores checked progress metadata; particle state is unchanged. */
    [[nodiscard("check soft-body observation restore")]]
    eve::Result<void> restoreObservation(const SimulationObservation& observation) override;

    void                setGravity(float x, float y, float z);
    [[nodiscard]] float getGravityX() const { return gravityX_; }
    [[nodiscard]] float getGravityY() const { return gravityY_; }
    [[nodiscard]] float getGravityZ() const { return gravityZ_; }
    void                setDeformationResistance(float value);
    [[nodiscard]] float getDeformationResistance() const;
    void                setIterations(int value);
    [[nodiscard]] int   getIterations() const { return iterations_; }
    void                setDamping(float value);
    [[nodiscard]] float getDamping() const { return damping_; }
    void                setParticleRadius(float value);
    [[nodiscard]] float getParticleRadius() const;
    void                setParticleMass(float value);
    [[nodiscard]] float getParticleMass() const { return particleMass_; }
    void                setPlasticity(float yield, float creep, float recovery, float maxDeformation);
    [[nodiscard]] float getPlasticYield() const;
    [[nodiscard]] float getPlasticCreep() const;
    [[nodiscard]] float getPlasticRecovery() const;
    [[nodiscard]] float getMaxDeformation() const;
    void                setSelfCollision(bool enabled) { selfCollision_ = enabled; }
    [[nodiscard]] bool  getSelfCollision() const { return selfCollision_; }
    void                setBounds(float x, float y, float z, float width, float height, float depth);
    void                clearBounds() { hasBounds_ = false; }

    void               pin(int index);
    void               unpin(int index);
    [[nodiscard]] bool isPinned(int index) const;
    [[nodiscard]] int  grabAt(float x, float y, float z, float radius);
    void               moveGrab(float x, float y, float z);
    void               releaseGrab() { grabIndex_ = -1; }
    [[nodiscard]] bool isGrabbing() const { return grabIndex_ >= 0; }
    [[nodiscard]] int  getGrabIndex() const { return grabIndex_; }
    void               applyForce(float x, float y, float z);
    void               interactAt(float x, float y, float z, float radius, float strength);

    /**
     * @brief Borrows a rigid-body world used for particle contacts.
     * @param world Nullable observer; pass null to disable world collision.
     * @ownership The soft body never owns the world.
     * @lifetime The world must outlive this object or be cleared before destruction.
     */
    void                   setCollideWorld(World3D* world);
    [[nodiscard]] World3D* getCollideWorld() const;

    /**
     * @brief Set a backend-neutral borrowed collision provider.
     * @lifetime The provider must outlive this object or be cleared before destruction.
     */
    void setCollisionWorld(softbody::ISoftBodyCollisionWorld* world) {
        ownedCollisionWorld_.reset();
        world_          = nullptr;
        collisionWorld_ = world;
    }

    void reset();
    void destroy();
    /** @brief Whether destroy() has disabled this runtime instance. */
    [[nodiscard]] bool isDestroyed() const noexcept { return destroyed_; }

    [[nodiscard]] int   getCols() const { return cols_; }
    [[nodiscard]] int   getRows() const { return rows_; }
    [[nodiscard]] int   getLayers() const;
    [[nodiscard]] int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    [[nodiscard]] float getParticleX(int index) const;
    [[nodiscard]] float getParticleY(int index) const;
    [[nodiscard]] float getParticleZ(int index) const;
    void                setParticlePosition(int index, float x, float y, float z);
    /** @brief Returns current cell volume divided by undeformed rest volume. */
    [[nodiscard]] float getVolumeRatio() const;

private:
    struct Particle {
        float x, y, z;
        float px, py, pz;
        float rx, ry, rz;
        bool  pinned = false;
    };
    struct Cluster {
        int   indices[8]{};
        float plastic[8][3]{};
    };

    SoftBody3D(int cols, int rows, int layers, float spacing, float originX, float originY, float originZ);
    [[nodiscard]] bool validIndex(int index) const noexcept;
    [[nodiscard]] int  indexOf(int x, int y, int z) const noexcept;
    void               updateSubsteps(float dt, int substeps);
    void               integrate(float dt);
    void               solveShapeMatching(float dt);
    void               solveSelfCollision();
    void               collideWorld(float dt);
    void               collideBounds();

    int                   cols_ = 0, rows_ = 0, layers_ = 0;
    float                 spacing_ = 1.f;
    std::vector<Particle> particles_;
    std::vector<Cluster>  clusters_;
    float                 gravityX_ = 0.f, gravityY_ = -9.8f, gravityZ_ = 0.f;
    float                 forceX_ = 0.f, forceY_ = 0.f, forceZ_ = 0.f;
    float                 damping_               = 0.04f;
    float                 deformationResistance_ = 0.8f;
    float                 particleRadius_        = 0.2f;
    float                 particleMass_          = 0.1f;
    int                   iterations_            = 5;
    bool                  selfCollision_         = false;
    float                 plasticYield_ = 0.f, plasticCreep_ = 0.f, plasticRecovery_ = 0.f, maxDeformation_ = 0.f;
    bool                  hasBounds_ = false;
    float                 boundX_ = 0.f, boundY_ = 0.f, boundZ_ = 0.f;
    float                 boundW_ = 0.f, boundH_ = 0.f, boundD_ = 0.f;
    int                   grabIndex_ = -1;
    float                 grabX_ = 0.f, grabY_ = 0.f, grabZ_ = 0.f;
    bool                  hasInteraction_ = false;
    float                 interactX_ = 0.f, interactY_ = 0.f, interactZ_ = 0.f;
    float                 interactRadius_ = 0.f, interactStrength_ = 0.f;
    World3D*                                      world_          = nullptr;
    softbody::ISoftBodyCollisionWorld*            collisionWorld_ = nullptr;
    std::unique_ptr<softbody::ISoftBodyCollisionWorld> ownedCollisionWorld_;
    bool                  destroyed_ = false;
    SimulationObservation observation_{};
};

}  // namespace eve::physics
