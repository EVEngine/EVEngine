#pragma once

/**
 * @file RTSEconomy.h
 * @brief RTS composition adapter over the canonical EconomyLedger account.
 */

#include "common/ResourceAccount.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "transaction/AtomicResourcePayment.h"

#include <string>

namespace eve::rts {

/**
 * @brief Connects an RTS player's economy to one atomic gameplay payment.
 *
 * EconomyLedger remains the authoritative balance/income/expense store. This
 * adapter owns only the account reservation protocol and delegates all
 * payment lifecycle work to AtomicResourcePayment. The ledger is borrowed and
 * must outlive the adapter; calls are synchronous on its owner thread.
 */
class RTSEconomyAdapter final {
public:
    /** @brief Bind this adapter to a caller-owned RTS economy ledger. */
    explicit RTSEconomyAdapter(eve::economy::EconomyLedger& ledger);

    RTSEconomyAdapter(const RTSEconomyAdapter&) = delete;
    RTSEconomyAdapter& operator=(const RTSEconomyAdapter&) = delete;
    RTSEconomyAdapter(RTSEconomyAdapter&&) = delete;
    RTSEconomyAdapter& operator=(RTSEconomyAdapter&&) = delete;
    ~RTSEconomyAdapter() = default;

    /**
     * @brief Return the account view for direct reservation/credit operations.
     * @return Borrowed account whose nonce is unique to this ledger adapter.
     */
    [[nodiscard]] eve::resource::IResourceAccount& account() noexcept;

    /**
     * @brief Atomically pay an RTS cost and publish its domain effect.
     * @param cost Canonical positive cost, borrowed for this call.
     * @param effect Borrowed effect participant; it is prepared before debit.
     * @param transactionId Non-empty correlation id; empty derives from the cost path.
     * @return Committed transaction receipt or rollback/compensation diagnostics.
     */
    [[nodiscard]] eve::Result<eve::transaction::TransactionReceipt> pay(
        const eve::resource::CostSpec& cost,
        eve::transaction::ITransactionParticipant& effect,
        std::string transactionId = {});

    /**
     * @brief Credit the ledger through the common account protocol.
     * @param cost Canonical positive resource quantity to add.
     * @return A checked credit receipt or a structured capacity/range failure.
     */
    [[nodiscard]] eve::Result<eve::resource::Receipt> credit(
        const eve::resource::CostSpec& cost);

private:
    eve::economy::EconomyLedgerResourceAccount account_;
};

}  // namespace eve::rts
