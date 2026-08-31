#pragma once

/**
 * @file RTSSystems.h
 * @brief Deterministic phase-one RTS systems and their ECS contracts.
 */

#include "rts/RTSAction.h"
#include "common/ResourceAccount.h"
#include "weapon/ProjectileRuntime.h"
#include "weapon/WeaponTypes.h"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <set>
#include <vector>

namespace eve::rts {

/** @brief Deterministic formation layout strategies. */
enum class FormationKind : std::uint8_t {
    Line,
    Grid,
    Wedge,
};

/** @brief Input to the pure formation planner. */
struct FormationSpec {
    FormationKind kind    = FormationKind::Line;
    float         spacing = 32.0f;
    int           columns = 0;

    /** @brief Validate spacing and grid column constraints. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Static metadata for one ECS system's access and phase contract. */
struct SystemContract {
    const char* name              = "";
    const char* view              = "";
    const char* readSet           = "";
    const char* writeSet          = "";
    const char* structuralChanges = "";
    const char* events            = "";
    const char* phase             = "";
};

/** @brief Authoritative damage route that produced one transient RTS frame event. */
enum class DamageChannel : std::uint8_t { Ability, Projectile, Weapon };
/** @brief Read-only observer invoked after canonical damage has committed. */
using DamageEventSink = std::function<void(
    const combat::DamageRequest& request, const combat::DamageOutcome& outcome,
    SimulationTick tick, DamageChannel channel)>;

/** @brief Transient weapon lifecycle event emitted after an authoritative firing attempt commits. */
enum class CombatFireEventKind : std::uint8_t {
    WeaponFired,
    ProjectileFired,
    ShotMissed,
    ReloadStarted,
    ReloadCompleted,
    WeaponDry,
    FireBlocked,
};
/** @brief Stable presentation payload for firing, launch, and miss feedback. */
struct CombatFireEvent {
    CombatFireEventKind kind = CombatFireEventKind::WeaponFired;
    SubjectRef source;
    SubjectRef target;
    WorldPosition point;
};
/** @brief Read-only observer invoked after a weapon consumes one authoritative shot. */
using CombatFireEventSink = std::function<void(const CombatFireEvent&, SimulationTick)>;

/** @brief Non-combat RTS state transition exposed to presentation without entering authoritative state. */
enum class LifecycleEventKind : std::uint8_t {
    SuppressionRecovered,
    ShieldRecharged,
    ConstructionCompleted,
    ProductionSpawnBlocked,
    ProductionSpawnCleared,
    UnitProduced,
    BuildingCaptured,
    AbilityCast,
    AbilityChannelStarted,
    AbilityChannelTick,
    AbilityChannelCompleted,
    AbilityInterrupted,
    AbilityChannelCancelled,
    StatusApplied,
    StatusExpired,
    AmmoProduced,
    SupplyDispatched,
    SupplyRelayDispatched,
    AmmoResupplied,
    SupplyRelayTransferred,
    SupplyReturning,
    SupplyReturned,
};
/** @brief Stable payload for one committed RTS lifecycle transition. */
struct LifecycleEvent {
    LifecycleEventKind kind = LifecycleEventKind::ConstructionCompleted;
    SubjectRef source;
    SubjectRef target;
    std::string detail;
    double value = 0.0;
};
/** @brief Read-only observer invoked after a non-combat lifecycle transition commits. */
using LifecycleEventSink = std::function<void(const LifecycleEvent&, SimulationTick)>;

/** @brief Applies one hostile kill's experience to canonical unit veterancy state. */
class VeterancySystem {
public:
    /**
     * @brief Award experience and apply every newly crossed rank modifier atomically.
     * @param unit Live unit receiving kill credit.
     * @param experience Positive defeated-target durability value.
     * @return Number of newly crossed ranks (zero when veterancy is disabled).
     */
    [[nodiscard]] static Result<int> award(Unit& unit, float experience);
};

/** @brief Resolves same-faction and match-team alliances without duplicating diplomacy state. */
class FactionRelationSystem {
public:
    /** @brief Return true when both live factions are identical or share a team in a common Match. */
    [[nodiscard]] static bool allied(Faction* left, Faction* right) noexcept;
    /** @brief Resolve generation-checked faction links through the same relationship policy. */
    [[nodiscard]] static bool allied(const FactionLink& left, const FactionLink& right) noexcept;
};

/** @brief Reads authoritative faction contact projections for command authorization. */
class FactionIntelSystem {
public:
    /**
     * @brief Return whether a stable subject is currently visible and detected.
     * Empty contact state preserves native integrations that do not install fog-of-war.
     */
    [[nodiscard]] static bool targetable(Faction* viewer, SubjectRef subject) noexcept;
};

/** @brief Return the phase-one RTS ECS contracts for tooling and review. */
[[nodiscard]] std::span<const SystemContract> systemContracts() noexcept;

/**
 * @brief Pure deterministic formation planner.
 *
 * Positions are generated in input order. No wall clock or random stream is
 * read, so replay and lockstep callers receive the same layout.
 */
class FormationPlanner {
public:
    /**
     * @brief Plan positions around an anchor for a fixed unit count.
     * @param count Number of positions to produce.
     * @param anchor Formation anchor position.
     * @param spec Layout strategy and spacing.
     * @return Exactly `count` positions or a structured validation failure.
     */
    [[nodiscard]] static Result<std::vector<WorldPosition>> plan(std::size_t count, WorldPosition anchor,
                                                                 const FormationSpec& spec);
};

/** @brief Caller-owned canonical terrain/occupancy validation used after RTS influence checks. */
using PlacementValidation = std::function<Result<void>(WorldPosition, LogicalId)>;

/** @brief Composes RTS powered build influence with the canonical building placement authority. */
class BuildInfluenceSystem {
public:
    /**
     * @brief Validate one proposed building position without mutating either domain.
     * @param faction Owning faction of the proposed structure.
     * @param position Proposed world-space anchor.
     * @param definition Stable building definition passed to the placement provider.
     * @param placement Canonical terrain, footprint, and occupancy validator.
     * @param requireInfluence Whether a completed powered allied influence source is required.
     */
    [[nodiscard]] static Result<void> validate(Faction& faction, WorldPosition position,
                                               LogicalId definition, const PlacementValidation& placement,
                                               bool requireInfluence = true);
};

/** @brief Receipt containing all order ids accepted by command fan-out. */
struct FanOutReceipt {
    std::size_t              requested = 0;
    std::size_t              accepted  = 0;
    std::vector<std::string> orderIds;
};

/**
 * @brief Validates a typed selection and fans one command out in formation order.
 *
 * This is a command boundary, not a new order implementation. It writes only
 * the selected units' generic OrderComponent and performs no structural ECS
 * mutation while its validation View is alive.
 */
class CommandFanOutSystem {
public:
    /**
     * @brief Submit one command to every selected live Unit.
     * @param unitHandles Generation-checked handles in player selection order.
     * @param command Common Move/Attack/Build/Gather command.
     * @param formation Deterministic target offset policy.
     * @return Accepted order ids in selection order, or a failure with no
     *         partially accepted command exposed to the caller.
     */
    [[nodiscard]] static Result<FanOutReceipt> fanOut(std::span<const ecs::EntityHandle> unitHandles,
                                                      const CommandSpec& command, const FormationSpec& formation);
};

/** @brief Moves units toward their active generic order targets. */
class MotionSystem {
public:
    /**
     * @brief Advance all Units with Motion and Orders components.
     * @param step Injected deterministic simulation step.
     * @return Number of units inspected, or a structured failure.
     */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

/** @brief Settles arrived finite movement orders so the canonical queue can advance. */
class MovementOrderSystem {
public:
    /** @brief Complete arrived Move and AttackMove orders after their final path waypoint. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Applies immediate Stop and persistent Hold/AttackMove command state. */
class CommandStateSystem {
public:
    /** @brief Synchronize combat guard flags and settle Stop orders immediately. */
    [[nodiscard]] static Result<std::size_t> step();
};

}  // namespace eve::rts

namespace eve::crowd { class Crowd; }
namespace eve::sensing { class SensingWorld; }
namespace eve::map { class Pathfinder; class Fov; }

namespace eve::rts {

/** @brief World/grid conversion used when an RTS composition consumes a canonical map Pathfinder. */
struct NavigationGrid {
    float cellSize = 1.0f;
    float originX = 0.0f;
    float originY = 0.0f;
};

/** @brief Notification emitted once when a newly planned order has no canonical map route. */
using NavigationEvent = std::function<void(Unit&, const OrderRecord&)>;

/** @brief Plans unit routes through the canonical map Pathfinder without owning map blockers or costs. */
class NavigationSystem {
public:
    /** @brief Replan changed movement orders and advance reached waypoints. */
    [[nodiscard]] static Result<std::size_t> step(map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                                   const NavigationEvent& unreachable = {});
};

/** @brief Maintains persistent two-point patrol state without owning pathfinding. */
class PatrolSystem {
public:
    /** @brief Initialize patrol origins and reverse direction at reached endpoints. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Arbitrates deterministic entry into narrow canonical-map cells. */
class TrafficReservationSystem {
public:
    /**
     * @brief Reserve each moving unit's next narrow cell by priority and stable subject identity.
     * @param pathfinder Canonical provider of cell walkability.
     * @param grid World/grid conversion shared with route planning.
     * @return Number of moving units inspected, or a structured failure.
     */
    [[nodiscard]] static Result<std::size_t> step(const map::Pathfinder& pathfinder,
                                                   const NavigationGrid& grid);
};

/** @brief Resolves the canonical FOV instance used by a faction; allies may deliberately share one instance. */
using FogProvider = std::function<map::Fov*(Faction&)>;

/** @brief Projects RTS vision into canonical map FOV and maintains faction last-known contacts. */
class FogOfWarSystem {
public:
    /** @brief Runtime-only revealer bindings; authoritative explored cells remain owned by map::Fov. */
    struct State {
        struct Binding {
            ecs::EntityHandle source{};
            ecs::EntityHandle faction{};
            map::Fov*          provider = nullptr;
            int                revealer = -1;
        };
        std::vector<Binding> bindings;
    };

    /** @brief Synchronize revealers, compute each unique provider once, and update persistent contacts. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, const NavigationGrid& grid,
                                                   State& state, const FogProvider& provider);
    /** @brief Remove all revealers previously installed by this state. */
    static void clear(State& state) noexcept;
    /** @brief Find a last-known contact by stable subject identity. */
    [[nodiscard]] static const Faction::Intel::Contact* contact(const Faction& faction,
                                                                SubjectRef subject) noexcept;
};

/** @brief Synchronizes linked RTS units through the canonical Crowd simulation. */
class CrowdMotionSystem {
public:
    /** @brief Push ECS targets into Crowd, advance it once, and project positions back to Unit::Motion. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, crowd::Crowd& crowd);
};

/** @brief Deterministically assigns idle auto-workers to compatible resource nodes. */
class WorkerAssignmentSystem {
public:
    /** @brief Assign nearest available nodes and enqueue Gather orders. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Callback that credits gathered resources to the authoritative account selected by the game. */
using ResourceCredit = std::function<Result<resource::Receipt>(Unit&, const resource::CostSpec&)>;

/** @brief Advances Gather/ReturnCargo orders without owning an economy balance. */
class MiningSystem {
public:
    /** @brief Gather stock or deliver integral cargo through the injected canonical credit boundary. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, const ResourceCredit& credit);
};

/** @brief Advances construction using valid builders that reached their assigned site. */
class ConstructionSystem {
public:
    /** @brief Advance incomplete buildings and settle completed Build orders. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const LifecycleEventSink& events = {});
};

/** @brief Assigns idle faction workers to incomplete or damaged buildings by deterministic distance. */
class WorkforceAssignmentSystem {
public:
    /** @brief Apply enabled faction construction/repair policies without owning worker orders. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Callback that debits repair costs from the authoritative account selected by the game. */
using RepairDebit = std::function<Result<resource::Receipt>(Unit&, Building&, const resource::CostSpec&)>;

/** @brief Repairs friendly completed buildings without owning an economy balance. */
class RepairSystem {
public:
    /** @brief Advance arrived Repair orders through the injected canonical debit boundary. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, const RepairDebit& debit);
};

/** @brief Resolves deterministic single-faction and contested building capture. */
class CaptureSystem {
public:
    /** @brief Advance capture forces, decay abandoned progress, and transfer faction ownership. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const LifecycleEventSink& events = {});
};

/** @brief Credits passive building income through the game-owned canonical economy boundary. */
using PassiveIncomeCredit = std::function<Result<resource::Receipt>(Building&, const resource::CostSpec&)>;

/** @brief Allocates faction power deterministically and settles powered passive income. */
class InfrastructureSystem {
public:
    /** @brief Recompute power from live completed buildings, then credit integral accrued income. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const PassiveIncomeCredit& credit = {});
};

/** @brief Resolves transport boarding, building garrisoning, and contained-unit synchronization. */
class ContainmentSystem {
public:
    /** @brief Board arrived friendly units and keep occupants attached to their live container. */
    [[nodiscard]] static Result<std::size_t> step();
    /** @brief Unload every live passenger from a transport into deterministic nearby slots. */
    [[nodiscard]] static Result<std::size_t> unload(Unit& transport, WorldPosition destination);
    /** @brief Evacuate every live garrison occupant and unblock building capture. */
    [[nodiscard]] static Result<std::size_t> evacuate(Building& building, WorldPosition destination);
};

/** @brief Assigns and transfers tactical ammunition through canonical WeaponEntity resource state. */
using AmmoProductionPurchase = std::function<Result<std::size_t>(
    Building&, std::string_view, std::int64_t, std::size_t)>;

class SupplySystem {
public:
    /**
     * @brief Produce stock through the economy port, dispatch suppliers, and transfer integral rounds.
     * @param step Fixed simulation time and tick.
     * @param purchase Optional canonical economy settlement boundary for produced ammunition.
     * @param pathfinder Optional canonical map pathfinder used for relay rendezvous selection.
     * @param grid Navigation sampling configuration.
     * @param events Optional deterministic lifecycle event sink.
     * @return Number of produced, dispatched, or transferred supply operations.
     */
    [[nodiscard]] static Result<std::size_t> step(
        const SimulationStep& step, const AmmoProductionPurchase& purchase = {},
        map::Pathfinder* pathfinder = nullptr, const NavigationGrid& grid = {},
        const LifecycleEventSink& events = {});
};

/** @brief A safe map-reachable rendezvous selected around a predicted moving relay intercept. */
struct SupplyRendezvousSelection {
    WorldPosition target;
    float threat = 0.0f;
    bool avoidedThreat = false;
};

/** @brief Selects a low-threat supply rendezvous while delegating reachability to the canonical map. */
class SupplyRendezvousSystem {
public:
    /** @brief Rank the predicted intercept and deterministic lateral/rear alternatives. */
    [[nodiscard]] static Result<SupplyRendezvousSelection> select(
        Unit& supplier, Unit& relay, WorldPosition predicted,
        map::Pathfinder& pathfinder, const NavigationGrid& grid);
};

/** @brief Derives stable multi-vehicle supply convoys and paces their leader. */
class SupplyConvoySystem {
public:
    /** @brief Group active supply missions by target and hold leaders whose followers exceed convoy spacing. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Recovers suppression and applies nearby friendly morale auras. */
class MoraleSystem {
public:
    /** @brief Advance deterministic suppression recovery and state transitions. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const LifecycleEventSink& events = {});
};

/** @brief Regenerates unit and building shield projections after their configured damage delay. */
class ShieldSystem {
public:
    /** @brief Advance cooldowns and clamp deterministic regeneration to capacity. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const LifecycleEventSink& events = {});
};

/** @brief Builds deterministic faction command networks with relay capacity and hostile jamming. */
class CommandNetworkSystem {
public:
    /** @brief Recompute all source loads, relay uplinks and recipient command state. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Debits an ability resource cost through the game-owned canonical economy boundary. */
using AbilityResourceDebit = std::function<Result<resource::Receipt>(Unit&, const resource::CostSpec&)>;

/** @brief Validates, schedules and settles active RTS abilities through canonical damage/effect owners. */
class AbilitySystem {
public:
    /** @brief Begin an immediate, cast-time, or periodic channel ability atomically. */
    [[nodiscard]] static Result<void> cast(Unit& caster, const AbilitySpec& spec,
                                           ecs::EntityHandle target, WorldPosition point,
                                           combat::DamageRuntime& damage,
                                           const AbilityResourceDebit& debit = {},
                                           const DamageEventSink& damageEvents = {},
                                           SimulationTick tick = {},
                                           const LifecycleEventSink& events = {});
    /** @brief Advance cooldowns/channels and settle completed or periodic casts. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  combat::DamageRuntime& damage,
                                                  const DamageEventSink& damageEvents = {},
                                                  const LifecycleEventSink& events = {});
};

/** @brief Tracks indirect-fire deployment after movement. */
class ArtillerySystem {
public:
    /** @brief Reset deployment on movement and advance stationary deployment timers. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

/** @brief Deterministic, canonical-map-validated artillery relocation choice. */
struct ArtilleryRelocationSelection {
    WorldPosition target;
    float threat = 0.0f;
    int conflicts = 0;
    bool avoidedThreat = false;
    bool deconflicted = false;
    bool avoidedDepartedPosition = false;
};

/** @brief Selects reachable shoot-and-scoot positions without taking ownership of map navigation. */
class ArtilleryRelocationSystem {
public:
    /**
     * @brief Rank reachable candidate positions by hostile coverage, weapon range, friendly conflicts and revisit.
     * @param unit Artillery unit that is about to relocate.
     * @param target Last fired-at world position.
     * @param distance Configured relocation distance.
     * @param weaponRange Maximum range of the firing weapon.
     * @param pathfinder Canonical map path provider used only for reachability checks.
     * @param grid Shared world/grid projection.
     */
    [[nodiscard]] static Result<ArtilleryRelocationSelection> select(
        Unit& unit, WorldPosition target, float distance, float weaponRange,
        map::Pathfinder& pathfinder, const NavigationGrid& grid);
};

/** @brief Assigns indirect-fire responders and automatic counter-battery ground attacks. */
class FireSupportSystem {
public:
    /** @brief Assign nearest eligible friendly artillery to a deterministic suppression corridor. */
    [[nodiscard]] static Result<std::size_t> request(Unit& requester, WorldPosition center, float radius,
                                                     int shotsPerResponder, std::size_t maxResponders = 0);
    /** @brief Cancel suppression orders previously assigned for this requester. */
    [[nodiscard]] static Result<std::size_t> cancel(Unit& requester);
    /** @brief Remove stale support requests and assign exposed hostile indirect-fire positions. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step);
};

/** @brief Updates escort guards and deterministic combat-group target commitments. */
class TacticsSystem {
public:
    /**
     * @brief Follow protected entities, intercept local threats, and distribute group fire.
     * @param damage Optional canonical damage resolver used to predict armor effectiveness.
     * @param step Injected deterministic time used to rotate retreat cover-fire teams.
     */
    [[nodiscard]] static Result<std::size_t> step(const combat::DamageRuntime* damage = nullptr,
                                                   const SimulationStep& step = {});
};

/** @brief Authoritative production boundary invoked by AI policy decisions. */
using AIProductionRequest = std::function<Result<void>(Faction&, Building&, const LogicalId&)>;

/** @brief Runs deterministic faction production and attack policy. */
class AISystem {
public:
    /** @brief Think for enabled factions and submit production/attack decisions. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const AIProductionRequest& requestProduction);
};

/** @brief Uses canonical sensing, weapon and damage providers for RTS automatic fire. */
using FireLineQuery = std::function<Result<bool>(WorldPosition origin, WorldPosition target,
                                                  ecs::EntityHandle source, ecs::EntityHandle targetEntity,
                                                  const weapon::WeaponDefinition& weapon)>;
/** @brief Absolute muzzle and target heights resolved by the authoritative map/game adapter. */
struct CombatHeightProfile { float source = 0.0f; float target = 0.0f; };
/** @brief Resolve absolute heights for one authorized projectile launch. */
using CombatHeightQuery = std::function<CombatHeightProfile(
    WorldPosition origin, WorldPosition target, ecs::EntityHandle source, ecs::EntityHandle targetEntity)>;

class CombatFireSystem {
public:
    /** @brief Adapter-owned keys previously mirrored into a shared sensing world. */
    struct State {
        std::set<std::string> mirroredSubjects;
        std::set<std::string> blockedSubjects;
    };
    /** @brief Mirror live targets, acquire enemies, advance weapons, fire, and settle direct impacts. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, State& state,
                                                  sensing::SensingWorld& sensing, combat::DamageRuntime& damage,
                                                  class RTSProjectileSystem* projectiles = nullptr,
                                                  const FireLineQuery& fireLine = {},
                                                  map::Pathfinder* pathfinder = nullptr,
                                                  const NavigationGrid& navigationGrid = {},
                                                  const CombatHeightQuery& heightQuery = {},
                                                  const DamageEventSink& damageEvents = {},
                                                  const CombatFireEventSink& fireEvents = {});
};

/** @brief Earliest canonical collision found along one projectile sweep segment. */
struct ProjectileCollision {
    WorldPosition position;
    ecs::EntityHandle entity{};
};

/** @brief Map/physics-owned swept collision query; empty means the segment remained clear. */
using ProjectileCollisionQuery = std::function<Result<std::optional<ProjectileCollision>>(
    WorldPosition from, float fromHeight, WorldPosition to, float toHeight,
    SubjectRef source, ecs::EntityHandle intendedTarget)>;

/** @brief Stable RTS damage payload paired with one shared projectile-pool handle. */
struct RTSProjectilePayloadSnapshot {
    std::uint64_t key = 0;
    SubjectRef source;
    SubjectRef faction;
    SubjectRef target;
    SubjectRef observer;
    WorldPosition targetPoint;
    float targetHeight = 0.0f;
    std::string damageType;
    double damage = 0.0;
    float radius = 0.0f;
    float splashMinimumDamageFactor = 0.1f;
    bool targetsGround = true;
    bool targetsAir = true;
    bool friendlyFire = false;
    bool blockedByObstacles = false;
    std::vector<std::string> requiredTargetTags;
    std::vector<std::string> excludedTargetTags;
};

/** @brief Complete in-flight RTS projectile state with stable gameplay relationships. */
struct RTSProjectileSystemSnapshot {
    weapon::ProjectileRuntimeSnapshot runtime;
    std::vector<RTSProjectilePayloadSnapshot> payloads;
};

/** @brief Resolve one stable RTS subject while restoring projectile relationships. */
using ProjectileSubjectResolver = std::function<ecs::Entity*(SubjectRef)>;

/** @brief RTS impact adapter over the canonical pooled weapon projectile trajectory runtime. */
class RTSProjectileSystem final : public weapon::IProjectileTargetProvider {
public:
    /** @brief Launch one already-authorized weapon shot into the canonical trajectory pool. */
    [[nodiscard]] Result<void> launch(SubjectRef source, ecs::EntityHandle faction, WorldPosition origin,
                                      ecs::EntityHandle target, WorldPosition targetPoint,
                                      const weapon::WeaponDefinition& weapon, double damageFactor = 1.0,
                                      ecs::EntityHandle observer = {}, float originHeight = 0.0f,
                                      float targetHeight = 0.0f);
    /** @brief Advance trajectories, detect swept impacts, and settle damage through DamageRuntime. */
    [[nodiscard]] Result<std::size_t> step(const SimulationStep& step, combat::DamageRuntime& damage,
                                           const ProjectileCollisionQuery& collision = {},
                                           const DamageEventSink& damageEvents = {});
    /** @brief Resolve homing targets for the canonical runtime. */
    [[nodiscard]] Result<weapon::ProjectilePoint> position(ecs::EntityHandle target) const override;
    /** @brief Return live canonical projectile count. */
    [[nodiscard]] std::size_t activeCount() const noexcept { return runtime_.activeCount(); }
    /** @brief Capture shared trajectories and stable RTS damage payloads. */
    [[nodiscard]] RTSProjectileSystemSnapshot snapshot() const;
    /** @brief Validate and atomically restore shared trajectories plus RTS payloads. */
    [[nodiscard]] Result<void> restore(const RTSProjectileSystemSnapshot& snapshot,
                                       const ProjectileSubjectResolver& resolver);

private:
    struct Payload {
        SubjectRef source;
        ecs::EntityHandle faction{};
        ecs::EntityHandle target{};
        ecs::EntityHandle observer{};
        WorldPosition targetPoint;
        float targetHeight = 0.0f;
        std::string damageType;
        double damage = 0.0;
        float radius = 0.0f;
        float splashMinimumDamageFactor = 0.1f;
        bool targetsGround = true;
        bool targetsAir = true;
        bool friendlyFire = false;
        bool blockedByObstacles = false;
        std::vector<std::string> requiredTargetTags;
        std::vector<std::string> excludedTargetTags;
    };
    static std::uint64_t key(weapon::ProjectileHandle handle) noexcept;
    weapon::ProjectileRuntime runtime_;
    std::map<std::uint64_t, Payload> payloads_;
};

/** @brief Sends active generic orders to one shared action executor. */
class OrderActionSystem {
public:
    /**
     * @brief Execute/advance active Unit orders and complete them when done.
     * @param step Injected deterministic simulation step.
     * @param executor Borrowed action adapter; it must outlive this call.
     * @return Number of action-bearing units inspected, or a structured failure.
     */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, IRTSActionExecutor& executor);
};

/** @brief Advances Building production queues without duplicating task state. */
using ProductionSpawn = std::function<Result<Unit*>(Building&, const production::ProductionTask&)>;
/** @brief Game/map-owned deterministic exit probe; empty means every valid exit is currently occupied. */
using ProductionSpawnPosition =
    std::function<Result<std::optional<WorldPosition>>(Building&, const production::ProductionTask&)>;

class BuildingProductionSystem {
public:
    /** @brief Advance all buildings and settle newly completed unit tasks through the game-owned factory. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step, const ProductionSpawn& spawn = {},
                                                   const ProductionSpawnPosition& position = {},
                                                   const LifecycleEventSink& events = {});
};

/** @brief Pauses canonical production tasks when a shared rally combat group reaches reinforcement limits. */
using ReinforcementCancel = std::function<Result<void>(Building&, std::string_view)>;
/** @brief Game-owned transactional enqueue boundary used while resolving reinforcement fallbacks. */
using ReinforcementEnqueue = std::function<Result<std::string>(Building&, std::string_view)>;

/** @brief Result of resolving a preferred reinforcement through a deterministic fallback chain. */
struct ReinforcementRequestReceipt {
    std::string requestedProduct;
    std::string queuedProduct;
    std::string taskId;
};

class ReinforcementProductionPolicySystem {
public:
    /** @brief Reconcile total/type caps and cross-factory type priorities before production advances. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step = {},
                                                   const ReinforcementCancel& cancel = {});
    /** @brief Try a preferred product and its acyclic configured fallback chain through the injected enqueue port. */
    [[nodiscard]] static Result<ReinforcementRequestReceipt> request(
        Building& building, std::string preferredProduct, const ReinforcementEnqueue& enqueue);
};

/** @brief Dispatches and unloads production reinforcements assigned to canonical unit containers. */
class ReinforcementSystem {
public:
    /** @brief Advance transport dispatch and arrival settlement. */
    [[nodiscard]] static Result<std::size_t> step();
};

/** @brief Advances lifecycle-only effects on Units and Buildings. */
class EffectSystem {
public:
    /** @brief Advance all attached effects by the injected simulation delta. */
    [[nodiscard]] static Result<std::size_t> step(const SimulationStep& step,
                                                  const LifecycleEventSink& events = {});
};

}  // namespace eve::rts
