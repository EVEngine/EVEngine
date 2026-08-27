#pragma once

/**
 * @file EconomyLedgerResourceAccount.h
 * @brief Resource-account adapter backed by an EconomyLedger.
 */

#include "common/ResourceAccount.h"
#include "economy/EconomyLedger.h"

#include <cstdint>
#include <unordered_map>

namespace eve::economy {

/**
 * @brief Adapts one caller-owned EconomyLedger to IResourceAccount.
 *
 * Resource ids map directly to EconomyLedger type ids. The protocol adapter
 * gives credit/debit strict all-or-nothing semantics for multi-resource
 * operations; the legacy EconomyLedger::credit API remains available for
 * gather overflow accounting and may still accept a partial credit at its
 * compatibility boundary. The ledger owns balance, income, expense and
 * capacity facts; this adapter owns only reservation state.
 *
 * The referenced ledger is borrowed and must outlive this adapter. Operations
 * are synchronous and must be called on the owner thread.
 */
class EconomyLedgerResourceAccount final : public eve::resource::IResourceAccount {
public:
    /** @brief Bind an account adapter to an existing caller-owned ledger. */
    explicit EconomyLedgerResourceAccount(EconomyLedger& ledger);

    EconomyLedgerResourceAccount(const EconomyLedgerResourceAccount&)            = delete;
    EconomyLedgerResourceAccount& operator=(const EconomyLedgerResourceAccount&) = delete;
    EconomyLedgerResourceAccount(EconomyLedgerResourceAccount&&)                 = delete;
    EconomyLedgerResourceAccount& operator=(EconomyLedgerResourceAccount&&)      = delete;

    ~EconomyLedgerResourceAccount() override = default;

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
    [[nodiscard]] eve::Result<void>         validateLedgerRange(const eve::resource::CostSpec& cost, bool debit) const;

    EconomyLedger&                                                      ledger_;
    std::unordered_map<eve::resource::ReservationId, ReservationRecord> reservations_;
    eve::resource::AccountNonce                                         accountNonce_;
    eve::resource::ReservationId                                        nextReservation_{1};
    eve::resource::ReceiptId                                            nextReceipt_{1};
};

}  // namespace eve::economy
