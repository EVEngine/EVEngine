#pragma once

/**
 * @file EquipmentUpgrade.h
 * @brief RPG adapter that pays materials and upgrades one inventory item instance.
 *
 * Upgrade mutates an existing max-stack-1 equipment stack in place (props key
 * `enhance`), instead of granting a new item id. Production owns the timed work;
 * inventory owns the instance; this adapter owns neither.
 */

#include "common/ResourceAccount.h"
#include "common/Time.h"
#include "common/definitions/DefinitionRuntime.h"
#include "inventory/InventorySystem.h"
#include "production/Production.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "common/Export.h"

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

/** @brief Stable prop key written by EquipmentUpgrade::settle and read by EquipmentSystem. */
inline constexpr const char* kEquipmentEnhanceProp = "enhance";

/** @brief Validated upgrade ladder step for one equipment instance. */
struct EquipmentUpgradeRecipe {
    std::string                       id;
    std::string                       process = "upgrade";
    eve::definition::DefinitionHandle definition;
    std::string                       itemId;
    int                               fromLevel = 0;
    int                               toLevel   = 1;
    eve::resource::CostSpec           ingredients;
    eve::Duration                     duration = eve::Duration::zero();
};

/** @brief Borrowed inputs for paying materials and enqueueing an upgrade job. */
struct EquipmentUpgradeRequest {
    production::WorkQueue*      production         = nullptr;
    resource::IResourceAccount* ingredients        = nullptr;
    inventory::Bag*             bag                = nullptr;
    int                         bagSlot            = -1;
    int                         expectedInstanceId = 0;
    std::string                 owner;
    EquipmentUpgradeRecipe      recipe;
    int                         priority = 0;
    std::string                 transactionId;
};

/** @brief Receipt for an accepted upgrade job. */
struct EquipmentUpgradeReceipt {
    transaction::TransactionReceipt transaction;
    std::string                     productionTaskId;
};

/**
 * @brief Composes equipment upgrade with production and inventory owners.
 * @remarks Calls are synchronous and owner-thread only. Borrowed pointers are
 * not retained. Ingredient balances stay on the supplied account, queued work
 * on WorkQueue, and the upgraded stack on Bag.
 */
class EVENGINE_API_PLATFORM EquipmentUpgrade final {
public:
    /** @brief Parse a non-negative enhance level from an item prop string. */
    [[nodiscard]] static eve::Result<int> parseEnhanceLevel(std::string_view text);

    /** @brief Atomically debit materials and enqueue pinned upgrade work. */
    [[nodiscard]] static eve::Result<EquipmentUpgradeReceipt> begin(EquipmentUpgradeRequest request);

    /**
     * @brief Apply the reserved enhance mutation and settle the task exactly once.
     * @param queue Authoritative queue containing the ready task.
     * @param bag Borrowed bag that must still hold the reserved instance.
     * @param taskId Stable production task id.
     * @return New enhance level, NoOp for a repeated completed receipt, or structured failure.
     */
    [[nodiscard]] static eve::Result<int> settle(production::WorkQueue& queue, inventory::Bag& bag,
                                                 std::string_view taskId);
};

}  // namespace eve::rpg
