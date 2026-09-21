#pragma once

/**
 * @file Crafting.h
 * @brief RPG recipe adapter over the shared production and inventory protocols.
 */

#include "common/ResourceAccount.h"
#include "common/Time.h"
#include "common/definitions/DefinitionRuntime.h"
#include "inventory/InventorySystem.h"
#include "production/Production.h"
#include "transaction/Transaction.h"

#include <string>
#include <cstdint>
#include <vector>

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

/** @brief Validated recipe projection used by crafting, alchemy, brewing and cooking. */
struct CraftingRecipe {
    std::string                                id;
    std::string                                process = "craft";
    eve::definition::DefinitionHandle          definition;
    eve::resource::CostSpec                    ingredients;
    std::vector<inventory::InventoryItemGrant> outputs;
    eve::Duration                              duration = eve::Duration::zero();
};

/** @brief Borrowed inputs for atomically paying ingredients and enqueuing recipe work. */
struct CraftingRequest {
    production::WorkQueue*      production = nullptr;
    resource::IResourceAccount* ingredients = nullptr;
    std::string                 owner;
    CraftingRecipe              recipe;
    int                         priority = 0;
    std::uint32_t               batchSize = 1;
    std::string                 transactionId;
};

/** @brief Receipt for an accepted recipe job. */
struct CraftingReceipt {
    transaction::TransactionReceipt transaction;
    std::string                     productionTaskId;
};

/**
 * @brief Composes RPG recipes with existing production and inventory owners.
 * @remarks Calls are synchronous and owner-thread only. The adapter retains no
 * borrowed pointers. Ingredient balances remain owned by the supplied account,
 * queued work by WorkQueue, and outputs by Bag.
 */
class Crafting final {
public:
    /** @brief Atomically debit ingredients and enqueue pinned recipe work. */
    [[nodiscard]] static eve::Result<CraftingReceipt> begin(CraftingRequest request);

    /**
     * @brief Publish every reserved recipe output and settle the task exactly once.
     * @param queue Authoritative queue containing the ready task.
     * @param output Borrowed authoritative destination inventory.
     * @param taskId Stable production task id.
     * @return Added item quantity, NoOp for the same completed receipt, or a
     * structured failure that leaves a retryable task and inventory unchanged.
     */
    [[nodiscard]] static eve::Result<int> settle(production::WorkQueue& queue, inventory::Bag& output,
                                                  std::string_view taskId);
};

}  // namespace eve::rpg
