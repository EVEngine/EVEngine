#pragma once

#include <string>
#include <vector>

namespace eve::animation {

class AnimPose;
class AnimSkeleton;

/** @brief Read-only spatial force provider sampled by dynamic bone particles. */
class DynamicBoneForceField {
public:
    virtual ~DynamicBoneForceField() = default;
    /** @brief Sample model-space acceleration. @param x Position X. @param y Position Y. @param z Position Z. @param time Simulation time. @param outX Output X. @param outY Output Y. @param outZ Output Z. */
    virtual void sampleForce(float x, float y, float z, float time, float &outX, float &outY, float &outZ) const = 0;
};

/** @brief One immutable model-space particle in a dynamic-bone debug snapshot. */
struct DynamicBoneDebugParticle { int chain=0,particle=0; float x=0.f,y=0.f,z=0.f,radius=0.f; bool sleeping=false; };
/** @brief One immutable model-space collider in a dynamic-bone debug snapshot. */
struct DynamicBoneDebugCollider { float startX=0.f,startY=0.f,startZ=0.f,endX=0.f,endY=0.f,endZ=0.f,radius=0.f; bool capsule=false,inside=false,enabled=true; };
/** @brief Owning renderer-neutral snapshot safe after the solver changes. */
struct DynamicBoneDebugSnapshot { std::vector<DynamicBoneDebugParticle> particles; std::vector<DynamicBoneDebugCollider> colliders; };
/** @brief Last-update counters for profiling and distance-culling verification. */
struct DynamicBoneUpdateStats { int activeChains=0,sleepingChains=0,particles=0,substeps=0,colliderTests=0,selfCollisionTests=0; };

/**
 * @brief World-space spring-chain solver that writes results as local bone rotations.
 *
 * The skeleton is borrowed and must outlive the solver. All methods are main-thread only.
 * Simulation is tolerance-bounded, not bit-exact across floating-point platforms.
 */
class DynamicBoneSolver {
public:
    /** @brief Construct for a borrowed skeleton, which may be null. */
    explicit DynamicBoneSolver(AnimSkeleton *skeleton);
    ~DynamicBoneSolver();
    DynamicBoneSolver(const DynamicBoneSolver &)            = default;
    DynamicBoneSolver &operator=(const DynamicBoneSolver &) = default;

    /** @brief Rebind the borrowed skeleton; changing it clears index-dependent state. */
    void setSkeleton(AnimSkeleton *skeleton);
    /**
     * @brief Return the borrowed skeleton, or null.
     * @ownership Borrowed; ownership remains with the animation source.
     * @lifetime Valid until setSkeleton(), skeleton destruction, or solver destruction.
     */
    AnimSkeleton *getSkeleton() const { return skeleton_; }

    /**
     * @brief Add an ancestor-to-descendant spring chain.
     * @param rootBone Chain root index.
     * @param endBone Chain tip index.
     * @param stiffness Animated-pose attraction in [0, 1].
     * @param damping Motion damping in [0, 1].
     * @param inertia Retained motion in [0, 1].
     * @param gravityScale Gravity and external-force multiplier.
     * @param radius Particle collision radius.
     * @param iterations Constraint projection passes.
     * @return Chain index, or -1 for invalid input.
     */
    int addChain(int rootBone, int endBone, float stiffness, float damping, float inertia,
                 float gravityScale = 1.f, float radius = 0.f, int iterations = 4);
    /** @brief Name-based addChain variant; returns -1 for invalid input. */
    int addChainByName(const std::string &rootBone, const std::string &endBone, float stiffness,
                       float damping, float inertia, float gravityScale = 1.f, float radius = 0.f,
                       int iterations = 4);
    /** @brief Return configured chain count. */
    int getChainCount() const;
    /** @brief Remove all chains and their simulation state. */
    void clearChains();
    /** @brief Set a valid chain's enabled state; invalid indices are ignored. */
    void setChainEnabled(int chainIndex, bool enabled);
    /** @brief Return enabled state, or false for an invalid index. */
    bool isChainEnabled(int chainIndex) const;
    /** @brief Restrict a chain to its animated model-space plane. @param chainIndex Chain index. @param axis 0 disables; 1, 2, and 3 freeze X, Y, and Z. */
    void setChainFreezeAxis(int chainIndex, int axis);
    /** @brief Enable or disable non-adjacent particle self-collision for a chain. */
    void setChainSelfCollision(int chainIndex, bool enabled);
    /** @brief Override simulation parameters for one chain particle, where zero is the root. */
    void setChainParticleParameters(int chainIndex, int particleIndex, float stiffness, float damping, float inertia,
                                    float gravityScale, float radius);
    /** @brief Add a virtual particle after the last bone along the animated terminal direction. */
    void setChainEndLength(int chainIndex, float length);
    /** @brief Add a virtual particle at a last-bone-local offset; this replaces end length mode. */
    void setChainEndOffset(int chainIndex, float x, float y, float z);
    /** @brief Remove a chain's virtual terminal particle. */
    void clearChainEnd(int chainIndex);

    /** @brief Set model-space gravity acceleration. */
    void setGlobalGravity(float x, float y, float z);
    /** @brief Set additional model-space acceleration such as wind. */
    void setExternalForce(float x, float y, float z);
    /**
     * @brief Set an optional spatial force field sampled on the calling thread.
     * @ownership Borrowed; ownership remains with the caller.
     * @lifetime The provider must outlive this solver or be cleared before destruction.
     */
    void setForceField(const DynamicBoneForceField *forceField) { forceField_ = forceField; }
    /**
     * @brief Return the borrowed spatial force field, or null.
     * @ownership Borrowed; ownership remains with the caller.
     * @lifetime Valid until setForceField(), provider destruction, or solver destruction.
     */
    const DynamicBoneForceField *getForceField() const { return forceField_; }
    /** @brief Return gravity X. */
    float getGlobalGravityX() const { return globalGravityX_; }
    /** @brief Return gravity Y. */
    float getGlobalGravityY() const { return globalGravityY_; }
    /** @brief Return gravity Z. */
    float getGlobalGravityZ() const { return globalGravityZ_; }
    /** @brief Set final rotation blend weight in [0, 1]. */
    void setWeight(float weight);
    /** @brief Return final rotation blend weight. */
    float getWeight() const { return weight_; }
    /** @brief Set preferred simulation frequency; zero means one step per update. */
    void setUpdateRate(float updatesPerSecond);
    /** @brief Return preferred simulation frequency. */
    float getUpdateRate() const { return updateRate_; }
    /** @brief Set root movement distance that triggers a simulation reset. */
    void setTeleportThreshold(float distance);
    /** @brief Return teleport reset distance; zero disables it. */
    float getTeleportThreshold() const { return teleportThreshold_; }
    /** @brief Set how much simulated particles follow root translation, from zero to one. */
    void setObjectMoveResponse(float value);
    /** @brief Return root translation response. */
    float getObjectMoveResponse() const { return objectMoveResponse_; }
    /** @brief Set the model-space point used for distance-based sleeping. */
    void setDistanceReference(float x, float y, float z);
    /** @brief Set sleep distance; zero disables distance sleeping. */
    void setDistanceLimit(float value);
    /** @brief Return whether a valid chain is currently distance-sleeping. */
    bool isChainSleeping(int chainIndex) const;

    /** @brief Add a fixed model-space sphere collider; non-positive radii are ignored. */
    void addColliderSphere(float centerX, float centerY, float centerZ, float radius);
    /** @brief Add a sphere collider following a bone-local offset. */
    void addBoneColliderSphere(int boneIndex, float offsetX, float offsetY, float offsetZ, float radius);
    /** @brief Add a fixed model-space capsule collider. */
    void addColliderCapsule(float startX, float startY, float startZ, float endX, float endY, float endZ,
                            float radius);
    /** @brief Add a capsule whose endpoints follow bone-local offsets. */
    void addBoneColliderCapsule(int boneIndex, float startX, float startY, float startZ, float endX, float endY,
                                float endZ, float radius);
    /** @brief Remove all colliders. */
    void clearColliders();
    /** @brief Return collider count. */
    int getColliderCount() const;
    /** @brief Remove a valid collider index; invalid indices are ignored. */
    void removeCollider(int index);
    /** @brief Enable or disable a collider without changing its index. */
    void setColliderEnabled(int index, bool enabled);
    /** @brief Select inside containment or outside exclusion for a collider. */
    void setColliderInside(int index, bool inside);
    /** @brief Change a collider radius; non-positive values are ignored. */
    void setColliderRadius(int index, float radius);
    /** @brief Reset all chains to the animated pose on their next update. */
    void reset();
    /** @brief Simulate one frame using injected dt and update pose local rotations. */
    void update(AnimPose *pose, float dt);
    /** @brief Copy current particles and colliders for tooling visualization. */
    DynamicBoneDebugSnapshot debugSnapshot() const;
    /** @brief Return counters captured by the most recent update. */
    DynamicBoneUpdateStats getLastUpdateStats() const { return lastUpdateStats_; }

private:
    struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };
    struct Collider {
        int bone = -1;
        Vec3 offset;
        Vec3 endOffset;
        Vec3 center;
        Vec3 end;
        float radius = 0.f;
        bool capsule = false;
        bool enabled = true;
        bool inside = false;
    };
    struct Chain {
        struct ParticleParameters {
            float stiffness = 0.5f, damping = 0.7f, inertia = 0.9f;
            float gravityScale = 1.f, radius = 0.f;
        };
        std::vector<int> bones;
        std::vector<float> restLength;
        std::vector<Vec3> current, previous, target;
        std::vector<ParticleParameters> particleParameters;
        float stiffness = 0.5f, damping = 0.7f, inertia = 0.9f;
        int freezeAxis = 0;
        float gravityScale = 1.f, radius = 0.f;
        float endLength = 0.f;
        Vec3 endOffset;
        int endMode = 0;
        int iterations = 4;
        bool enabled = true, initialized = false, sleeping = false, selfCollision = false;
    };

    std::vector<int> buildChainByIndexes(int rootBone, int endBone) const;
    bool validateChain(const std::vector<int> &bones) const;
    void captureTargets(Chain &chain, const AnimPose &pose);
    void initializeChain(Chain &chain);
    void simulateChain(Chain &chain, float dt);
    void constrainSegment(Chain &chain, size_t particleIndex) const;
    void writeChainRotations(const Chain &chain, AnimPose &pose) const;
    void updateColliderCenters(const AnimPose &pose);
    bool resolveCollider(const Collider &collider, float particleRadius, Vec3 &point) const;

    AnimSkeleton *skeleton_ = nullptr;
    float globalGravityX_ = 0.f, globalGravityY_ = -9.8f, globalGravityZ_ = 0.f;
    float externalForceX_ = 0.f, externalForceY_ = 0.f, externalForceZ_ = 0.f;
    float weight_ = 1.f, updateRate_ = 60.f, teleportThreshold_ = 2.f;
    float objectMoveResponse_ = 1.f, distanceLimit_ = 0.f;
    Vec3 distanceReference_;
    const DynamicBoneForceField *forceField_ = nullptr;
    float simulationTime_ = 0.f;
    DynamicBoneUpdateStats lastUpdateStats_;
    std::vector<Chain> chains_;
    std::vector<Collider> colliders_;
};

}  // namespace eve::animation
