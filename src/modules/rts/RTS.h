#pragma once

/**
 * @file RTS.h
 * @brief RTS module owner and phase-one composition profile entry point.
 */

#include "common/Module.h"
#include "rts/RTSAttributes.h"
#include "rts/RTSEffects.h"
#include "rts/RTSProductionAction.h"
#include "rts/RTSSystems.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace eve::rts {

/**
 * @brief Owns RTS root entities and coordinates the phase-one systems.
 *
 * The module owns only the ECS handles it created. Components remain the
 * authoritative state owners, while action lifecycle state remains owned by
 * the caller-provided action::ActionRuntime through IRTSActionExecutor.
 */
class RTS : public Module {
public:
    Module_REG(RTS);

    /** @brief Construct an empty RTS composition profile. */
    RTS() = default;
    /** @brief Destroy only live entities created through this module. */
    ~RTS() override;

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
    /** @brief Create and own one Player root with a valid subject identity. */
    [[nodiscard]] Result<Player*> newPlayer(SubjectRef subject);
    /** @brief Create and own one Faction root with a valid subject identity. */
    [[nodiscard]] Result<Faction*> newFaction(SubjectRef subject);

    /**
     * @brief Fan one common command out through a player's selected unit handles.
     * @param selection Player-owned generation-checked selection projection.
     * @param command Generic Move/Attack/Build/Gather command.
     * @param formation Deterministic formation layout.
     * @return Accepted order ids in selection order.
     */
    [[nodiscard]] Result<FanOutReceipt> fanOut(const Player::Selection& selection, const CommandSpec& command,
                                               const FormationSpec& formation) const;

    /** @brief Read a canonical selected combat attribute through the RTS facade. */
    [[nodiscard]] Result<double> readUnitAttribute(Unit& unit, std::string_view attribute) const;
    /** @brief Set a canonical selected combat attribute through the RTS facade. */
    [[nodiscard]] Result<void> setUnitAttribute(Unit& unit, std::string_view attribute, double value) const;
    /** @brief Apply a typed effect to a module-owned Unit component. */
    [[nodiscard]] Result<effects::EffectHandle> applyEffect(Unit& unit, const RTSEffectDefinition& definition) const;
    /** @brief Apply a typed effect to a module-owned Building component. */
    [[nodiscard]] Result<effects::EffectHandle> applyEffect(Building&                  building,
                                                            const RTSEffectDefinition& definition) const;

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

    /**
     * @brief Run phase-one simulation systems in deterministic order.
     * @param step Injected simulation tick and duration.
     * @param executor Borrowed adapter over the shared ActionRuntime.
     * @return Number of processed records across the systems.
     */
    [[nodiscard]] Result<std::size_t> step(const SimulationStep& step, IRTSActionExecutor& executor);

    /** @brief Return the number of live Unit roots owned by this module. */
    [[nodiscard]] std::size_t unitCount() const noexcept;
    /** @brief Return the number of live Building roots owned by this module. */
    [[nodiscard]] std::size_t buildingCount() const noexcept;
    /** @brief Return the number of live Player roots owned by this module. */
    [[nodiscard]] std::size_t playerCount() const noexcept;
    /** @brief Return the number of live Faction roots owned by this module. */
    [[nodiscard]] std::size_t factionCount() const noexcept;

private:
    std::vector<ecs::EntityHandle> units_;
    std::vector<ecs::EntityHandle> buildings_;
    std::vector<ecs::EntityHandle> players_;
    std::vector<ecs::EntityHandle> factions_;
};

}  // namespace eve::rts
