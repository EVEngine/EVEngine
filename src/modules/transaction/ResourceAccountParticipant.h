#pragma once

/**
 * @file ResourceAccountParticipant.h
 * @brief Transaction participant for an atomic resource debit.
 */

#include "common/ResourceAccount.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eve::transaction {

/** @brief Lifecycle state of a resource debit participant. */
enum class ResourceDebitState : std::uint8_t {
    Idle,
    Prepared,
    Committed,
    RolledBack,
    Compensated,
};

/**
 * @brief Adapts IResourceAccount debit semantics to Coordinator.
 *
 * `prepare` reserves the complete cost, `commit` consumes that reservation,
 * `rollback` releases an uncommitted reservation, and `compensate` credits
 * the same cost after a later participant has already committed. This class
 * owns no account balance; the referenced account and CostSpec remain
 * caller-owned and must outlive the synchronous Coordinator call.
 */
class ResourceDebitParticipant final : public ITransactionParticipant {
public:
    /**
     * @brief Bind one debit cost to an account.
     * @param account Borrowed account whose balance will be debited.
     * @param cost Validated or pending-validation canonical resource cost.
     * @param name Stable diagnostic name used by Coordinator.
     */
    ResourceDebitParticipant(eve::resource::IResourceAccount& account,
                             eve::resource::CostSpec cost,
                             std::string name = "resource-debit");

    ResourceDebitParticipant(const ResourceDebitParticipant&) = delete;
    ResourceDebitParticipant& operator=(const ResourceDebitParticipant&) = delete;
    ResourceDebitParticipant(ResourceDebitParticipant&&) = delete;
    ResourceDebitParticipant& operator=(ResourceDebitParticipant&&) = delete;

    /** @brief Return the diagnostic name supplied at construction. */
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    /** @copydoc ITransactionParticipant::prepare */
    [[nodiscard]] eve::Result<void> prepare(const TransactionContext& context) override;

    /** @copydoc ITransactionParticipant::commit */
    [[nodiscard]] eve::Result<void> commit(const TransactionContext& context) override;

    /** @copydoc ITransactionParticipant::rollback */
    [[nodiscard]] eve::Result<void> rollback(const TransactionContext& context) override;

    /** @copydoc ITransactionParticipant::compensate */
    [[nodiscard]] eve::Result<void> compensate(const TransactionContext& context) override;

    /** @brief Return the participant lifecycle state for diagnostics/tests. */
    [[nodiscard]] ResourceDebitState state() const noexcept { return state_; }

private:
    eve::resource::IResourceAccount& account_;
    eve::resource::CostSpec           cost_;
    eve::resource::Reservation        reservation_;
    std::string                       name_;
    ResourceDebitState                state_ = ResourceDebitState::Idle;
};

}  // namespace eve::transaction
