#pragma once
#include "common/Export.h"
#include "common/WorldSphereSweep.h"


/**
 * @file CombatCarrier.h
 * @brief Composable combat-carrier runtime: motion ops, triggers, and impacts.
 *
 * A carrier is a pooled flight vehicle. Ballistics (motionOps), firing timing
 * (triggers), and attack outcomes (impacts) are assembled on a CarrierRecipe
 * without hard-coding projectile subclasses. Damage settlement stays outside
 * this module — impacts emit owning CarrierEvent values for game/combat adapters.
 *
 * @see docs/dev/战斗载体与投射物行为组合框架.md
 */

#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Time.h"
#include "weapon/ProjectileRuntime.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::weapon {

/** @brief Ordered ballistic operators applied each simulation step. */
enum class CarrierMotionOpKind : std::uint8_t {
    SteerHoming,     /**< Rotate velocity toward a target; does not integrate. */
    SteerAvoidBody,  /**< Deflect around injected body/shield samples; does not integrate. */
    ApplyGravity,    /**< Subtract gravity from velocity.y. */
    Accelerate,      /**< Increase speed along the current velocity direction. */
    CurveSway,       /**< Add sinusoidal lateral velocity (curve modifier); does not integrate. */
    CurveHelix,      /**< Add rotating lateral velocity (corkscrew); does not integrate. */
    IntegrateLinear, /**< Integrate position from velocity. */
    RotateVelocity,  /**< Rotate velocity about a fixed simulation-space axis. */
    SteerOrbit,      /**< Steer tangentially around a target with radial correction. */
    ApplyDrag,       /**< Exponential speed damping; never reverses velocity. */
    SpeedOscillate,  /**< Set speed to recipe speed plus a sinusoidal offset. */
    SteerWander,     /**< Seeded smooth random steering, independent of global RNG. */
    SteerPredictive  /**< Lead a moving target from consecutive position samples. */
};

/** @brief When an impact may fire. */
enum class CarrierTriggerKind : std::uint8_t {
    OnExpire,     /**< Age crossed lifetime this step. */
    OnFuse,       /**< Age crossed fuse time this step. */
    OnInterval,   /**< Periodic while alive. */
    OnHit,        /**< Hit probe reported contact. */
    OnProximity,  /**< Target is within radius (level-triggered). */
    OnDistance,   /**< Accumulated integrated path crossed distance. */
    OnSpeedBelow, /**< Speed crossed threshold downwards. */
    OnApex        /**< Vertical velocity crossed from positive to non-positive. */
};

/** @brief What happens when a matching trigger fires. */
enum class CarrierImpactKind : std::uint8_t {
    EmitHit,    /**< Push a damage-oriented CarrierEvent. */
    Splash,     /**< Push a splash CarrierEvent at the carrier position. */
    Pierce,     /**< Consume pierce budget and suppress release for this trigger. */
    Bounce,     /**< Reflect velocity by contact normal; suppress release. */
    Release,    /**< Request carrier release unless suppressed. */
    SpawnChild, /**< Queue a child spawn using childRecipeId. */
    EmitTrigger /**< Emit a notification without damage or automatic response. */
};

/** @brief One ballistic operator plus its parameters. */
struct CarrierMotionOp {
    CarrierMotionOpKind kind               = CarrierMotionOpKind::IntegrateLinear;
    double              gravity            = 0.0;
    double              maxTurnRateDegrees = 0.0;
    double              acceleration       = 0.0;
    /** @brief SteerAvoidBody: meters ahead at which a body counts as blocking. */
    double avoidLookAhead = 0.0;
    /** @brief SteerAvoidBody: blend weight of avoidance versus current velocity (≥0). */
    double avoidStrength = 1.0;
    /** @brief SteerAvoidBody: extra meters added to each body radius. */
    double avoidRadiusPadding = 0.0;
    /** @brief CurveSway/CurveHelix: lateral amplitude in meters (helix radius). */
    double curveAmplitude = 0.0;
    /** @brief CurveSway/CurveHelix: oscillation frequency in Hz (>0). */
    double curveFrequencyHz = 0.0;
    /** @brief RotateVelocity/SteerOrbit axis; finite and nonzero. */
    ProjectileVector axis{0.0, 1.0, 0.0};
    /** @brief RotateVelocity/SteerOrbit signed angular rate in degrees/second. */
    double angularRateDegrees = 0.0;
    /** @brief SteerOrbit desired radius in simulation units (>0). */
    double orbitRadius = 1.0;
    /** @brief ApplyDrag exponential damping coefficient in inverse seconds (>=0). */
    double drag = 0.0;
    /** @brief SteerWander deterministic stream seed; mixed with carrier slot/generation. */
    int randomSeed = 0;
    /** @brief SteerPredictive maximum extrapolation horizon in seconds (>0). */
    double predictionSeconds = 1.0;
    /** @brief Compare complete operator specifications, including stream identity. */
    friend bool operator==(const CarrierMotionOp&, const CarrierMotionOp&) = default;
};

/** @brief One trigger specification. */
struct CarrierTrigger {
    CarrierTriggerKind kind            = CarrierTriggerKind::OnExpire;
    Duration           fuse            = Duration::zero();
    Duration           interval        = Duration::zero();
    double             proximityRadius = 0.0;
    /** @brief OnDistance path length, or OnSpeedBelow speed threshold; positive. */
    double threshold = 0.0;
    /** @brief Compare trigger specifications. */
    friend bool operator==(const CarrierTrigger&, const CarrierTrigger&) = default;
};

/** @brief One impact bound to a trigger kind. */
struct CarrierImpact {
    CarrierImpactKind kind          = CarrierImpactKind::Release;
    CarrierTriggerKind on           = CarrierTriggerKind::OnExpire;
    double            damage        = 0.0;
    double            splashRadius  = 0.0;
    int               pierceCount   = 0;   /**< Initial pierce budget when kind==Pierce. */
    int               bounceCount   = 0;   /**< Initial bounce budget when kind==Bounce. */
    double            restitution   = 1.0; /**< Bounce speed scale in [0,1+]. */
    LogicalId         childRecipeId;
    std::string       damageType;
    std::string       element;
    /** @brief Bounce tangential velocity loss in [0,1]; restitution scales the remaining vector. */
    double bounceFriction = 0.0;
    /** @brief Seeded bounce cone half-angle in degrees [0,90]; zero is exact specular reflection. */
    double bounceSpreadDegrees = 0.0;
    /** @brief Nonnegative bounce random-stream seed, independent of wander streams. */
    int randomSeed = 0;
    /** @brief Compare impact specifications. */
    friend bool operator==(const CarrierImpact&, const CarrierImpact&) = default;
};

/**
 * @brief Data-driven composition recipe for one carrier family.
 *
 * Recipes are immutable inputs to spawn; the runtime copies budgets into live
 * state. validate() rejects empty motion, non-positive lifetime, and incomplete
 * child/homing/hit bindings.
 */
struct CarrierRecipe {
    LogicalId                     id;
    Duration                      lifetime = Duration::zero();
    double                        speed    = 0.0;
    std::vector<CarrierMotionOp>  motionOps;
    std::vector<CarrierTrigger>   triggers;
    std::vector<CarrierImpact>    impacts;

    /** @brief Master collision switch; disabled skips all OnHit queries and actions. */
    bool collisionEnabled = true;
    /** @brief Select the optional physics-world probe instead of the ordinary hit probe.
     * Inactive when collisionEnabled is false or no OnHit trigger is present.
     */
    bool physicsWorldSweep = false;

    /** @brief Validate finite parameters and composition constraints. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Compare complete immutable recipe content. */
    friend bool operator==(const CarrierRecipe&, const CarrierRecipe&) = default;
};

/** @brief Generation-qualified reference to one pooled carrier slot. */
struct CarrierHandle {
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;

    /** @brief Operator ==. */
    friend bool operator==(const CarrierHandle&, const CarrierHandle&) = default;
};

/** @brief Owning spawn request. */
struct CarrierSpawnRequest {
    ProjectilePoint                  position;
    ProjectileVector                 direction;
    std::optional<ecs::EntityHandle> target;
    ecs::EntityHandle                source{};
};

/** @brief Multi-projectile aim pattern for one salvo. */
enum class CarrierVolleyPattern : std::uint8_t {
    Fan, /**< Spread across ±spreadDegrees/2 in the aim plane. */
    Ring /**< Evenly spaced around a full circle in the aim plane. */
};

/**
 * @brief Deterministic multi-direction spawn descriptor.
 *
 * Directions are derived from the base request with no RNG: Fan uses a symmetric
 * yaw spread around the aim vector; Ring uses equal steps over 360°.
 */
struct CarrierVolleySpec {
    int                  count         = 1;
    CarrierVolleyPattern pattern       = CarrierVolleyPattern::Fan;
    double               spreadDegrees = 0.0; /**< Fan total angle; ignored by Ring. */
};

/** @brief Live motion snapshot for one carrier. */
struct CarrierMotion {
    ProjectilePoint  position;
    ProjectileVector velocity;
};

/** @brief Owning observable state for one live carrier. */
struct CarrierState {
    CarrierHandle                    handle;
    LogicalId                        recipeId;
    CarrierMotion                    motion;
    Duration                         age;
    Duration                         lifetime;
    int                              pierceRemaining = 0;
    int                              bounceRemaining = 0;
    std::optional<ecs::EntityHandle> target;
    ecs::EntityHandle                source{};
    /** @brief Last nonzero heading, retained through a stop so speed operators can restart. */
    ProjectileVector heading{1.0, 0.0, 0.0};
    /** @brief Previous step's curve velocity contribution, removed before the next motion stack. */
    ProjectileVector curveVelocity{};
    /** @brief Previous owning target sample; predictive steering never retains provider references. */
    std::optional<ProjectilePoint> previousTargetPosition;

    /** @brief Integrated path length; owned by this runtime and restored for trigger replay. */
    double distanceTravelled = 0.0;
    /** @brief Age at the previous outer-update target sample, for variable-dt predictive steering. */
    Duration previousTargetSampleAge = Duration::zero();
    /** @brief Generation-qualified targets already pierced; a carrier damages each of these only once. */
    std::vector<ecs::EntityHandle> piercedTargets;
    /** @brief Physics contacts already pierced, using world/body generations rather than fabricated ECS IDs. */
    std::vector<eve::WorldSweepIdentity> piercedWorldBodies;
};

/** @brief Owning event emitted by an impact. */
struct CarrierEvent {
    /** @brief Physics provenance, independent of an optional ECS gameplay target. */
    std::optional<eve::WorldSphereContact> worldContact;
    CarrierHandle     carrier;
    LogicalId         recipeId;
    CarrierTriggerKind trigger = CarrierTriggerKind::OnExpire;
    CarrierImpactKind impact  = CarrierImpactKind::EmitHit;
    ProjectilePoint   position;
    ProjectileVector  normal{};
    ecs::EntityHandle source{};
    ecs::EntityHandle target{};
    double            damage       = 0.0;
    double            splashRadius = 0.0;
    std::string       damageType;
    std::string       element;
    /** @brief Carrier age at the event, including substep/contact timing. */
    Duration age = Duration::zero();
};

/** @brief Deferred child spawn recorded during update. */
struct CarrierChildSpawn {
    LogicalId                        recipeId;
    CarrierSpawnRequest              request;
    CarrierHandle                    parent;
};

/** @brief Owning result of one atomic carrier simulation step. */
struct CarrierFrame {
    std::vector<CarrierHandle>     advanced;
    std::vector<CarrierHandle>     released;
    std::vector<CarrierEvent>      events;
    std::vector<CarrierChildSpawn> childSpawns;
};

/** @brief One pool slot including generation, retained while empty. */
struct CarrierSlotSnapshot {
    std::uint32_t                generation = 0;
    std::optional<CarrierState>  state;
};

/**
 * @brief Bounded simulation work per carrier per outer update.
 * Zero maxStep preserves caller-supplied fixed steps; positive values subdivide automatically.
 * Exhaustion fails the whole update without consuming time. No elapsed time is dropped.
 */
struct CarrierSimulationSettings {
    Duration      maxStep           = Duration::zero();
    std::uint32_t maxIterations     = 256;
    std::uint32_t maxCollisions     = 64;
    std::uint32_t maxTriggerFirings = 4096;
    /** @brief Validate finite work budgets and a nonnegative step duration. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> validate() const;
    friend bool operator==(const CarrierSimulationSettings&, const CarrierSimulationSettings&) = default;
};

/** @brief Complete deterministic carrier-pool state for rollback/save. */
struct CarrierRuntimeSnapshot {
    std::vector<CarrierSlotSnapshot> slots;
    /** @brief Schema 4 adds generation-qualified pierced physics bodies; other versions are rejected. */
    std::uint32_t schemaVersion = 4;
    /** @brief Owning recipe context; restore rejects missing or modified active recipes. */
    std::vector<CarrierRecipe> recipes;
    CarrierSimulationSettings  simulation;
};

/**
 * @brief Optional swept-hit boundary used only by OnHit triggers.
 *
 * Implementations are called synchronously without locks and must not retain
 * motion references across calls. The selected probe is required only for collision-enabled OnHit recipes; absence is a
 * structured update failure.
 */
class ICarrierHitProbe {
public:
    /** @brief Releases ICarrierHitProbe resources. */
    virtual ~ICarrierHitProbe() = default;

    /** @brief Owning contact reported between previous and current motion. */
    struct Contact {
        ecs::EntityHandle target{};
        ProjectilePoint   point{};
        ProjectileVector  normal{0.0, 1.0, 0.0};
        /** @brief Canonical normalized impact time in [0,1] along this swept segment.
         * Legacy probes may omit it: compatibility-only projection from point supplies the inward time.
         * point is the carrier center at impact; normal points out of the obstacle.
         */
        std::optional<double> fraction;
        /** @brief Optional physics provenance; target stays empty unless a gameplay adapter maps it. */
        std::optional<eve::WorldSphereContact> worldContact;
    };

    /**
     * @brief Query contacts for one candidate step.
     * @return Owning contact list, or a structured failure that rolls back the frame.
     */
    [[nodiscard]] virtual Result<std::vector<Contact>> query(CarrierHandle handle,
                                                             const CarrierMotion& previous,
                                                             const CarrierMotion& current) const = 0;
};

/**
 * @brief One body/shield sample used by SteerAvoidBody to curve around defenses.
 *
 * `facing` + `frontConeDegrees` model a directional shield: avoidance engages
 * only when the carrier approaches from within the front cone. A cone of 180°
 * or greater treats the body as omnidirectional cover.
 */
struct CarrierBodyDefenseSample {
    ProjectilePoint  center{};
    double           radius           = 0.0;
    ProjectileVector facing{0.0, 0.0, 1.0};
    double           frontConeDegrees = 180.0;
};

/**
 * @brief Optional body/shield boundary for SteerAvoidBody.
 *
 * Called synchronously without locks. Must not retain motion references across
 * calls. Missing provider when a live recipe uses SteerAvoidBody fails the frame.
 */
class ICarrierBodyDefenseProvider {
public:
    /** @brief Releases ICarrierBodyDefenseProvider resources. */
    virtual ~ICarrierBodyDefenseProvider() = default;

    /**
     * @brief Sample body/shield volumes relevant to one carrier step.
     * @param target Homing target when present; may be a null handle for area defenses.
     */
    [[nodiscard]] virtual Result<std::vector<CarrierBodyDefenseSample>> sample(
        CarrierHandle handle, ecs::EntityHandle target, const CarrierMotion& motion) const = 0;
};

/**
 * @brief Build a default recipe matching a classic ProjectileDefinition mode.
 *
 * Linear/Ballistic/Homing map to motion-op stacks with OnExpire→Release and
 * OnHit→EmitHit+Release. Homing requires a positive turn rate already validated
 * by ProjectileDefinition.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<CarrierRecipe> carrierRecipeFromProjectile(
    const ProjectileDefinition& definition, double damage = 0.0);

/**
 * @brief Synchronous custom motion strategy, invoked per proposed segment before collisions.
 * @ownership Borrowed only during updateWithMotion; arguments are borrowed for evaluate only.
 * @remarks Owner thread only, without locks. Do not mutate the runtime or external state from evaluate.
 * Replay requires the same pure function and inputs; callbacks are not stored in snapshots.
 */
class ICarrierMotionFunction {
public:
    /** @brief Destroy the caller-owned strategy. */
    virtual ~ICarrierMotionFunction() = default;
    /**
     * @brief Return final position and velocity, or failure to abort the whole frame.
     * @param before Pre-step state, including stable handle and age.
     * @param proposed Built-in motion at the end of this step; return it to keep the result.
     * @param step Proposed segment dt, clamped to remaining time, lifetime and configured maxStep.
     * @return Owning motion with finite coordinates and velocity.
     */
    [[nodiscard]] virtual Result<CarrierMotion> evaluate(const CarrierState& before, const CarrierMotion& proposed,
                                                         Duration step) const = 0;
};

/**
 * @brief Owner-thread deterministic carrier simulator with composable behaviors.
 *
 * update stages every live carrier on a candidate copy and publishes only after
 * all target/hit lookups succeed. Providers are borrowed only for the synchronous
 * call, must not mutate this runtime reentrantly, and may be destroyed after it
 * returns. Same-backend replay requires identical dt/provider inputs and recipe
 * context; cross-backend floating-point comparisons use 1e-6 simulation units.
 * Child spawns are attempted at frame end; pool
 * exhaustion fails the whole frame without partial publish.
 */
class EVENGINE_API_WORLD CombatCarrierRuntime {
public:
    /** @brief Combat carrier runtime. */
    CombatCarrierRuntime();

    /** @brief Resize the pool while empty; capacity must be in 1..1048576. */
    [[nodiscard]] Result<void> configurePool(std::uint32_t capacity);

    /**
     * @brief Copy validated solver settings while the pool is empty; conflicts fail atomically.
     * @ownership Caller retains settings; no references are retained.
     * @remarks Owner thread only; reentrant mutation is rejected. Snapshots include these settings.
     */
    [[nodiscard]] Result<void> configureSimulation(const CarrierSimulationSettings& settings);

    /**
     * @brief Register a recipe, or replace it only when no live carrier uses the id.
     * Equal registration is a no-op; conflicting live replacement fails atomically.
     * @ownership Runtime copies the recipe; callers may discard their instance.
     */
    [[nodiscard]] Result<void> registerRecipe(const CarrierRecipe& recipe);

    /** @brief Look up a previously registered recipe by id. */
    [[nodiscard]] std::optional<CarrierRecipe> findRecipe(const LogicalId& id) const;

    /** @brief Spawn one carrier from a registered recipe id. */
    [[nodiscard]] Result<CarrierHandle> spawn(const LogicalId& recipeId, const CarrierSpawnRequest& request);

    /** @brief Spawn one carrier from an inline validated recipe (also registers it). */
    [[nodiscard]] Result<CarrierHandle> spawn(const CarrierRecipe& recipe, const CarrierSpawnRequest& request);

    /**
     * @brief Spawn multiple carriers in deterministic Fan/Ring directions.
     *
     * Each projectile shares the base position/target/source and receives a
     * rotated aim vector. Fails without partial spawns when the pool cannot fit
     * the full volley or any single spawn is invalid.
     */
    [[nodiscard]] Result<std::vector<CarrierHandle>> spawnVolley(const LogicalId&           recipeId,
                                                                 const CarrierSpawnRequest& request,
                                                                 const CarrierVolleySpec&   volley);
    /** @brief Spawn volley. */
    [[nodiscard]] Result<std::vector<CarrierHandle>> spawnVolley(const CarrierRecipe&       recipe,
                                                                 const CarrierSpawnRequest& request,
                                                                 const CarrierVolleySpec&   volley);

    /**
     * @brief Atomically advance all live carriers by supplied deterministic time.
     * @param targets Optional provider required when any live recipe uses homing/proximity.
     * @param hits Ordinary collision probe for enabled OnHit recipes without physicsWorldSweep.
     * @param worldSweep Optional physics-world adapter for enabled OnHit recipes selecting physicsWorldSweep.
     * @ownership Probes are borrowed for this call only, on the owner thread, without locks.
     * The world adapter owns shape/filter/body-to-entity mapping; no physics dependency is required otherwise.
     * @param bodies Optional provider required when any live recipe uses SteerAvoidBody.
     */
    [[nodiscard]] Result<CarrierFrame> update(Duration delta, const IProjectileTargetProvider* targets = nullptr,
                                              const ICarrierHitProbe*            hits       = nullptr,
                                              const ICarrierBodyDefenseProvider* bodies     = nullptr,
                                              const ICarrierHitProbe*            worldSweep = nullptr);

    /**
     * @brief Advance using a caller-owned custom motion function, before hits and triggers.
     * @ownership All providers are borrowed synchronously and never retained.
     * @remarks Owner thread only. Reentrant mutation fails; callback failures abort publication.
     * Custom distance is the swept chord length. Return proposed to retain built-in curve bookkeeping.
     * @cost One user function call per live carrier per step, amortized by fixed simulation ticks.
     */
    [[nodiscard]] Result<CarrierFrame> updateWithMotion(Duration delta, const ICarrierMotionFunction& motion,
                                                        const IProjectileTargetProvider*   targets    = nullptr,
                                                        const ICarrierHitProbe*            hits       = nullptr,
                                                        const ICarrierBodyDefenseProvider* bodies     = nullptr,
                                                        const ICarrierHitProbe*            worldSweep = nullptr);

    /** @brief Explicitly release one live handle; stale handles are rejected. */
    [[nodiscard]] Result<void> release(CarrierHandle handle);

    /** @brief Return an owning state copy, or empty for a stale/released handle. */
    [[nodiscard]] std::optional<CarrierState> find(CarrierHandle handle) const;

    /** @brief Return owning live states in stable slot order. */
    [[nodiscard]] std::vector<CarrierState> states() const;

    /** @brief Capture live carriers plus empty-slot generations. */
    [[nodiscard]] CarrierRuntimeSnapshot snapshot() const;

    /** @brief Atomically replace this pool from a validated snapshot. */
    [[nodiscard]] Result<void> restore(const CarrierRuntimeSnapshot& snapshot);

    /** @brief Number of live carriers. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return activeCount_; }

    /** @brief Current fixed pool capacity. */
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

private:
    [[nodiscard]] Result<CarrierFrame> updateImpl(Duration delta, const IProjectileTargetProvider* targets,
                                                  const ICarrierHitProbe*            hits,
                                                  const ICarrierBodyDefenseProvider* bodies,
                                                  const ICarrierMotionFunction*      motion,
                                                  const ICarrierHitProbe*            worldSweep);
    bool                               updating_ = false;
    CarrierSimulationSettings          simulation_;
    struct Slot {
        std::uint32_t               generation = 0;
        std::optional<CarrierState> state;
    };

    struct LiveRecipe {
        CarrierRecipe recipe;
        int           initialPierce  = 0;
        int           initialBounce  = 0;
        bool          needsHoming    = false;
        bool          needsHit       = false;
        bool          needsProximity = false;
        bool          needsBodyAvoid = false;
    };

    [[nodiscard]] Result<CarrierHandle> spawnPrepared(const LiveRecipe&          live,
                                                      const CarrierSpawnRequest& request);
    [[nodiscard]] static Result<LiveRecipe> prepareRecipe(const CarrierRecipe& recipe);
    [[nodiscard]] static Result<void> validateSpawn(const CarrierRecipe&        recipe,
                                                    const CarrierSpawnRequest&  request);

    std::vector<Slot>              slots_;
    std::vector<LiveRecipe>        recipes_;
    std::size_t                    activeCount_ = 0;
};

}  // namespace eve::weapon
