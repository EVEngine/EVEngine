#pragma once

/**
 * @file InventoryResourceAccount.h
 * @brief Resource-account and cost adapters backed by Bag item stacks.
 */

#include "common/ResourceAccount.h"
#include "inventory/Bag.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace eve::inventory {

/**
 * @brief Adapts one Bag's item quantities to the common resource protocol.
 *
 * Each `ResourceId` is the canonical ItemDefinition id.  The bag's slot
 * vector remains the only balance store; this adapter owns only reservation
 * records and an opaque AccountNonce.  It therefore works for both item
 * prices and weapon ammunition without creating a second resource map.
 * The Bag is borrowed and must outlive this adapter. Calls are synchronous on
 * the Bag owner thread. Failed multi-item mutations restore slots, generated
 * instance ids and queued inventory events before returning.
 */
class InventoryResourceAccount final : public eve::resource::IResourceAccount {
public:
    /** @brief Bind the adapter to one caller-owned inventory bag. */
    explicit InventoryResourceAccount(Bag& bag);

    InventoryResourceAccount(const InventoryResourceAccount&)            = delete;
    InventoryResourceAccount& operator=(const InventoryResourceAccount&) = delete;
    InventoryResourceAccount(InventoryResourceAccount&&)                 = delete;
    InventoryResourceAccount& operator=(InventoryResourceAccount&&)      = delete;
    ~InventoryResourceAccount() override                                 = default;

    /** @copydoc eve::resource::IResourceAccount::canAfford */
    [[nodiscard]] eve::Result<eve::resource::Affordability> canAfford(
        const eve::resource::CostSpec& cost) const override;
    /** @copydoc eve::resource::IResourceAccount::reserve */
    [[nodiscard]] eve::Result<eve::resource::Reservation> reserve(const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::debit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> debit(const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::credit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> credit(const eve::resource::CostSpec& cost) override;
    /** @copydoc eve::resource::IResourceAccount::commit */
    [[nodiscard]] eve::Result<eve::resource::Receipt> commit(const eve::resource::Reservation& reservation) override;
    /** @copydoc eve::resource::IResourceAccount::rollback */
    [[nodiscard]] eve::Result<void> rollback(const eve::resource::Reservation& reservation) override;

private:
    enum class ReservationState : std::uint8_t { Active, Committed, RolledBack };
    struct ReservationRecord {
        eve::resource::CostSpec cost;
        ReservationState        state = ReservationState::Active;
    };

    [[nodiscard]] eve::Result<std::int64_t> balanceOf(const eve::resource::ResourceId& resource) const;
    [[nodiscard]] eve::Result<std::int64_t> activeReservationsFor(const eve::resource::ResourceId& resource) const;
    [[nodiscard]] eve::Result<void>         activeReservationsAreCovered() const;
    [[nodiscard]] eve::Result<void>         applyDelta(const eve::resource::CostSpec& cost, bool debit);

    Bag&                                                                bag_;
    std::unordered_map<eve::resource::ReservationId, ReservationRecord> reservations_;
    eve::resource::AccountNonce                                         accountNonce_;
    eve::resource::ReservationId                                        nextReservation_{1};
    eve::resource::ReceiptId                                            nextReceipt_{1};
};

/**
 * @brief Creates canonical costs for item and ammunition quantities.
 *
 * Item ids are passed through as ResourceId values. The account adapter then
 * resolves them against the authoritative ItemDefinition/Bag path; no
 * parallel price or ammunition table is introduced here.
 */
class ItemCostAdapter final {
public:
    /**
     * @brief Build a positive cost for an inventory item.
     * @param itemId Registered ItemDefinition id.
     * @param quantity Positive quantity.
     * @return Canonical item cost or a structured validation failure.
     */
    [[nodiscard]] static eve::Result<eve::resource::CostSpec> itemCost(std::string_view itemId, std::int64_t quantity);

    /**
     * @brief Build a positive cost for ammunition stored as an item stack.
     * @param ammoItemId Registered ammunition ItemDefinition id.
     * @param quantity Positive number of rounds/charges.
     * @return Canonical ammunition cost or a structured validation failure.
     */
    [[nodiscard]] static eve::Result<eve::resource::CostSpec> ammoCost(std::string_view ammoItemId,
                                                                       std::int64_t     quantity);
};

}  // namespace eve::inventory
