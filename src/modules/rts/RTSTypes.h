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
#include "common/Time.h"
#include "rts/RTSEffects.h"

#include <cstddef>
#include <cstdint>
#include <memory>
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

/** @brief Commands shared by movement, combat, construction and gathering. */
enum class OrderKind : std::uint8_t {
    Move,
    Attack,
    Build,
    Gather,
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

    /** @brief Validate coordinates and command metadata without mutating state. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Owning projection of the active generic order used by RTS systems. */
struct OrderRecord {
    std::string   id;
    OrderKind     kind = OrderKind::Move;
    WorldPosition target;
    std::string   definitionId;
    int           formationSlot = -1;
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
using FactionLink = EntityLink<FactionLinkTag>;
using WeaponLink  = EntityLink<WeaponLinkTag>;

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
    /** @brief Return the number of retained order records. */
    [[nodiscard]] std::size_t orderCount() const noexcept;

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

    COMPONENT(Identity, identity)
    COMPONENT(Authority, authority)
    COMPONENT(Economy, economy)
    COMPONENT(Social, social)
    COMPONENT(Members, members)
    COMPONENT(GameEvent, eventStream)

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

}  // namespace eve::rts
