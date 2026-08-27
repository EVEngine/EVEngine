#pragma once

/**
 * @file DialoguePayment.h
 * @brief Dialogue payment specifications and transaction composition.
 *
 * Dialogue does not own balances.  A money account and a reputation account
 * are borrowed IResourceAccount adapters, normally backed by EconomyLedger
 * and AttributeSet respectively.  This file only composes those adapters with
 * transaction participants supplied by StatePatch or a gameplay Action.
 */

#include "common/ResourceAccount.h"
#include "common/StateAccess.h"
#include "common/Value.h"
#include "transaction/Transaction.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eve::dialogue {

/**
 * @brief Canonical money/reputation cost attached to a dialogue choice or command.
 *
 * Values are positive integral quantities.  The specification is policy data,
 * not a balance mirror: the actual values are read and changed only through
 * the account adapters supplied to DialoguePaymentAdapter.
 */
struct PaymentSpec {
    /** @brief Optional money amount, in the configured money resource. */
    std::optional<std::int64_t> money;
    /** @brief Optional reputation amount, in the configured reputation resource. */
    std::optional<std::int64_t> reputation;

    /** @brief Whether this specification contains no charge. */
    [[nodiscard]] bool empty() const noexcept { return !money && !reputation; }
    /** @brief Validate that every present amount is a positive integer. */
    [[nodiscard]] eve::Result<void> validate() const;
    /** @brief Parse `{ "money": n, "reputation": n }` from canonical Value. */
    [[nodiscard]] static eve::Result<PaymentSpec> fromValue(const eve::Value& value);
    /** @brief Project this payment into a deterministic canonical Value object. */
    [[nodiscard]] eve::Value toValue() const;
};

/**
 * @brief Borrowed account bindings used by one DialogueFlow integration.
 *
 * `money` and `reputation` are normally instances of
 * `EconomyLedgerResourceAccount` and `AttributeSetResourceAccount`.  The
 * Dialogue module never stores a second balance or casts these pointers back
 * to a concrete implementation.  All operations are synchronous and must be
 * called on the owner thread while the adapters remain alive.
 */
struct DialogueAccountBindings {
    /** @brief Borrowed authoritative account for money. */
    eve::resource::IResourceAccount* money = nullptr;
    /** @brief Borrowed authoritative account for reputation. */
    eve::resource::IResourceAccount* reputation = nullptr;
    /** @brief ResourceId used inside the money account. */
    std::string moneyResource = "gold";
    /** @brief ResourceId used inside the reputation account. */
    std::string reputationResource = "reputation";
};

/**
 * @brief Transaction participant that delegates an atomic state mutation.
 *
 * The provider is borrowed.  `prepare` performs no observable mutation and
 * `commit` calls the provider's all-or-nothing apply operation.  This
 * participant must be the final state-writing participant in a composed
 * payment because generic IStateMutation has no inverse operation; account
 * participants are therefore committed before it and compensate on failure.
 */
class DialogueStateMutationParticipant final : public eve::transaction::ITransactionParticipant {
public:
    /**
     * @brief Bind a copied mutation set to one borrowed mutation provider.
     * @param provider Authoritative StatePatch-compatible mutation provider.
     * @param mutations Persistent or volatile mutations owned by this participant.
     */
    DialogueStateMutationParticipant(eve::IStateMutation& provider, std::span<const eve::StateMutation> mutations);

    DialogueStateMutationParticipant(const DialogueStateMutationParticipant&)            = delete;
    DialogueStateMutationParticipant& operator=(const DialogueStateMutationParticipant&) = delete;
    DialogueStateMutationParticipant(DialogueStateMutationParticipant&&)                 = delete;
    DialogueStateMutationParticipant& operator=(DialogueStateMutationParticipant&&)      = delete;
    ~DialogueStateMutationParticipant() override                                         = default;

    /** @brief Stable diagnostic name. */
    [[nodiscard]] std::string_view name() const noexcept override { return "dialogue.state-mutation"; }
    /** @copydoc eve::transaction::ITransactionParticipant::prepare */
    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::commit */
    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::rollback */
    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext& context) override;
    /** @copydoc eve::transaction::ITransactionParticipant::compensate */
    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext& context) override;

private:
    enum class Phase : std::uint8_t { Idle, Prepared, Committed, RolledBack };

    eve::IStateMutation&            provider_;
    std::vector<eve::StateMutation> mutations_;
    Phase                           phase_ = Phase::Idle;
};

/**
 * @brief Composes Dialogue accounts with StatePatch or Action participants.
 *
 * This adapter owns only copied payment configuration and temporary
 * reservation participants.  It owns no account balance, world state, action
 * execution, callback, or provider.  Participants are prepared in account
 * order and effects are committed after account debits; if an effect fails,
 * already committed debits are compensated.  The call is synchronous and
 * non-reentrant on the owner thread.
 */
class DialoguePaymentAdapter final {
public:
    /** @brief Construct an adapter with borrowed account bindings. */
    explicit DialoguePaymentAdapter(DialogueAccountBindings bindings = {});

    /** @brief Replace borrowed account bindings; no balance is copied. */
    void setBindings(DialogueAccountBindings bindings);
    /** @brief Return the current borrowed bindings for diagnostics. */
    [[nodiscard]] const DialogueAccountBindings& bindings() const noexcept { return bindings_; }

    /**
     * @brief Execute a payment and zero or more staged domain effects atomically.
     * @param transactionId Non-empty transaction correlation identity.
     * @param payment Validated money/reputation specification.
     * @param effects Borrowed transaction participants; they must be unique and
     *        any non-compensatable StatePatch participant must be last.
     * @return A committed transaction receipt or structured diagnostics.
     */
    [[nodiscard]] eve::Result<eve::transaction::TransactionReceipt> execute(
        std::string transactionId, const PaymentSpec& payment,
        std::span<eve::transaction::ITransactionParticipant*> effects) const;

    /**
     * @brief Execute a payment together with one StatePatch-compatible mutation set.
     * @param transactionId Non-empty transaction correlation identity.
     * @param payment Validated money/reputation specification.
     * @param provider Borrowed all-or-nothing provider, such as StatePatch.
     * @param mutations Mutation values copied before transaction preparation.
     * @return A committed receipt; failure leaves account and provider unchanged.
     */
    [[nodiscard]] eve::Result<eve::transaction::TransactionReceipt> executeStateMutation(
        std::string transactionId, const PaymentSpec& payment, eve::IStateMutation& provider,
        std::span<const eve::StateMutation> mutations) const;

private:
    DialogueAccountBindings bindings_;
};

}  // namespace eve::dialogue
