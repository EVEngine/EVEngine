#pragma once

/**
 * @file ShopTransaction.h
 * @brief Atomic RPG currency and inventory purchase/sale orchestration.
 */

#include "common/Result.h"

#include <string>

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

class GameState;

/** @brief Product transaction boundary for an RPG shop trade. */
class ShopTransaction {
public:
    /** @brief Buy a quantity using the authoritative price and item in ShopCatalogue. */
    [[nodiscard]] static eve::Result<int> buyOffer(GameState &gameState, inventory::Bag &bag,
                                                    const std::string &currencyId,
                                                    const std::string &offerId, int quantity);
    /** @brief Sell a quantity using the authoritative price and item in ShopCatalogue. */
    [[nodiscard]] static eve::Result<int> sellOffer(GameState &gameState, inventory::Bag &bag,
                                                     const std::string &currencyId,
                                                     const std::string &offerId, int quantity);
    /**
     * @brief Atomically debit currency and add the purchased item quantity.
     * @return Committed item quantity, or structured failure with neither owner mutated.
     * @ownership GameState and Bag remain caller-owned and must outlive this call.
     * @thread Call on both participants' owning simulation thread.
     * @reentrancy Inventory observers run only after currency and Bag reach their final state.
     */
    [[nodiscard]] static eve::Result<int> buy(GameState &gameState, inventory::Bag &bag,
                                               const std::string &currencyId,
                                               const std::string &itemId, int quantity,
                                               double unitPrice);

    /**
     * @brief Atomically remove an item quantity and credit its sale proceeds.
     * @return Committed item quantity, or structured failure with neither owner mutated.
     * @ownership GameState and Bag remain caller-owned and must outlive this call.
     * @thread Call on both participants' owning simulation thread.
     * @reentrancy Inventory observers run only after currency and Bag reach their final state.
     */
    [[nodiscard]] static eve::Result<int> sell(GameState &gameState, inventory::Bag &bag,
                                                const std::string &currencyId,
                                                const std::string &itemId, int quantity,
                                                double unitPrice);
};

}  // namespace eve::rpg
