#pragma once

/**
 * @file RTS.h
 * @brief RTS module owner and phase-one composition profile entry point.
 */

#include "common/Module.h"
#include "common/GameplayControl.h"
#include "common/Snapshot.h"
#include "rts/RTSAttributes.h"
#include "rts/RTSArchetype.h"
#include "rts/RTSContent.h"
#include "rts/RTSTech.h"
#include "rts/RTSMatch.h"
#include "rts/RTSReplay.h"
#include "rts/RTSEffects.h"
#include "rts/RTSProductionAction.h"
#include "rts/RTSSystems.h"
#include "rts/RTSSnapshot.h"

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace eve::rts {

/** @brief Owning transient projection of one canonical damage transaction completed this RTS frame. */
struct RTSFrameDamageEvent {
    std::uint64_t sequence = 0;
    SimulationTick tick;
    DamageChannel channel = DamageChannel::Weapon;
    combat::DamageRequest request;
    combat::DamageOutcome outcome;
};

/** @brief Owning transient projection of one committed canonical weapon lifecycle event. */
struct RTSFrameCombatEvent {
    std::uint64_t sequence = 0;
    SimulationTick tick;
    CombatFireEvent event;
};

/** @brief Owning transient projection of one committed non-combat RTS lifecycle event. */
struct RTSFrameLifecycleEvent {
    std::uint64_t sequence = 0;
    SimulationTick tick;
    LifecycleEvent event;
};

/**
 * @brief Owns RTS root entities and coordinates the phase-one systems.
 *
 * The module owns only the ECS handles it created. Components remain the
 * authoritative state owners, while action lifecycle state remains owned by
 * the caller-provided action::ActionRuntime through IRTSActionExecutor.
 */
class RTS : public Module, public IGameplayControlProvider {
public:
    Module_REG(RTS);

    /** @brief Construct an empty RTS composition profile. */
    RTS();
    /** @brief Destroy only live entities created through this module. */
    ~RTS() override;

    /** @copydoc IGameplayControlProvider::gameplayDomain */
    [[nodiscard]] std::string_view gameplayDomain() const noexcept override;
    /** @copydoc IGameplayControlProvider::observeGameplay */
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession& session,
                                                               SubjectRef instance) const override;
    /** @copydoc IGameplayControlProvider::availableGameplayActions */
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession& session, SubjectRef instance, SubjectRef subject) const override;
    /** @copydoc IGameplayControlProvider::submitGameplay */
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession& session,
                                                                 SubjectRef instance,
                                                                 const GameplayCommand& command) override;
    /** @copydoc IGameplayControlProvider::advanceGameplay */
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session,
                                                               SubjectRef instance,
                                                               const SimulationStep& step) override;
    /** @copydoc IGameplayControlProvider::gameplayEvents */
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession& session,
                                                                     SubjectRef instance,
                                                                     std::uint64_t afterSequence) const override;

    /**
     * @brief Create and own one Unit root.
     * @param subject Stable persistent subject identity.
     * @param definition Optional logical unit definition.
     * @return Borrowed pointer valid until the entity is destroyed or this
     *         module is destroyed.
     */
    [[nodiscard]] Result<Unit*> newUnit(SubjectRef subject, LogicalId definition = {});
    /**
     * @brief Create and own one Building root.
     * @param subject Stable persistent subject identity.
     * @param definition Optional logical building definition.
     * @return Borrowed pointer valid until the entity is destroyed or this
     *         module is destroyed.
     */
    [[nodiscard]] Result<Building*> newBuilding(SubjectRef subject, LogicalId definition = {});
    /** @brief Create and own one harvestable resource node. */
    [[nodiscard]] Result<ResourceNode*> newResourceNode(SubjectRef subject, std::string resourceType,
                                                        float amount, WorldPosition position,
                                                        std::size_t workerCapacity = 1);
    /** @brief Create and own one Player root with a valid subject identity. */
    [[nodiscard]] Result<Player*> newPlayer(SubjectRef subject);
    /** @brief Create and own one Faction root with a valid subject identity. */
    [[nodiscard]] Result<Faction*> newFaction(SubjectRef subject);
    /** @brief Atomically create a Unit, bind its owning faction and register canonical membership. */
    [[nodiscard]] Result<Unit*> newFactionUnit(Faction& faction, SubjectRef subject,
                                                LogicalId definition = {});
    /** @brief Atomically create a Building, bind its owning faction and register canonical membership. */
    [[nodiscard]] Result<Building*> newFactionBuilding(Faction& faction, SubjectRef subject,
                                                        LogicalId definition = {});
    /** @brief Create and own one independent RTS match composition root. */
    [[nodiscard]] Result<Match*> newMatch(SubjectRef subject);
    /** @brief Configure one owned setup-phase match victory rule. */
    [[nodiscard]] Result<void> configureMatch(Match& match, VictoryRule rule,
                                               std::string archetype = {}, double targetValue = 0.0) const;
    /** @brief Add one owned faction to one owned setup-phase match. */
    [[nodiscard]] Result<void> addMatchParticipant(Match& match, Faction& faction, int team) const;
    /** @brief Start one owned match after participant/rule validation. */
    [[nodiscard]] Result<void> startMatch(Match& match) const;
    /** @brief Surrender one owned faction from one owned running match. */
    [[nodiscard]] Result<void> surrenderMatch(Match& match, Faction& faction) const;
    /** @brief Return an owning deterministic projection of one match lifecycle. */
    [[nodiscard]] Result<Value> inspectMatch(Match& match) const;

    /** @brief Resolve a module-owned unit by persistent subject identity. */
    [[nodiscard]] Unit* findUnit(SubjectRef subject) const noexcept;
    /** @brief Resolve a module-owned building by persistent subject identity. */
    [[nodiscard]] Building* findBuilding(SubjectRef subject) const noexcept;
    /** @brief Resolve a module-owned resource node by persistent subject identity. */
    [[nodiscard]] ResourceNode* findResourceNode(SubjectRef subject) const noexcept;
    /** @brief Resolve a module-owned faction by persistent subject identity. */
    [[nodiscard]] Faction* findFaction(SubjectRef subject) const noexcept;
    /** @brief Resolve a module-owned match by persistent subject identity. */
    [[nodiscard]] Match* findMatch(SubjectRef subject) const noexcept;
    /** @brief Capture all module-owned RTS root state using stable subject relationships. */
    [[nodiscard]] Result<RTSStateSnapshot> snapshotState() const;
    /** @brief Validate and atomically restore an exact-topology RTS root snapshot. */
    [[nodiscard]] Result<void> restoreState(const RTSStateSnapshot& snapshot);
    /**
     * @brief Materialize a complete snapshot into an empty RTS module.
     * @param snapshot Stable root snapshot whose relationships are rebound after creation.
     * @return Success when every root and relationship was restored; failures leave the module empty.
     */
    [[nodiscard]] Result<void> rebuildState(const RTSStateSnapshot& snapshot);
    /** @brief Encode the stable RTS synchronization projection as deterministic compact JSON. */
    [[nodiscard]] Result<std::string> canonicalStateJson() const;
    /** @brief Hash the deterministic synchronization projection through the engine snapshot boundary. */
    [[nodiscard]] Result<ContentId> stateHash(const SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Fan one common command out through a player's selected unit handles.
     * @param selection Player-owned generation-checked selection projection.
     * @param command Generic Move/Attack/Build/Gather command.
     * @param formation Deterministic formation layout.
     * @return Accepted order ids in selection order.
     */
    [[nodiscard]] Result<FanOutReceipt> fanOut(Player::Selection& selection, const CommandSpec& command,
                                               const FormationSpec& formation) const;

    /**
     * @brief Resolve stable unit identities and fan out one generic command.
     * @param subjects Stable identities in deterministic selection order.
     * @param command Generic command accepted by the shared order component.
     * @param formation Deterministic formation layout.
     * @return Accepted order ids, or an atomic failure when any identity is missing.
     */
    [[nodiscard]] Result<FanOutReceipt> commandUnits(std::span<const SubjectRef> subjects,
                                                      const CommandSpec& command,
                                                      const FormationSpec& formation = {}) const;

    /** @brief Set automatic combat stance and pursuit leash for module-owned units atomically. */
    [[nodiscard]] Result<void> setUnitStance(std::span<const SubjectRef> subjects,
                                             CombatStance stance, float leashRange) const;
    /** @brief Set collision/traffic contention priority for module-owned units atomically. */
    [[nodiscard]] Result<void> setUnitMovementPriority(std::span<const SubjectRef> subjects,
                                                       int priority) const;
    /** @brief Atomically bind one worker to a compatible resource node and friendly dropoff. */
    [[nodiscard]] Result<void> assignWorker(Unit& worker, ResourceNode& node,
                                             Building& dropoff) const;
    /** @brief Enable or disable deterministic idle-worker assignment for module-owned units atomically. */
    [[nodiscard]] Result<void> setWorkerAutoAssignment(std::span<const SubjectRef> subjects,
                                                        bool enabled) const;
    /** @brief Configure deterministic idle-worker construction and repair assignment for one faction. */
    [[nodiscard]] Result<void> configureWorkforce(
        Faction& faction, bool autoConstruction, int maxBuildersPerSite,
        bool autoRepair, int maxRepairersPerBuilding, int reserveWorkers) const;
    /** @brief Replace one capable friendly worker's orders with construction of an unfinished building. */
    [[nodiscard]] Result<std::string> assignBuilder(Unit& worker, Building& building) const;
    /** @brief Add or remove reserve ammunition through the unit's canonical weapon ammunition owner. */
    [[nodiscard]] Result<int> addUnitReserveAmmo(Unit& unit, int rounds) const;
    /** @brief Add or remove mobile ammunition supply, clamped to the unit's carrying capacity. */
    [[nodiscard]] Result<float> addUnitAmmoSupply(Unit& unit, float rounds) const;
    /** @brief Add or remove depot ammunition supply, clamped to the building's storage capacity. */
    [[nodiscard]] Result<float> addBuildingAmmoSupply(Building& building, float rounds) const;
    /** @brief Enable or disable automatic supply dispatch and cancel an active supply mission when disabled. */
    [[nodiscard]] Result<void> setUnitAutoResupply(Unit& unit, bool enabled) const;
    /** @brief Assign persistent distributed fire along a non-degenerate world-space corridor. */
    [[nodiscard]] Result<FanOutReceipt> suppressArea(
        std::span<const SubjectRef> subjects, WorldPosition start, WorldPosition end,
        float width = 1.0f, int shotsPerUnit = 0) const;
    /** @brief Assign stable circular guard slots around one live friendly unit or building. */
    [[nodiscard]] Result<FanOutReceipt> escortUnits(
        std::span<const SubjectRef> subjects, SubjectRef protectedSubject,
        float guardRadius = 6.0f, float spacing = 1.5f) const;

    /**
     * @brief Project live module roots into an owning script/debug value.
     * @return Deterministically ordered arrays of units, buildings, resource nodes and factions.
     */
    [[nodiscard]] Value inspectState() const;

    /**
     * @brief Project damage transactions completed since the latest step boundary.
     * @return Owning presentation data excluded from deterministic snapshots and state hashes.
     */
    [[nodiscard]] Value inspectFrameEvents() const;

    /** @brief Read a canonical selected combat attribute through the RTS facade. */
    [[nodiscard]] Result<double> readUnitAttribute(Unit& unit, std::string_view attribute) const;
    /** @brief Set a canonical selected combat attribute through the RTS facade. */
    [[nodiscard]] Result<void> setUnitAttribute(Unit& unit, std::string_view attribute, double value) const;
    /** @brief Apply a typed effect to a module-owned Unit component. */
    [[nodiscard]] Result<effects::EffectHandle> applyEffect(Unit& unit, const RTSEffectDefinition& definition) const;
    /** @brief Apply a typed effect to a module-owned Building component. */
    [[nodiscard]] Result<effects::EffectHandle> applyEffect(Building&                  building,
                                                            const RTSEffectDefinition& definition) const;
    /** @brief Restore health on one live owned unit or building, capped by canonical maximum health. */
    [[nodiscard]] Result<double> heal(SubjectRef source, SubjectRef target, double amount) const;
    /** @brief Resolve and apply one canonical data-defined status effect to an owned combat root. */
    [[nodiscard]] Result<effects::EffectHandle> applyStatusEffect(
        SubjectRef source, SubjectRef target, std::string effect, double durationOverride = -1.0);

    /**
     * @brief Submit a building production action through the canonical transaction facade.
     * @param building Borrowed module-owned building.
     * @param action Borrowed shared ActionRuntime.
     * @param account Borrowed authoritative resource account.
     * @param cost Positive resource cost copied into the transaction.
     * @param product Stable production product identifier.
     * @param duration Positive deterministic production duration.
     * @param productionKind Production queue kind, defaulting to `unit`.
     * @param priority Production queue priority.
     * @param transactionId Optional transaction correlation id.
     * @return Committed build receipt or a checked failure with no partial state.
     */
    [[nodiscard]] Result<RTSBuildReceipt> build(Building& building, action::ActionRuntime& action,
                                                resource::IResourceAccount& account, resource::CostSpec cost,
                                                std::string product, Duration duration,
                                                std::string productionKind = "unit", int priority = 0,
                                                std::string transactionId = {});

    /** @brief Configure or clear a faction-wide production resource floor shared by all factories. */
    [[nodiscard]] Result<void> setProductionResourceReserve(
        Faction& faction, std::string resource, std::int64_t amount, int minimumPriority);

    /** @brief Atomically cancel one paid production task and refund its complete cost. */
    [[nodiscard]] Result<RTSCancelProductionReceipt> cancelProduction(
        Building& building, resource::IResourceAccount& account, std::string productionTaskId,
        std::string orderId, resource::CostSpec refund, std::string reason = "production cancelled");

    /** @brief Atomically import an RTS content pack into the caller-owned canonical definition registry. */
    [[nodiscard]] Result<ContentImportReceipt> loadContent(definitions::DefinitionRegistry& registry,
                                                            std::string_view json) {
        auto loaded = RTSContentLoader::load(registry, json);
        if (loaded) definitions_ = &registry;
        return loaded;
    }
    /** @brief Attach the canonical definition registry used to settle completed research. */
    void setDefinitionRegistry(definitions::DefinitionRegistry* registry) noexcept { definitions_ = registry; }

    /** @brief Install the game-owned economy credit boundary used by mining deliveries. */
    void setResourceCredit(ResourceCredit credit) { resourceCredit_ = std::move(credit); }
    /** @brief Install the game-owned economy debit boundary used by building repair. */
    void setRepairDebit(RepairDebit debit) { repairDebit_ = std::move(debit); }
    /** @brief Attach the canonical Crowd provider used by Units with a bound Unit::Crowd link. */
    void setCrowdProvider(crowd::Crowd* crowd) noexcept;
    /** @brief Attach the canonical map pathfinder and world/grid conversion used by unit navigation. */
    void setNavigationProvider(map::Pathfinder* pathfinder, NavigationGrid grid = {},
                               NavigationEvent unreachable = {}) noexcept;
    /** @brief Install the faction-to-canonical-FOV resolver used by fog-of-war projection. */
    void setFogProvider(FogProvider provider) noexcept;
    /** @brief Attach canonical sensing and damage coordinators used by automatic combat. */
    void setCombatProviders(sensing::SensingWorld* sensing, combat::DamageRuntime* damage) noexcept;
    /** @brief Install the map/game-owned line-of-fire query used by direct weapons. */
    void setFireLineQuery(FireLineQuery query) { fireLineQuery_ = std::move(query); }
    /** @brief Install the map/game-owned absolute projectile launch-height query. */
    void setCombatHeightQuery(CombatHeightQuery query) { combatHeightQuery_ = std::move(query); }
    /** @brief Install the map/physics-owned swept collision query for in-flight projectiles. */
    void setProjectileCollisionQuery(ProjectileCollisionQuery query) {
        projectileCollisionQuery_ = std::move(query);
    }
    /** @brief Install the authoritative production boundary used by enabled faction AI policies. */
    void setAIProductionRequest(AIProductionRequest request) { aiProductionRequest_ = std::move(request); }
    /** @brief Install the authoritative economy query used by resource-target matches. */
    void setMatchResourceQuery(MatchResourceQuery query) { matchResourceQuery_ = std::move(query); }
    /** @brief Install the authoritative economy credit boundary used by powered passive income. */
    void setPassiveIncomeCredit(PassiveIncomeCredit credit) { passiveIncomeCredit_ = std::move(credit); }
    /** @brief Install the authoritative economy purchase boundary used by building ammunition production. */
    void setAmmoProductionPurchase(AmmoProductionPurchase purchase) {
        ammoProductionPurchase_ = std::move(purchase);
    }
    /** @brief Install the game-owned factory that materializes completed canonical unit production tasks. */
    void setProductionSpawn(ProductionSpawn spawn) { productionSpawn_ = std::move(spawn); }
    /** @brief Install the game/map-owned deterministic building-exit placement probe. */
    void setProductionSpawnPosition(ProductionSpawnPosition position) {
        productionSpawnPosition_ = std::move(position);
    }
    /** @brief Install the game-owned atomic cancellation/refund boundary for capped reinforcements. */
    void setReinforcementCancel(ReinforcementCancel cancel) { reinforcementCancel_ = std::move(cancel); }

    /**
     * @brief Run phase-one simulation systems in deterministic order.
     * @param step Injected simulation tick and duration.
     * @param executor Borrowed adapter over the shared ActionRuntime.
     * @return Number of processed records across the systems.
     */
    [[nodiscard]] Result<std::size_t> step(const SimulationStep& step, IRTSActionExecutor& executor);

    /**
     * @brief Advance one script-owned fixed step through the canonical ActionRuntime adapter.
     * @param seconds Positive deterministic step duration in seconds.
     * @return Number of processed records across the composed systems.
     * @remarks Native games should prefer `step(SimulationStep, IRTSActionExecutor&)` and inject their shared
     *          runtime. This convenience boundary exists for self-contained Squirrel examples.
     */
    [[nodiscard]] Result<std::size_t> stepScript(double seconds);

    /** @brief Configure canonical providers owned by the self-contained Squirrel composition profile. */
    [[nodiscard]] Result<void> configureScriptWorld(int width, int height, float cellSize,
                                                     float originX = 0.0f, float originY = 0.0f);
    /** @brief Change one cell in the script profile's canonical pathfinder and crowd field. */
    [[nodiscard]] Result<void> setScriptNavigationBlocked(int x, int y, bool blocked);
    /** @brief Change one positive cell traversal cost in the script profile's canonical providers. */
    [[nodiscard]] Result<void> setScriptNavigationCost(int x, int y, float cost);
    /** @brief Set one finite authoritative terrain elevation shared by fog and combat queries. */
    [[nodiscard]] Result<void> setScriptTerrainElevation(int x, int y, float elevation);
    /** @brief Read one authoritative script-profile terrain elevation. */
    [[nodiscard]] Result<float> scriptTerrainElevation(int x, int y) const;
    /** @brief Credit one faction's script-profile canonical economy account. */
    [[nodiscard]] Result<void> addScriptResource(Faction& faction, std::string resource, std::int64_t amount);
    /** @brief Read one faction balance from the script-profile canonical economy ledger. */
    [[nodiscard]] Result<std::int64_t> scriptResource(Faction& faction, std::string_view resource) const;
    /**
     * @brief Configure a faction's deterministic worker replenishment, army production, and attack policy.
     * @param faction Owned faction whose strategy is changed.
     * @param workerDefinition Unit definition used until the worker target is met.
     * @param armyDefinition Unit definition produced for the attack force.
     * @param targetBuildingDefinition Preferred hostile building definition.
     * @param desiredWorkers Minimum live worker count.
     * @param attackThreshold Minimum live army count before attacking.
     * @param thinkInterval Positive decision interval in seconds.
     * @param formationSpacing Positive attack formation spacing.
     * @param enabled Whether the strategy runs.
     */
    [[nodiscard]] Result<void> configureScriptAI(Faction& faction, LogicalId workerDefinition,
        LogicalId armyDefinition, LogicalId targetBuildingDefinition, int desiredWorkers,
        int attackThreshold, float thinkInterval = 1.0f, float formationSpacing = 1.2f,
        bool enabled = true);
    /** @brief Atomically load a content pack into the script profile's canonical definition registry. */
    [[nodiscard]] Result<ContentImportReceipt> loadScriptContent(std::string_view json);
    /** @brief Queue one data-defined unit through canonical production, action, and economy providers. */
    [[nodiscard]] Result<RTSBuildReceipt> queueScriptUnit(Building& producer, SubjectRef unitSubject,
                                                           LogicalId unitDefinition, int priority = 0);
    /** @brief Queue a preferred reinforcement, deterministically trying its configured substitution chain. */
    [[nodiscard]] Result<ReinforcementRequestReceipt> queueScriptReinforcement(
        Building& producer, SubjectRef unitSubject, LogicalId preferredDefinition, int priority = 0);
    /** @brief Start one paid data-defined construction project assigned to a canonical worker order. */
    [[nodiscard]] Result<Building*> startScriptConstruction(Faction& faction, SubjectRef buildingSubject,
                                                             LogicalId buildingDefinition, WorldPosition position,
                                                             Unit& builder);
    /** @brief Cancel one unfinished paid construction, clear its builders, and refund remaining value. */
    [[nodiscard]] Result<resource::Receipt> cancelScriptConstruction(Building& building);
    /** @brief Sell one completed building, refund its configured value and every paid queued task. */
    [[nodiscard]] Result<resource::Receipt> sellScriptBuilding(Building& building);
    /** @brief Queue one paid data-defined upgrade through canonical research production. */
    [[nodiscard]] Result<RTSBuildReceipt> queueScriptResearch(Building& producer, std::string upgrade,
                                                               int priority = 0);
    /** @brief Cancel one queued script production/research task by live queue index and refund its full cost. */
    [[nodiscard]] Result<RTSCancelProductionReceipt> cancelScriptProduction(
        Building& producer, int queueIndex = -1);
    /** @brief Set the command copied to completed production, optionally creating a reinforcement group. */
    [[nodiscard]] Result<void> setBuildingRally(
        Building& producer, CommandSpec command, bool groupedReinforcements = false) const;
    /** @brief Copy a grouped reinforcement rally and composition policy from a friendly producer. */
    [[nodiscard]] Result<void> linkBuildingRally(Building& producer, Building& source) const;
    /** @brief Clear rally dispatch, transport assignment, and reinforcement policy. */
    [[nodiscard]] Result<void> clearBuildingRally(Building& producer) const;
    /** @brief Set the maximum living size of a linked reinforcement group; zero removes the cap. */
    [[nodiscard]] Result<void> setReinforcementLimit(Building& producer, std::size_t maximum) const;
    /** @brief Set or clear one unit type's living cap across a linked reinforcement group. */
    [[nodiscard]] Result<void> setReinforcementTypeLimit(
        Building& producer, std::string unitType, std::size_t maximum) const;
    /** @brief Set or clear one unit type's contention priority across a linked reinforcement group. */
    [[nodiscard]] Result<void> setReinforcementTypePriority(
        Building& producer, std::string unitType, int priority) const;
    /** @brief Set or clear one acyclic preferred-to-substitute mapping across a linked group. */
    [[nodiscard]] Result<void> setReinforcementFallback(
        Building& producer, std::string preferred, std::string fallback) const;
    /** @brief Configure delayed cancellation/refund while a reinforcement group remains capped. */
    [[nodiscard]] Result<void> setReinforcementAutoCancel(Building& producer, float seconds) const;
    /** @brief Route reinforcements through an exclusive friendly transport, or clear with null. */
    [[nodiscard]] Result<void> setReinforcementTransport(
        Building& producer, Unit* transport, std::size_t minimumLoad = 0) const;
    /** @brief Cast one canonical data-defined ability through the composed damage/effect/economy providers. */
    [[nodiscard]] Result<void> castScriptAbility(Unit& caster, std::string ability,
                                                  SubjectRef target, WorldPosition point);
    /** @brief Cancel an active ability channel without refunding its resource cost or cooldown. */
    [[nodiscard]] Result<void> cancelScriptAbility(Unit& caster);
    /** @brief Assign eligible friendly indirect-fire units to a deterministic suppression corridor. */
    [[nodiscard]] Result<std::size_t> requestFireSupport(
        Unit& requester, WorldPosition center, float radius, int shotsPerResponder,
        std::size_t maxResponders = 0) const;
    /** @brief Cancel every suppression mission assigned for one owned requester. */
    [[nodiscard]] Result<std::size_t> cancelFireSupport(Unit& requester) const;
    /** @brief Unload every passenger from a module-owned transport around a deterministic destination. */
    [[nodiscard]] Result<std::size_t> unloadTransport(Unit& transport, WorldPosition destination) const;
    /** @brief Evacuate every occupant from a module-owned building around a deterministic destination. */
    [[nodiscard]] Result<std::size_t> evacuateBuilding(Building& building, WorldPosition destination) const;
    /** @brief Change the cloak projection of a live module-owned unit. */
    [[nodiscard]] Result<void> setUnitCloaked(Unit& unit, bool cloaked) const;
    /** @brief Queue one stable-identity command on the script profile's deterministic tick timeline. */
    [[nodiscard]] Result<void> queueScriptCommand(RTSReplayCommand command);
    /** @brief Export the complete retained script-profile command history. */
    [[nodiscard]] Result<std::string> exportScriptCommandLog() const;
    /** @brief Atomically import commands at or after the next script simulation tick. */
    [[nodiscard]] Result<void> importScriptCommandLog(std::string_view text, bool clearExisting = true);
    /** @brief Return the most recently completed script simulation tick, or zero before stepping. */
    [[nodiscard]] std::uint64_t scriptTick() const noexcept;
    /** @brief Capture the complete current script-profile state under a non-empty in-memory name. */
    [[nodiscard]] Result<void> captureScriptCheckpoint(std::string name);
    /** @brief Atomically restore a named script-profile checkpoint with unchanged root topology. */
    [[nodiscard]] Result<void> restoreScriptCheckpoint(std::string_view name);
    /** @brief Remove one named in-memory script-profile checkpoint. */
    [[nodiscard]] Result<void> removeScriptCheckpoint(std::string_view name);
    /** @brief Query canonical script-profile fog visibility for one faction cell. */
    [[nodiscard]] Result<bool> scriptCellVisible(Faction& faction, int x, int y) const;
    /** @brief Query canonical script-profile explored memory for one faction cell. */
    [[nodiscard]] Result<bool> scriptCellExplored(Faction& faction, int x, int y) const;
    /** @brief Project one faction's visible, radar, or last-known stable contact. */
    [[nodiscard]] Result<Value> scriptContact(Faction& faction, SubjectRef target) const;

    /**
     * @brief Remove one owned unit, building, or resource node by stable identity.
     * @param subject Stable identity of the root to remove.
     * @return Applied when a root was removed, or NotFound when no owned gameplay root matches.
     * @remarks Removing a transport also removes its passengers. Removing a live building releases its
     *          garrison in place; destruction cleanup removes occupants that died with the building.
     */
    [[nodiscard]] Result<void> remove(SubjectRef subject);
    /**
     * @brief Physically remove dead units and buildings and repair all module-owned relationships.
     * @return Number of gameplay roots removed in this cleanup pass.
     */
    [[nodiscard]] Result<std::size_t> cleanupDestroyed();

    /** @brief Return the number of live Unit roots owned by this module. */
    [[nodiscard]] std::size_t unitCount() const noexcept;
    /** @brief Return the number of live Building roots owned by this module. */
    [[nodiscard]] std::size_t buildingCount() const noexcept;
    /** @brief Return the number of live resource nodes owned by this module. */
    [[nodiscard]] std::size_t resourceNodeCount() const noexcept;
    /** @brief Return the number of live Player roots owned by this module. */
    [[nodiscard]] std::size_t playerCount() const noexcept;
    /** @brief Return the number of live Faction roots owned by this module. */
    [[nodiscard]] std::size_t factionCount() const noexcept;
    /** @brief Return the number of live Match roots owned by this module. */
    [[nodiscard]] std::size_t matchCount() const noexcept;

private:
    struct GameplayRuntime;
    [[nodiscard]] Player* resolvePlayer(SubjectRef subject) const noexcept;
    [[nodiscard]] Unit* resolveUnit(SubjectRef subject) const noexcept;
    [[nodiscard]] bool ownsSubject(SubjectRef subject) const noexcept;
    void recordDamageEvent(const combat::DamageRequest& request,
                           const combat::DamageOutcome& outcome,
                           SimulationTick tick, DamageChannel channel);
    void recordCombatEvent(const CombatFireEvent& event, SimulationTick tick);
    void recordLifecycleEvent(const LifecycleEvent& event, SimulationTick tick);
    void clearOwnedRoots() noexcept;
    [[nodiscard]] Result<void> rebindScriptRootProviders();
    [[nodiscard]] Result<void> materialize(Unit& unit);
    [[nodiscard]] Result<void> materialize(Building& building);
    void removeUnitRoot(Unit& unit);
    void removeBuildingRoot(Building& building, bool destroyOccupants);
    void removeResourceNodeRoot(ResourceNode& node);
    std::vector<ecs::EntityHandle> units_;
    std::vector<ecs::EntityHandle> buildings_;
    std::vector<ecs::EntityHandle> resourceNodes_;
    std::vector<ecs::EntityHandle> players_;
    std::vector<ecs::EntityHandle> factions_;
    std::vector<ecs::EntityHandle> matches_;
    std::vector<ecs::EntityHandle> weapons_;
    ResourceCredit resourceCredit_;
    RepairDebit repairDebit_;
    crowd::Crowd* crowd_ = nullptr;
    map::Pathfinder* pathfinder_ = nullptr;
    NavigationGrid navigationGrid_;
    NavigationEvent navigationEvent_;
    FogProvider fogProvider_;
    FogOfWarSystem::State fogState_;
    sensing::SensingWorld* sensing_ = nullptr;
    combat::DamageRuntime* damage_ = nullptr;
    CombatFireSystem::State combatState_;
    FireLineQuery fireLineQuery_;
    CombatHeightQuery combatHeightQuery_;
    ProjectileCollisionQuery projectileCollisionQuery_;
    RTSProjectileSystem projectiles_;
    std::vector<RTSFrameDamageEvent> frameDamageEvents_;
    std::vector<RTSFrameCombatEvent> frameCombatEvents_;
    std::vector<RTSFrameLifecycleEvent> frameLifecycleEvents_;
    std::uint64_t frameEventSequence_ = 0;
    bool preserveFrameEventsOnNextStep_ = false;
    AIProductionRequest aiProductionRequest_;
    definitions::DefinitionRegistry* definitions_ = nullptr;
    MatchResourceQuery matchResourceQuery_;
    PassiveIncomeCredit passiveIncomeCredit_;
    AmmoProductionPurchase ammoProductionPurchase_;
    ProductionSpawn productionSpawn_;
    ProductionSpawnPosition productionSpawnPosition_;
    ReinforcementCancel reinforcementCancel_;
    struct ScriptRuntime;
    std::unique_ptr<ScriptRuntime> scriptRuntime_;
    std::unique_ptr<GameplayRuntime> gameplayRuntime_;
    std::uint64_t                   nextGameplayEventSequence_ = 1;
    std::vector<GameplayEvent>      gameplayEvents_;
};

}  // namespace eve::rts
