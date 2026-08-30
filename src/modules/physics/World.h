#pragma once

#include "common/Snapshot.h"
#include "physics/PhysicsHandles.h"
#include "physics/SimulationBackend.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class b2World;
class b2Body;
class b2Fixture;
class b2Contact;
struct b2Manifold;
struct b2ContactImpulse;

namespace eve::graphics {
class Graphics;
}

namespace eve::physics {

class Body;
class Fixture;
class ContactRelay;

/**
 * @brief Box2D world wrapper (2D physics) with pixel-space coordinates.
 * Handles stepping, gravity, raycasts, AABB queries and contact/impact events.
 */
class World {
public:
    struct ContactEvent {
        int bodyAId = 0;
        int bodyBId = 0;
        std::string fixtureATag;
        std::string fixtureBTag;
    };

    struct ImpactEvent : ContactEvent {
        float pointX = 0.f;
        float pointY = 0.f;
        float normalX = 0.f;
        float normalY = 0.f;
        float relativeNormalSpeed = 0.f;
        float normalImpulse = 0.f;
        float tangentImpulse = 0.f;
    };

    /**
     * @brief Result of a point probe: deepest non-sensor fixture within radius.
     * Normal points from the shape toward the probed point (pixels).
     */
    struct ClothContact {
        bool   hit = false;
        float  nx = 0.f;
        float  ny = 0.f;
        float  depth = 0.f;  // radius - distance (pixels); > 0 when inside
        Body  *body = nullptr;
    };

    /**
     * @brief Creates a physics world.
     * @param gravityX/gravityY  Gravity vector in pixels/s^2.
     * @param sleep              Whether bodies may sleep when idle.
     * @param meter              Pixels per meter conversion factor.
     */
    World(float gravityX, float gravityY, bool sleep, float meter);
    ~World();

    World(const World &)            = delete;
    World &operator=(const World &) = delete;

    /** @brief Steps the simulation by dt seconds (5 velocity / 2 position iterations). */
    void update(float dt);
    /** @brief Steps with explicit iteration counts. */
    void updateFull(float dt, int velocityIterations, int positionIterations);

    /**
     * @brief Advances the domain with an injected deterministic simulation step.
     * @param step Tick and fixed duration supplied by SimulationClock or replay.
     * @param settings Solver policy; the backend consumes it without hidden clamping.
     * @return Applied when the step completed, or a structured rejection/failure.
     * @remarks A rejected step leaves solver state and the current tick unchanged.
     */
    [[nodiscard("check the physics step outcome or explicitly ignore it")]]
    eve::Result<void> step(const eve::SimulationStep &step, const SimulationSettings &settings = {});

    /** @brief Snapshot of completed backend steps and logical simulation time. */
    [[nodiscard]] SimulationObservation simulationObservation() const noexcept;
    /** @brief Selected CPU/GPU/mock backend family. */
    [[nodiscard]] SimulationBackendKind backendKind() const noexcept;
    /** @brief Replay/numeric guarantee declared by the selected backend. */
    [[nodiscard]] SimulationDeterminism backendDeterminism() const noexcept;
    /** @brief Current deterministic tick; save data should persist this value. */
    [[nodiscard]] eve::SimulationTick simulationTick() const noexcept { return simulationTick_; }
    /** @brief Process-local identity used by PhysicsLink; invalid after destruction. */
    [[nodiscard]] PhysicsWorldHandle runtimeHandle() const noexcept { return runtimeHandle_; }
    /** @brief Whether optional accelerator selection fell back to CPU. */
    [[nodiscard]] bool usedBackendFallback() const noexcept { return backendFallback_; }
    /**
     * @brief Returns the selection outcome, including an absent-capability warning.
     * @return A copy safe for logging or policy inspection.
     */
    [[nodiscard("inspect backend selection diagnostics")]]
    eve::Status backendSelectionStatus() const;

    /**
     * @brief Captures a versioned, integrity-checked world snapshot.
     * @param hashProvider Injected digest provider used to seal the envelope.
     * @return A snapshot containing the exact SimulationTick and body state.
     * @remarks The provider is not retained; this call is owner-thread-only.
     */
    [[nodiscard("check or persist the physics snapshot")]]
    eve::Result<eve::SnapshotEnvelope> snapshot(const eve::SnapshotHashProvider &hashProvider) const;

    /**
     * @brief Restores a verified snapshot without exposing partial state.
     * @param snapshot Versioned envelope produced for this world schema.
     * @param hashProvider Provider used to verify its content hash.
     * @return Applied when all body identities and tick metadata match.
     * @remarks The snapshot is borrowed for this call and runtime handles are
     *          never persisted or reused from its payload.
     */
    [[nodiscard("check the physics snapshot restore outcome")]]
    eve::Result<void> restore(const eve::SnapshotEnvelope &snapshot, const eve::SnapshotHashProvider &hashProvider);

    /** @brief Sets the world gravity vector in pixels/s^2. */
    void  setGravity(float gx, float gy);
    float getGravityX() const;
    float getGravityY() const;

    /** @brief Changes the pixels-per-meter conversion. */
    void  setMeter(float pixelsPerMeter);
    float getMeter() const { return meter_; }

    /**
     * @brief Creates a body in pixel-space units.
     * @return Borrowed nullable body owned by this world; null means creation failed.
     * @ownership World owns the body and its fixtures; callers must destroy it through this API.
     * @lifetime Valid until Body::destroy(), World::destroy(), or world teardown; use PhysicsBodyHandle across frames.
     * @thread Call on the owning physics thread.
     * @reentrancy Creation does not invoke callbacks; do not re-enter structural world mutation while using the result.
     */
    Body *newBody(const std::string &bodyType, float x, float y);
    /** @brief Resolves a live body handle; returns null for a stale or foreign handle. */
    [[nodiscard]] Body *findBody(PhysicsBodyHandle handle) const;

    /** @brief Destroys a body (null is ignored). */
    void destroyBody(Body *body);
    /** @brief Destroys the underlying Box2D world and resets event buffers. */
    void destroy();

    /** @brief True while the underlying Box2D world is alive. */
    bool isValid() const { return !destroyed_ && world_ != nullptr; }

    /**
     * @brief Probe the deepest non-sensor fixture within `radius` of a pixel point.
     * Returns false when nothing is hit. Used by Cloth for particle-vs-body collision.
     */
    bool pointProbe(float x, float y, float radius, ClothContact *out) const;

    /** @brief Optional: draw fixture AABBs via Graphics::drawSolidRect. */
    void drawDebug(graphics::Graphics *gfx);

    /**
     * @brief Closest raycast in pixel space from (x1,y1) to (x2,y2).
     * Returns hit body id, or -1. Read hit details via getRayHit*.
     */
    int rayCast(float x1, float y1, float x2, float y2);
    bool  hasRayHit() const { return rayHitBodyId_ >= 0; }
    int   getRayHitBodyId() const { return rayHitBodyId_; }
    float getRayHitX() const { return rayHitX_; }
    float getRayHitY() const { return rayHitY_; }
    float getRayHitNormalX() const { return rayHitNormalX_; }
    float getRayHitNormalY() const { return rayHitNormalY_; }
    /** @brief Fraction along the segment [0,1] of the closest hit. */
    float getRayHitFraction() const { return rayHitFraction_; }

    /**
     * @brief Query fixtures overlapping an axis-aligned box in pixel space (x,y,w,h).
     * Returns match count; read ids with getQueryBodyId(i).
     */
    int queryAABB(float x, float y, float w, float h);
    int getQueryCount() const { return static_cast<int>(queryBodyIds_.size()); }
    int getQueryBodyId(int index) const;

    int getBeginContactCount() const { return int(beginContacts_.size()); }
    int getBeginContactBodyAId(int index) const;
    int getBeginContactBodyBId(int index) const;
    std::string getBeginContactFixtureATag(int index) const;
    std::string getBeginContactFixtureBTag(int index) const;
    int getEndContactCount() const { return int(endContacts_.size()); }
    int getEndContactBodyAId(int index) const;
    int getEndContactBodyBId(int index) const;
    std::string getEndContactFixtureATag(int index) const;
    std::string getEndContactFixtureBTag(int index) const;

    int getImpactCount() const { return int(impacts_.size()); }
    int getImpactBodyAId(int index) const;
    int getImpactBodyBId(int index) const;
    std::string getImpactFixtureATag(int index) const;
    std::string getImpactFixtureBTag(int index) const;
    float getImpactPointX(int index) const;
    float getImpactPointY(int index) const;
    float getImpactNormalX(int index) const;
    float getImpactNormalY(int index) const;
    float getImpactRelativeNormalSpeed(int index) const;
    float getImpactNormalImpulse(int index) const;
    float getImpactTangentImpulse(int index) const;
    /** @brief Clears collected begin/end contact and impact event buffers. */
    void clearContactEvents();

    /** @brief Converts a pixel-space length to meters. */
    float toMeters(float pixels) const;
    /** @brief Converts a meter-space length to pixels. */
    float toPixels(float meters) const;

    /**
     * @brief Exposes the underlying Box2D world for tightly-scoped backend integration.
     * @return Borrowed nullable backend pointer; never transfer or retain it across steps.
     * @ownership World owns the Box2D world; callers must not delete the returned pointer.
     * @lifetime Valid until World::destroy() or wrapper destruction.
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks; backend mutation must not re-enter world callbacks.
     */
    b2World *raw() { return world_; }
    /**
     * @brief Exposes the underlying Box2D world for read-only backend integration.
     * @return Borrowed nullable backend pointer.
     * @ownership World owns the Box2D world; callers must not delete it.
     * @lifetime Valid until World::destroy() or wrapper destruction.
     * @thread Call only on the owning physics thread.
     * @reentrancy Does not invoke callbacks and is invalid across world mutation.
     */
    const b2World *raw() const { return world_; }

    void onBeginContact(b2Contact *contact);
    void onEndContact(b2Contact *contact);
    void onPreSolve(b2Contact *contact, const b2Manifold *oldManifold);
    void onPostSolve(b2Contact *contact, const b2ContactImpulse *impulse);

    void forgetBody(Body *body);
    void forgetFixture(Fixture *fixture);

    int nextBodyId();
    PhysicsBodyHandle nextBodyRuntimeHandle();

private:
    friend class Body;
    friend class Fixture;

    b2World      *world_ = nullptr;
    ContactRelay *relay_ = nullptr;
    std::unique_ptr<ISimulationBackend> simulation_;
    PhysicsWorldHandle                  runtimeHandle_          = PhysicsWorldHandle::invalid();
    float         meter_ = 30.f;
    int           nextId_ = 1;
    std::uint32_t                       nextBodyHandleIndex_    = 1u;
    bool          destroyed_ = false;
    eve::SimulationTick                 simulationTick_         = eve::SimulationTick::zero();
    eve::Status                         backendSelectionStatus_ = eve::Status::success();
    bool                                backendFallback_        = false;

    std::unordered_set<Body *>    bodies_;
    std::unordered_set<Fixture *> fixtures_;

    int   rayHitBodyId_   = -1;
    float rayHitX_        = 0.f;
    float rayHitY_        = 0.f;
    float rayHitNormalX_  = 0.f;
    float rayHitNormalY_  = 0.f;
    float rayHitFraction_ = 0.f;

    std::vector<int> queryBodyIds_;

    struct PreSolveData {
        float pointX = 0.f;
        float pointY = 0.f;
        float normalX = 0.f;
        float normalY = 0.f;
        float relativeNormalSpeed = 0.f;
    };
    std::unordered_map<b2Contact *, PreSolveData> preSolve_;
    std::vector<ContactEvent> beginContacts_;
    std::vector<ContactEvent> endContacts_;
    std::vector<ImpactEvent> impacts_;
};

}  // namespace eve::physics
