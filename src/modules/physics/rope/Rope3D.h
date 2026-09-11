#pragma once

#include "physics/backend/SimulationBackend.h"

#include <cstdint>
#include <vector>

namespace eve::physics {

class DistanceField3D;
class World3D;

/** @brief Result of a topology-changing rope operation. */
enum class RopeTopologyChange { Changed, Unchanged };

/** @brief Stable identifier for a rope-owned analytic collider. */
struct RopeColliderId {
    std::uint64_t          value = 0;
    [[nodiscard]] explicit operator bool() const { return value != 0; }
    friend bool            operator==(RopeColliderId, RopeColliderId) = default;
};

/** @brief Result of a rope collider mutation. */
enum class RopeColliderChange { Changed, Unchanged };

/**
 * @brief Renderer-independent particle rope with XPBD stretch and bend constraints.
 *
 * The rope owns an ordered particle chain and its structural elements. All coordinates
 * are meters in a +Y-up space. The instance is simulation-thread affine; it invokes no
 * callbacks and owns no graphics or rigid-body resources. Floating-point replay is
 * tolerance-bounded for identical fixed steps and inputs.
 */
class Rope3D final : public ISimulationBackend {
public:
    /** @brief Compact solver vector exposed only as a value type. */
    struct Vec3 {
        float x = 0.f, y = 0.f, z = 0.f;
    };
    /**
     * @brief Creates a straight rope including both endpoints.
     * @param particleCount Number of particles, at least two.
     * @param startX Start point X in meters.
     * @param startY Start point Y in meters.
     * @param startZ Start point Z in meters.
     * @param endX End point X in meters.
     * @param endY End point Y in meters.
     * @param endZ End point Z in meters.
     * @throws eve::Exception when inputs are invalid.
     */
    Rope3D(int particleCount, float startX, float startY, float startZ, float endX, float endY, float endZ);

    /** @brief Advances using a compatibility-generated monotonic fixed tick. */
    void update(float dt);

    /** @brief Advances the rope atomically under the shared simulation contract. */
    [[nodiscard("check the rope step outcome")]]
    eve::Result<void> step(const eve::SimulationStep& step, const SimulationSettings& settings) override;
    [[nodiscard]] SimulationObservation observation() const noexcept override { return observation_; }
    [[nodiscard]] SimulationBackendKind kind() const noexcept override { return SimulationBackendKind::Cpu; }
    [[nodiscard]] SimulationDeterminism determinism() const noexcept override {
        return SimulationDeterminism::ToleranceBounded;
    }
    [[nodiscard("check rope observation restore")]]
    eve::Result<void> restoreObservation(const SimulationObservation& observation) override;

    /** @brief Sets uniform acceleration in meters per second squared. */
    void                setGravity(float x, float y, float z);
    [[nodiscard]] float getGravityX() const { return gravity_.x; }
    [[nodiscard]] float getGravityY() const { return gravity_.y; }
    [[nodiscard]] float getGravityZ() const { return gravity_.z; }

    /** @brief Sets stretch compliance in inverse newtons; zero is inextensible. */
    void                setStretchCompliance(float compliance);
    [[nodiscard]] float getStretchCompliance() const noexcept { return stretchCompliance_; }
    /** @brief Enables or disables structural distance constraints without changing topology. */
    void               setDistanceConstraintsEnabled(bool enabled) { distanceConstraintsEnabled_ = enabled; }
    [[nodiscard]] bool getDistanceConstraintsEnabled() const { return distanceConstraintsEnabled_; }
    /** @brief Sets bend compliance; larger values make the rope easier to bend. */
    void                setBendCompliance(float compliance);
    [[nodiscard]] float getBendCompliance() const noexcept { return bendCompliance_; }
    /** @brief Enables or disables bend constraints independently from stretch constraints. */
    void               setBendConstraintsEnabled(bool enabled) { bendConstraintsEnabled_ = enabled; }
    [[nodiscard]] bool getBendConstraintsEnabled() const { return bendConstraintsEnabled_; }
    /** @brief Sets fractional bend slack in [0,0.5] before bend resistance activates. */
    void                setMaxBending(float fraction);
    [[nodiscard]] float getMaxBending() const { return maxBending_; }
    /** @brief Configures local permanent bend absorption; zero creep disables plasticity. */
    void                setPlasticity(float yield, float creep);
    [[nodiscard]] float getPlasticYield() const { return plasticYield_; }
    [[nodiscard]] float getPlasticCreep() const { return plasticCreep_; }
    /** @brief Returns absorbed local bend fraction, or zero for an invalid bend index. */
    [[nodiscard]] float getBendPlasticity(int bendIndex) const;
    /** @brief Sets the maximum fractional compression in [0,1]. */
    void                setMaxCompression(float fraction);
    [[nodiscard]] float getMaxCompression() const noexcept { return maxCompression_; }
    /** @brief Sets Verlet velocity damping in [0,1]. */
    void                setDamping(float damping);
    [[nodiscard]] float getDamping() const { return damping_; }
    /** @brief Sets each particle's mass in kilograms. */
    void                setParticleMass(float mass);
    [[nodiscard]] float getParticleMass() const { return particleMass_; }
    /** @brief Sets collision radius in meters. */
    void                setRadius(float radius);
    [[nodiscard]] float getRadius() const { return radius_; }
    /** @brief Enables non-neighbour particle self collision. */
    void               setSelfCollision(bool enabled) { selfCollision_ = enabled; }
    [[nodiscard]] bool getSelfCollision() const { return selfCollision_; }

    /** @brief Constrains particles to an axis-aligned box (origin plus positive extents). */
    void setBounds(float x, float y, float z, float width, float height, float depth);
    /** @brief Disables the axis-aligned collision box. */
    void clearBounds() { hasBounds_ = false; }
    /** @brief Sets tangential contact damping in [0,1]. */
    void                setCollisionFriction(float friction);
    [[nodiscard]] float getCollisionFriction() const { return collisionFriction_; }
    /** @brief Adds a sphere collider owned by this rope. */
    [[nodiscard("retain the collider id")]]
    eve::Result<RopeColliderId> addSphereCollider(float x, float y, float z, float radius);
    /** @brief Adds a one-sided plane collider using a normalized outward normal. */
    [[nodiscard("retain the collider id")]]
    eve::Result<RopeColliderId> addPlaneCollider(float x, float y, float z, float nx, float ny, float nz);
    /** @brief Moves a previously created sphere without changing its radius. */
    [[nodiscard("check collider update")]]
    eve::Result<RopeColliderChange> moveSphereCollider(RopeColliderId id, float x, float y, float z);
    /** @brief Removes a rope-owned analytic collider. */
    [[nodiscard("check collider removal")]]
    eve::Result<RopeColliderChange> removeCollider(RopeColliderId id);
    /** @brief Removes every rope-owned analytic collider. */
    void clearColliders() { colliders_.clear(); }
    /** @brief Attaches a borrowed rigid-body world for mesh/shape collision and reaction impulses. */
    void setCollideWorld(World3D* world) { world_ = world; }
    /** @brief Returns the borrowed rigid-body collision world, or null. */
    [[nodiscard]] World3D* getCollideWorld() const { return world_; }
    /** @brief Attaches a borrowed signed-distance field for static environment collision. */
    void setCollideSdf(DistanceField3D* field) { sdf_ = field; }
    /** @brief Returns the borrowed signed-distance collision field, or null. */
    [[nodiscard]] DistanceField3D* getCollideSdf() const { return sdf_; }
    /** @brief Enables swept-sphere continuous collision against World3D and SDF geometry. */
    void setContinuousCollision(bool enabled) { continuousCollision_ = enabled; }
    /** @brief Reports whether continuous collision is enabled. */
    [[nodiscard]] bool getContinuousCollision() const { return continuousCollision_; }
    /** @brief Sets particle/world normal restitution in [0,1]. */
    void setCollisionRestitution(float restitution);
    /** @brief Returns particle/world normal restitution. */
    [[nodiscard]] float getCollisionRestitution() const { return collisionRestitution_; }

    /** @brief Pins a particle at its current position. */
    [[nodiscard("check whether the attachment changed")]]
    eve::Result<RopeTopologyChange> pin(int particleIndex);
    /** @brief Pins a particle at an explicit animated target. */
    [[nodiscard("check whether the attachment changed")]]
    eve::Result<RopeTopologyChange> attach(int particleIndex, float x, float y, float z);
    /** @brief Moves an existing attachment target. */
    [[nodiscard("check the attachment target update")]]
    eve::Result<RopeTopologyChange> moveAttachment(int particleIndex, float x, float y, float z);
    /** @brief Releases a pinned particle. */
    [[nodiscard("check whether the attachment changed")]]
    eve::Result<RopeTopologyChange> detach(int particleIndex);
    [[nodiscard]] bool              isAttached(int particleIndex) const;

    /** @brief Applies a one-frame acceleration to every free particle. */
    void applyForce(float x, float y, float z);
    /** @brief Scales all active element rest lengths, supporting reel/winch behavior. */
    [[nodiscard("check the requested rope length")]]
    eve::Result<RopeTopologyChange> setRestLength(float length);
    /**
     * @brief Changes material length at one endpoint by adding/removing particles near the requested spacing.
     * @param length Positive target rest length.
     * @param particleSpacing Positive preferred spacing for inserted particles.
     * @param fromEnd True edits the last endpoint; false edits the first.
     * @return Whether topology or material length changed. Torn ropes are rejected.
     */
    [[nodiscard("check cursor topology change")]]
    eve::Result<RopeTopologyChange> changeLength(float length, float particleSpacing, bool fromEnd = true);
    [[nodiscard]] float             getRestLength() const;
    [[nodiscard]] float             calculateLength() const;

    /** @brief Deactivates one structural element and splits the simulated chain. */
    [[nodiscard("check the topology change")]]
    eve::Result<RopeTopologyChange> cut(int elementIndex);
    /** @brief Re-enables a cut element using its current endpoint distance. */
    [[nodiscard("check the topology change")]]
    eve::Result<RopeTopologyChange> repair(int elementIndex);
    [[nodiscard]] bool              isElementActive(int elementIndex) const;
    [[nodiscard]] float             getElementForce(int elementIndex) const;
    /** @brief Sets a positive per-element multiplier for the global tearing threshold. */
    [[nodiscard("check the element tearing material update")]]
    eve::Result<RopeTopologyChange> setElementTearResistance(int elementIndex, float multiplier);

    /** @brief Enables automatic tearing above a tensile-force threshold in newtons. */
    void setTearing(float resistance, int maxTearsPerStep);
    /** @brief Disables automatic tearing. */
    void disableTearing() { tearingEnabled_ = false; }
    /** @brief Monotonic revision incremented by cut, repair, resize, and automatic tears. */
    [[nodiscard]] std::uint64_t getTopologyRevision() const { return topologyRevision_; }
    /** @brief Number of elements automatically torn by the most recent update/step. */
    [[nodiscard]] int getLastTornElementCount() const { return static_cast<int>(lastTornElements_.size()); }
    /** @brief Returns a torn element index, or -1 for an invalid event index. */
    [[nodiscard]] int getLastTornElement(int eventIndex) const;

    [[nodiscard]] int   getParticleCount() const { return static_cast<int>(particles_.size()); }
    [[nodiscard]] int   getElementCount() const { return static_cast<int>(elements_.size()); }
    [[nodiscard]] float getParticleX(int index) const;
    [[nodiscard]] float getParticleY(int index) const;
    [[nodiscard]] float getParticleZ(int index) const;
    [[nodiscard]] float getParticleVelocityX(int index, float dt) const;
    [[nodiscard]] float getParticleVelocityY(int index, float dt) const;
    [[nodiscard]] float getParticleVelocityZ(int index, float dt) const;
    /** @brief Samples current rope position by normalized active material length. */
    [[nodiscard]] float getSampleX(float mu) const;
    [[nodiscard]] float getSampleY(float mu) const;
    [[nodiscard]] float getSampleZ(float mu) const;
    /** @brief Samples a normalized current tangent by active material length. */
    [[nodiscard]] float getSampleTangentX(float mu) const;
    [[nodiscard]] float getSampleTangentY(float mu) const;
    [[nodiscard]] float getSampleTangentZ(float mu) const;

private:
    struct Particle {
        Vec3 position;
        Vec3 previous;
        Vec3 attachment;
        bool attached = false;
    };
    struct Element {
        int   a              = 0;
        int   b              = 0;
        float restLength     = 0.f;
        float lambda         = 0.f;
        float force          = 0.f;
        float tearResistance = 1.f;
        bool  active         = true;
    };
    enum class ColliderKind { Sphere, Plane };
    struct Collider {
        RopeColliderId id;
        ColliderKind   kind = ColliderKind::Sphere;
        Vec3           position{};
        Vec3           normal{};
        float          radius = 0.f;
    };

    [[nodiscard]] bool validParticle(int index) const noexcept;
    [[nodiscard]] bool validElement(int index) const noexcept;
    void               integrate(float dt);
    void               solveStretch(float dt);
    void               solveBend(float dt);
    void               updatePlasticity(float dt);
    void               solveSelfCollision();
    void               collideBounds();
    void               solveExternalCollisions();
    void               collideBorrowedSurfaces(float dt);
    void               applyAttachments();
    void               applyTearing();
    void               syncBendState();
    [[nodiscard]] Vec3 samplePosition(float mu) const;
    [[nodiscard]] Vec3 sampleTangent(float mu) const;

    std::vector<Particle> particles_;
    std::vector<Element>  elements_;
    Vec3                  gravity_{0.f, -9.81f, 0.f};
    Vec3                  force_{};
    float                 stretchCompliance_          = 0.f;
    bool                  distanceConstraintsEnabled_ = true;
    float                 bendCompliance_             = 0.002f;
    bool                  bendConstraintsEnabled_     = true;
    float                 maxBending_                 = 0.025f;
    float                 plasticYield_               = 0.f;
    float                 plasticCreep_               = 0.f;
    std::vector<float>    bendPlasticity_;
    float                 maxCompression_ = 0.f;
    float                 damping_        = 0.01f;
    float                 particleMass_   = 0.1f;
    float                 radius_         = 0.04f;
    bool                  selfCollision_  = true;
    bool                  hasBounds_      = false;
    Vec3                  boundsMin_{};
    Vec3                  boundsMax_{};
    float                 collisionFriction_ = 0.f;
    std::uint64_t         nextColliderId_    = 1;
    std::vector<Collider> colliders_;
    World3D*              world_                = nullptr;
    DistanceField3D*      sdf_                  = nullptr;
    bool                  continuousCollision_  = true;
    float                 collisionRestitution_ = 0.f;
    bool                  tearingEnabled_       = false;
    float                 tearResistance_       = 1000.f;
    int                   maxTearsPerStep_      = 1;
    std::uint64_t         topologyRevision_     = 0;
    std::vector<int>      lastTornElements_;
    SimulationObservation observation_{};
};

}  // namespace eve::physics
