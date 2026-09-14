#pragma once

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
    SteerHoming,       /**< Rotate velocity toward a target; does not integrate. */
    SteerAvoidBody,    /**< Deflect around injected body/shield samples; does not integrate. */
    ApplyGravity,      /**< Subtract gravity from velocity.y. */
    Accelerate,        /**< Increase speed along the current velocity direction. */
    CurveSway,         /**< Add sinusoidal lateral velocity (curve modifier); does not integrate. */
    CurveHelix,        /**< Add rotating lateral velocity (corkscrew); does not integrate. */
    IntegrateLinear    /**< Integrate position from velocity. */
};

/** @brief When an impact may fire. */
enum class CarrierTriggerKind : std::uint8_t {
    OnExpire,     /**< Age crossed lifetime this step. */
    OnFuse,       /**< Age crossed fuse time this step. */
    OnInterval,   /**< Periodic while alive. */
    OnHit,        /**< Hit probe reported contact. */
    OnProximity   /**< Homing/proximity target entered radius. */
};

/** @brief What happens when a matching trigger fires. */
enum class CarrierImpactKind : std::uint8_t {
    EmitHit,    /**< Push a damage-oriented CarrierEvent. */
    Splash,     /**< Push a splash CarrierEvent at the carrier position. */
    Pierce,     /**< Consume pierce budget and suppress release for this trigger. */
    Bounce,     /**< Reflect velocity by contact normal; suppress release. */
    Release,    /**< Request carrier release unless suppressed. */
    SpawnChild  /**< Queue a child spawn using childRecipeId. */
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
};

/** @brief One trigger specification. */
struct CarrierTrigger {
    CarrierTriggerKind kind            = CarrierTriggerKind::OnExpire;
    Duration           fuse            = Duration::zero();
    Duration           interval        = Duration::zero();
    double             proximityRadius = 0.0;
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

    /** @brief Validate finite parameters and composition constraints. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Generation-qualified reference to one pooled carrier slot. */
struct CarrierHandle {
    std::uint32_t slot       = 0;
    std::uint32_t generation = 0;

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
};

/** @brief Owning event emitted by an impact. */
struct CarrierEvent {
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

/** @brief Complete deterministic carrier-pool state for rollback/save. */
struct CarrierRuntimeSnapshot {
    std::vector<CarrierSlotSnapshot> slots;
};

/**
 * @brief Optional swept-hit boundary used only by OnHit triggers.
 *
 * Implementations are called synchronously without locks and must not retain
 * motion references across calls. Missing probe with an OnHit trigger is a
 * structured update failure.
 */
class ICarrierHitProbe {
public:
    virtual ~ICarrierHitProbe() = default;

    /** @brief Owning contact reported between previous and current motion. */
    struct Contact {
        ecs::EntityHandle target{};
        ProjectilePoint   point{};
        ProjectileVector  normal{0.0, 1.0, 0.0};
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
[[nodiscard]] Result<CarrierRecipe> carrierRecipeFromProjectile(const ProjectileDefinition& definition,
                                                                double damage = 0.0);

/**
 * @brief Owner-thread deterministic carrier simulator with composable behaviors.
 *
 * update stages every live carrier on a candidate copy and publishes only after
 * all target/hit lookups succeed. Child spawns are attempted at frame end; pool
 * exhaustion fails the whole frame without partial publish.
 */
class CombatCarrierRuntime {
public:
    CombatCarrierRuntime();

    /** @brief Resize the pool while empty; capacity must be in 1..1048576. */
    [[nodiscard]] Result<void> configurePool(std::uint32_t capacity);

    /**
     * @brief Register or replace a recipe by id.
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
    [[nodiscard]] Result<std::vector<CarrierHandle>> spawnVolley(const CarrierRecipe&       recipe,
                                                                 const CarrierSpawnRequest& request,
                                                                 const CarrierVolleySpec&   volley);

    /**
     * @brief Atomically advance all live carriers by supplied deterministic time.
     * @param targets Optional provider required when any live recipe uses homing/proximity.
     * @param hits Optional probe required when any live recipe uses OnHit.
     * @param bodies Optional provider required when any live recipe uses SteerAvoidBody.
     */
    [[nodiscard]] Result<CarrierFrame> update(Duration                           delta,
                                              const IProjectileTargetProvider*    targets = nullptr,
                                              const ICarrierHitProbe*            hits    = nullptr,
                                              const ICarrierBodyDefenseProvider* bodies  = nullptr);

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
