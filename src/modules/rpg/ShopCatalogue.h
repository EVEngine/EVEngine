#pragma once

/** @file ShopCatalogue.h @brief Strict, atomically replaceable RPG shop content. */

#include "common/Result.h"

#include <string>

namespace eve::rpg {

/** @brief One immutable offer in the process-local shop catalogue. */
struct ShopOffer {
    std::string id;
    std::string itemId;
    std::string displayName;
    std::string description;
    int         buyPrice = 0;
    int         sellPrice = 0;
};

/** @brief Process-local validated offer catalogue used by RPG shop transactions and UI. */
class ShopCatalogue {
public:
    /**
     * @brief Strictly validate and atomically replace the entire offer catalogue.
     * @return Committed offer count, or a structured failure that preserves the previous catalogue.
     * @remarks Every item must already exist in inventory::ItemRegistry. Unknown fields are rejected.
     * @thread Call on the owning content/simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string &json);
    /** @brief Remove all offers. Intended for teardown and isolated tests. */
    static void clear();
    /** @brief Return the number of committed offers in deterministic source order. */
    static int count();
    /** @brief Find an offer by stable offer id, or null when absent. */
    static const ShopOffer *find(const std::string &id);
    /** @brief Return an offer by deterministic source index, or null when out of range. */
    static const ShopOffer *at(int index);
};

}  // namespace eve::rpg
