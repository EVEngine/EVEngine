#pragma once

/**
 * @file AttributeSetResourceAccount.h
 * @brief Resource-account adapter backed by AttributeSet base values.
 */

#include "attributes/AttributeSet.h"
#include "common/ResourceAccount.h"

#include <cstdint>
#include <unordered_map>

namespace eve::attributes {

/**
 * @brief Adapts integral AttributeSet base values to IResourceAccount.
 *
 * The resource id is the AttributeSet attribute id. The adapter reads and
 * writes the base value; modifiers remain AttributeSet projections and are
 * intentionally not folded into an account balance. Attribute values used as
 * resources must be finite, integral and non-negative. AttributeSet owns the
 * balance; this adapter only owns active reservation bookkeeping.
 *
 * The referenced AttributeSet is borrowed and must outlive this adapter. All
 * operations are synchronous and must be called on the owner thread.
 */
class AttributeSetResourceAccount final : public eve::resource::IResourceAccount {
public:
    /** @brief Bind an account adapter to an existing caller-owned AttributeSet. */
    explicit AttributeSetResourceAccount(AttributeSet& attributes);

    AttributeSetResourceAccount(const AttributeSetResourceAccount&)            = delete;
    AttributeSetResourceAccount& operator=(const AttributeSetResourceAccount&) = delete;
    AttributeSetResourceAccount(AttributeSetResourceAccount&&)                 = delete;
    AttributeSetResourceAccount& operator=(AttributeSetResourceAccount&&)      = delete;

    ~AttributeSetResourceAccount() override = default;

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

    AttributeSet&                                                       attributes_;
    std::unordered_map<eve::resource::ReservationId, ReservationRecord> reservations_;
    eve::resource::AccountNonce                                         accountNonce_;
    eve::resource::ReservationId                                        nextReservation_{1};
    eve::resource::ReceiptId                                            nextReceipt_{1};
};

}  // namespace eve::attributes
