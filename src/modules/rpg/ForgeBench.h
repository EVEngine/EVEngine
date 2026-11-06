#pragma once

/**
 * @file ForgeBench.h
 * @brief Script-facing forge that composes Crafting + EquipmentUpgrade over one bag.
 */

#include "common/Result.h"
#include "common/Time.h"
#include "inventory/InventoryResourceAccount.h"
#include "production/Production.h"
#include "rpg/Crafting.h"
#include "rpg/EquipmentUpgrade.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/Export.h"

namespace eve::inventory {
class Bag;
class EquipmentSet;
}  // namespace eve::inventory

namespace eve::rpg {

class RPGActor;

/**
 * @brief Owns a production WorkQueue and borrows bag / optional equipment+actor.
 *
 * Craft pays bag materials and grants outputs. Upgrade mutates one bag instance's
 * `enhance` prop. When equipment + actor are bound, successful upgrade/equip paths
 * can refresh modifiers through EquipmentSystem.
 */
class EVENGINE_API_PLATFORM ForgeBench final {
public:
    ForgeBench();
    ~ForgeBench();

    ForgeBench(const ForgeBench&)            = delete;
    ForgeBench& operator=(const ForgeBench&) = delete;
    ForgeBench(ForgeBench&&) noexcept;
    ForgeBench& operator=(ForgeBench&&) noexcept;

    /**
     * @brief Borrow the authoritative bag and optional gear/actor mirrors.
     * @ownership Callers own bag/equipment/actor; destroy this bench first.
     */
    [[nodiscard]] eve::Result<void> bind(inventory::Bag* bag, inventory::EquipmentSet* equipment = nullptr,
                                         RPGActor* actor = nullptr);

    /** @brief Clear borrowed collaborators without releasing the production queue. */
    void unbind();

    /** @brief True when a bag is currently bound. */
    [[nodiscard]] bool isBound() const { return bag_ != nullptr; }

    /** @brief Atomically pay materials and enqueue a craft recipe. */
    [[nodiscard]] eve::Result<CraftingReceipt> craft(CraftingRecipe recipe, std::uint32_t batchSize = 1,
                                                     int priority = 0);

    /**
     * @brief Parse a craft recipe JSON object and enqueue it.
     * @param json Object with id, process, ingredients, outputs[{item,quantity}], duration.
     */
    [[nodiscard]] eve::Result<CraftingReceipt> craftFromJson(const std::string& json, std::uint32_t batchSize = 1,
                                                             int priority = 0);

    /** @brief Atomically pay materials and enqueue an equipment upgrade. */
    [[nodiscard]] eve::Result<EquipmentUpgradeReceipt> upgrade(EquipmentUpgradeRecipe recipe, int bagSlot,
                                                               int expectedInstanceId, int priority = 0);

    /**
     * @brief Parse an upgrade recipe JSON object and enqueue it for one bag instance.
     * @param json Object with id, process, itemId, fromLevel, toLevel, ingredients, duration.
     */
    [[nodiscard]] eve::Result<EquipmentUpgradeReceipt> upgradeFromJson(const std::string& json, int bagSlot,
                                                                       int expectedInstanceId, int priority = 0);

    /** @brief Advance the owned forge queue by one deterministic simulation step. */
    [[nodiscard]] eve::Result<void> advance(eve::SimulationTick tick, eve::Duration dt);

    /** @brief Settle one ready craft or upgrade task into the bound bag. */
    [[nodiscard]] eve::Result<int> settle(std::string_view taskId);

    /**
     * @brief Settle every ReadyToSettle / SettlementFailed task once.
     * @return Number of tasks that committed an Applied settlement.
     */
    [[nodiscard]] eve::Result<int> settleReady();

    /** @brief Packed production handle for diagnostics (0 when unbound/released). */
    [[nodiscard]] std::uint64_t queueHandle() const noexcept;

    /** @brief Last accepted simulation tick, or zero before the first advance. */
    [[nodiscard]] eve::SimulationTick tick() const noexcept { return tick_; }

    /** @brief Task count currently retained by the forge queue. */
    [[nodiscard]] int taskCount() const;

    /** @brief Task id at stable enumeration index, or empty. */
    [[nodiscard]] std::string taskIdAt(int index) const;

    /** @brief Task state name at stable enumeration index, or empty. */
    [[nodiscard]] std::string taskStateAt(int index) const;

    /** @brief Task kind at stable enumeration index, or empty. */
    [[nodiscard]] std::string taskKindAt(int index) const;

    /** @brief Task progress seconds at stable enumeration index. */
    [[nodiscard]] float taskProgressAt(int index) const;

    /** @brief Task duration seconds at stable enumeration index. */
    [[nodiscard]] float taskDurationAt(int index) const;

    /** @brief Refresh actor equipment modifiers when actor+equipment are bound. */
    [[nodiscard]] int syncEquipment() const;

private:
    [[nodiscard]] eve::ResultRef<production::WorkQueue> queue() const;

    production::WorkQueueHandleRef queueRef_{};
    inventory::Bag*                bag_       = nullptr;
    inventory::EquipmentSet*       equipment_ = nullptr;
    RPGActor*                      actor_     = nullptr;
    eve::SimulationTick            tick_      = eve::SimulationTick::zero();
};

}  // namespace eve::rpg
