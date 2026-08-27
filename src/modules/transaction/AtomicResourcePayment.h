#pragma once

/**
 * @file AtomicResourcePayment.h
 * @brief Small transaction facade for a resource cost plus one domain effect.
 */

#include "common/ResourceAccount.h"
#include "transaction/ResourceAccountParticipant.h"

#include <span>
#include <string>

namespace eve::transaction {

/**
 * @brief Coordinates a resource debit with one domain effect participant.
 *
 * The effect is prepared before the resource reservation is committed.  A
 * preparation failure therefore never charges the account.  If a later
 * commit fails after the effect became visible, Coordinator uses the
 * participant's compensation contract to restore the effect and the account.
 * This helper owns neither participant nor account; all references are
 * borrowed for the synchronous call and must remain valid until it returns.
 * The caller and both participants must use the same owner thread.
 */
class AtomicResourcePayment final {
public:
    /**
     * @brief Execute already prepared domain participants as one transaction.
     * @param context Correlation metadata copied into the receipt.
     * @param participants Borrowed, unique participants in commit order.
     * @return A committed receipt or complete rollback/compensation diagnostics.
     * @remarks This overload is the composition boundary for domain adapters
     *          which have more than one effect (for example Card movement plus
     *          card state plus a ruleset effect). The payment helper does not
     *          own the participants.
     */
    [[nodiscard]] static eve::Result<TransactionReceipt> execute(
        const TransactionContext& context,
        std::span<ITransactionParticipant*> participants);

    /**
     * @brief Execute a resource debit and several domain participants atomically.
     * @param context Correlation metadata copied into the receipt.
     * @param account Borrowed authoritative account.
     * @param cost Valid canonical positive cost borrowed for this call.
     * @param participants Domain participants; the debit is committed last.
     * @return A committed receipt or complete rollback/compensation diagnostics.
     * @remarks All domain participants are prepared before the account is
     *          reserved. The debit participant is appended after the supplied
     *          participants so a failed debit compensates already-visible
     *          domain effects in reverse order.
     */
    [[nodiscard]] static eve::Result<TransactionReceipt> execute(
        const TransactionContext& context, eve::resource::IResourceAccount& account,
        const eve::resource::CostSpec& cost,
        std::span<ITransactionParticipant*> participants);

    /**
     * @brief Execute an effect without a resource cost.
     * @param context Correlation metadata copied into the transaction receipt.
     * @param effect Borrowed domain participant; it owns its staged state.
     * @return A committed receipt or structured lifecycle diagnostics.
     */
    [[nodiscard]] static eve::Result<TransactionReceipt> execute(
        const TransactionContext& context, ITransactionParticipant& effect);

    /**
     * @brief Execute one canonical cost and one domain effect atomically.
     * @param context Correlation metadata copied into the transaction receipt.
     * @param account Borrowed authoritative resource account.
     * @param cost Valid canonical positive cost borrowed for this call.
     * @param effect Borrowed domain participant; it owns its staged state.
     * @return A committed receipt, or a failure after rollback/compensation.
     * @remarks The account is addressed by the reservation's AccountNonce;
     *          no current/default account lookup is performed.
     */
    [[nodiscard]] static eve::Result<TransactionReceipt> execute(
        const TransactionContext& context, eve::resource::IResourceAccount& account,
        const eve::resource::CostSpec& cost, ITransactionParticipant& effect);

    /**
     * @brief Convenience overload using a stable string transaction id.
     * @param transactionId Non-empty transaction id owned by the call.
     * @param account Borrowed authoritative resource account.
     * @param cost Valid canonical positive cost borrowed for this call.
     * @param effect Borrowed domain participant.
     * @return A committed receipt or structured lifecycle diagnostics.
     */
    [[nodiscard]] static eve::Result<TransactionReceipt> execute(
        std::string transactionId, eve::resource::IResourceAccount& account,
        const eve::resource::CostSpec& cost, ITransactionParticipant& effect);
};

}  // namespace eve::transaction
