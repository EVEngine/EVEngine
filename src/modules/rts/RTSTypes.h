#pragma once

/**
 * @file RTSTypes.h
 * @brief RTS domain roots and composition-only components.
 *
 * RTS deliberately has several small ECS roots instead of a universal
 * GameObject/GameplaySubject base.  Unit, Building, Player and Faction own
 * only their domain identity; orthogonal state is attached as components and
 * cross-domain relationships are represented by typed links.
 */

#include "attributes/AttributeProjection.h"
#include "common/ECS.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "combat/Damage.h"
#include "common/Time.h"
#include "rts/RTSEffects.h"
#include "production/Production.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::orders {
class CommandQueue;
}

namespace eve::production {
class WorkQueue;
}

namespace eve::rts {

class RTSProductionActionAdapter;

/** @brief Two-dimensional deterministic RTS world position. */
struct WorldPosition {
    float x = 0.0f;
    float y = 0.0f;
};

/** @brief Automatic target-acquisition policy for an RTS unit. */
enum class CombatStance : std::uint8_t { Passive, Defensive, Aggressive };

/** @brief Return the stable lower-case spelling of a combat stance. */
[[nodiscard]] const char* combatStanceName(CombatStance stance) noexcept;

/** @brief Commands shared by movement, combat, construction and gathering. */
enum class OrderKind : std::uint8_t {
    Move,
    Attack,
    Build,
    Gather,
    ReturnCargo,
    AttackMove,
    Stop,
    HoldPosition,
    Patrol,
    Repair,
    Garrison,
    BoardTransport,
    Capture,
    AttackGround,
    Resupply,
    Escort,
    SuppressArea,
    SupplyRelay,
};

/**
 * @brief Return the stable lower-case spelling of an RTS order kind.
 * @return Borrowed pointer to immutable process-lifetime storage; never null for a valid enum value.
 * @ownership The returned string is static and caller-owned storage is not involved.
 * @lifetime Valid for the lifetime of the process; do not free or retain it as mutable state.
 * @thread Thread-safe because the pointed-to characters are immutable.
 * @reentrancy Does not invoke callbacks and is safe during re-entrant queries.
 */
[[nodiscard]] const char* orderKindName(OrderKind kind) noexcept;

/**
 * @brief One domain-neutral command submitted to a unit's generic order queue.
 *
 * The definition id is a logical recipe/action id. It is not an ECS handle
 * and is intentionally copied into the order payload.
 */
struct CommandSpec {
    OrderKind     kind = OrderKind::Move;
    WorldPosition target;
    std::string   definitionId;
    int           priority       = 0;
    double        timeoutSeconds = 0.0;
    ecs::EntityHandle targetEntity{};
    WorldPosition secondaryTarget;
    float         radius = 0.0f;
    bool          append = false;

    /** @brief Validate coordinates and command metadata without mutating state. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning projection of the active generic order used by RTS systems. */
struct OrderRecord {
    std::string   id;
    OrderKind     kind = OrderKind::Move;
    WorldPosition target;
    std::string   definitionId;
    ecs::EntityHandle targetEntity{};
    WorldPosition secondaryTarget;
    float         radius        = 0.0f;
    bool          append        = false;
    int           formationSlot = -1;
};

/** @brief Target relationship accepted by one RTS ability. */
enum class AbilityTarget : std::uint8_t { Self, Ally, Enemy, Point };

/** @brief Typed ability policy; lifecycle effects remain owned by the canonical effect container. */
struct AbilitySpec {
    std::string id;
    LogicalId casterDefinition;
    AbilityTarget target = AbilityTarget::Enemy;
    std::string resourceType;
    std::string damageType = "damage.magic";
    float range = 0.0f;
    float radius = 0.0f;
    float cooldown = 0.0f;
    std::int64_t resourceCost = 0;
    float damage = 0.0f;
    float healing = 0.0f;
    float castTime = 0.0f;
    float channelTickInterval = 0.0f;
    bool interruptOnDamage = true;
    bool appliesEffect = false;
    RTSEffectDefinition effect;
};

/** @brief Entity-local sorted tags; this is the RTS entity's tag authority. */
class TagSet {
public:
    /** @brief Add a tag in deterministic lexical order. */
    [[nodiscard]] Result<void> add(std::string_view tag);
    /** @brief Remove a tag, returning NotFound when it is absent. */
    [[nodiscard]] Result<void> remove(std::string_view tag);
    /** @brief Test exact tag membership. */
    [[nodiscard]] bool contains(std::string_view tag) const noexcept;
    /** @brief Return tags in deterministic order. */
    [[nodiscard]] const std::vector<std::string>& values() const noexcept { return values_; }

private:
    std::vector<std::string> values_;
};

/** @brief Typed link tag for links to another ECS entity. */
struct EntityLinkTag {};

/**
 * @brief Generation-checked link to an ECS entity.
 *
 * The link owns no target. `bind()` validates the handle immediately;
 * `resolve()` performs the generation check again, so target destruction or
 * slot reuse becomes observable as a stale link. `fromHandleForRestore()` is
 * reserved for restore/import code that will validate before use.
 */
template <typename Tag>
class EntityLink {
public:
    /** @brief Bind a live ECS handle, rejecting a null or stale handle. */
    [[nodiscard]] static Result<EntityLink> bind(ecs::EntityHandle handle) {
        if (ecs::try_get(handle) == nullptr) {
            return Result<EntityLink>::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "RTS entity link target is not live", "link.handle"));
        }
        EntityLink result;
        result.handle_ = handle;
        result.bound_  = true;
        return Result<EntityLink>::success(std::move(result));
    }

    /**
     * @brief Rehydrate a serialized runtime link before its target is known.
     * @remarks The returned link must be checked with isStale()/resolve() before use.
     */
    [[nodiscard]] static EntityLink fromHandleForRestore(ecs::EntityHandle handle) noexcept {
        EntityLink result;
        result.handle_ = handle;
        result.bound_  = true;
        return result;
    }

    /** @brief Whether a handle has been assigned, including a stale one. */
    [[nodiscard]] bool isBound() const noexcept { return bound_; }
    /** @brief Whether the assigned target no longer resolves at its generation. */
    [[nodiscard]] bool isStale() const noexcept { return bound_ && ecs::try_get(handle_) == nullptr; }
    /** @brief Resolve the target for this call, or return null when stale. */
    [[nodiscard]] ecs::Entity* resolve() const noexcept { return bound_ ? ecs::try_get(handle_) : nullptr; }
    /** @brief Return the local runtime handle retained by this link. */
    [[nodiscard]] const ecs::EntityHandle& handle() const noexcept { return handle_; }
    /** @brief Clear the relationship after source-side destruction. */
    void reset() noexcept {
        bound_  = false;
        handle_ = {};
    }

private:
    ecs::EntityHandle handle_{};
    bool              bound_ = false;
};

/** @brief Typed link tag for a provider-owned capability or registry entry. */
struct ServiceLinkTag {};

/**
 * @brief Strong key link to an external module-owned service object.
 *
 * A service link retains only a stable logical key, never a provider pointer.
 * The provider owns stale detection for its key; the RTS component remains
 * valid when the optional provider is absent.
 */
template <typename Tag>
class ServiceLink {
public:
    /** @brief Bind a non-empty provider key. */
    [[nodiscard]] static Result<ServiceLink> bind(std::string_view key) {
        if (key.empty()) {
            return Result<ServiceLink>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS service link key must not be empty", "link.key"));
        }
        ServiceLink result;
        result.key_ = key;
        return Result<ServiceLink>::success(std::move(result));
    }

    /** @brief Rehydrate a provider key during restore/import. */
    [[nodiscard]] static ServiceLink fromKeyForRestore(std::string_view key) {
        ServiceLink result;
        result.key_ = key;
        return result;
    }

    /** @brief Whether the link contains a provider key. */
    [[nodiscard]] bool isBound() const noexcept { return !key_.empty(); }
    /** @brief Whether the key is empty and therefore cannot resolve. */
    [[nodiscard]] bool isStale() const noexcept { return key_.empty(); }
    /** @brief Return the stable provider key. */
    [[nodiscard]] const std::string& key() const noexcept { return key_; }
    /** @brief Clear the relationship after source-side destruction. */
    void reset() noexcept { key_.clear(); }

private:
    std::string key_;
};

struct FactionLinkTag {};
struct WeaponLinkTag {};
struct ResourceNodeLinkTag {};
struct BuildingLinkTag {};
struct ContainerLinkTag {};
using FactionLink = EntityLink<FactionLinkTag>;
using WeaponLink  = EntityLink<WeaponLinkTag>;
using ResourceNodeLink = EntityLink<ResourceNodeLinkTag>;
using BuildingLink     = EntityLink<BuildingLinkTag>;
using ContainerLink    = EntityLink<ContainerLinkTag>;

struct SensingLinkTag {};
struct SteeringLinkTag {};
struct CrowdLinkTag {};
struct ActionLinkTag {};
struct SettlementLinkTag {};
struct PlacementLinkTag {};
struct EconomyLinkTag {};
struct AuthorityLinkTag {};
struct SocialLinkTag {};
struct GameEventLinkTag {};
using SensingLink    = ServiceLink<SensingLinkTag>;
using SteeringLink   = ServiceLink<SteeringLinkTag>;
using CrowdLink      = ServiceLink<CrowdLinkTag>;
using ActionLink     = ServiceLink<ActionLinkTag>;
using SettlementLink = ServiceLink<SettlementLinkTag>;
using PlacementLink  = ServiceLink<PlacementLinkTag>;
using EconomyLink    = ServiceLink<EconomyLinkTag>;
using AuthorityLink  = ServiceLink<AuthorityLinkTag>;
using SocialLink     = ServiceLink<SocialLinkTag>;
using GameEventLink  = ServiceLink<GameEventLinkTag>;

/** @brief Canonical attributes component adapter, backed by attributes::AttributeSet. */
class AttributeComponent {
public:
    AttributeComponent();
    ~AttributeComponent();
    AttributeComponent(const AttributeComponent& other);
    AttributeComponent& operator=(const AttributeComponent& other);
    AttributeComponent(AttributeComponent&& other) noexcept;
    AttributeComponent& operator=(AttributeComponent&& other) noexcept;

    /** @brief Set one authoritative base value. */
    [[nodiscard]] Result<void> setBase(std::string_view attribute, double value);
    /** @brief Add to one authoritative base value. */
    [[nodiscard]] Result<void> modifyBase(std::string_view attribute, double delta);
    /** @brief Return whether an attribute exists. */
    [[nodiscard]] bool has(std::string_view attribute) const;
    /** @brief Read the deterministic final value or a caller-supplied default. */
    [[nodiscard]] Result<double> getFinal(std::string_view attribute, double fallback = 0.0) const;
    /** @brief Return the number of canonical modifiers. */
    [[nodiscard]] std::size_t modifierCount() const noexcept;
    /**
     * @brief Return a canonical modifier in deterministic sequence order.
     * @return A nullable borrowed pointer into the canonical AttributeSet, or
     *         `nullptr` when `index` is outside the current range.
     * @ownership Borrowed; AttributeComponent owns the underlying modifier and
     *            the caller must not delete or transfer the pointer.
     * @nullable Yes, for an invalid index.
     * @lifetime Valid until the next component mutation, restore or destruction;
     *           copy the value or query again after that boundary.
     * @thread Owner-thread-affine; no synchronization is provided.
     * @reentrancy Side-effect free and invokes no external callbacks.
     */
    [[nodiscard]] const eve::attributes::AttributeModifier* modifierAt(int index) const noexcept;

    /** @brief Bind the canonical attribute state to an ECS owner generation. */
    [[nodiscard]] Result<void> bindOwner(ecs::EntityHandle owner);
    /** @brief Return whether initial unit stat bases have been installed. */
    [[nodiscard]] bool initialized() const noexcept;
    /** @brief Seed unit stat bases once without replacing existing values. */
    [[nodiscard]] Result<void> initialize(std::span<const eve::attributes::AttributeSnapshotBase> values);
    /** @brief Return whether the canonical attribute owner is stale. */
    [[nodiscard]] bool isStale() const noexcept;
    /** @brief Add a modifier with the common source/priority/sequence contract. */
    [[nodiscard]] Result<eve::attributes::ModifierId> addModifier(eve::attributes::AttributeModifier modifier);
    /** @brief Capture selected values with owner and revision metadata. */
    [[nodiscard]] Result<eve::attributes::AttributeProjectionSnapshot> snapshot(
        std::span<const std::string_view> attributes) const;
    /** @brief Restore selected values after generation and revision checks. */
    [[nodiscard]] Result<void> restore(const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                       Revision                                            expectedRevision);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Generic orders component adapter; the queue remains the sole order owner. */
class OrderComponent {
public:
    /** @brief Complete RTS order projection snapshot; entity handles must be rebound by the owning domain. */
    struct Snapshot {
        std::string queueJson;
        std::map<std::string, CommandSpec> extended;
    };
    OrderComponent();
    ~OrderComponent();
    OrderComponent(const OrderComponent& other);
    OrderComponent& operator=(const OrderComponent& other);
    OrderComponent(OrderComponent&& other) noexcept;
    OrderComponent& operator=(OrderComponent&& other) noexcept;

    /** @brief Append a Move/Attack/Build/Gather command. */
    [[nodiscard]] Result<std::string> enqueue(const CommandSpec& command, int formationSlot = -1);
    /** @brief Replace unfinished commands with one new command. */
    [[nodiscard]] Result<std::string> replace(const CommandSpec& command, int formationSlot = -1);
    /** @brief Project the active order or return NotFound when idle. */
    [[nodiscard]] Result<OrderRecord> current() const;
    /** @brief Complete the active order by its stable queue id. */
    [[nodiscard]] Result<void> complete(std::string_view orderId);
    /** @brief Fail the active order by its stable queue id. */
    [[nodiscard]] Result<void> fail(std::string_view orderId, std::string_view reason);
    /** @brief Cancel an active or queued order by its stable queue id. */
    [[nodiscard]] Result<void> cancel(std::string_view orderId, std::string_view reason);
    /** @brief Return whether no order is currently retained. */
    [[nodiscard]] bool empty() const noexcept;
    /** @brief Remove active, queued, and retained order history. */
    void clear() noexcept;
    /** @brief Return the number of retained order records. */
    [[nodiscard]] std::size_t orderCount() const noexcept;
    /** @brief Serialize the canonical generic queue for persistence/replay. */
    [[nodiscard]] Result<std::string> snapshot() const;
    /** @brief Transactionally restore the canonical generic queue. */
    [[nodiscard]] Result<void> restore(std::string_view json);
    /** @brief Capture the generic queue and every RTS-specific payload atomically. */
    [[nodiscard]] Result<Snapshot> snapshotState() const;
    /** @brief Transactionally restore a complete RTS order projection. */
    [[nodiscard]] Result<void> restoreState(const Snapshot& snapshot);

private:
    friend class RTSProductionActionAdapter;
    [[nodiscard]] orders::CommandQueue* queueForComposition() noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Production adapter whose queue owns all task state. */
class ProductionComponent {
public:
    ProductionComponent();
    ~ProductionComponent();
    ProductionComponent(const ProductionComponent& other);
    ProductionComponent& operator=(const ProductionComponent& other);
    ProductionComponent(ProductionComponent&& other) noexcept;
    ProductionComponent& operator=(ProductionComponent&& other) noexcept;

    /** @brief Enqueue a production task with an injected simulation duration. */
    [[nodiscard]] Result<std::string> enqueue(std::string_view owner, std::string_view kind, std::string_view product,
                                              Duration duration, int priority = 0);
    /** @brief Advance the queue using a caller-owned deterministic step. */
    [[nodiscard]] Result<void> advance(const SimulationStep& step);
    /** @brief Return the number of retained tasks. */
    [[nodiscard]] std::size_t taskCount() const noexcept;
    /** @brief Borrow a retained canonical task by enqueue index. */
    [[nodiscard]] OptionalRef<production::ProductionTask> taskAt(int index);
    /** @brief Borrow a retained canonical task by stable task id. */
    [[nodiscard]] OptionalRef<production::ProductionTask> find(std::string_view taskId);
    /** @brief Pause a queued or running task through the canonical queue. */
    [[nodiscard]] Result<void> pause(std::string_view taskId);
    /** @brief Resume a paused task through the canonical queue. */
    [[nodiscard]] Result<void> resume(std::string_view taskId);
    /** @brief Cancel a non-terminal task through the canonical queue. */
    [[nodiscard]] Result<void> cancel(std::string_view taskId, std::string_view reason = "cancelled");
    /** @brief Copy completed canonical tasks of one kind for domain settlement. */
    [[nodiscard]] std::vector<production::ProductionTask> completed(std::string_view kind) const;
    /** @brief Serialize the canonical production queue for persistence/replay. */
    [[nodiscard]] Result<std::string> snapshot() const;
    /** @brief Transactionally restore the canonical production queue. */
    [[nodiscard]] Result<void> restore(std::string_view json);

private:
    friend class RTSProductionActionAdapter;
    [[nodiscard]] production::WorkQueue* queueForComposition() noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief RTS unit domain root.
 *
 * This class owns no generic gameplay behavior. Its components are the
 * composition points consumed by RTS systems and adapters.
 */
class Unit : public ecs::Entity {
public:
    ENTITY(Unit, ecs::Entity)

    /** @brief Release the entity through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime identity and logical unit definition. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };
    /** @brief Logical unit definition identity. */
    struct Definition {
        LogicalId id;
    };
    /** @brief Canonical attribute state. */
    struct Attributes {
        AttributeComponent values;
    };
    /** @brief Entity-local tags. */
    struct Tags {
        TagSet values;
    };
    /** @brief Lifecycle-only active effects. */
    struct Effects {
        RTSEffectComponent values;
    };
    /** @brief Generic command/order state. */
    struct Orders {
        OrderComponent values;
    };
    /** @brief Sensing provider link. */
    struct Sensing {
        SensingLink link;
    };
    /** @brief Steering provider link. */
    struct Steering {
        SteeringLink link;
    };
    /** @brief Crowd provider link. */
    struct Crowd {
        CrowdLink link;
        float     radius  = 0.5f;
        float     heading = 0.0f;
    };
    /** @brief Weapon entity link. */
    struct Weapon {
        WeaponLink link;
    };
    /** @brief Shared action runtime/provider link. */
    struct Action {
        ActionLink link;
    };
    /** @brief Settlement provider link. */
    struct Settlement {
        SettlementLink link;
    };
    /** @brief Owning faction link. */
    struct Faction {
        rts::FactionLink link;
    };
    /** @brief Authoritative kinematic state used by the phase-one move slice. */
    struct Motion {
        float x             = 0.0f;
        float y             = 0.0f;
        float speed         = 1.0f;
        float arrivalRadius = 0.01f;
        bool  arrived       = true;
        bool  airborne      = false;
    };
    /** @brief RTS projection of a canonical map path; the map provider remains the grid authority. */
    struct Navigation {
        std::vector<WorldPosition> waypoints;
        std::size_t                waypointIndex = 0;
        std::string                plannedOrderId;
        WorldPosition              plannedGoal;
        WorldPosition              patrolOrigin;
        int                        movementPriority = 0;
        bool                       patrolInitialized = false;
        bool                       patrolTowardTarget = true;
        bool                       trafficWaiting = false;
        bool                       unreachable = false;
        bool                       unreachableReported = false;
    };
    /** @brief Vision contribution projected into a faction-owned canonical map FOV provider. */
    struct Vision {
        float sightRange = 0.0f;
        float detectionRange = 0.0f;
        float detectionStrength = 0.0f;
        float radarRange = 0.0f;
        float radarResolution = 0.0f;
        float jammingRange = 0.0f;
        float stealth = 0.0f;
        bool  enabled = true;
        bool  cloaked = false;
    };
    /** @brief Worker gathering and delivery state; balances remain owned by the linked economy account. */
    struct Worker {
        std::string       resourceType;
        ResourceNodeLink  resourceNode;
        BuildingLink      dropoff;
        float             cargo       = 0.0f;
        float             capacity    = 0.0f;
        float             gatherRate  = 0.0f;
        float             buildRate   = 1.0f;
        float             repairRate  = 0.0f;
        bool              autoAssign  = false;
    };
    /** @brief RTS stance and pursuit limits used by automatic combat policies. */
    struct Combat {
        ecs::EntityHandle target{};
        CombatStance      stance           = CombatStance::Defensive;
        float             acquisitionRange = 0.0f;
        float             engagementRange  = 0.0f;
        float             leashRange       = 0.0f;
        float             guardX           = 0.0f;
        float             guardY           = 0.0f;
        bool              guardSet         = false;
        bool              holdPosition     = false;
        bool              attackMove       = false;
        float             suppressionPerShot = 0.0f;
        float             upgradeDamageFactor = 1.0f;
        float             turnRateDegrees = 0.0f;
        float             aimToleranceDegrees = 0.01f;
        float             firingHeight = 1.0f;
        float             targetHeight = 1.0f;
        std::uint64_t     shotSequence = 0;
        std::map<std::string, float> targetPriorities;
    };
    /** @brief Canonical combat durability state consumed by combat::DamageRuntime. */
    struct Durability {
        combat::CombatState state;
        bool                alive = true;
    };
    /** @brief RTS shield layer consumed before canonical health damage and regenerated deterministically. */
    struct Shield {
        float value = 0.0f;
        float capacity = 0.0f;
        float regenRate = 0.0f;
        float regenDelay = 0.0f;
        float cooldown = 0.0f;
    };
    /** @brief Persistent combat experience and deterministic veteran/elite modifiers. */
    struct Veterancy {
        float experience = 0.0f;
        int level = 0;
        float veteranThreshold = 0.0f;
        float eliteThreshold = 0.0f;
        float veteranDamageFactor = 1.0f;
        float eliteDamageFactor = 1.0f;
        float veteranHealthFactor = 1.0f;
        float eliteHealthFactor = 1.0f;
    };
    /** @brief Command-network policy and derived connection state for a mobile unit or relay. */
    struct Command {
        float range = 0.0f;
        int capacity = 0;
        int load = 0;
        int cost = 1;
        int priority = 0;
        float jammingRange = 0.0f;
        float outOfCommandSpeedFactor = 1.0f;
        float outOfCommandDamageFactor = 1.0f;
        bool requiresCommand = false;
        bool relayRequiresUplink = false;
        bool relayActive = false;
        bool jammed = false;
        bool inCommand = true;
        ecs::EntityHandle source{};
        ecs::EntityHandle uplink{};
    };
    /** @brief Ability cooldowns and an optional deterministic cast/channel in progress. */
    struct Abilities {
        struct Cooldown { std::string id; float remaining = 0.0f; };
        struct Channel {
            AbilitySpec spec;
            ecs::EntityHandle target{};
            WorldPosition point;
            double startingHealth = 0.0;
            float remaining = 0.0f;
            float tickRemaining = 0.0f;
        };
        std::vector<Cooldown> cooldowns;
        std::optional<Channel> channel;
    };
    /** @brief Capture contribution while executing a Capture order. */
    struct Capture {
        float rate = 0.0f;
    };
    /** @brief Unit containment state for transports and building garrisons. */
    struct Containment {
        ContainerLink                  container;
        std::size_t                    capacity = 0;
        std::vector<ecs::EntityHandle> occupants;
    };
    /** @brief Tactical ammunition logistics; weapon ammunition remains authoritative in WeaponEntity. */
    struct Supply {
        float stock = 0.0f;
        float capacity = 0.0f;
        float range = 0.0f;
        float transferRate = 0.0f;
        float transferProgress = 0.0f;
        float reservedStock = 0.0f;
        float autoThreshold = 0.5f;
        float priority = 1.0f;
        bool  autoDispatch = false;
        bool  relayEnabled = false;
        bool  returning = false;
        bool  rendezvousActive = false;
        bool  convoyWaiting = false;
        std::size_t convoyIndex = 0;
        ecs::EntityHandle assignedTarget{};
        ecs::EntityHandle convoyLeader{};
        WorldPosition returnPoint;
        WorldPosition rendezvousPoint;
        float rendezvousThreat = 0.0f;
        bool rendezvousAvoidedThreat = false;
        float routeThreat = 0.0f;
        bool routeAvoidedThreat = false;
    };
    /** @brief Suppression, recovery, retreat policy, and friendly morale aura. */
    struct Morale {
        float suppression = 0.0f;
        float capacity = 0.0f;
        float recoveryRate = 0.0f;
        float suppressedSpeedFactor = 1.0f;
        float suppressedDamageFactor = 1.0f;
        float retreatThreshold = 0.8f;
        float retreatDistance = 6.0f;
        float auraRange = 0.0f;
        float auraSuppressionFactor = 1.0f;
        float auraRecoveryBonus = 0.0f;
        bool active = false;
        bool retreatEnabled = false;
        bool retreating = false;
    };
    /** @brief Indirect-fire deployment and deterministic area-fire policy. */
    struct Artillery {
        float deployTime = 0.0f;
        float deployRemaining = 0.0f;
        float shootAndScootDistance = 0.0f;
        float previousX = 0.0f;
        float previousY = 0.0f;
        WorldPosition relocationTarget;
        WorldPosition departedPosition;
        float relocationThreat = 0.0f;
        int relocationConflictCount = 0;
        bool  positionInitialized = false;
        bool  relocating = false;
        bool  hasDepartedPosition = false;
        std::uint32_t shotSequence = 0;
        int suppressionShotsRemaining = -1;
        ecs::EntityHandle fireSupportRequester{};
        ecs::EntityHandle observedFireSpotter{};
        bool usingObservedFire = false;
        SimulationTick lastFireTick{};
        WorldPosition lastFirePosition;
        bool autoCounterBattery = false;
        std::uint64_t counterBatteryWindowTicks = 0;
    };
    /** @brief Escort geometry and combat-group coordination state. */
    struct Tactics {
        std::uint64_t combatGroup = 0;
        int threatSector = 0;
        float coordinatedVolleyInterval = 0.0f;
        float volleyReleaseRemaining = 0.0f;
        float fireControlEffectiveness = 1.0f;
        bool volleyHolding = false;
        ecs::EntityHandle escortTarget{};
        float escortOffsetX = 0.0f;
        float escortOffsetY = 0.0f;
        float protectionRange = 8.0f;
        float guardX = 0.0f;
        float guardY = 0.0f;
        bool  guardSet = false;
        int escortScreenSector = 0;
        bool escortSectorMatched = false;
        bool escortReinforcing = false;
        int escortReinforcementSector = 0;
        bool escortRearGuard = false;
        SubjectRef escortInterceptTarget;
        std::uint32_t escortHandoffCount = 0;
        int retreatFireTeam = -1;
        float retreatCoverElapsed = 0.0f;
        bool retreatCovering = false;
    };
    /** @brief Upgrade ids already projected onto this unit, preventing repeated multiplicative application. */
    struct Technology {
        std::vector<std::string> applied;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(Attributes, attributes)
    COMPONENT(Tags, tags)
    COMPONENT(Effects, effects)
    COMPONENT(Orders, orders)
    COMPONENT(Sensing, sensing)
    COMPONENT(Steering, steering)
    COMPONENT(Crowd, crowd)
    COMPONENT(Weapon, weapon)
    COMPONENT(Action, action)
    COMPONENT(Settlement, settlement)
    COMPONENT(Faction, faction)
    COMPONENT(Motion, motion)
    COMPONENT(Navigation, navigation)
    COMPONENT(Vision, vision)
    COMPONENT(Worker, worker)
    COMPONENT(Combat, combat)
    COMPONENT(Durability, durability)
    COMPONENT(Shield, shield)
    COMPONENT(Veterancy, veterancy)
    COMPONENT(Command, command)
    COMPONENT(Abilities, abilities)
    COMPONENT(Capture, capture)
    COMPONENT(Containment, containment)
    COMPONENT(Supply, supply)
    COMPONENT(Morale, morale)
    COMPONENT(Artillery, artillery)
    COMPONENT(Tactics, tactics)
    COMPONENT(Technology, technology)

    /**
     * @brief Create a unit and initialize its self handle and identity.
     * @param subject Stable persistent subject identity; may be nil for a low-level ECS fixture.
     * @param definition Optional logical unit definition.
     * @return Borrowed nullable pointer to the ECS-owned unit; null means creation failed.
     * @ownership The current ECS world owns the entity; callers must release it through ECS and must not delete it.
     * @lifetime Valid until ECS destruction of the entity or its world; retain an EntityHandle across frames instead.
     * @thread Call on the owning ECS/simulation thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter ECS mutation while using the returned
     * pointer.
     */
    [[nodiscard]] static Unit* createUnit(SubjectRef subject = {}, LogicalId definition = {});
};

/** @brief RTS building domain root with placement and production composition. */
class Building : public ecs::Entity {
public:
    ENTITY(Building, ecs::Entity)

    /** @brief Release the entity through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };
    /** @brief Logical building definition identity. */
    struct Definition {
        LogicalId id;
    };
    /** @brief Placement world key and authoritative cell state. */
    struct Placement {
        PlacementLink link;
        int           cellX    = 0;
        int           cellY    = 0;
        float         worldX   = 0.0f;
        float         worldY   = 0.0f;
        float         rotation = 0.0f;
        bool          placed   = false;
    };
    /** @brief Canonical production queue. */
    struct Production {
        ProductionComponent values;
    };
    /** @brief Economy account/provider link. */
    struct Economy {
        EconomyLink link;
    };
    /** @brief Generic building orders. */
    struct Orders {
        OrderComponent values;
    };
    /** @brief Building-local tags. */
    struct Tags {
        TagSet values;
    };
    /** @brief Lifecycle-only effects. */
    struct Effects {
        RTSEffectComponent values;
    };
    /** @brief Settlement provider link. */
    struct Settlement {
        SettlementLink link;
    };
    /** @brief Owning faction link. */
    struct Faction {
        rts::FactionLink link;
    };
    /** @brief Construction progress and generation-checked assigned builders. */
    struct Construction {
        float                          progress = 1.0f;
        float                          buildTimeSeconds = 0.0f;
        bool                           paused = false;
        std::vector<ecs::EntityHandle> builders;
    };
    /** @brief Building health and repair economy configuration. */
    struct Integrity {
        combat::CombatState state;
        bool        alive               = true;
        float       repairCostPerHealth = 0.0f;
        float       repairCostRemainder = 0.0f;
        std::string repairResource;
    };
    /** @brief RTS shield layer for structures; canonical CombatState remains health authority. */
    struct Shield {
        float value = 0.0f;
        float capacity = 0.0f;
        float regenRate = 0.0f;
        float regenDelay = 0.0f;
        float cooldown = 0.0f;
    };
    /** @brief Deterministic contested capture state. */
    struct Capture {
        bool              capturable = false;
        float             durationSeconds = 5.0f;
        float             progress = 0.0f;
        ecs::EntityHandle capturingFaction{};
        bool              blockedByGarrison = false;
    };
    /** @brief Resource delivery policy; actual balances remain in the economy provider. */
    struct Dropoff {
        std::vector<std::string> acceptedResources;
        float                    radius = 1.0f;
    };
    /** @brief Rally order copied to newly produced units. */
    struct Rally {
        bool          enabled = false;
        CommandSpec   command;
        std::uint64_t combatGroup = 0;
        ecs::EntityHandle transport{};
        std::size_t minimumTransportLoad = 1;
        bool transportActive = false;
        bool productionSpawnBlocked = false;
        std::string blockedProductionTask;
        std::vector<std::string> settledProductionTasks;
        std::vector<ecs::EntityHandle> reinforcements;
        std::size_t reinforcementLimit = 0;
        std::map<std::string, std::size_t> reinforcementTypeLimits;
        std::map<std::string, int> reinforcementTypePriorities;
        std::map<std::string, std::string> reinforcementFallbacks;
        bool reinforcementCapped = false;
        std::string reinforcementPolicyPausedTask;
        float reinforcementAutoCancelDelay = 0.0f;
        float reinforcementCappedSeconds = 0.0f;
    };
    /** @brief Canonical weapon entity used by an armed building. */
    struct Weapon {
        WeaponLink link;
    };
    /** @brief Automatic targeting policy for a stationary defensive building. */
    struct Combat {
        ecs::EntityHandle target{};
        ecs::EntityHandle airDefenseNetworkRoot{};
        float acquisitionRange = 0.0f;
        float engagementRange = 0.0f;
        float airDefenseNetworkRange = 0.0f;
        std::size_t airDefenseNetworkSize = 0;
        float turnRateDegrees = 0.0f;
        float aimToleranceDegrees = 0.01f;
        float firingHeight = 2.0f;
        float targetHeight = 1.5f;
        std::uint64_t shotSequence = 0;
        std::map<std::string, float> targetPriorities;
    };
    /** @brief Building garrison capacity and generation-checked occupants. */
    struct Garrison {
        std::size_t                    capacity = 0;
        float                          damageBonusPerOccupant = 0.0f;
        std::vector<ecs::EntityHandle> occupants;
    };
    /** @brief Static ammunition stock and transfer policy for completed buildings. */
    struct Supply {
        float stock = 0.0f;
        float capacity = 0.0f;
        float range = 0.0f;
        float transferRate = 0.0f;
        float transferProgress = 0.0f;
        std::string productionResource;
        std::int64_t productionCostPerRound = 0;
        float productionRate = 0.0f;
        float productionProgress = 0.0f;
    };
    /** @brief Static vision contribution projected into a faction-owned canonical map FOV provider. */
    struct Vision {
        float sightRange = 0.0f;
        float detectionRange = 0.0f;
        float detectionStrength = 0.0f;
        float radarRange = 0.0f;
        float radarResolution = 0.0f;
        float jammingRange = 0.0f;
        bool  enabled = true;
    };
    /** @brief Upgrade ids already projected onto this building. */
    struct Technology {
        std::vector<std::string> applied;
    };
    /** @brief RTS infrastructure projection; economy balances remain owned by the linked provider. */
    struct Infrastructure {
        float powerProduced = 0.0f;
        float powerConsumed = 0.0f;
        int   powerPriority = 0;
        bool  powered = true;
        float buildInfluenceRadius = 0.0f;
        std::string incomeResource;
        float incomeRate = 0.0f;
        float incomeProgress = 0.0f;
    };
    /** @brief Static command provider and hostile jamming policy. */
    struct Command {
        float range = 0.0f;
        int capacity = 0;
        int load = 0;
        float jammingRange = 0.0f;
        bool jammed = false;
        bool active = false;
    };
    /** @brief Last indirect-fire exposure for static artillery and counter-battery observation. */
    struct IndirectFire {
        SimulationTick lastFireTick{};
        WorldPosition lastFirePosition;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Definition, definition)
    COMPONENT(Placement, placement)
    COMPONENT(Production, production)
    COMPONENT(Economy, economy)
    COMPONENT(Orders, orders)
    COMPONENT(Tags, tags)
    COMPONENT(Effects, effects)
    COMPONENT(Settlement, settlement)
    COMPONENT(Faction, faction)
    COMPONENT(Construction, construction)
    COMPONENT(Integrity, integrity)
    COMPONENT(Shield, shield)
    COMPONENT(Capture, capture)
    COMPONENT(Dropoff, dropoff)
    COMPONENT(Rally, rally)
    COMPONENT(Weapon, weapon)
    COMPONENT(Combat, combat)
    COMPONENT(Garrison, garrison)
    COMPONENT(Supply, supply)
    COMPONENT(Vision, vision)
    COMPONENT(Technology, technology)
    COMPONENT(Infrastructure, infrastructure)
    COMPONENT(Command, command)
    COMPONENT(IndirectFire, indirectFire)

    /**
     * @brief Create a building and initialize its self handle and identity.
     * @param subject Stable persistent subject identity; may be nil for a low-level ECS fixture.
     * @param definition Optional logical building definition.
     * @return Borrowed nullable pointer to the ECS-owned building; null means creation failed.
     * @ownership The current ECS world owns the entity; callers must release it through ECS and must not delete it.
     * @lifetime Valid until ECS destruction of the entity or its world; retain an EntityHandle across frames instead.
     * @thread Call on the owning ECS/simulation thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter ECS mutation while using the returned
     * pointer.
     */
    [[nodiscard]] static Building* createBuilding(SubjectRef subject = {}, LogicalId definition = {});
};

/** @brief Harvestable RTS resource node; deposited balances are owned by an external resource account. */
class ResourceNode : public ecs::Entity {
public:
    ENTITY(ResourceNode, ecs::Entity)

    /** @brief Release the node through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime and persistent identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };
    /** @brief Authoritative world position and interaction radius. */
    struct Position {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 1.0f;
    };
    /** @brief Authoritative remaining resource stock. */
    struct Stock {
        std::string resourceType;
        float       remaining = 0.0f;
        float       maximum = 0.0f;
        bool        infinite = false;
    };
    /** @brief Concurrent worker capacity and current generation-checked assignments. */
    struct Harvest {
        std::size_t                    capacity = 1;
        std::vector<ecs::EntityHandle> workers;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Position, position)
    COMPONENT(Stock, stock)
    COMPONENT(Harvest, harvest)

    /** @brief Create a resource node and initialize all composition components. */
    [[nodiscard]] static ResourceNode* createResourceNode(SubjectRef subject = {});
};

/** @brief Player domain root; selection is RTS-local, authority/economy/social are links. */
class Player : public ecs::Entity {
public:
    ENTITY(Player, ecs::Entity)

    /** @brief Release the entity through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };
    /** @brief Authority provider link. */
    struct Authority {
        AuthorityLink link;
    };
    /** @brief Economy account/provider link. */
    struct Economy {
        EconomyLink link;
    };
    /** @brief Social graph/provider link. */
    struct Social {
        SocialLink link;
    };
    /** @brief RTS-owned selection handles; handles are generation checked on use. */
    struct Selection {
        std::vector<ecs::EntityHandle> units;
        std::vector<ecs::EntityHandle> buildings;
    };
    /** @brief Event stream/provider link. */
    struct GameEvent {
        GameEventLink link;
    };
    COMPONENT(Identity, identity)
    COMPONENT(Authority, authority)
    COMPONENT(Economy, economy)
    COMPONENT(Social, social)
    COMPONENT(Selection, selection)
    COMPONENT(GameEvent, eventStream)

    /**
     * @brief Create a player and initialize its self handle and identity.
     * @return Borrowed nullable pointer to the ECS-owned player; null means creation failed.
     * @ownership The current ECS world owns the entity; callers must release it through ECS and must not delete it.
     * @lifetime Valid until ECS destruction of the entity or its world; retain an EntityHandle across frames instead.
     * @thread Call on the owning ECS/simulation thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter ECS mutation while using the returned
     * pointer.
     */
    [[nodiscard]] static Player* createPlayer(SubjectRef subject = {});
};

/** @brief Faction domain root; membership is a set of typed runtime handles. */
class Faction : public ecs::Entity {
public:
    ENTITY(Faction, ecs::Entity)

    /** @brief Release the entity through the ECS generation boundary. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef        subject;
        std::string       displayName;
    };
    /** @brief Authority provider link. */
    struct Authority {
        AuthorityLink link;
    };
    /** @brief Economy account/provider link. */
    struct Economy {
        EconomyLink link;
    };
    /** @brief Social graph/provider link. */
    struct Social {
        SocialLink link;
    };
    /** @brief Authoritative faction membership handles. */
    struct Members {
        std::vector<ecs::EntityHandle> units;
        std::vector<ecs::EntityHandle> buildings;
    };
    /** @brief Event stream/provider link. */
    struct GameEvent {
        GameEventLink link;
    };
    /** @brief Deterministic high-level production and attack policy for an AI-controlled faction. */
    struct Strategy {
        LogicalId workerDefinition;
        LogicalId armyDefinition;
        LogicalId targetBuildingDefinition;
        int desiredWorkers = 0;
        int attackThreshold = 1;
        float thinkInterval = 1.0f;
        float thinkAccumulator = 0.0f;
        float formationSpacing = 1.1f;
        bool enabled = false;
    };
    /** @brief Deterministic idle-worker assignment policy for construction and repair. */
    struct Workforce {
        bool autoConstruction = false;
        bool autoRepair = false;
        std::size_t maxBuildersPerSite = 2;
        std::size_t maxRepairersPerBuilding = 2;
        std::size_t reserveWorkers = 0;
    };
    /** @brief Cross-factory resource floors reserved for production at or above a priority threshold. */
    struct ProductionPolicy {
        struct Reserve {
            std::int64_t amount = 0;
            int minimumPriority = 0;
        };
        std::map<std::string, Reserve> resourceReserves;
    };
    /** @brief Persistent last-known enemy contacts derived from the faction's canonical FOV provider. */
    struct Intel {
        struct Contact {
            SubjectRef    subject;
            std::string   kind;
            WorldPosition position;
            double        ageSeconds = 0.0;
            bool          visible = false;
            bool          detected = false;
        };
        /** @brief Whether command authorization must require a current visible, detected contact. */
        bool enabled = false;
        std::vector<Contact> contacts;
    };
    /** @brief Completed research definitions and canonical production tasks already consumed. */
    struct Technology {
        std::vector<std::string> unlocked;
        std::vector<std::string> consumedTasks;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Authority, authority)
    COMPONENT(Economy, economy)
    COMPONENT(Social, social)
    COMPONENT(Members, members)
    COMPONENT(GameEvent, eventStream)
    COMPONENT(Strategy, strategy)
    COMPONENT(Workforce, workforce)
    COMPONENT(ProductionPolicy, productionPolicy)
    COMPONENT(Intel, intel)
    COMPONENT(Technology, technology)

    /**
     * @brief Create a faction and initialize its self handle and identity.
     * @return Borrowed nullable pointer to the ECS-owned faction; null means creation failed.
     * @ownership The current ECS world owns the entity; callers must release it through ECS and must not delete it.
     * @lifetime Valid until ECS destruction of the entity or its world; retain an EntityHandle across frames instead.
     * @thread Call on the owning ECS/simulation thread.
     * @reentrancy Creation does not invoke user callbacks; do not re-enter ECS mutation while using the returned
     * pointer.
     */
    [[nodiscard]] static Faction* createFaction(SubjectRef subject = {});
};

/** @brief Supported deterministic RTS victory policies. */
enum class VictoryRule : std::uint8_t { Annihilation, DestroyHeadquarters, ResourceTarget };
/** @brief Lifecycle of one independent RTS match root. */
enum class MatchPhase : std::uint8_t { Setup, Running, Finished };

/** @brief Independent match composition root; factions may participate in different matches. */
class Match : public ecs::Entity {
public:
    ENTITY(Match, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    /** @brief Runtime and persistent identity. */
    struct Identity {
        ecs::EntityHandle self{};
        SubjectRef subject;
    };
    /** @brief Configured victory rule; immutable while the match is running. */
    struct Rules {
        VictoryRule rule = VictoryRule::Annihilation;
        std::string archetype;
        double targetValue = 0.0;
    };
    /** @brief One faction's team and elimination projection. */
    struct Participants {
        struct Entry {
            FactionLink faction;
            int team = 0;
            bool eliminated = false;
            bool surrendered = false;
            std::string reason;
        };
        std::vector<Entry> entries;
    };
    /** @brief Authoritative match outcome. */
    struct State {
        MatchPhase phase = MatchPhase::Setup;
        int winningTeam = -1;
        std::uint64_t updateSequence = 0;
    };
    /** @brief Deterministic retained lifecycle events. */
    struct Events {
        struct Event {
            std::uint64_t sequence = 0;
            std::string kind;
            SubjectRef faction;
            int team = -1;
            std::string reason;
        };
        std::vector<Event> values;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Rules, rules)
    COMPONENT(Participants, participants)
    COMPONENT(State, state)
    COMPONENT(Events, events)

    /** @brief Create a match and initialize every composition component. */
    [[nodiscard]] static Match* createMatch(SubjectRef subject = {});
};

}  // namespace eve::rts
